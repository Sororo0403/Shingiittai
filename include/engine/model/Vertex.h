#pragma once
#include <DirectXMath.h>
#include <cstdint>

struct Vertex {
    DirectX::XMFLOAT3 position{};
    DirectX::XMFLOAT2 uv{};
};
