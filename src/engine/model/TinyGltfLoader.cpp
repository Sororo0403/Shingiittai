#include "TinyGltfLoader.h"
#include "Material.h"
#include "MaterialManager.h"
#include "MeshManager.h"
#include "TextureManager.h"
#include "Vertex.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#if __has_include("tiny_gltf.h")
#include "tiny_gltf.h"
#elif __has_include("../../../externals/tinygltf/tiny_gltf.h")
#include "../../../externals/tinygltf/tiny_gltf.h"
#else
#error tinygltf header not found. Please add tinygltf include path.
#endif

using namespace DirectX;

namespace {

const tinygltf::Accessor &GetAccessor(const tinygltf::Model &model, int index) {
    if (index < 0 || index >= static_cast<int>(model.accessors.size())) {
        throw std::runtime_error("invalid accessor index");
    }
    return model.accessors[index];
}

const tinygltf::BufferView &GetBufferView(const tinygltf::Model &model,
                                          int index) {
    if (index < 0 || index >= static_cast<int>(model.bufferViews.size())) {
        throw std::runtime_error("invalid bufferView index");
    }
    return model.bufferViews[index];
}

const tinygltf::Buffer &GetBuffer(const tinygltf::Model &model, int index) {
    if (index < 0 || index >= static_cast<int>(model.buffers.size())) {
        throw std::runtime_error("invalid buffer index");
    }
    return model.buffers[index];
}

const unsigned char *GetAccessorDataPtr(const tinygltf::Model &model,
                                        const tinygltf::Accessor &accessor,
                                        const tinygltf::BufferView &view) {
    const tinygltf::Buffer &buffer = GetBuffer(model, view.buffer);
    const size_t offset = view.byteOffset + accessor.byteOffset;
    if (offset >= buffer.data.size()) {
        throw std::runtime_error("accessor offset out of range");
    }
    return buffer.data.data() + offset;
}

size_t GetAccessorStride(const tinygltf::Accessor &accessor,
                         const tinygltf::BufferView &view) {
    const int stride = accessor.ByteStride(view);
    if (stride > 0) {
        return static_cast<size_t>(stride);
    }

    return tinygltf::GetNumComponentsInType(accessor.type) *
           tinygltf::GetComponentSizeInBytes(accessor.componentType);
}

float ReadFloat(const unsigned char *p) {
    float value = 0.0f;
    std::memcpy(&value, p, sizeof(float));
    return value;
}

uint32_t ReadIndex(const unsigned char *p, int componentType) {
    switch (componentType) {
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
        return static_cast<uint32_t>(*reinterpret_cast<const uint8_t *>(p));
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
        return static_cast<uint32_t>(*reinterpret_cast<const uint16_t *>(p));
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
        return *reinterpret_cast<const uint32_t *>(p);
    default:
        throw std::runtime_error("unsupported index component type");
    }
}

std::string GetNodeName(const tinygltf::Model &model, int nodeIndex) {
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(model.nodes.size())) {
        return "";
    }

    const auto &node = model.nodes[nodeIndex];
    if (!node.name.empty()) {
        return node.name;
    }
    return "Node_" + std::to_string(nodeIndex);
}

