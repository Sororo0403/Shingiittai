#include "Enemy.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

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

bool Enemy::IsWarpSuspendedForPresentation() const {
    return suspendWarpForPresentation_;
}

bool Enemy::DecideWarpTargetNearPlayer(DirectX::XMFLOAT3 &outTarget) {
    float playerForwardX = std::sin(playerObs_.facingYaw);
    float playerForwardZ = std::cos(playerObs_.facingYaw);
    float playerForwardLength =
        std::sqrt(playerForwardX * playerForwardX + playerForwardZ * playerForwardZ);

    if (playerForwardLength <= 0.0001f) {
        playerForwardX = playerObs_.velocity.x;
        playerForwardZ = playerObs_.velocity.z;
        playerForwardLength =
            std::sqrt(playerForwardX * playerForwardX + playerForwardZ * playerForwardZ);
    }

    if (playerForwardLength <= 0.0001f) {
        playerForwardX = std::sin(facingYaw_);
        playerForwardZ = std::cos(facingYaw_);
        playerForwardLength = 1.0f;
    }

    playerForwardX /= playerForwardLength;
    playerForwardZ /= playerForwardLength;

    const float toPlayerX = playerPos_.x - tf_.position.x;
    const float toPlayerZ = playerPos_.z - tf_.position.z;
    const float nearDistance = config_.core.nearAttackDistance;
    const bool isAlreadyNear =
        (toPlayerX * toPlayerX + toPlayerZ * toPlayerZ) <=
        nearDistance * nearDistance;

    if (warp_.approachSlot == WarpApproachSlot::None) {
        warp_.approachSlot = isAlreadyNear
                                 ? WarpApproachSlot::Back
                                 : ((std::rand() % 100 < 48)
                                        ? WarpApproachSlot::Front
                                        : WarpApproachSlot::Back);
    } else if (isAlreadyNear && warp_.approachSlot == WarpApproachSlot::Front) {
        warp_.approachSlot = WarpApproachSlot::Back;
    }

    outTarget = playerPos_;
    if (warp_.approachSlot == WarpApproachSlot::Front) {
        outTarget.x += playerForwardX * warpApproachFrontDistance_;
        outTarget.z += playerForwardZ * warpApproachFrontDistance_;
    } else {
        outTarget.x -= playerForwardX * warpApproachBackDistance_;
        outTarget.z -= playerForwardZ * warpApproachBackDistance_;
    }
    outTarget.y = tf_.position.y;
    FinalizeWarpTargetFacing(outTarget);
    return true;
}

bool Enemy::DecideWarpTargetFarFromPlayer(DirectX::XMFLOAT3 &outTarget) {
    const float angle =
        static_cast<float>(std::rand() % 360) * 3.14159265f / 180.0f;
    const float t =
        static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    const float radius =
        warpNearRadiusMax_ + (warpNearRadiusMax_ - warpNearRadiusMin_) * t + 2.0f;

    outTarget = playerPos_;
    outTarget.x += std::cos(angle) * radius;
    outTarget.z += std::sin(angle) * radius;
    outTarget.y = tf_.position.y;
    FinalizeWarpTargetFacing(outTarget);
    return true;
}

bool Enemy::DecideWarpTargetInPlayerView(DirectX::XMFLOAT3 &outTarget) {
    float forwardX = std::sin(playerObs_.facingYaw);
    float forwardZ = std::cos(playerObs_.facingYaw);
    float forwardLength =
        std::sqrt(forwardX * forwardX + forwardZ * forwardZ);

    if (forwardLength <= 0.0001f) {
        forwardX = playerObs_.velocity.x;
        forwardZ = playerObs_.velocity.z;
        forwardLength =
            std::sqrt(forwardX * forwardX + forwardZ * forwardZ);
    }
    if (forwardLength <= 0.0001f) {
        forwardX = std::sin(facingYaw_);
        forwardZ = std::cos(facingYaw_);
        forwardLength = 1.0f;
    }

    forwardX /= forwardLength;
    forwardZ /= forwardLength;
    const float rightX = forwardZ;
    const float rightZ = -forwardX;

    const float laneT =
        static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    const float jitterT =
        static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    const float spreadT =
        static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);

    float sideOffset = 0.0f;
    float forwardOffset = 0.0f;
    const float sideSign = (std::rand() % 2 == 0) ? -1.0f : 1.0f;
    const float jitter = -0.45f + 0.90f * jitterT;
    if (laneT < 0.28f) {
        sideOffset = sideSign * (4.85f + 1.75f * spreadT);
        forwardOffset = 2.65f + 1.65f * jitterT;
    } else if (laneT < 0.56f) {
        sideOffset = sideSign * (2.90f + 1.55f * spreadT);
        forwardOffset = 6.35f + 1.75f * jitterT;
    } else if (laneT < 0.78f) {
        sideOffset = jitter * 1.45f;
        forwardOffset = 7.35f + 1.65f * spreadT;
    } else {
        sideOffset = jitter * 1.85f;
        forwardOffset = 1.85f + 0.95f * spreadT;
    }

    outTarget = playerPos_;
    outTarget.x += forwardX * forwardOffset + rightX * sideOffset;
    outTarget.z += forwardZ * forwardOffset + rightZ * sideOffset;
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
    if (IsWarpSuspendedForPresentation()) {
        ResetWarpContext();
        return false;
    }

    ResetWarpContext();
    warp_.type = WarpType::Approach;
    if (!DecideWarpTargetNearPlayer(warp_.targetPos)) {
        ResetWarpContext();
        return false;
    }

    warp_.hasValidTarget = true;
    warp_.followupKind = SelectNearPressureAction();
    warp_.followupStep = ActionStep::Charge;
    return true;
}

