#include "Enemy.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {
float Random01() {
    return static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
}

float PhantomWarpMoveTime(bool finalWarp) { return finalWarp ? 0.24f : 0.20f; }

float PhantomWarpEndTime(bool finalWarp) { return finalWarp ? 0.18f : 0.42f; }
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

bool Enemy::DecideWarpTargetFarSlash(DirectX::XMFLOAT3 &outTarget) {
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

    const float sideSign = (std::rand() % 2 == 0) ? -1.0f : 1.0f;
    const float sideT = Random01();
    const float rightX = forwardZ;
    const float rightZ = -forwardX;

    outTarget = playerPos_;
    outTarget.x -= forwardX * farWarpSlashDistance_;
    outTarget.z -= forwardZ * farWarpSlashDistance_;
    outTarget.x += rightX * sideSign * (1.6f + 2.4f * sideT);
    outTarget.z += rightZ * sideSign * (1.6f + 2.4f * sideT);
    outTarget.y = tf_.position.y;
    FinalizeWarpTargetFacing(outTarget);
    return true;
}

bool Enemy::DecideWarpTargetInPlayerView(DirectX::XMFLOAT3 &outTarget) {
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
    const float rightX = forwardZ;
    const float rightZ = -forwardX;

    const float sideSign = (std::rand() % 2 == 0) ? -1.0f : 1.0f;
    const float laneT = Random01();
    const float sideOffset = sideSign * (2.2f + 4.2f * Random01());
    const float forwardOffset = 2.0f + 5.8f * laneT;

    outTarget = playerPos_;
    outTarget.x += forwardX * forwardOffset + rightX * sideOffset;
    outTarget.z += forwardZ * forwardOffset + rightZ * sideOffset;
    outTarget.y = tf_.position.y;
    FinalizeWarpTargetFacing(outTarget);
    return true;
}

bool Enemy::DecideWarpTargetBehindPlayer(DirectX::XMFLOAT3 &outTarget) {
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

    outTarget = playerPos_;
    outTarget.x += forwardX * warpApproachBackDistance_;
    outTarget.z += forwardZ * warpApproachBackDistance_;
    outTarget.y = tf_.position.y;
    FinalizeWarpTargetFacing(outTarget);
    return true;
}

