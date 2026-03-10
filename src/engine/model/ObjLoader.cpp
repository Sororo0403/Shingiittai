#include "ObjLoader.h"
#include "MeshManager.h"
#include "TextureManager.h"
#include "Vertex.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

using namespace DirectX;

void ObjLoader::Initialize(TextureManager *textureManager,
                           MeshManager *meshManager) {
    textureManager_ = textureManager;
    meshManager_ = meshManager;
}

Model ObjLoader::Load(const std::wstring &path) {
    if (!meshManager_ || !textureManager_) {
        throw std::runtime_error("ObjLoader not initialized");
    }

    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Failed to open OBJ file");
    }

    std::filesystem::path objPath(path);
    std::filesystem::path objDir = objPath.parent_path();

    // OBJ buffers
    std::vector<XMFLOAT3> positions;
    std::vector<XMFLOAT2> uvs;

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    // MTL state
    std::wstring mtlPath;
    std::string currentMaterial;

    // materialName -> diffuse texture path
    std::unordered_map<std::string, std::wstring> materialTextures;

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string type;
        iss >> type;

        // vertex position
        if (type == "v") {
            XMFLOAT3 p{};
            iss >> p.x >> p.y >> p.z;
            positions.push_back(p);
        }
        // texture coord
        else if (type == "vt") {
            XMFLOAT2 uv{};
            iss >> uv.x >> uv.y;
            uv.y = 1.0f - uv.y;
            uvs.push_back(uv);
        }
        // material library
        else if (type == "mtllib") {
            std::string mtlFile;
            iss >> mtlFile;

            mtlPath = (objDir / mtlFile).wstring();

            std::ifstream mtl(mtlPath);
            if (!mtl) {
                continue;
            }

            std::string mtlLine;
            std::string currentMtl;

            while (std::getline(mtl, mtlLine)) {
                std::istringstream miss(mtlLine);
                std::string mtlType;
                miss >> mtlType;

                if (mtlType == "newmtl") {
                    miss >> currentMtl;
                } else if (mtlType == "map_Kd" && !currentMtl.empty()) {
                    std::string tex;
                    miss >> tex;
                    materialTextures[currentMtl] = (objDir / tex).wstring();
                }
            }
        }
        // use material
        else if (type == "usemtl") {
            iss >> currentMaterial;
        }
        // face
        else if (type == "f") {
            std::string s[3];
            iss >> s[0] >> s[1] >> s[2];

            ObjIndex idx[3] = {
                ParseFaceToken(s[0]),
                ParseFaceToken(s[1]),
                ParseFaceToken(s[2]),
            };

            uint16_t base = static_cast<uint16_t>(vertices.size());

            for (int i = 0; i < 3; i++) {
                XMFLOAT2 uv{0.0f, 0.0f};
                if (idx[i].uv != UINT32_MAX && idx[i].uv < uvs.size()) {
                    uv = uvs[idx[i].uv];
                }

                vertices.push_back({positions[idx[i].pos], uv});

                indices.push_back(static_cast<uint16_t>(base + i));
            }
        }
    }

    // Mesh 作成
    uint32_t meshId = meshManager_->CreateMesh(
        vertices.data(), sizeof(Vertex), static_cast<uint32_t>(vertices.size()),
        indices.data(), static_cast<uint32_t>(indices.size()));

    // Texture 決定
    uint32_t textureId = 0;

    if (!currentMaterial.empty() &&
        materialTextures.contains(currentMaterial)) {

        textureId = textureManager_->Load(materialTextures[currentMaterial]);
    }

    // Model
    Model model;
    model.meshId = meshId;
    model.textureId = textureId;
    return model;
}

ObjIndex ObjLoader::ParseFaceToken(const std::string &token) {
    ObjIndex idx{};

    size_t firstSlash = token.find('/');
    size_t secondSlash = token.find('/', firstSlash + 1);

    // v
    idx.pos = std::stoi(token.substr(0, firstSlash)) - 1;

    // vt（無い場合あり）
    if (firstSlash != std::string::npos && secondSlash > firstSlash + 1) {
        idx.uv = std::stoi(token.substr(firstSlash + 1,
                                        secondSlash - firstSlash - 1)) -
                 1;
    } else {
        idx.uv = UINT32_MAX;
    }

    return idx;
}