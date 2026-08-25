#include "model/AssimpMeshLoader.h"
#include "model/Material.h"
#include "model/MaterialManager.h"
#include "model/MeshManager.h"
#include "model/Vertex.h"
#include "texture/TextureManager.h"
#include <DirectXMath.h>
#include <algorithm>
#include <array>
#include <assimp/GltfMaterial.h>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <functional>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace DirectX;

namespace {
constexpr float kEpsilon = 0.000001f;

float FiniteOr(float value, float fallback) {
    return std::isfinite(value) ? value : fallback;
}

float ClampFinite(float value, float minimum, float maximum, float fallback) {
    return std::clamp(FiniteOr(value, fallback), minimum, maximum);
}

XMFLOAT3 SanitizeFloat3(const aiVector3D &value,
                        const XMFLOAT3 &fallback) {
    return {FiniteOr(value.x, fallback.x), FiniteOr(value.y, fallback.y),
            FiniteOr(value.z, fallback.z)};
}

XMFLOAT3 SanitizeNormal(const aiVector3D &value) {
    XMFLOAT3 normal = SanitizeFloat3(value, {0.0f, 1.0f, 0.0f});
    XMVECTOR vector = XMLoadFloat3(&normal);
    const float lengthSq = XMVectorGetX(XMVector3LengthSq(vector));
    if (!std::isfinite(lengthSq) || lengthSq <= kEpsilon) {
        return {0.0f, 1.0f, 0.0f};
    }
    XMStoreFloat3(&normal, XMVector3Normalize(vector));
    return normal;
}

XMFLOAT4 SanitizeTangent(const aiVector3D &value) {
    XMFLOAT3 tangent = SanitizeFloat3(value, {1.0f, 0.0f, 0.0f});
    XMVECTOR vector = XMLoadFloat3(&tangent);
    const float lengthSq = XMVectorGetX(XMVector3LengthSq(vector));
    if (!std::isfinite(lengthSq) || lengthSq <= kEpsilon) {
        return {1.0f, 0.0f, 0.0f, 1.0f};
    }
    const XMVECTOR normalized = XMVector3Normalize(vector);
    return {XMVectorGetX(normalized), XMVectorGetY(normalized),
            XMVectorGetZ(normalized), 1.0f};
}

XMFLOAT4X4 ToMatrix(const aiMatrix4x4 &m) {
    return {m.a1, m.b1, m.c1, m.d1, m.a2, m.b2, m.c2, m.d2,
            m.a3, m.b3, m.c3, m.d3, m.a4, m.b4, m.c4, m.d4};
}

uint32_t CheckedUint32Size(size_t value, const char *message) {
    (void)message;
    if (value > (std::numeric_limits<uint32_t>::max)()) {
        return UINT32_MAX;
    }
    return static_cast<uint32_t>(value);
}

int CheckedIntSize(size_t value, const char *message) {
    (void)message;
    if (value > static_cast<size_t>((std::numeric_limits<int>::max)())) {
        return (std::numeric_limits<int>::max)();
    }
    return static_cast<int>(value);
}

bool TryParseEmbeddedTextureIndex(const std::string &name, unsigned int &index) {
    if (name.size() <= 1 || name[0] != '*') {
        return false;
    }

    unsigned int parsed = 0;
    const char *begin = name.data() + 1;
    const char *end = name.data() + name.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end) {
        return false;
    }

    index = parsed;
    return true;
}

bool IsUsableMesh(const aiMesh *mesh) {
    return mesh != nullptr && mesh->HasPositions() && mesh->mNumVertices != 0 &&
           mesh->mVertices != nullptr &&
           (mesh->mNumFaces == 0 || mesh->mFaces != nullptr) &&
           static_cast<size_t>(mesh->mNumFaces) <=
               (std::numeric_limits<size_t>::max)() / 3u;
}