bool Enemy::RefreshLiveBehindWarpTarget() {
    const bool shouldRefresh =
        (warp_.feintFollowup && warp_.approachSlot == WarpApproachSlot::Back) ||
        (warp_.phantomChain && warp_.phantomFinal);
    if (!shouldRefresh) {
        return false;
    }

    return DecideWarpTargetBehindPlayer(warp_.targetPos);
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

    float startTime = config_.warp.startTime;
    if (warp_.phantomChain) {
        startTime = warp_.phantomFinal ? 0.13f : 0.11f;
    } else if (warp_.feintFollowup) {
        startTime *= 0.55f;
    } else if (warp_.farSlashFollowup) {
        startTime *= 0.44f;
    }

    if (stateTimer_ >= startTime) {
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
    RefreshLiveBehindWarpTarget();

    float t = 1.0f;
    const float moveTime =
        warp_.phantomChain
            ? PhantomWarpMoveTime(warp_.phantomFinal)
            : warp_.farSlashFollowup ? config_.warp.moveTime * 0.72f
                                      : config_.warp.moveTime;
    if (moveTime > 0.0001f) {
        t = stateTimer_ / moveTime;
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

    if (stateTimer_ >= moveTime) {
        tf_.position = warp_.targetPos;
        const bool delayFinalLock = warp_.phantomChain && warp_.phantomFinal;
        if ((warp_.faceLivePlayerOnEnd ||
             warp_.followupKind == ActionKind::Smash ||
             warp_.followupKind == ActionKind::Sweep) &&
            !delayFinalLock) {
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
    RefreshLiveBehindWarpTarget();
    const bool delayFinalLock = warp_.phantomChain && warp_.phantomFinal;
    if ((warp_.faceLivePlayerOnEnd || warp_.followupKind == ActionKind::Smash ||
         warp_.followupKind == ActionKind::Sweep) &&
        !delayFinalLock) {
        UpdateFacingToPlayer();
        LockCurrentFacing();
    } else if (warp_.hasTargetYaw) {
        facingYaw_ = NormalizeAngle(warp_.targetYaw);
        LockCurrentFacing();
    } else {
        UpdateFacingToPlayerWithSpeed(deltaTime, idleTurnSpeed_ * 0.75f);
    }

    const float endTime =
        warp_.phantomChain
            ? PhantomWarpEndTime(warp_.phantomFinal)
            : warp_.isFeint ? config_.warp.endTime * warpFeintEndTimeScale_
                             : warp_.farSlashFollowup
                                   ? config_.warp.endTime * 0.38f
                                   : config_.warp.endTime;
    if (stateTimer_ < endTime) {
        return;
    }

    const ActionKind followupKind = warp_.followupKind;
    const ActionStep followupStep = warp_.followupStep;
    const bool isFeint = warp_.isFeint;
    const bool feintFollowup = warp_.feintFollowup;
    const bool immediateFollowup = warp_.immediateFollowup;
    const bool farSlashFollowup = warp_.farSlashFollowup;
    const bool phantomChain = warp_.phantomChain;
    const bool phantomFinal = warp_.phantomFinal;
    const int phantomRemaining = warp_.phantomViewWarpsRemaining;
    EndAttack();
    if (phantomChain && !phantomFinal) {
        const float earlyStrikeChance =
            0.18f + 0.42f * TechniqueUnlock(BossPhase::Phase3);
        const bool canCutInEarly = phantomRemaining > 1;
        if (canCutInEarly && Random01() < earlyStrikeChance) {
            const ActionKind finisher =
                (std::rand() % 2 == 0) ? ActionKind::Smash
                                       : ActionKind::Sweep;
            BeginPhantomWarpStep(0, true, finisher);
            return;
        }
        if (phantomRemaining > 1) {
            BeginPhantomWarpStep(phantomRemaining - 1, false,
                                 ActionKind::None);
        } else {
            const ActionKind finisher =
                (std::rand() % 2 == 0) ? ActionKind::Smash
                                       : ActionKind::Sweep;
            BeginPhantomWarpStep(0, true, finisher);
        }
        return;
    }
    if (isFeint) {
        BeginStalkAction();
        return;
    }

    if (followupKind == ActionKind::Smash || followupKind == ActionKind::Sweep) {
        if (!(phantomChain && phantomFinal)) {
            UpdateFacingToPlayer();
            LockCurrentFacing();
        }
        if (!farSlashFollowup && !IsPlayerInMeleeFront()) {
            BeginChaseAction();
            return;
        }
        BeginAction(followupKind, followupStep);
        quickSlashActive_ = immediateFollowup || (phantomChain && phantomFinal);
        farSlashActive_ = farSlashFollowup;
        if (farSlashActive_) {
            const float chargeTime =
                followupKind == ActionKind::Smash ? GetCurrentSmashChargeTime()
                                                  : GetCurrentSweepChargeTime();
            const float tellTime = followupKind == ActionKind::Smash
                                       ? smashTellTime_
                                       : sweepTellTime_;
            IssueAttackCue(EnemyAttackCueType::Telegraph, followupKind,
                           chargeTime + tellTime);
        }
        warpFeintFollowupLocked_ = feintFollowup;
        warpFeintImmediate_ = feintFollowup && immediateFollowup;
        if (phantomChain && phantomFinal) {
            LockCurrentFacing();
            phantomFinalLockTimer_ = phantomFinalLockDuration_;
            warpFeintDecisionMade_ = true;
            directionFeintDecisionMade_ = true;
        }
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
