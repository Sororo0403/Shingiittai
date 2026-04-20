#pragma once
#include <DirectXMath.h>

struct WarpPostEffectParam {
    DirectX::XMFLOAT2 center; // 0～1 screen uv
    float radius;             // 0～1
    float strength;           // distortion strength
    float time;               // animation time

    DirectX::XMFLOAT2 center2; // 2点目（任意）
    float radius2;
    float strength2;

    float enabled; // 0 or 1
};