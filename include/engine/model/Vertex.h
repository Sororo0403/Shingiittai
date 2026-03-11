#pragma once
#include <DirectXMath.h>
#include <cstdint>

struct Vertex {
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT2 uv;

    uint32_t boneIndex[4] = {0, 0, 0, 0};
    float boneWeight[4] = {0, 0, 0, 0};
};