#pragma once
#include <DirectXMath.h>

enum class ActionKind {
    None,
    Smash,
    Sweep,
    BladeClash,
    Warp,
    Stalk,
    ArcaneLaser,
    CataclysmLaser,
};

struct AttackParam {
    float damage = 0.0f;
    float knockback = 0.0f;
    DirectX::XMFLOAT3 hitBoxSize = {1.0f, 1.0f, 1.0f};
};