std::vector<Vertex> BuildVertices(const aiMesh &mesh) {
    std::vector<Vertex> vertices;
    vertices.reserve(mesh.mNumVertices);
    for (unsigned int index = 0; index < mesh.mNumVertices; ++index) {
        Vertex vertex{};
        vertex.position =
            SanitizeFloat3(mesh.mVertices[index], {0.0f, 0.0f, 0.0f});
        vertex.bindPosition = vertex.position;
        if (mesh.HasNormals()) {
            vertex.normal = SanitizeNormal(mesh.mNormals[index]);
        }
        vertex.uv = mesh.HasTextureCoords(0)
                        ? XMFLOAT2{FiniteOr(mesh.mTextureCoords[0][index].x,
                                           0.0f),
                                   FiniteOr(mesh.mTextureCoords[0][index].y,
                                           0.0f)}
                        : XMFLOAT2{};
        if (mesh.HasTangentsAndBitangents()) {
            vertex.tangent = SanitizeTangent(mesh.mTangents[index]);
        }
        vertices.push_back(vertex);
    }
    return vertices;
}

std::vector<uint32_t> BuildTriangleIndices(const aiMesh &mesh,
                                           size_t vertexCount) {
    std::vector<uint32_t> indices;
    indices.reserve(static_cast<size_t>(mesh.mNumFaces) * 3u);
    for (unsigned int index = 0; index < mesh.mNumFaces; ++index) {
        const aiFace &face = mesh.mFaces[index];
        if (face.mNumIndices != 3 || face.mIndices == nullptr) {
            continue;
        }
        const bool valid = face.mIndices[0] < vertexCount &&
                           face.mIndices[1] < vertexCount &&
                           face.mIndices[2] < vertexCount;
        if (!valid) {
            continue;
        }
        indices.insert(indices.end(), {face.mIndices[0], face.mIndices[1],
                                       face.mIndices[2]});
    }
    return indices;
}

bool MeshDataSizesFit(const std::vector<Vertex> &vertices,
                      const std::vector<uint32_t> &indices) {
    const size_t maximum =
        static_cast<size_t>((std::numeric_limits<uint32_t>::max)());
    const std::array<bool, 4> valid = {
        !vertices.empty(), !indices.empty(), vertices.size() <= maximum,
        indices.size() <= maximum};
    return std::all_of(valid.begin(), valid.end(), [](bool value) {
        return value;
    });
}

void FillSourceBounds(const std::vector<Vertex> &vertices,
                      ModelSubMesh &subMesh) {
    subMesh.vertexCount = static_cast<uint32_t>(vertices.size());
    subMesh.sourcePositions.reserve(vertices.size());
    subMesh.sourceBoundsMin = vertices.front().position;
    subMesh.sourceBoundsMax = vertices.front().position;
    for (const Vertex &vertex : vertices) {
        subMesh.sourcePositions.push_back(vertex.position);
        subMesh.sourceBoundsMin.x =
            (std::min)(subMesh.sourceBoundsMin.x, vertex.position.x);
        subMesh.sourceBoundsMin.y =
            (std::min)(subMesh.sourceBoundsMin.y, vertex.position.y);
        subMesh.sourceBoundsMin.z =
            (std::min)(subMesh.sourceBoundsMin.z, vertex.position.z);
        subMesh.sourceBoundsMax.x =
            (std::max)(subMesh.sourceBoundsMax.x, vertex.position.x);
        subMesh.sourceBoundsMax.y =
            (std::max)(subMesh.sourceBoundsMax.y, vertex.position.y);
        subMesh.sourceBoundsMax.z =
            (std::max)(subMesh.sourceBoundsMax.z, vertex.position.z);
    }
}

uint32_t ResolveBoneIndex(const aiBone &bone, Model &model) {
    const std::string boneName = bone.mName.C_Str();
    const auto existing = model.boneMap.find(boneName);
    if (existing != model.boneMap.end()) {
        return existing->second;
    }
    const uint32_t boneIndex =
        CheckedUint32Size(model.bones.size(), "AssimpMeshLoader bone overflow");
    if (boneIndex == UINT32_MAX ||
        boneIndex > static_cast<uint32_t>((std::numeric_limits<int>::max)())) {
        return UINT32_MAX;
    }
    model.boneMap[boneName] = boneIndex;
    BoneInfo info{};
    info.name = boneName;
    info.offsetMatrix = ToMatrix(bone.mOffsetMatrix);
    model.bones.push_back(info);
    return boneIndex;
}

