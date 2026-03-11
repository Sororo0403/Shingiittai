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

} // namespace

void AssimpLoader::Initialize(TextureManager *textureManager,
                              MeshManager *meshManager) {
    textureManager_ = textureManager;
    meshManager_ = meshManager;
}

Model AssimpLoader::Load(const std::string &path) {

    Assimp::Importer importer;

    const aiScene *scene = importer.ReadFile(
        path, aiProcess_Triangulate | aiProcess_FlipUVs |
                  aiProcess_JoinIdenticalVertices | aiProcess_LimitBoneWeights);

    if (!scene || !scene->HasMeshes()) {
        throw std::runtime_error("Assimp load failed");
    }

    aiMesh *mesh = scene->mMeshes[0];

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    vertices.reserve(mesh->mNumVertices);

    for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
        Vertex v{};

        v.position = {mesh->mVertices[i].x, mesh->mVertices[i].y,
                      mesh->mVertices[i].z};

        if (mesh->HasTextureCoords(0)) {
            v.uv.x = mesh->mTextureCoords[0][i].x;
            v.uv.y = mesh->mTextureCoords[0][i].y;
        }

        vertices.push_back(v);
    }

    for (unsigned int i = 0; i < mesh->mNumFaces; i++) {

        const aiFace &face = mesh->mFaces[i];

        for (unsigned int j = 0; j < face.mNumIndices; j++) {
            indices.push_back(face.mIndices[j]);
        }
    }

    Model model;

    uint32_t textureId = 0;

    if (scene->HasMaterials()) {

        aiMaterial *mat = scene->mMaterials[mesh->mMaterialIndex];

        aiString texPath;

        if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS) {

            std::string texName = texPath.C_Str();

            if (!texName.empty() && texName[0] == '*') {

                int texIndex = std::atoi(texName.c_str() + 1);
                aiTexture *tex = scene->mTextures[texIndex];

                if (tex->mHeight == 0) {

                    textureId = textureManager_->LoadFromMemory(
                        reinterpret_cast<uint8_t *>(tex->pcData), tex->mWidth);

                } else {

                    textureId = textureManager_->LoadFromMemory(
                        reinterpret_cast<uint8_t *>(tex->pcData),
                        tex->mWidth * tex->mHeight * 4);
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
            std::string boneName = bone->mName.C_Str();

            uint32_t boneIndex = 0;

            auto it = model.boneMap.find(boneName);

            if (it == model.boneMap.end()) {
                boneIndex = static_cast<uint32_t>(model.bones.size());
                model.boneMap[boneName] = boneIndex;

                BoneInfo info{};
                info.offsetMatrix = ToMatrix(bone->mOffsetMatrix);

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
    }

    if (scene->HasAnimations()) {
        aiAnimation *anim = scene->mAnimations[0];

        model.animation.duration = static_cast<float>(anim->mDuration);

        model.animation.ticksPerSecond = static_cast<float>(
            anim->mTicksPerSecond != 0 ? anim->mTicksPerSecond : 25.0);

        for (unsigned int i = 0; i < anim->mNumChannels; i++) {
            aiNodeAnim *channel = anim->mChannels[i];

            BoneAnimation boneAnim;

            for (unsigned int k = 0; k < channel->mNumPositionKeys; k++) {
                auto &key = channel->mPositionKeys[k];

                boneAnim.positions.push_back(
                    {static_cast<float>(key.mTime),
                     {key.mValue.x, key.mValue.y, key.mValue.z}});
            }

            for (unsigned int k = 0; k < channel->mNumRotationKeys; k++) {
                auto &key = channel->mRotationKeys[k];

                boneAnim.rotations.push_back(
                    {static_cast<float>(key.mTime),
                     {key.mValue.x, key.mValue.y, key.mValue.z, key.mValue.w}});
            }

            for (unsigned int k = 0; k < channel->mNumScalingKeys; k++) {
                auto &key = channel->mScalingKeys[k];

                boneAnim.scales.push_back(
                    {static_cast<float>(key.mTime),
                     {key.mValue.x, key.mValue.y, key.mValue.z}});
            }

            model.animation.channels[channel->mNodeName.C_Str()] = boneAnim;
        }
    }

    uint32_t meshId = meshManager_->CreateMesh(
        vertices.data(), sizeof(Vertex), static_cast<uint32_t>(vertices.size()),
        indices.data(), static_cast<uint32_t>(indices.size()));

    model.meshId = meshId;
    model.textureId = textureId;

    return model;
}