XMMATRIX MakeNodeLocalMatrix(const tinygltf::Node &node) {
    if (node.matrix.size() == 16) {
        XMFLOAT4X4 m = {
            static_cast<float>(node.matrix[0]),
            static_cast<float>(node.matrix[1]),
            static_cast<float>(node.matrix[2]),
            static_cast<float>(node.matrix[3]),
            static_cast<float>(node.matrix[4]),
            static_cast<float>(node.matrix[5]),
            static_cast<float>(node.matrix[6]),
            static_cast<float>(node.matrix[7]),
            static_cast<float>(node.matrix[8]),
            static_cast<float>(node.matrix[9]),
            static_cast<float>(node.matrix[10]),
            static_cast<float>(node.matrix[11]),
            static_cast<float>(node.matrix[12]),
            static_cast<float>(node.matrix[13]),
            static_cast<float>(node.matrix[14]),
            static_cast<float>(node.matrix[15]),
        };
        return XMMatrixTranspose(XMLoadFloat4x4(&m));
    }

    XMFLOAT3 t{0.0f, 0.0f, 0.0f};
    XMFLOAT4 r{0.0f, 0.0f, 0.0f, 1.0f};
    XMFLOAT3 s{1.0f, 1.0f, 1.0f};

    if (node.translation.size() == 3) {
        t = {static_cast<float>(node.translation[0]),
             static_cast<float>(node.translation[1]),
             static_cast<float>(node.translation[2])};
    }
    if (node.rotation.size() == 4) {
        r = {static_cast<float>(node.rotation[0]),
             static_cast<float>(node.rotation[1]),
             static_cast<float>(node.rotation[2]),
             static_cast<float>(node.rotation[3])};
    }
    if (node.scale.size() == 3) {
        s = {static_cast<float>(node.scale[0]), static_cast<float>(node.scale[1]),
             static_cast<float>(node.scale[2])};
    }

    return XMMatrixScaling(s.x, s.y, s.z) *
           XMMatrixRotationQuaternion(XMQuaternionNormalize(XMLoadFloat4(&r))) *
           XMMatrixTranslation(t.x, t.y, t.z);
}

void DecomposeMatrix(const XMMATRIX &matrix, XMFLOAT3 &scale,
                     XMFLOAT4 &rotation, XMFLOAT3 &translation) {
    XMVECTOR scaleV = XMVectorSet(1.0f, 1.0f, 1.0f, 0.0f);
    XMVECTOR rotationV = XMQuaternionIdentity();
    XMVECTOR translationV = XMVectorZero();
    XMMatrixDecompose(&scaleV, &rotationV, &translationV, matrix);
    XMStoreFloat3(&scale, scaleV);
    XMStoreFloat4(&rotation, XMQuaternionNormalize(rotationV));
    XMStoreFloat3(&translation, translationV);
}

XMFLOAT4X4 ToFloat4x4(const XMMATRIX &matrix) {
    XMFLOAT4X4 out{};
    XMStoreFloat4x4(&out, matrix);
    return out;
}

XMFLOAT4X4 ReadMat4AsRowMajor(const unsigned char *p) {
    float values[16]{};
    std::memcpy(values, p, sizeof(values));
    XMFLOAT4X4 m = {
        values[0],  values[1],  values[2],  values[3],
        values[4],  values[5],  values[6],  values[7],
        values[8],  values[9],  values[10], values[11],
        values[12], values[13], values[14], values[15],
    };
    return ToFloat4x4(XMMatrixTranspose(XMLoadFloat4x4(&m)));
}

void NormalizeWeights(Vertex &v) {
    const float sum =
        v.boneWeight.x + v.boneWeight.y + v.boneWeight.z + v.boneWeight.w;
    if (sum > 0.000001f) {
        v.boneWeight.x /= sum;
        v.boneWeight.y /= sum;
        v.boneWeight.z /= sum;
        v.boneWeight.w /= sum;
    }
}

void BuildGlobalNodeMatrices(const std::vector<NodeInfo> &nodes,
                             std::vector<XMMATRIX> &globalMatrices) {
    globalMatrices.resize(nodes.size());
    for (size_t i = 0; i < nodes.size(); i++) {
        const XMMATRIX local =
            XMMatrixScaling(nodes[i].bindScale.x, nodes[i].bindScale.y,
                            nodes[i].bindScale.z) *
            XMMatrixRotationQuaternion(
                XMQuaternionNormalize(XMLoadFloat4(&nodes[i].bindRotation))) *
            XMMatrixTranslation(nodes[i].bindTranslation.x,
                                nodes[i].bindTranslation.y,
                                nodes[i].bindTranslation.z);

        const int parent = nodes[i].parentIndex;
        if (parent < 0 || static_cast<size_t>(parent) >= nodes.size()) {
            globalMatrices[i] = local;
        } else {
            globalMatrices[i] = local * globalMatrices[parent];
        }
    }
}

size_t GetAnimationOutputIndex(const tinygltf::AnimationSampler &sampler,
                               size_t keyIndex) {
    return sampler.interpolation == "CUBICSPLINE" ? keyIndex * 3 + 1 : keyIndex;
}

} // namespace