void AddBoneWeights(const aiMesh &mesh, size_t vertexCount, Model &model,
                    ModelSubMesh &subMesh) {
    if (!mesh.HasBones()) {
        return;
    }
    for (unsigned int index = 0; index < mesh.mNumBones; ++index) {
        const aiBone *bone = mesh.mBones[index];
        if (bone == nullptr) {
            continue;
        }
        const uint32_t boneIndex = ResolveBoneIndex(*bone, model);
        if (boneIndex == UINT32_MAX) {
            continue;
        }
        JointWeightData &weightData =
            subMesh.skinClusterData[bone->mName.C_Str()];
        weightData.inverseBindPoseMatrix = model.bones[boneIndex].offsetMatrix;
        for (unsigned int weightIndex = 0; weightIndex < bone->mNumWeights;
             ++weightIndex) {
            const uint32_t vertexId = bone->mWeights[weightIndex].mVertexId;
            const float weight = ClampFinite(
                bone->mWeights[weightIndex].mWeight, 0.0f, 1.0f, 0.0f);
            if (vertexId < vertexCount && weight > 0.0f) {
                weightData.vertexWeights.push_back({weight, vertexId});
            }
        }
    }
}

bool LoadEmbeddedTexture(TextureManager *manager, const aiScene &scene,
                         const std::string &name, uint32_t &textureId) {
    unsigned int textureIndex = 0;
    if (!TryParseEmbeddedTextureIndex(name, textureIndex) ||
        textureIndex >= scene.mNumTextures) {
        return false;
    }
    const aiTexture *texture = scene.mTextures[textureIndex];
    if (texture == nullptr || texture->mWidth == 0 || texture->pcData == nullptr) {
        return false;
    }
    if (texture->mHeight == 0) {
        textureId = manager->LoadFromMemory(
            reinterpret_cast<const uint8_t *>(texture->pcData),
            texture->mWidth);
        return true;
    }
    if (static_cast<size_t>(texture->mWidth) >
        (std::numeric_limits<size_t>::max)() /
            static_cast<size_t>(texture->mHeight)) {
        return false;
    }
    const size_t pixelCount = static_cast<size_t>(texture->mWidth) *
                              static_cast<size_t>(texture->mHeight);
    if (pixelCount > (std::numeric_limits<size_t>::max)() / 4u) {
        return false;
    }
    std::vector<uint8_t> pixels(pixelCount * 4u);
    for (size_t index = 0; index < pixelCount; ++index) {
        const aiTexel &source = texture->pcData[index];
        const size_t destination = index * 4u;
        pixels[destination + 0u] = source.r;
        pixels[destination + 1u] = source.g;
        pixels[destination + 2u] = source.b;
        pixels[destination + 3u] = source.a;
    }
    textureId = manager->CreateFromRgbaPixels(texture->mWidth, texture->mHeight,
                                               pixels.data());
    return true;
}

bool TryLoadTexture(TextureManager *manager, const aiScene &scene,
                    const std::string &modelPath, const aiMaterial *material,
                    aiTextureType type, uint32_t &textureId) {
    aiString texturePath;
    if (material == nullptr ||
        material->GetTexture(type, 0, &texturePath) != AI_SUCCESS) {
        return false;
    }
    const std::string name = texturePath.C_Str();
    if (!name.empty() && name.front() == '*') {
        return LoadEmbeddedTexture(manager, scene, name, textureId);
    }
    const std::filesystem::path fullPath =
        std::filesystem::path(modelPath).parent_path() / name;
    textureId = manager->Load(fullPath.wstring());
    return true;
}

