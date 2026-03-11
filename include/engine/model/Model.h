#pragma once
#include <DirectXMath.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct BoneInfo {
    DirectX::XMFLOAT4X4 offsetMatrix;
};

struct Model {
    uint32_t meshId = 0;
    uint32_t textureId = 0;

    std::vector<BoneInfo> bones;
    std::unordered_map<std::string, uint32_t> boneMap;
};