void TinyGltfLoader::Initialize(TextureManager *textureManager,
                                MeshManager *meshManager,
                                MaterialManager *materialManager) {
    textureManager_ = textureManager;
    meshManager_ = meshManager;
    materialManager_ = materialManager;
}

Model TinyGltfLoader::Load(const std::string &path) {
    if (!textureManager_ || !meshManager_ || !materialManager_) {
        throw std::runtime_error("TinyGltfLoader is not initialized");
    }

    tinygltf::TinyGLTF loader;
    tinygltf::Model gltfModel;
    std::string warn;
    std::string err;

    const std::filesystem::path filePath(path);
    const bool isBinary = filePath.extension() == ".glb";
    const bool ok = isBinary
                        ? loader.LoadBinaryFromFile(&gltfModel, &err, &warn, path)
                        : loader.LoadASCIIFromFile(&gltfModel, &err, &warn, path);
    if (!ok) {
        throw std::runtime_error("[TinyGltfLoader] load failed: " + err);
    }
    if (gltfModel.meshes.empty()) {
        throw std::runtime_error("[TinyGltfLoader] no meshes in file");
    }

    Model model{};
    model.localTransform = ToFloat4x4(XMMatrixIdentity());

    std::vector<int> parentOfNode(gltfModel.nodes.size(), -1);
    for (size_t i = 0; i < gltfModel.nodes.size(); i++) {
        for (int child : gltfModel.nodes[i].children) {
            if (child >= 0 && child < static_cast<int>(parentOfNode.size())) {
                parentOfNode[child] = static_cast<int>(i);
            }
        }
    }

    model.nodes.resize(gltfModel.nodes.size());
    for (size_t i = 0; i < gltfModel.nodes.size(); i++) {
        model.nodes[i].name = GetNodeName(gltfModel, static_cast<int>(i));
        model.nodes[i].parentIndex = parentOfNode[i];
        DecomposeMatrix(MakeNodeLocalMatrix(gltfModel.nodes[i]),
                        model.nodes[i].bindScale, model.nodes[i].bindRotation,
                        model.nodes[i].bindTranslation);
    }

    std::vector<XMMATRIX> globalBindMatrices;
    BuildGlobalNodeMatrices(model.nodes, globalBindMatrices);

    int activeSkinIndex = -1;
    int activeMeshNode = -1;
    for (size_t i = 0; i < gltfModel.nodes.size(); i++) {
        const auto &node = gltfModel.nodes[i];
        if (node.mesh >= 0) {
            activeMeshNode = static_cast<int>(i);
            if (node.skin >= 0) {
                activeSkinIndex = node.skin;
                break;
            }
        }
    }

    if (activeMeshNode >= 0 &&
        activeMeshNode < static_cast<int>(globalBindMatrices.size())) {
        model.localTransform = ToFloat4x4(globalBindMatrices[activeMeshNode]);
    }

    if (activeSkinIndex >= 0 &&
        activeSkinIndex < static_cast<int>(gltfModel.skins.size())) {
        const auto &skin = gltfModel.skins[activeSkinIndex];
        model.bones.resize(skin.joints.size());

        for (size_t i = 0; i < skin.joints.size(); i++) {
            const int jointNodeIndex = skin.joints[i];
            model.bones[i].name = GetNodeName(gltfModel, jointNodeIndex);
            model.bones[i].nodeIndex = static_cast<uint32_t>(jointNodeIndex);
            model.bones[i].inverseBindMatrix = ToFloat4x4(XMMatrixIdentity());
            model.boneMap[model.bones[i].name] = static_cast<uint32_t>(i);
        }

        if (skin.inverseBindMatrices >= 0) {
            const auto &accessor = GetAccessor(gltfModel, skin.inverseBindMatrices);
            const auto &view = GetBufferView(gltfModel, accessor.bufferView);
            const unsigned char *base = GetAccessorDataPtr(gltfModel, accessor, view);
            const size_t stride = GetAccessorStride(accessor, view);
            const size_t count = (std::min)(model.bones.size(),
                                            static_cast<size_t>(accessor.count));

            for (size_t i = 0; i < count; i++) {
                model.bones[i].inverseBindMatrix =
                    ReadMat4AsRowMajor(base + i * stride);
            }
        } else {
            for (size_t i = 0; i < model.bones.size(); i++) {
                const uint32_t nodeIndex = model.bones[i].nodeIndex;
                XMVECTOR det = XMVectorZero();
                model.bones[i].inverseBindMatrix =
                    ToFloat4x4(XMMatrixInverse(&det, globalBindMatrices[nodeIndex]));
            }
        }
    }

    const std::filesystem::path parentDir = filePath.parent_path();
    for (size_t meshIndex = 0; meshIndex < gltfModel.meshes.size(); meshIndex++) {
        const auto &mesh = gltfModel.meshes[meshIndex];
        for (const auto &primitive : mesh.primitives) {
            if (primitive.mode != TINYGLTF_MODE_TRIANGLES) {
                continue;
            }

            auto posIt = primitive.attributes.find("POSITION");
            if (posIt == primitive.attributes.end()) {
                continue;
            }

            const auto &posAccessor = GetAccessor(gltfModel, posIt->second);
            const auto &posView = GetBufferView(gltfModel, posAccessor.bufferView);
            const unsigned char *posBase =
                GetAccessorDataPtr(gltfModel, posAccessor, posView);
            const size_t posStride = GetAccessorStride(posAccessor, posView);

            std::vector<Vertex> vertices(posAccessor.count);
            for (size_t i = 0; i < vertices.size(); i++) {
                const unsigned char *p = posBase + i * posStride;
                vertices[i].position = {ReadFloat(p + 0), ReadFloat(p + 4),
                                        ReadFloat(p + 8)};
                vertices[i].uv = {0.0f, 0.0f};
            }

            auto uvIt = primitive.attributes.find("TEXCOORD_0");
            if (uvIt != primitive.attributes.end()) {
                const auto &uvAccessor = GetAccessor(gltfModel, uvIt->second);
                const auto &uvView = GetBufferView(gltfModel, uvAccessor.bufferView);
                const unsigned char *uvBase =
                    GetAccessorDataPtr(gltfModel, uvAccessor, uvView);
                const size_t uvStride = GetAccessorStride(uvAccessor, uvView);
                const size_t count =
                    (std::min)(vertices.size(), static_cast<size_t>(uvAccessor.count));

                for (size_t i = 0; i < count; i++) {
                    const unsigned char *p = uvBase + i * uvStride;
                    vertices[i].uv = {ReadFloat(p + 0), 1.0f - ReadFloat(p + 4)};
                }
            }

            auto jointsIt = primitive.attributes.find("JOINTS_0");
            auto weightsIt = primitive.attributes.find("WEIGHTS_0");
            if (!model.bones.empty() && jointsIt != primitive.attributes.end() &&
                weightsIt != primitive.attributes.end()) {
                const auto &jAccessor = GetAccessor(gltfModel, jointsIt->second);
                const auto &jView = GetBufferView(gltfModel, jAccessor.bufferView);
                const unsigned char *jBase =
                    GetAccessorDataPtr(gltfModel, jAccessor, jView);
                const size_t jStride = GetAccessorStride(jAccessor, jView);

                const auto &wAccessor = GetAccessor(gltfModel, weightsIt->second);
                const auto &wView = GetBufferView(gltfModel, wAccessor.bufferView);
                const unsigned char *wBase =
                    GetAccessorDataPtr(gltfModel, wAccessor, wView);
                const size_t wStride = GetAccessorStride(wAccessor, wView);

                const size_t count = (std::min)(
                    vertices.size(),
                    (std::min)(static_cast<size_t>(jAccessor.count),
                               static_cast<size_t>(wAccessor.count)));

                for (size_t i = 0; i < count; i++) {
                    const unsigned char *jp = jBase + i * jStride;
                    const unsigned char *wp = wBase + i * wStride;

                    uint32_t joints[4]{};
                    if (jAccessor.componentType ==
                        TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                        for (int k = 0; k < 4; k++) {
                            joints[k] = static_cast<uint32_t>(jp[k]);
                        }
                    } else if (jAccessor.componentType ==
                               TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                        for (int k = 0; k < 4; k++) {
                            joints[k] =
                                static_cast<uint32_t>(
                                    reinterpret_cast<const uint16_t *>(jp)[k]);
                        }
                    } else {
                        continue;
                    }

                    float weights[4]{};
                    if (wAccessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
                        for (int k = 0; k < 4; k++) {
                            weights[k] = ReadFloat(wp + sizeof(float) * k);
                        }
                    } else if (wAccessor.componentType ==
                               TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                        for (int k = 0; k < 4; k++) {
                            weights[k] = static_cast<float>(wp[k]) / 255.0f;
                        }
                    } else if (wAccessor.componentType ==
                               TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                        for (int k = 0; k < 4; k++) {
                            weights[k] =
                                static_cast<float>(
                                    reinterpret_cast<const uint16_t *>(wp)[k]) /
                                65535.0f;
                        }
                    } else {
                        continue;
                    }

                    vertices[i].boneIndex = {joints[0], joints[1], joints[2],
                                             joints[3]};
                    vertices[i].boneWeight = {weights[0], weights[1], weights[2],
                                              weights[3]};
                    NormalizeWeights(vertices[i]);
                }
            }

            if (primitive.indices < 0) {
                throw std::runtime_error("[TinyGltfLoader] primitive has no indices");
            }

            const auto &idxAccessor = GetAccessor(gltfModel, primitive.indices);
            const auto &idxView = GetBufferView(gltfModel, idxAccessor.bufferView);
            const unsigned char *idxBase =
                GetAccessorDataPtr(gltfModel, idxAccessor, idxView);
            const size_t idxStride = GetAccessorStride(idxAccessor, idxView);

            std::vector<uint32_t> indices(idxAccessor.count);
            for (size_t i = 0; i < indices.size(); i++) {
                indices[i] = ReadIndex(idxBase + i * idxStride,
                                       idxAccessor.componentType);
            }

            Material material{};
            material.color = {1.0f, 1.0f, 1.0f, 1.0f};
            XMStoreFloat4x4(&material.uvTransform,
                            XMMatrixTranspose(XMMatrixIdentity()));
            material.enableTexture = 0;
            uint32_t textureId = 0;

            if (primitive.material >= 0 &&
                primitive.material < static_cast<int>(gltfModel.materials.size())) {
                const auto &gltfMaterial = gltfModel.materials[primitive.material];
                const auto &baseColor =
                    gltfMaterial.pbrMetallicRoughness.baseColorFactor;
                if (baseColor.size() == 4) {
                    material.color = {static_cast<float>(baseColor[0]),
                                      static_cast<float>(baseColor[1]),
                                      static_cast<float>(baseColor[2]),
                                      static_cast<float>(baseColor[3])};
                }

                const int texIndex =
                    gltfMaterial.pbrMetallicRoughness.baseColorTexture.index;
                if (texIndex >= 0 &&
                    texIndex < static_cast<int>(gltfModel.textures.size())) {
                    const auto &texture = gltfModel.textures[texIndex];
                    if (texture.source >= 0 &&
                        texture.source < static_cast<int>(gltfModel.images.size())) {
                        const auto &image = gltfModel.images[texture.source];
                        if (!image.uri.empty() && image.uri.rfind("data:", 0) != 0) {
                            textureId =
                                textureManager_->Load((parentDir / image.uri).wstring());
                            material.enableTexture = 1;
                        }
                    }
                }
            }

            ModelSubMesh subMesh{};
            subMesh.meshId = meshManager_->CreateMesh(
                vertices.data(), sizeof(Vertex),
                static_cast<uint32_t>(vertices.size()), indices.data(),
                static_cast<uint32_t>(indices.size()));
            subMesh.textureId = textureId;
            subMesh.materialId = materialManager_->CreateMaterial(material);
            model.subMeshes.push_back(subMesh);
        }
    }

    for (size_t a = 0; a < gltfModel.animations.size(); a++) {
        const auto &animation = gltfModel.animations[a];
        AnimationClip clip{};
        clip.ticksPerSecond = 1.0f;

        for (const auto &channel : animation.channels) {
            if (channel.sampler < 0 ||
                channel.sampler >= static_cast<int>(animation.samplers.size()) ||
                channel.target_node < 0 ||
                channel.target_node >= static_cast<int>(model.nodes.size())) {
                continue;
            }

            const auto &sampler = animation.samplers[channel.sampler];
            if (sampler.input < 0 || sampler.output < 0) {
                continue;
            }

            const auto &inputAccessor = GetAccessor(gltfModel, sampler.input);
            const auto &inputView =
                GetBufferView(gltfModel, inputAccessor.bufferView);
            const unsigned char *timeBase =
                GetAccessorDataPtr(gltfModel, inputAccessor, inputView);
            const size_t timeStride = GetAccessorStride(inputAccessor, inputView);

            const auto &outputAccessor = GetAccessor(gltfModel, sampler.output);
            const auto &outputView =
                GetBufferView(gltfModel, outputAccessor.bufferView);
            const unsigned char *valueBase =
                GetAccessorDataPtr(gltfModel, outputAccessor, outputView);
            const size_t valueStride = GetAccessorStride(outputAccessor, outputView);

            AnimationChannel *targetChannel = nullptr;
            for (auto &existing : clip.channels) {
                if (existing.nodeIndex ==
                    static_cast<uint32_t>(channel.target_node)) {
                    targetChannel = &existing;
                    break;
                }
            }
            if (!targetChannel) {
                AnimationChannel newChannel{};
                newChannel.nodeIndex = static_cast<uint32_t>(channel.target_node);
                clip.channels.push_back(std::move(newChannel));
                targetChannel = &clip.channels.back();
            }

            const bool isCubicSpline = sampler.interpolation == "CUBICSPLINE";
            const size_t keyCount = inputAccessor.count;
            const size_t requiredOutputCount =
                isCubicSpline ? keyCount * 3 : keyCount;
            if (outputAccessor.count < requiredOutputCount) {
                continue;
            }

            if (channel.target_path == "translation" &&
                outputAccessor.type == TINYGLTF_TYPE_VEC3) {
                for (size_t i = 0; i < keyCount; i++) {
                    const float t = ReadFloat(timeBase + i * timeStride);
                    const unsigned char *v =
                        valueBase + GetAnimationOutputIndex(sampler, i) * valueStride;
                    targetChannel->boneAnimation.positions.push_back(
                        {t, {ReadFloat(v + 0), ReadFloat(v + 4), ReadFloat(v + 8)}});
                    clip.duration = (std::max)(clip.duration, t);
                }
            } else if (channel.target_path == "rotation" &&
                       outputAccessor.type == TINYGLTF_TYPE_VEC4) {
                for (size_t i = 0; i < keyCount; i++) {
                    const float t = ReadFloat(timeBase + i * timeStride);
                    const unsigned char *v =
                        valueBase + GetAnimationOutputIndex(sampler, i) * valueStride;
                    targetChannel->boneAnimation.rotations.push_back(
                        {t,
                         {ReadFloat(v + 0), ReadFloat(v + 4), ReadFloat(v + 8),
                          ReadFloat(v + 12)}});
                    clip.duration = (std::max)(clip.duration, t);
                }
            } else if (channel.target_path == "scale" &&
                       outputAccessor.type == TINYGLTF_TYPE_VEC3) {
                for (size_t i = 0; i < keyCount; i++) {
                    const float t = ReadFloat(timeBase + i * timeStride);
                    const unsigned char *v =
                        valueBase + GetAnimationOutputIndex(sampler, i) * valueStride;
                    targetChannel->boneAnimation.scales.push_back(
                        {t, {ReadFloat(v + 0), ReadFloat(v + 4), ReadFloat(v + 8)}});
                    clip.duration = (std::max)(clip.duration, t);
                }
            }
        }

        if (clip.channels.empty()) {
            continue;
        }

        std::string animationName = animation.name;
        if (animationName.empty()) {
            animationName = "Anim_" + std::to_string(a);
        }
        model.animations[animationName] = std::move(clip);
    }

    if (model.subMeshes.empty()) {
        throw std::runtime_error("[TinyGltfLoader] no supported primitives");
    }

    model.meshId = model.subMeshes[0].meshId;
    model.textureId = model.subMeshes[0].textureId;
    model.materialId = model.subMeshes[0].materialId;
    model.finalBoneMatrices.resize(model.bones.size());

    if (!model.animations.empty()) {
        model.currentAnimation = model.animations.begin()->first;
        model.animationTime = 0.0f;
        model.isLoop = true;
        model.isPlaying = true;
        model.animationFinished = false;
    }

    return model;
}