Material BuildMaterial(const aiMaterial *source, bool hasTexture,
                       uint32_t textureId, bool hasNormalTexture,
                       uint32_t normalTextureId) {
    Material material{};
    material.color = {1, 1, 1, 1};
    material.reflectionStrength = 0.18f;
    material.reflectionFresnelStrength = 0.12f;
    aiColor4D diffuse{};
    if (source != nullptr &&
        aiGetMaterialColor(source, AI_MATKEY_COLOR_DIFFUSE, &diffuse) ==
            AI_SUCCESS) {
        material.color.x = ClampFinite(diffuse.r, 0.0f, 1.0f, 1.0f);
        material.color.y = ClampFinite(diffuse.g, 0.0f, 1.0f, 1.0f);
        material.color.z = ClampFinite(diffuse.b, 0.0f, 1.0f, 1.0f);
    }
    float opacity = 1.0f;
    if (source != nullptr &&
        source->Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS) {
        material.color.w = ClampFinite(opacity, 0.0f, 1.0f, 1.0f);
    }
    XMStoreFloat4x4(&material.uvTransform,
                    XMMatrixTranspose(XMMatrixIdentity()));
    material.enableTexture = hasTexture ? 1 : 0;
    material.enableNormalMap = hasNormalTexture ? 1 : 0;
    material.baseColorTextureId = hasTexture ? textureId : UINT32_MAX;
    material.normalTextureId =
        hasNormalTexture ? normalTextureId : UINT32_MAX;
    return material;
}

} // namespace

void AssimpMeshLoader::Initialize(TextureManager *textureManager,
                                  MeshManager *meshManager,
                                  MaterialManager *materialManager) {
    if (!textureManager || !meshManager || !materialManager) {
        textureManager_ = nullptr;
        meshManager_ = nullptr;
        materialManager_ = nullptr;
        return;
    }

    textureManager_ = textureManager;
    meshManager_ = meshManager;
    materialManager_ = materialManager;
}

bool AssimpMeshLoader::IsInitialized() const {
    return textureManager_ && meshManager_ && materialManager_;
}

void AssimpMeshLoader::LoadMeshes(const aiScene *scene, const std::string &path,
                                  Model &model) const {
    if (!IsInitialized()) {
        return;
    }
    if (!scene) {
        return;
    }

    for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes;
         ++meshIndex) {
        const aiMesh *mesh = scene->mMeshes[meshIndex];
        if (!IsUsableMesh(mesh)) {
            continue;
        }
        const std::vector<Vertex> vertices = BuildVertices(*mesh);
        const std::vector<uint32_t> indices =
            BuildTriangleIndices(*mesh, vertices.size());
        if (!MeshDataSizesFit(vertices, indices)) {
            continue;
        }

        ModelSubMesh subMesh{};
        FillSourceBounds(vertices, subMesh);
        AddBoneWeights(*mesh, vertices.size(), model, subMesh);
        const aiMaterial *materialSource =
            scene->HasMaterials() && mesh->mMaterialIndex < scene->mNumMaterials
                ? scene->mMaterials[mesh->mMaterialIndex]
                : nullptr;
        uint32_t textureId = 0;
        uint32_t normalTextureId = UINT32_MAX;
        const bool hasTexture =
            TryLoadTexture(textureManager_, *scene, path, materialSource,
                           aiTextureType_BASE_COLOR, textureId) ||
            TryLoadTexture(textureManager_, *scene, path, materialSource,
                           aiTextureType_DIFFUSE, textureId);
        const bool hasNormalTexture =
            TryLoadTexture(textureManager_, *scene, path, materialSource,
                           aiTextureType_NORMALS, normalTextureId) ||
            TryLoadTexture(textureManager_, *scene, path, materialSource,
                           aiTextureType_HEIGHT, normalTextureId);
        const uint32_t meshId = meshManager_->CreateMesh(
            vertices.data(), sizeof(Vertex), subMesh.vertexCount,
            indices.data(), static_cast<uint32_t>(indices.size()));
        if (meshId == UINT32_MAX) {
            continue;
        }
        const Material material =
            BuildMaterial(materialSource, hasTexture, textureId,
                          hasNormalTexture, normalTextureId);
        subMesh.meshId = meshId;
        subMesh.textureId = textureId;
        subMesh.normalTextureId = normalTextureId;
        subMesh.materialId = materialManager_->CreateMaterial(material);
        model.subMeshes.push_back(std::move(subMesh));
    }

    if (!model.subMeshes.empty()) {
        model.meshId = model.subMeshes.front().meshId;
        model.textureId = model.subMeshes.front().textureId;
        model.materialId = model.subMeshes.front().materialId;
    }
    if (!model.bones.empty()) {
        BuildBoneHierarchy(scene, model);
    }
}

