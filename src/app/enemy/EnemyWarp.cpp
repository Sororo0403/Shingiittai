#include "Enemy.h"

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
    float toEnemyX = tf_.position.x - playerPos_.x;
    float toEnemyZ = tf_.position.z - playerPos_.z;
    float length = std::sqrt(toEnemyX * toEnemyX + toEnemyZ * toEnemyZ);
    if (length <= 0.0001f) {
        toEnemyX = std::sin(facingYaw_);
        toEnemyZ = std::cos(facingYaw_);
        length = 1.0f;
    }

    const float frontX = toEnemyX / length;
    const float frontZ = toEnemyZ / length;
    const float rightX = frontZ;
    const float rightZ = -frontX;

    int slotRoll = std::rand() % 100;
    if (slotRoll < 38) {
        warp_.approachSlot = WarpApproachSlot::FrontLeft;
    } else if (slotRoll < 76) {
        warp_.approachSlot = WarpApproachSlot::FrontRight;
    } else {
        warp_.approachSlot = WarpApproachSlot::LongFront;
    }

    outTarget = playerPos_;
    if (warp_.approachSlot == WarpApproachSlot::FrontLeft) {
        outTarget.x += frontX * warpApproachForwardDistance_ -
                       rightX * warpApproachSideDistance_;
        outTarget.z += frontZ * warpApproachForwardDistance_ -
                       rightZ * warpApproachSideDistance_;
    } else if (warp_.approachSlot == WarpApproachSlot::FrontRight) {
        outTarget.x += frontX * warpApproachForwardDistance_ +
                       rightX * warpApproachSideDistance_;
        outTarget.z += frontZ * warpApproachForwardDistance_ +
                       rightZ * warpApproachSideDistance_;
    } else {
        outTarget.x += frontX * warpApproachLongFrontDistance_;
        outTarget.z += frontZ * warpApproachLongFrontDistance_;
    }
    outTarget.y = tf_.position.y;
    return true;
}

bool Enemy::DecideWarpTargetFarFromPlayer(DirectX::XMFLOAT3 &outTarget) const {
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
    return true;
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
    return true;
}

void Enemy::ResetPostActionState() { postActionOption_ = PostActionOption::None; }

void Enemy::BeginBackWarpPostAction() {
    if (IsWarpSuspendedForPresentation()) {
        ResetPostActionState();
        return;
    }

    ResetWarpContext();
    warp_.type = WarpType::Escape;
    if (!DecideWarpTargetFarFromPlayer(warp_.targetPos)) {
        ResetPostActionState();
        return;
    }

    warp_.hasValidTarget = true;
    BeginAction(ActionKind::Warp, ActionStep::Start);
}

void Enemy::ResetWarpContext() { warp_ = WarpContext{}; }

void Enemy::ResetChainContext() { chain_ = ChainContext{}; }

bool Enemy::DecideNextChainAction(ActionKind finishedKind, ActionKind &outKind,
                                  ActionStep &outStep) const {
    (void)finishedKind;
    outKind = ActionKind::None;
    outStep = ActionStep::None;
    return false;
}

void Enemy::SetupSweepWarpSmashChain() { ResetChainContext(); }

void Enemy::SetupWaveWarpSmashChain() { ResetChainContext(); }

void Enemy::OverrideWarpFollowupByChain() {}

bool Enemy::TryStartPostActionWarpChain(ActionKind finishedKind) {
    if (IsWarpSuspendedForPresentation()) {
        return false;
    }

    const float distance = GetDistanceToPlayer();

    if (finishedKind == ActionKind::Sweep &&
        distance <= config_.chain.sweepWarpSmashMaxDistance) {
        float r = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
        if (r < config_.chain.sweepWarpSmashChance) {
            ResetWarpContext();
            warp_.type = WarpType::Approach;
            if (!DecideWarpTargetNearPlayer(warp_.targetPos)) {
                ResetWarpContext();
                return false;
            }
            warp_.hasValidTarget = true;
            BeginAction(ActionKind::Warp, ActionStep::Start);
            return true;
        }
    }

    if (finishedKind == ActionKind::Wave &&
        distance >= config_.chain.waveWarpSmashMinDistance) {
        float r = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
        if (r < config_.chain.waveWarpSmashChance) {
            ResetWarpContext();
            warp_.type = WarpType::Approach;
            if (!DecideWarpTargetNearPlayer(warp_.targetPos)) {
                ResetWarpContext();
                return false;
            }
            warp_.hasValidTarget = true;
            BeginAction(ActionKind::Warp, ActionStep::Start);
            return true;
        }
    }

    return false;
}

