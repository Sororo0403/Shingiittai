#include "AssimpLoader.h"
#include "MeshManager.h"
#include "TextureManager.h"
#include "Vertex.h"
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <filesystem>
#include <stdexcept>
#include <vector>

using namespace DirectX;

namespace {

XMFLOAT4X4 ToMatrix(const aiMatrix4x4 &m) {
    return {m.a1, m.b1, m.c1, m.d1, m.a2, m.b2, m.c2, m.d2,
            m.a3, m.b3, m.c3, m.d3, m.a4, m.b4, m.c4, m.d4};
}

void NormalizeWeights(std::vector<Vertex> &vertices) {
    for (auto &v : vertices) {
        float sum = v.boneWeight[0] + v.boneWeight[1] + v.boneWeight[2] +
                    v.boneWeight[3];

        if (sum > 0.00001f) {
            v.boneWeight[0] /= sum;
            v.boneWeight[1] /= sum;
            v.boneWeight[2] /= sum;
            v.boneWeight[3] /= sum;
        }
    }
}

const aiNode *FindNodeByName(const aiNode *node, const std::string &name) {
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

void BuildBoneHierarchy(const aiScene *scene, Model &model) {
    if (!scene || !scene->mRootNode) {
        return;
    }

    for (size_t i = 0; i < model.bones.size(); i++) {
        const std::string &boneName = model.bones[i].name;

        const aiNode *node = FindNodeByName(scene->mRootNode, boneName);
        if (!node) {
            model.bones[i].parentIndex = -1;
            model.bones[i].localBindMatrix = ToMatrix(aiMatrix4x4());
            continue;
        }

        model.bones[i].localBindMatrix = ToMatrix(node->mTransformation);

        int parentIndex = -1;
        const aiNode *parent = node->mParent;

        while (parent) {
            auto it = model.boneMap.find(parent->mName.C_Str());
            if (it != model.boneMap.end()) {
                parentIndex = static_cast<int>(it->second);
                break;
            }
            parent = parent->mParent;
        }

        model.bones[i].parentIndex = parentIndex;
    }
}

void LoadAnimation(const aiScene *scene, Model &model) {
    if (!scene || !scene->HasAnimations() || scene->mNumAnimations == 0) {
        return;
    }

    aiAnimation *anim = scene->mAnimations[0];
    if (!anim) {
        return;
    }

    model.animation.duration = static_cast<float>(anim->mDuration);
    model.animation.ticksPerSecond = static_cast<float>(
        anim->mTicksPerSecond != 0.0 ? anim->mTicksPerSecond : 25.0);

    for (unsigned int i = 0; i < anim->mNumChannels; i++) {
        aiNodeAnim *channel = anim->mChannels[i];
        if (!channel) {
            continue;
        }

        BoneAnimation boneAnim;

        for (unsigned int k = 0; k < channel->mNumPositionKeys; k++) {
            const aiVectorKey &key = channel->mPositionKeys[k];
            boneAnim.positions.push_back(
                {static_cast<float>(key.mTime),
                 XMFLOAT3{key.mValue.x, key.mValue.y, key.mValue.z}});
        }

        for (unsigned int k = 0; k < channel->mNumRotationKeys; k++) {
            const aiQuatKey &key = channel->mRotationKeys[k];
            boneAnim.rotations.push_back(
                {static_cast<float>(key.mTime),
                 XMFLOAT4{key.mValue.x, key.mValue.y, key.mValue.z,
                          key.mValue.w}});
        }

        for (unsigned int k = 0; k < channel->mNumScalingKeys; k++) {
            const aiVectorKey &key = channel->mScalingKeys[k];
            boneAnim.scales.push_back(
                {static_cast<float>(key.mTime),
                 XMFLOAT3{key.mValue.x, key.mValue.y, key.mValue.z}});
        }

        model.animation.channels[channel->mNodeName.C_Str()] = boneAnim;
    }
}

} // namespace

void AssimpLoader::Initialize(TextureManager *textureManager,
                              MeshManager *meshManager) {
    textureManager_ = textureManager;
    meshManager_ = meshManager;
}

Model AssimpLoader::Load(const std::string &path) {
    if (!textureManager_ || !meshManager_) {
        throw std::runtime_error("AssimpLoader is not initialized");
    }

    Assimp::Importer importer;

    const aiScene *scene = importer.ReadFile(
        path, aiProcess_Triangulate | aiProcess_FlipUVs |
                  aiProcess_JoinIdenticalVertices | aiProcess_LimitBoneWeights);

    if (!scene || !scene->HasMeshes()) {
        throw std::runtime_error("Assimp load failed");
    }

    aiMesh *mesh = scene->mMeshes[0];
    if (!mesh) {
        throw std::runtime_error("Mesh is null");
    }

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    vertices.reserve(mesh->mNumVertices);
    indices.reserve(mesh->mNumFaces * 3);

    for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
        Vertex v{};

        v.position = {mesh->mVertices[i].x, mesh->mVertices[i].y,
                      mesh->mVertices[i].z};

        if (mesh->HasTextureCoords(0)) {
            v.uv = {mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y};
        }

        vertices.push_back(v);
    }