const aiNode *AssimpMeshLoader::FindNodeByName(const aiNode *node,
                                               const std::string &name) const {
    if (!node) {
        return nullptr;
    }

    if (name == node->mName.C_Str()) {
        return node;
    }

    for (unsigned int i = 0; i < node->mNumChildren; i++) {
        const aiNode *found = FindNodeByName(node->mChildren[i], name);
        if (found) {
            return found;
        }
    }

    return nullptr;
}

void AssimpMeshLoader::BuildBoneHierarchy(const aiScene *scene,
                                          Model &model) const {
    if (!scene || !scene->mRootNode) {
        return;
    }

    for (size_t i = 0; i < model.bones.size(); i++) {
        const std::string &boneName = model.bones[i].name;

        const aiNode *node = FindNodeByName(scene->mRootNode, boneName);
        if (!node) {
            model.bones[i].parentIndex = -1;
            model.bones[i].localBindMatrix = ToMatrix(aiMatrix4x4());
            model.bones[i].parentAdjustmentMatrix = ToMatrix(aiMatrix4x4());
            continue;
        }

        aiMatrix4x4 adjustment{};
        int parentIndex = -1;
        const aiNode *parent = node->mParent;

        while (parent) {
            auto it = model.boneMap.find(parent->mName.C_Str());
            if (it != model.boneMap.end()) {
                parentIndex = static_cast<int>(it->second);
                break;
            }

            adjustment *= parent->mTransformation;
            parent = parent->mParent;
        }

        model.bones[i].parentIndex = parentIndex;
        model.bones[i].parentAdjustmentMatrix = ToMatrix(adjustment);
        model.bones[i].localBindMatrix =
            ToMatrix(node->mTransformation * adjustment);
    }

    ReorderBonesParentFirst(model);
}

void AssimpMeshLoader::ReorderBonesParentFirst(Model &model) const {
    const size_t boneCount = model.bones.size();
    if (boneCount <= 1) {
        return;
    }
    if (boneCount > static_cast<size_t>((std::numeric_limits<int>::max)())) {
        return;
    }

    std::vector<std::vector<size_t>> children(boneCount);
    std::vector<size_t> roots;
    roots.reserve(boneCount);

    for (size_t boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
        const int parentIndex = model.bones[boneIndex].parentIndex;
        if (parentIndex >= 0 && static_cast<size_t>(parentIndex) < boneCount) {
            children[static_cast<size_t>(parentIndex)].push_back(boneIndex);
        } else {
            roots.push_back(boneIndex);
        }
    }

    std::vector<BoneInfo> orderedBones;
    std::vector<int> oldToNew(boneCount, -1);
    orderedBones.reserve(boneCount);

    std::function<void(size_t, int)> visit = [&](size_t oldIndex,
                                                 int newParentIndex) {
        if (oldToNew[oldIndex] >= 0) {
            return;
        }

        BoneInfo bone = model.bones[oldIndex];
        bone.parentIndex = newParentIndex;
        const int newIndex =
            CheckedIntSize(orderedBones.size(),
                           "AssimpMeshLoader reordered bone count overflow");
        oldToNew[oldIndex] = newIndex;
        orderedBones.push_back(bone);

        for (size_t childIndex : children[oldIndex]) {
            visit(childIndex, newIndex);
        }
    };

    for (size_t rootIndex : roots) {
        visit(rootIndex, -1);
    }

    for (size_t boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
        if (oldToNew[boneIndex] < 0) {
            visit(boneIndex, -1);
        }
    }

    model.bones = std::move(orderedBones);
    model.boneMap.clear();
    for (size_t boneIndex = 0; boneIndex < model.bones.size(); ++boneIndex) {
        model.boneMap[model.bones[boneIndex].name] =
            CheckedUint32Size(boneIndex,
                              "AssimpMeshLoader reordered bone count overflow");
    }
}
