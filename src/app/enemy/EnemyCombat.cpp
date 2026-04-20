#include "Enemy.h"

#include <algorithm>
#include <cmath>

OBB Enemy::MakeOBB(const Transform &tf, const DirectX::XMFLOAT3 &size) const {
    OBB box{};
    box.center = tf.position;
    box.center.y += size.y * 0.5f;
    box.size = size;
    box.rotation = tf.rotation;
    return box;
}

OBB Enemy::GetBodyOBB() const { return MakeOBB(bodyTf_, bodySize_); }
OBB Enemy::GetLeftHandOBB() const { return MakeOBB(leftHandTf_, handSize_); }
OBB Enemy::GetRightHandOBB() const { return MakeOBB(rightHandTf_, handSize_); }

OBB Enemy::GetAttackOBB() const {
    switch (action_.kind) {
    case ActionKind::Smash:
        return GetSmashAttackOBB();
    case ActionKind::Sweep:
        return GetSweepAttackOBB();
    default:
        return OBB{};
    }
}

OBB Enemy::GetSmashAttackOBB() const {
    const float usedYaw = ShouldUseLockedAttackYaw() ? lockedAttackYaw_ : facingYaw_;
    const float forwardX = std::sin(usedYaw);
    const float forwardZ = std::cos(usedYaw);
    const float rightX = std::cos(usedYaw);
    const float rightZ = -std::sin(usedYaw);

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
    const float usedYaw = ShouldUseLockedAttackYaw() ? lockedAttackYaw_ : facingYaw_;
    const float forwardX = std::sin(usedYaw);
    const float forwardZ = std::cos(usedYaw);
    const float rightX = std::cos(usedYaw);
    const float rightZ = -std::sin(usedYaw);
    const float handDirX = rightHandTf_.position.x - bodyTf_.position.x;
    const float handDirZ = rightHandTf_.position.z - bodyTf_.position.z;
    const float sideDot = handDirX * rightX + handDirZ * rightZ;
    const float sideSign = (sideDot >= 0.0f) ? 1.0f : -1.0f;

    Transform attackTf{};
    attackTf.scale = {1.0f, 1.0f, 1.0f};
    attackTf.rotation = rightHandTf_.rotation;
    attackTf.position = rightHandTf_.position;
    attackTf.position.x += rightX * (0.28f * sideSign) + forwardX * 0.24f;
    attackTf.position.y += 0.04f;
    attackTf.position.z += rightZ * (0.28f * sideSign) + forwardZ * 0.24f;

    return MakeOBB(attackTf, GetCurrentAttackHitBoxSize());
}

float Enemy::GetCurrentAttackDamage() const {
    const AttackParam *param = GetCurrentAttackParam();
    return param ? param->damage : 0.0f;
}

float Enemy::GetCurrentAttackKnockback() const {
    const AttackParam *param = GetCurrentAttackParam();
    return param ? param->knockback : 0.0f;
}

DirectX::XMFLOAT3 Enemy::GetCurrentAttackHitBoxSize() const {
    const AttackParam *param = GetCurrentAttackParam();
    return param ? param->hitBoxSize : DirectX::XMFLOAT3{0.1f, 0.1f, 0.1f};
}

float Enemy::GetDistanceToPlayer() const {
    const float dx = playerPos_.x - tf_.position.x;
    const float dy = playerPos_.y - tf_.position.y;
    const float dz = playerPos_.z - tf_.position.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

const AttackTimingParam *Enemy::GetCurrentAttackTiming() const {
    switch (action_.kind) {
    case ActionKind::Smash:
        return &config_.attacks.smash.melee.base.timing;
    case ActionKind::Sweep:
        return &config_.attacks.sweep.melee.base.timing;
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
    default:
        return nullptr;
    }
}

bool Enemy::IsCurrentAttackInActiveWindow() const {
    const AttackTimingParam *timing = GetCurrentAttackTiming();
    if (!timing) {
        return false;
    }
    const float t = GetCurrentActionTime();
    return t >= timing->activeStartTime && t <= timing->activeEndTime;
}

bool Enemy::IsCurrentAttackInRecoveryWindow() const {
    const AttackTimingParam *timing = GetCurrentAttackTiming();
    return timing ? GetCurrentActionTime() >= timing->recoveryStartTime : false;
}

bool Enemy::ShouldUseLockedAttackYaw() const {
    switch (action_.kind) {
    case ActionKind::Smash:
    case ActionKind::Sweep:
        return true;
    default:
        return false;
    }
}

void Enemy::TakeDamage(float damage) {
    if (deathFinished_ || isDying_ || introActive_) {
        return;
    }

    hp_ -= damage;
    if (hp_ < 0.0f) {
        hp_ = 0.0f;
    }

    UpdateBossPhase();
    if (hp_ <= 0.0f) {
        isDying_ = true;
        deathTimer_ = 0.0f;
        deathStartY_ = tf_.position.y;
        hitReactionTimer_ = 0.0f;
        tf_.scale = {1.0f, 1.0f, 1.0f};
        EndAttack();
        UpdateParts();
        return;
    }

    hitReactionTimer_ = hitReactionDuration_;
}

void Enemy::NotifyAttackConnected() { currentActionConnected_ = true; }

void Enemy::NotifyAttackGuarded() { currentActionGuarded_ = true; }

bool Enemy::NotifyCountered() { return ApplyCounterBreakReaction(); }

bool Enemy::ApplyCounterBreakReaction() {
    RegisterCounterSuccessReaction();

    const bool isCounterBreakableAction =
        action_.kind == ActionKind::Smash || action_.kind == ActionKind::Sweep;
    if (!isCounterBreakableAction) {
        return false;
    }

    float dx = tf_.position.x - playerPos_.x;
    float dz = tf_.position.z - playerPos_.z;
    float length = std::sqrt(dx * dx + dz * dz);
    if (length < 0.0001f) {
        length = 1.0f;
    }

    dx /= length;
    dz /= length;

    constexpr float counterPushBack = 0.9f;
    tf_.position.x += dx * counterPushBack;
    tf_.position.z += dz * counterPushBack;

    EndAttack();
    counterRecoilTimer_ = counterRecoilDuration_;
    ResetChainContext();
    ResetPostActionState();
    stateTimer_ = 0.0f;

    tactic_ = DecideTactic();
    closePressureTimer_ = 0.0f;
    stagnantTimer_ = 0.0f;
    isDistanceStagnant_ = false;

    UpdateFacingToPlayer();
    UpdateParts();
    return true;
}
