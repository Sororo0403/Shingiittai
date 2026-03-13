#pragma once
#include <DirectXMath.h>
#include <cstdint>

struct Vertex {
    DirectX::XMFLOAT3 position{};
    DirectX::XMFLOAT2 uv{};

    DirectX::XMUINT4 boneIndex{0, 0, 0, 0};
    DirectX::XMFLOAT4 boneWeight{0.0f, 0.0f, 0.0f, 0.0f};
};