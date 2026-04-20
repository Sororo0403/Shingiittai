#include "Enemy.h"
#include "ModelManager.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

// ============================================================
// OBB生成共通処理
// ============================================================
OBB Enemy::MakeOBB(const Transform &tf, const DirectX::XMFLOAT3 &size) const {
    OBB box{};
    box.center = tf.position;
    box.center.y += size.y * 0.5f;
    box.size = size;
    box.rotation = tf.rotation;
    return box;
}

// ============================================================
// 各部位の当たり判定取得
// ============================================================
OBB Enemy::GetBodyOBB() const { return MakeOBB(bodyTf_, bodySize_); }
OBB Enemy::GetLeftHandOBB() const { return MakeOBB(leftHandTf_, handSize_); }
OBB Enemy::GetRightHandOBB() const { return MakeOBB(rightHandTf_, handSize_); }

// ============================================================
// 攻撃用OBB取得
// ============================================================
OBB Enemy::GetAttackOBB() const {
    switch (action_.kind) {
    case ActionKind::Smash:
        return GetSmashAttackOBB();
    case ActionKind::Sweep:
        return GetSweepAttackOBB();
    case ActionKind::Rush:
        return GetRushAttackOBB();
    default:
        return OBB{};
    }
}

OBB Enemy::GetSmashAttackOBB() const {
    float usedYaw = ShouldUseLockedAttackYaw() ? lockedAttackYaw_ : facingYaw_;

    float forwardX = std::sinf(usedYaw);
    float forwardZ = std::cosf(usedYaw);
    float rightX = std::cosf(usedYaw);
    float rightZ = -std::sinf(usedYaw);

    Transform attackTf{};
    attackTf.scale = {1.0f, 1.0f, 1.0f};
    attackTf.rotation = rightHandTf_.rotation;
    attackTf.position = rightHandTf_.position;
    attackTf.position.x += forwardX * 0.50f + rightX * 0.08f;
    attackTf.position.y += 0.05f;
    attackTf.position.z += forwardZ * 0.50f + rightZ * 0.08f;

    return MakeOBB(attackTf, GetCurrentAttackHitBoxSize());
}

OBB Enemy::GetSweepAttackOBB() const {
    float usedYaw = ShouldUseLockedAttackYaw() ? lockedAttackYaw_ : facingYaw_;

    float forwardX = std::sinf(usedYaw);
    float forwardZ = std::cosf(usedYaw);
    float rightX = std::cosf(usedYaw);
    float rightZ = -std::sinf(usedYaw);
    float handDirX = rightHandTf_.position.x - bodyTf_.position.x;
    float handDirZ = rightHandTf_.position.z - bodyTf_.position.z;
    float sideDot = handDirX * rightX + handDirZ * rightZ;
    float sideSign = (sideDot >= 0.0f) ? 1.0f : -1.0f;

    Transform attackTf{};
    attackTf.scale = {1.0f, 1.0f, 1.0f};
    attackTf.rotation = rightHandTf_.rotation;
    attackTf.position = rightHandTf_.position;
    attackTf.position.x += rightX * (0.28f * sideSign) + forwardX * 0.24f;
    attackTf.position.y += 0.04f;
    attackTf.position.z += rightZ * (0.28f * sideSign) + forwardZ * 0.24f;

    return MakeOBB(attackTf, GetCurrentAttackHitBoxSize());
}

OBB Enemy::GetRushAttackOBB() const {
    float usedYaw = rushCurrentYaw_;

    float forwardX = std::sinf(usedYaw);
    float forwardZ = std::cosf(usedYaw);

    Transform attackTf{};
    attackTf.scale = {1.0f, 1.0f, 1.0f};
    attackTf.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    attackTf.position = bodyTf_.position;
    attackTf.position.x += forwardX * config_.attacks.rush.attackForwardOffset;
    attackTf.position.y += config_.attacks.rush.attackHeightOffset;
    attackTf.position.z += forwardZ * config_.attacks.rush.attackForwardOffset;

    return MakeOBB(attackTf, GetCurrentAttackHitBoxSize());
}

float Enemy::GetCurrentAttackDamage() const {
    const AttackParam *param = GetCurrentAttackParam();
    if (!param) {
        return 0.0f;
    }
    return param->damage;
}

float Enemy::GetCurrentAttackKnockback() const {
    const AttackParam *param = GetCurrentAttackParam();
    if (!param) {
        return 0.0f;
    }
    return param->knockback;
}

DirectX::XMFLOAT3 Enemy::GetCurrentAttackHitBoxSize() const {
    const AttackParam *param = GetCurrentAttackParam();
    if (!param) {
        return {0.1f, 0.1f, 0.1f};
    }
    return param->hitBoxSize;
}

// ============================================================
// プレイヤーとの距離計算
// ============================================================
float Enemy::GetDistanceToPlayer() const {
    float dx = playerPos_.x - tf_.position.x;
    float dy = playerPos_.y - tf_.position.y;
    float dz = playerPos_.z - tf_.position.z;

    return std::sqrtf(dx * dx + dy * dy + dz * dz);
}

const AttackTimingParam *Enemy::GetCurrentAttackTiming() const {
    switch (action_.kind) {
    case ActionKind::Smash:
        return &config_.attacks.smash.melee.base.timing;
    case ActionKind::Sweep:
        return &config_.attacks.sweep.melee.base.timing;
    case ActionKind::Rush:
        return &config_.attacks.rush.base.timing;
    default:
        return nullptr;
    }
}

AttackParam *Enemy::GetCurrentAttackParam() {
    switch (action_.kind) {
    case ActionKind::Smash:
        return &config_.attacks.smash.melee.base.attack;
    case ActionKind::Sweep:
        return &config_.attacks.sweep.melee.base.attack;
    case ActionKind::Shot:
        return &config_.attacks.shot.attack;
    case ActionKind::Wave:
        return &config_.attacks.wave.attack;
    case ActionKind::Rush:
        return &config_.attacks.rush.base.attack;
    default:
        return nullptr;
    }
}

const AttackParam *Enemy::GetCurrentAttackParam() const {
    switch (action_.kind) {
    case ActionKind::Smash:
        return &config_.attacks.smash.melee.base.attack;
    case ActionKind::Sweep:
        return &config_.attacks.sweep.melee.base.attack;
    case ActionKind::Shot:
        return &config_.attacks.shot.attack;
    case ActionKind::Wave:
        return &config_.attacks.wave.attack;
    case ActionKind::Rush:
        return &config_.attacks.rush.base.attack;
    default:
        return nullptr;
    }
}

bool Enemy::IsCurrentAttackInActiveWindow() const {
    const AttackTimingParam *timing = GetCurrentAttackTiming();
    if (!timing) {
        return false;
    }

    float t = GetCurrentActionTime();
    return (t >= timing->activeStartTime && t <= timing->activeEndTime);
}

bool Enemy::IsCurrentAttackInRecoveryWindow() const {
    const AttackTimingParam *timing = GetCurrentAttackTiming();
    if (!timing) {
        return false;
    }

    return GetCurrentActionTime() >= timing->recoveryStartTime;
}

// ============================================================
// 現在の攻撃が、向き固定して攻撃判定を出すタイプか
// ============================================================
bool Enemy::ShouldUseLockedAttackYaw() const {
    switch (action_.kind) {
    case ActionKind::Smash:
    case ActionKind::Sweep:
    case ActionKind::Rush:
        return true;
    default:
        return false;
    }
}