    for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
        const aiFace &face = mesh->mFaces[i];

        for (unsigned int j = 0; j < face.mNumIndices; j++) {
            indices.push_back(face.mIndices[j]);
        }
    }

    Model model{};

    uint32_t textureId = 0;

    if (scene->HasMaterials() && mesh->mMaterialIndex < scene->mNumMaterials) {
        aiMaterial *mat = scene->mMaterials[mesh->mMaterialIndex];
        aiString texPath;

        if (mat &&
            mat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS) {
            std::string texName = texPath.C_Str();

            if (!texName.empty() && texName[0] == '*') {
                int texIndex = std::atoi(texName.c_str() + 1);

                if (texIndex >= 0 &&
                    static_cast<unsigned int>(texIndex) < scene->mNumTextures) {
                    aiTexture *tex = scene->mTextures[texIndex];

                    if (tex->mHeight == 0) {
                        textureId = textureManager_->LoadFromMemory(
                            reinterpret_cast<uint8_t *>(tex->pcData),
                            tex->mWidth);
                    } else {
                        textureId = textureManager_->LoadFromMemory(
                            reinterpret_cast<uint8_t *>(tex->pcData),
                            tex->mWidth * tex->mHeight * 4);
                    }
                }
            } else {
                std::filesystem::path modelPath(path);
                auto fullPath = modelPath.parent_path() / texName;
                textureId = textureManager_->Load(fullPath.wstring());
            }
        }
    }

    if (mesh->HasBones()) {
        for (unsigned int i = 0; i < mesh->mNumBones; i++) {
            aiBone *bone = mesh->mBones[i];
            if (!bone) {
                continue;
            }

            std::string boneName = bone->mName.C_Str();
            uint32_t boneIndex = 0;

            auto it = model.boneMap.find(boneName);
            if (it == model.boneMap.end()) {
                boneIndex = static_cast<uint32_t>(model.bones.size());
                model.boneMap[boneName] = boneIndex;

                BoneInfo info{};
                info.name = boneName;
                info.offsetMatrix = ToMatrix(bone->mOffsetMatrix);
                info.localBindMatrix = ToMatrix(aiMatrix4x4());
                info.parentIndex = -1;

                model.bones.push_back(info);
            } else {
                boneIndex = it->second;
            }

            for (unsigned int w = 0; w < bone->mNumWeights; w++) {
                uint32_t vertexId = bone->mWeights[w].mVertexId;
                float weight = bone->mWeights[w].mWeight;

                if (vertexId >= vertices.size()) {
                    continue;
                }

                for (int k = 0; k < 4; k++) {
                    if (vertices[vertexId].boneWeight[k] == 0.0f) {
                        vertices[vertexId].boneIndex[k] = boneIndex;
                        vertices[vertexId].boneWeight[k] = weight;
                        break;
                    }
                }
            }
        }

        NormalizeWeights(vertices);
        BuildBoneHierarchy(scene, model);
    }

    LoadAnimation(scene, model);

    uint32_t meshId = meshManager_->CreateMesh(
        vertices.data(), sizeof(Vertex), static_cast<uint32_t>(vertices.size()),
        indices.data(), static_cast<uint32_t>(indices.size()));

    model.meshId = meshId;
    model.textureId = textureId;
    model.finalBoneMatrices.resize(model.bones.size());

    return model;
}