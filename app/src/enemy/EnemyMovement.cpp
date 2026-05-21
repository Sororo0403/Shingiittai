#include "Enemy.h"

#include <cmath>
#include <cstdlib>

void Enemy::ClampToArena() {
}

void Enemy::UpdateStalkByStep(float deltaTime) {
    switch (action_.step) {
    case ActionStep::Move:
        UpdateStalkMove(deltaTime);
        break;
    default:
        EndAttack();
        break;
    }
}

void Enemy::UpdateStalkMove(float deltaTime) {
    UpdateFacingToPlayerWithSpeed(deltaTime, idleTurnSpeed_ * 1.15f);

    const float usedYaw = facingYaw_;
    const float forwardX = std::sin(usedYaw);
    const float forwardZ = std::cos(usedYaw);

    const float distance = GetDistanceToPlayer();
    float moveX = 0.0f;
    float moveZ = 0.0f;
    if (distance > config_.core.nearAttackDistance) {
        moveX = forwardX;
        moveZ = forwardZ;
    }

    tf_.position.x += moveX * stalkMoveSpeed_ * deltaTime;
    tf_.position.z += moveZ * stalkMoveSpeed_ * deltaTime;

    const float pounceDistance =
        config_.core.nearAttackDistance + stalkPounceDistanceBonus_;
    if (stateTimer_ >= stalkPounceMinTime_ &&
        GetDistanceToPlayer() <= pounceDistance) {
        float chance = stalkPounceChance_;
        if (phase_ != BossPhase::Phase1) {
            chance += 0.18f;
        }
        if (playerObs_.isAttacking || playerObs_.isGuarding) {
            chance += 0.10f;
        }

        const float roll =
            static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
        if (roll < chance) {
            BeginPressureAction();
            return;
        }
    }

    if (stateTimer_ >= currentHoldDuration_) {
        EndAttack();
    }
}

void Enemy::BeginStalkAction() {
    BeginAction(ActionKind::Stalk, ActionStep::Move);
    EnterHold(RandomRange(stalkDurationMin_, stalkDurationMax_));
    stalkRepeatCount_++;
}

void Enemy::UpdateFacingToPlayer() {
    const float dx = playerPos_.x - tf_.position.x;
    const float dz = playerPos_.z - tf_.position.z;
    facingYaw_ = std::atan2(dx, dz);
    SyncBaseRotationToFacing();
}

void Enemy::FaceTargetImmediately(const DirectX::XMFLOAT3 &targetPosition) {
    playerPos_ = targetPosition;
    const float dx = targetPosition.x - tf_.position.x;
    const float dz = targetPosition.z - tf_.position.z;
    facingYaw_ = std::atan2(dx, dz);
    lockedAttackYaw_ = facingYaw_;
    SyncBaseRotationToFacing();
    UpdateParts();
}

void Enemy::LockCurrentFacing() {
    lockedAttackYaw_ = facingYaw_;
    SyncBaseRotationToFacing();
}

void Enemy::SyncBaseRotationToFacing() {
    DirectX::XMVECTOR rot =
        DirectX::XMQuaternionRotationRollPitchYaw(0.0f, facingYaw_, 0.0f);
    DirectX::XMStoreFloat4(&tf_.rotation, rot);
}

float Enemy::NormalizeAngle(float angle) const {
    while (angle > 3.14159265f) {
        angle -= 6.28318530f;
    }
    while (angle < -3.14159265f) {
        angle += 6.28318530f;
    }
    return angle;
}

void Enemy::UpdateFacingToPlayerWithSpeed(float deltaTime, float turnSpeed) {
    const float dx = playerPos_.x - tf_.position.x;
    const float dz = playerPos_.z - tf_.position.z;

    const float targetYaw = std::atan2(dx, dz);
    float diff = NormalizeAngle(targetYaw - facingYaw_);
    const float maxStep = turnSpeed * deltaTime;

    if (diff > maxStep) {
        diff = maxStep;
    } else if (diff < -maxStep) {
        diff = -maxStep;
    }

    facingYaw_ = NormalizeAngle(facingYaw_ + diff);
    SyncBaseRotationToFacing();
}

float Enemy::GetVisualYaw() const {
    if (ShouldUseLockedAttackYaw()) {
        return lockedAttackYaw_;
    }
    return facingYaw_;
}

float Enemy::GetTelegraphYaw() const { return GetVisualYaw(); }