void Enemy::ResetWarpContext() { warp_ = WarpContext{}; }

void Enemy::UpdateWarpStart(float deltaTime) {
    if (!warp_.hasDeparturePos) {
        warp_.departurePos = tf_.position;
        warp_.hasDeparturePos = true;
    }

    if (warp_.type == WarpType::Approach) {
        UpdateFacingToPlayerWithSpeed(deltaTime, chargeTurnSpeed_ * 0.45f);
    }

    isVisible_ = true;
    warp_.collisionDisabled = false;

    float startTime = config_.warp.startTime;
    if (action_.id == ActionId::WarpBackstab) {
        startTime *= 0.55f;
        if (startTime < 0.08f) {
            startTime = 0.08f;
        }
    }

    if (stateTimer_ >= startTime) {
        isVisible_ = false;
        warp_.collisionDisabled = true;
        warpTrailEmitTimer_ = 0.0f;
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
    const float moveTime = config_.warp.moveTime;
    if (moveTime > 0.0001f) {
        t = stateTimer_ / moveTime;
    }
    t = (t < 0.0f) ? 0.0f : ((t > 1.0f) ? 1.0f : t);

    const float eased = 1.0f - std::pow(1.0f - t, 2.6f);
    tf_.position.x =
        warp_.departurePos.x + (warp_.targetPos.x - warp_.departurePos.x) * eased;
    tf_.position.y =
        warp_.departurePos.y + (warp_.targetPos.y - warp_.departurePos.y) * eased;
    tf_.position.z =
        warp_.departurePos.z + (warp_.targetPos.z - warp_.departurePos.z) * eased;

    warpTrailEmitTimer_ = 0.0f;

    if (warp_.hasTargetYaw) {
        facingYaw_ = NormalizeAngle(warp_.targetYaw);
    } else {
        UpdateFacingToPlayer();
    }
    LockCurrentFacing();

    if (stateTimer_ >= moveTime) {
        tf_.position = warp_.targetPos;
        const bool hasMeleeFollowup =
            warp_.followupKind == ActionKind::Smash ||
            warp_.followupKind == ActionKind::Sweep;
        if (warp_.faceLivePlayerOnEnd || hasMeleeFollowup) {
            UpdateFacingToPlayer();
            LockCurrentFacing();
        } else if (warp_.hasTargetYaw) {
            facingYaw_ = NormalizeAngle(warp_.targetYaw);
            LockCurrentFacing();
        }
        ResetWarpTrails();
        EmitWarpTrailGhost(warp_.targetPos, warpTrailScaleMax_ * 1.14f);
        ChangeActionStep(ActionStep::End);
    }
}

void Enemy::UpdateWarpEnd(float deltaTime) {
    isVisible_ = true;
    warp_.collisionDisabled = false;
    const bool hasMeleeFollowup =
        warp_.followupKind == ActionKind::Smash ||
        warp_.followupKind == ActionKind::Sweep;
    if (warp_.faceLivePlayerOnEnd || hasMeleeFollowup) {
        UpdateFacingToPlayer();
        LockCurrentFacing();
    } else if (warp_.hasTargetYaw) {
        facingYaw_ = NormalizeAngle(warp_.targetYaw);
        LockCurrentFacing();
    } else {
        UpdateFacingToPlayerWithSpeed(deltaTime, idleTurnSpeed_ * 0.55f);
    }

    const float endTime = config_.warp.endTime;
    if (stateTimer_ < endTime) {
        return;
    }

    const ActionKind followupKind = warp_.followupKind;
    const ActionStep followupStep = warp_.followupStep;
    const WarpType warpType = warp_.type;

    EndAttack();
    if (warpType == WarpType::Approach && followupKind != ActionKind::None &&
        followupStep != ActionStep::None) {
        if ((followupKind == ActionKind::Smash ||
             followupKind == ActionKind::Sweep)) {
            UpdateFacingToPlayer();
            LockCurrentFacing();
        }
        tactic_ = TacticState::Melee;
        BeginAction(followupKind, followupStep);
        return;
    }

    tactic_ = DecideTactic();
    BeginActionFromTactic(tactic_);
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
        for (int i = 1; i < kWarpTrailGhostCount_; ++i) {
            if (warpTrailGhosts_[i].life < warpTrailGhosts_[slot].life) {
                slot = i;
            }
        }
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