bool Enemy::TryStartBackWarpPostAction(ActionKind finishedKind) {
    if (IsWarpSuspendedForPresentation()) {
        return false;
    }

    float chance = 0.0f;
    switch (finishedKind) {
    case ActionKind::Smash:
        chance = backWarpAfterSmashChance_;
        break;
    case ActionKind::Sweep:
        chance = backWarpAfterSweepChance_;
        break;
    case ActionKind::Wave:
        chance = backWarpAfterWaveChance_;
        break;
    default:
        return false;
    }

    float r = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    if (r >= chance) {
        return false;
    }

    postActionOption_ = PostActionOption::BackWarp;
    BeginBackWarpPostAction();
    return true;
}

bool Enemy::TryContinueChain() { return TryStartPostActionWarpChain(action_.kind); }

void Enemy::UpdateWarpStart(float deltaTime) {
    if (!warp_.hasDeparturePos) {
        warp_.departurePos = tf_.position;
        warp_.hasDeparturePos = true;
    }

    if (warp_.type == WarpType::Approach) {
        UpdateFacingToPlayerWithSpeed(deltaTime, chargeTurnSpeed_ * 0.45f);
    }

    isVisible_ = false;
    warp_.collisionDisabled = true;

    if (stateTimer_ >= config_.warp.startTime) {
        warpTrailEmitTimer_ = 0.0f;
        EmitWarpTrailGhost(warp_.departurePos, warpTrailScaleMax_);
        ChangeActionStep(ActionStep::Move);
    }
}

void Enemy::UpdateWarpMove(float deltaTime) {
    if (!warp_.hasValidTarget) {
        EndAttack();
        return;
    }

    float t = 1.0f;
    if (config_.warp.moveTime > 0.0001f) {
        t = stateTimer_ / config_.warp.moveTime;
    }
    t = (t < 0.0f) ? 0.0f : ((t > 1.0f) ? 1.0f : t);

    const float eased = 1.0f - std::pow(1.0f - t, 2.6f);
    tf_.position.x =
        warp_.departurePos.x + (warp_.targetPos.x - warp_.departurePos.x) * eased;
    tf_.position.y =
        warp_.departurePos.y + (warp_.targetPos.y - warp_.departurePos.y) * eased;
    tf_.position.z =
        warp_.departurePos.z + (warp_.targetPos.z - warp_.departurePos.z) * eased;

    warpTrailEmitTimer_ += deltaTime;
    while (warpTrailEmitTimer_ >= warpTrailInterval_) {
        warpTrailEmitTimer_ -= warpTrailInterval_;
        const float scale =
            warpTrailScaleMax_ - (warpTrailScaleMax_ - warpTrailScaleMin_) * t;
        EmitWarpTrailGhost(tf_.position, scale);
    }

    UpdateFacingToPlayer();
    LockCurrentFacing();

    if (stateTimer_ >= config_.warp.moveTime) {
        tf_.position = warp_.targetPos;
        ChangeActionStep(ActionStep::End);
    }
}

void Enemy::UpdateWarpEnd(float deltaTime) {
    isVisible_ = true;
    warp_.collisionDisabled = false;
    UpdateFacingToPlayerWithSpeed(deltaTime, idleTurnSpeed_ * 0.55f);

    if (stateTimer_ < config_.warp.endTime) {
        return;
    }

    EndAttack();
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
