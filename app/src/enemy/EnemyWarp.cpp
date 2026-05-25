#include "Enemy.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {
float Random01() {
    return static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
}
} // namespace

void Enemy::UpdateWarpByStep(float deltaTime) {
    switch (action_.step) {
    case ActionStep::Start:
        UpdateWarpStart(deltaTime);
        break;
    case ActionStep::Move:
        UpdateWarpMove(deltaTime);
        break;
    case ActionStep::End:
        UpdateWarpEnd(deltaTime);
        break;
    default:
        EndAttack();
        break;
    }
}

bool Enemy::DecideWarpTargetNearPlayer(DirectX::XMFLOAT3 &outTarget) {
    float forwardX = playerPos_.x - tf_.position.x;
    float forwardZ = playerPos_.z - tf_.position.z;
    float forwardLength = std::sqrt(forwardX * forwardX + forwardZ * forwardZ);
    if (forwardLength <= 0.0001f) {
        forwardX = std::sin(facingYaw_);
        forwardZ = std::cos(facingYaw_);
        forwardLength = 1.0f;
    }
    forwardX /= forwardLength;
    forwardZ /= forwardLength;

    if (warp_.approachSlot == WarpApproachSlot::None) {
        warp_.approachSlot = (std::rand() % 100 < 42)
                                 ? WarpApproachSlot::Front
                                 : WarpApproachSlot::Back;
    }

    outTarget = playerPos_;
    if (warp_.approachSlot == WarpApproachSlot::Front) {
        outTarget.x -= forwardX * warpApproachFrontDistance_;
        outTarget.z -= forwardZ * warpApproachFrontDistance_;
    } else {
        outTarget.x += forwardX * warpApproachBackDistance_;
        outTarget.z += forwardZ * warpApproachBackDistance_;
    }
    outTarget.y = tf_.position.y;
    FinalizeWarpTargetFacing(outTarget);
    return true;
}

void Enemy::FinalizeWarpTargetFacing(DirectX::XMFLOAT3 &target) {
    const float dx = playerPos_.x - target.x;
    const float dz = playerPos_.z - target.z;
    if (dx * dx + dz * dz > 0.0001f) {
        warp_.targetYaw = NormalizeAngle(std::atan2(dx, dz));
    } else {
        warp_.targetYaw = NormalizeAngle(facingYaw_);
    }
    warp_.hasTargetYaw = true;
}

bool Enemy::PrepareWarpContext() {
    ResetWarpContext();

    const float distance = GetDistanceToPlayer();
    warp_.isCutIn = distance >= warpCutInDistance_;
    if (warp_.isCutIn) {
        warp_.approachSlot = WarpApproachSlot::Back;
    }

    if (!DecideWarpTargetNearPlayer(warp_.targetPos)) {
        ResetWarpContext();
        return false;
    }

    warp_.hasValidTarget = true;

    float feintChance = warpFeintChance_;
    if (playerObs_.isAttacking) {
        feintChance += 0.12f;
    }
    if (warp_.isCutIn) {
        feintChance *= 0.35f;
    }
    warp_.isFeint = Random01() < std::clamp(feintChance, 0.0f, 0.65f);

    if (!warp_.isFeint) {
        warp_.followupKind = SelectNearPressureAction();
        warp_.followupStep = ActionStep::Charge;
    }
    return true;
}

void Enemy::ResetWarpContext() { warp_ = WarpContext{}; }

void Enemy::UpdateWarpStart(float deltaTime) {
    if (!warp_.hasDeparturePos) {
        warp_.departurePos = tf_.position;
        warp_.hasDeparturePos = true;
    }

    UpdateFacingToPlayerWithSpeed(deltaTime, chargeTurnSpeed_ * 0.45f);
    isVisible_ = true;
    warp_.collisionDisabled = false;

    if (stateTimer_ >= config_.warp.startTime) {
        isVisible_ = false;
        warp_.collisionDisabled = true;
        EmitWarpTrailGhost(warp_.departurePos, warpTrailScaleMax_);
        ChangeActionStep(ActionStep::Move);
    }
}

void Enemy::UpdateWarpMove(float deltaTime) {
    (void)deltaTime;

    if (!warp_.hasValidTarget) {
        EndAttack();
        return;
    }

    float t = 1.0f;
    if (config_.warp.moveTime > 0.0001f) {
        t = stateTimer_ / config_.warp.moveTime;
    }
    t = std::clamp(t, 0.0f, 1.0f);

    const float eased = 1.0f - std::pow(1.0f - t, 2.6f);
    tf_.position.x =
        warp_.departurePos.x +
        (warp_.targetPos.x - warp_.departurePos.x) * eased;
    tf_.position.y =
        warp_.departurePos.y +
        (warp_.targetPos.y - warp_.departurePos.y) * eased;
    tf_.position.z =
        warp_.departurePos.z +
        (warp_.targetPos.z - warp_.departurePos.z) * eased;

    if (warp_.hasTargetYaw) {
        facingYaw_ = NormalizeAngle(warp_.targetYaw);
        LockCurrentFacing();
    }

    if (stateTimer_ >= config_.warp.moveTime) {
        tf_.position = warp_.targetPos;
        UpdateFacingToPlayer();
        LockCurrentFacing();
        ResetWarpTrails();
        EmitWarpTrailGhost(warp_.targetPos, warpTrailScaleMax_ * 1.14f);
        ChangeActionStep(ActionStep::End);
    }
}

void Enemy::UpdateWarpEnd(float deltaTime) {
    isVisible_ = true;
    warp_.collisionDisabled = false;
    UpdateFacingToPlayerWithSpeed(deltaTime, idleTurnSpeed_ * 0.75f);

    const float endTime =
        warp_.isFeint ? config_.warp.endTime * warpFeintEndTimeScale_
                      : config_.warp.endTime;
    if (stateTimer_ < endTime) {
        return;
    }

    const ActionKind followupKind = warp_.followupKind;
    const ActionStep followupStep = warp_.followupStep;
    const bool isFeint = warp_.isFeint;
    EndAttack();
    if (isFeint) {
        BeginStalkAction();
        return;
    }

    if (followupKind == ActionKind::Smash || followupKind == ActionKind::Sweep) {
        UpdateFacingToPlayer();
        LockCurrentFacing();
        BeginAction(followupKind, followupStep);
        return;
    }

    BeginChaseAction();
}

void Enemy::UpdateWarpTrails(float deltaTime) {
    for (auto &trail : warpTrailGhosts_) {
        if (!trail.isActive) {
            continue;
        }

        trail.life -= deltaTime;
        if (trail.life <= 0.0f) {
            trail.life = 0.0f;
            trail.isActive = false;
        }
    }
}

void Enemy::EmitWarpTrailGhost(const DirectX::XMFLOAT3 &position, float scale) {
    int slot = -1;
    for (int i = 0; i < kWarpTrailGhostCount_; ++i) {
        if (!warpTrailGhosts_[i].isActive) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        slot = 0;
    }

    warpTrailGhosts_[slot].position = position;
    warpTrailGhosts_[slot].life = warpTrailLife_;
    warpTrailGhosts_[slot].scale = scale;
    warpTrailGhosts_[slot].isActive = true;
}

void Enemy::ResetWarpTrails() {
    warpTrailEmitTimer_ = 0.0f;
    for (auto &trail : warpTrailGhosts_) {
        trail = WarpTrailGhost{};
    }
}
