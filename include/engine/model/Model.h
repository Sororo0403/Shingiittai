#pragma once
#include <DirectXMath.h>
#include <cstdint>

struct Model {
    DirectX::XMFLOAT3 position{0, 0, 0};
    DirectX::XMFLOAT3 rotation{0, 0, 0};
    DirectX::XMFLOAT3 scale{1, 1, 1};

    uint32_t meshId = 0;
    uint32_t textureId = 0;
};
