#pragma once
#include <DirectXMath.h>

/// <summary>
/// 敵が実行できる行動の種類
/// </summary>
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

/// <summary>
/// 攻撃の予備動作、持続、硬直、威力をまとめた調整値
/// </summary>
struct AttackParam {
    float damage = 0.0f;
    float knockback = 0.0f;
    DirectX::XMFLOAT3 hitBoxSize = {1.0f, 1.0f, 1.0f};
};
