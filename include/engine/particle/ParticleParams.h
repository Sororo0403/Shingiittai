#pragma once
#include <DirectXMath.h>

struct ParticleParams {
    float emissionRate = 10.0f;
    float lifeTime = 1.0f;
    float speed = 3.0f;
    float startScale = 0.2f;

    DirectX::XMFLOAT3 baseDirection = {0, 0, 1};
    DirectX::XMFLOAT4 startColor = {1, 1, 1, 1};
};