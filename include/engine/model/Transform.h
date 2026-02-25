#pragma once
#include <DirectXMath.h>

struct Transform {
    DirectX::XMFLOAT3 position{0, 0, 0};
    DirectX::XMFLOAT3 rotation{0, 0, 0};
    DirectX::XMFLOAT3 scale{1, 1, 1};
};