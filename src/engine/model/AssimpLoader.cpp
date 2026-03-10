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

void AssimpLoader::Initialize(TextureManager *textureManager,
                              MeshManager *meshManager) {
    textureManager_ = textureManager;
    meshManager_ = meshManager;
}

Model AssimpLoader::Load(const std::string &path) {
    Assimp::Importer importer;

    const aiScene *scene =
        importer.ReadFile(path, aiProcess_Triangulate | aiProcess_FlipUVs |
                                    aiProcess_JoinIdenticalVertices);

    if (!scene || !scene->HasMeshes()) {
        throw std::runtime_error("Assimp load failed");
    }

    aiMesh *mesh = scene->mMeshes[0];

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    vertices.reserve(mesh->mNumVertices);

    for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
        XMFLOAT3 pos{mesh->mVertices[i].x, mesh->mVertices[i].y,
                     mesh->mVertices[i].z};

        XMFLOAT2 uv{0, 0};

        if (mesh->HasTextureCoords(0)) {
            uv.x = mesh->mTextureCoords[0][i].x;
            uv.y = mesh->mTextureCoords[0][i].y;
        }

        vertices.push_back({pos, uv});
    }

    for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
        const aiFace &face = mesh->mFaces[i];

        for (unsigned int j = 0; j < face.mNumIndices; j++) {
            indices.push_back(face.mIndices[j]);
        }
    }

    uint32_t meshId = meshManager_->CreateMesh(
        vertices.data(), sizeof(Vertex), static_cast<uint32_t>(vertices.size()),
        indices.data(), static_cast<uint32_t>(indices.size()));

    uint32_t textureId = 0;

    if (scene->HasMaterials()) {
        aiMaterial *mat = scene->mMaterials[mesh->mMaterialIndex];

        aiString texPath;

        if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS) {
            std::filesystem::path modelPath(path);

            auto fullPath = modelPath.parent_path() / texPath.C_Str();

            textureId = textureManager_->Load(fullPath.wstring());
        }
    }

    Model model;
    model.meshId = meshId;
    model.textureId = textureId;

    return model;
}