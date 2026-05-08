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
    float playerForwardX = playerObs_.velocity.x;
    float playerForwardZ = playerObs_.velocity.z;
    float playerForwardLength =
        std::sqrt(playerForwardX * playerForwardX + playerForwardZ * playerForwardZ);

    if (playerForwardLength <= 0.0001f) {
        playerForwardX = playerPos_.x - tf_.position.x;
        playerForwardZ = playerPos_.z - tf_.position.z;
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

    const float backX = -playerForwardX;
    const float backZ = -playerForwardZ;
    const float rightX = playerForwardZ;
    const float rightZ = -playerForwardX;

    if (warp_.approachSlot == WarpApproachSlot::None) {
        int slotRoll = std::rand() % 100;
        if (slotRoll < 42) {
            warp_.approachSlot = WarpApproachSlot::BackLeft;
        } else if (slotRoll < 84) {
            warp_.approachSlot = WarpApproachSlot::BackRight;
        } else {
            warp_.approachSlot = WarpApproachSlot::DirectBack;
        }
    }

    outTarget = playerPos_;
    if (warp_.approachSlot == WarpApproachSlot::BackLeft) {
        outTarget.x += backX * warpApproachForwardDistance_ -
                       rightX * warpApproachSideDistance_;
        outTarget.z += backZ * warpApproachForwardDistance_ -
                       rightZ * warpApproachSideDistance_;
    } else if (warp_.approachSlot == WarpApproachSlot::BackRight) {
        outTarget.x += backX * warpApproachForwardDistance_ +
                       rightX * warpApproachSideDistance_;
        outTarget.z += backZ * warpApproachForwardDistance_ +
                       rightZ * warpApproachSideDistance_;
    } else {
        outTarget.x += backX * warpApproachLongFrontDistance_;
        outTarget.z += backZ * warpApproachLongFrontDistance_;
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
    warp_.followupKind = SelectNearPressureAction();
    warp_.followupStep = ActionStep::Charge;
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
    outKind = ActionKind::None;
    outStep = ActionStep::None;
    if (!chain_.active) {
        return false;
    }

    if (finishedKind == ActionKind::Smash) {
        return false;
    }

    if (chain_.stepCount >= chain_.maxSteps) {
        return false;
    }

    const float distance = GetDistanceToPlayer();
    if (chain_.starter == ChainStarter::SweepWarpSmash &&
        distance <= config_.chain.approachContinueDistance) {
        outKind = ActionKind::Warp;
        outStep = ActionStep::Start;
        return true;
    }

    if (chain_.starter == ChainStarter::WaveWarpSmash &&
        distance >= config_.chain.waveWarpSmashMinDistance) {
        outKind = ActionKind::Warp;
        outStep = ActionStep::Start;
        return true;
    }

    return false;
}

void Enemy::SetupSweepWarpSmashChain() {
    chain_.active = true;
    chain_.starter = ChainStarter::SweepWarpSmash;
    chain_.stepCount = 0;
    chain_.maxSteps = (std::max)(1, config_.chain.warpApproachMaxSteps);
}

void Enemy::SetupWaveWarpSmashChain() {
    chain_.active = true;
    chain_.starter = ChainStarter::WaveWarpSmash;
    chain_.stepCount = 0;
    chain_.maxSteps = (std::max)(1, config_.chain.warpApproachMaxSteps);
}

void Enemy::OverrideWarpFollowupByChain() {
    if (!chain_.active) {
        return;
    }

    if (chain_.starter == ChainStarter::SweepWarpSmash ||
        chain_.starter == ChainStarter::WaveWarpSmash) {
        warp_.followupKind = ActionKind::Smash;
        warp_.followupStep = ActionStep::Charge;
        ++chain_.stepCount;
    }
}

bool Enemy::TryStartPostActionWarpChain(ActionKind finishedKind) {
    if (IsWarpSuspendedForPresentation()) {
        return false;
    }

    const float distance = GetDistanceToPlayer();

    if (finishedKind == ActionKind::Sweep &&
        distance <= config_.chain.sweepWarpSmashMaxDistance) {
        float r = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
        if (r < config_.chain.sweepWarpSmashChance) {
            SetupSweepWarpSmashChain();
            ResetWarpContext();
            warp_.type = WarpType::Approach;
            if (!DecideWarpTargetNearPlayer(warp_.targetPos)) {
                ResetWarpContext();
                ResetChainContext();
                return false;
            }
            warp_.hasValidTarget = true;
            OverrideWarpFollowupByChain();
            BeginAction(ActionKind::Warp, ActionStep::Start);
            return true;
        }
    }

    if (finishedKind == ActionKind::Wave &&
        distance >= config_.chain.waveWarpSmashMinDistance) {
        float r = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
        if (r < config_.chain.waveWarpSmashChance) {
            SetupWaveWarpSmashChain();
            ResetWarpContext();
            warp_.type = WarpType::Approach;
            if (!DecideWarpTargetNearPlayer(warp_.targetPos)) {
                ResetWarpContext();
                ResetChainContext();
                return false;
            }
            warp_.hasValidTarget = true;
            OverrideWarpFollowupByChain();
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

bool Enemy::TryContinueChain() {
    ActionKind nextKind = ActionKind::None;
    ActionStep nextStep = ActionStep::None;
    const ActionKind finishedKind = action_.kind;

    if (DecideNextChainAction(finishedKind, nextKind, nextStep)) {
        ResetWarpContext();
        warp_.type = WarpType::Approach;
        if (!DecideWarpTargetNearPlayer(warp_.targetPos)) {
            ResetWarpContext();
            ResetChainContext();
            return false;
        }
        warp_.hasValidTarget = true;
        OverrideWarpFollowupByChain();
        BeginAction(nextKind, nextStep);
        return true;
    }

    if (chain_.active && finishedKind == ActionKind::Smash) {
        ResetChainContext();
    }

    return TryStartPostActionWarpChain(finishedKind);
}

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

    float startTime = config_.warp.startTime;
    if (action_.id == ActionId::WarpBackstab) {
        startTime *= 0.55f;
        if (startTime < 0.08f) {
            startTime = 0.08f;
        }
    }

    if (stateTimer_ >= startTime) {
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

    const ActionKind followupKind = warp_.followupKind;
    const ActionStep followupStep = warp_.followupStep;
    const WarpType warpType = warp_.type;

    EndAttack();
    if (warpType == WarpType::Approach && followupKind != ActionKind::None &&
        followupStep != ActionStep::None) {
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
