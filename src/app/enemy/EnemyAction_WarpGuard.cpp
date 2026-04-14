#include "Enemy.h"
#include "ModelManager.h"
#include "imgui.h"

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

void Enemy::UpdateGuardByStep(float deltaTime) {
    switch (action_.step) {
    case ActionStep::Move:
        UpdateGuardMove(deltaTime);
        break;
    case ActionStep::Hold:
        UpdateGuardHold(deltaTime);
        break;
    case ActionStep::Recovery:
        UpdateGuardRecovery(deltaTime);
        break;
    default:
        EndAttack();
        break;
    }
}

// ============================================================
// ワープ処理
// ============================================================
bool Enemy::DecideWarpTargetNearPlayer(DirectX::XMFLOAT3 &outTarget) {
    float toEnemyX = tf_.position.x - playerPos_.x;
    float toEnemyZ = tf_.position.z - playerPos_.z;
    float len = std::sqrtf(toEnemyX * toEnemyX + toEnemyZ * toEnemyZ);
    if (len <= 0.0001f) {
        toEnemyX = std::sinf(facingYaw_);
        toEnemyZ = std::cosf(facingYaw_);
        len = 1.0f;
    }

    float frontX = toEnemyX / len;
    float frontZ = toEnemyZ / len;
    float rightX = frontZ;
    float rightZ = -frontX;

    WarpApproachSlot slot = WarpApproachSlot::FrontLeft;
    int slotRoll = std::rand() % 100;
    if (slotRoll < 38) {
        slot = WarpApproachSlot::FrontLeft;
    } else if (slotRoll < 76) {
        slot = WarpApproachSlot::FrontRight;
    } else {
        slot = WarpApproachSlot::LongFront;
    }
    warp_.approachSlot = slot;

    outTarget = playerPos_;
    if (slot == WarpApproachSlot::FrontLeft) {
        outTarget.x += frontX * warpApproachForwardDistance_ -
                       rightX * warpApproachSideDistance_;
        outTarget.z += frontZ * warpApproachForwardDistance_ -
                       rightZ * warpApproachSideDistance_;
    } else if (slot == WarpApproachSlot::FrontRight) {
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
    float angle = (std::rand() % 360) * 3.14159265f / 180.0f;

    float t = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    float radius =
        warpFarRadiusMin_ + (warpFarRadiusMax_ - warpFarRadiusMin_) * t;

    outTarget = playerPos_;
    outTarget.x += std::cosf(angle) * radius;
    outTarget.z += std::sinf(angle) * radius;
    outTarget.y = tf_.position.y;

    return true;
}

bool Enemy::PrepareWarpContext() {
    ResetWarpContext();

    int approachWeight = warpApproachWeight_;
    int escapeWeight = 0;

    if (isDistanceStagnant_) {
        approachWeight += stagnantWarpBonus_;
    }

    if (farDistanceTimer_ >= farDistanceWarpTimeThreshold_) {
        approachWeight += farDistanceWarpBonus_;
    }

    bool canUseEscapeWarp =
        (closePressureTimer_ >= closePressureTimeThreshold_) &&
        (warpEscapeCooldownTimer_ <= 0.0f);

    if (canUseEscapeWarp) {
        escapeWeight = warpEscapeWeight_;
    }

    int warpTypeTotal = approachWeight + escapeWeight;
    if (warpTypeTotal <= 0) {
        warp_.type = WarpType::Approach;
    } else {
        int wr = std::rand() % warpTypeTotal;
        warp_.type =
            (wr < approachWeight) ? WarpType::Approach : WarpType::Escape;
    }

    bool ok = false;
    if (warp_.type == WarpType::Escape) {
        ok = DecideWarpTargetFarFromPlayer(warp_.targetPos);
    } else {
        warp_.type = WarpType::Approach;
        ok = DecideWarpTargetNearPlayer(warp_.targetPos);
    }

    if (!ok) {
        ResetWarpContext();
        return false;
    }

    warp_.hasValidTarget = true;

    if (chain_.active && (chain_.starter == ChainStarter::SweepWarpSmash ||
                          chain_.starter == ChainStarter::WaveWarpSmash)) {
        OverrideWarpFollowupByChain();
    } else {
        DecideWarpFollowupFromContext();
        SetupChainFromWarpContext();
    }

    return true;
}

void Enemy::DecideWarpFollowupFromContext() {
    if (warp_.type == WarpType::Approach) {
        int smashWeight = 0;
        int sweepWeight = 0;
        int rushWeight = 0;

        switch (warp_.approachSlot) {
        case WarpApproachSlot::FrontLeft:
        case WarpApproachSlot::FrontRight:
            sweepWeight = nearSweepWeight_ + 18;
            smashWeight = nearSmashWeight_ / 2;
            if (phase_ == BossPhase::Phase2) {
                sweepWeight += phase2NearSweepBonus_;
                rushWeight = nearRushWeight_ / 3;
            }
            break;

        case WarpApproachSlot::LongFront:
            smashWeight = nearSmashWeight_ + 8;
            rushWeight = nearRushWeight_ + 10;
            if (phase_ == BossPhase::Phase2) {
                smashWeight += phase2NearSmashBonus_;
                rushWeight += phase2NearRushBonus_ + 16;
            }
            break;

        case WarpApproachSlot::None:
        default:
            smashWeight = nearSmashWeight_;
            sweepWeight = nearSweepWeight_;
            if (phase_ == BossPhase::Phase2) {
                rushWeight = nearRushWeight_ + phase2NearRushBonus_;
            }
            break;
        }

        int total = smashWeight + sweepWeight + rushWeight;
        if (total <= 0) {
            warp_.followupKind = ActionKind::Smash;
            warp_.followupStep = ActionStep::Charge;
            return;
        }

        int r = std::rand() % total;
        if (r < smashWeight) {
            warp_.followupKind = ActionKind::Smash;
            warp_.followupStep = ActionStep::Charge;
        } else if (r < smashWeight + sweepWeight) {
            warp_.followupKind = ActionKind::Sweep;
            warp_.followupStep = ActionStep::Charge;
        } else {
            warp_.followupKind = ActionKind::Rush;
            warp_.followupStep = ActionStep::Charge;
        }
    } else if (warp_.type == WarpType::Escape) {
        int total = farShotWeight_ + farWaveWeight_;
        if (total <= 0) {
            warp_.followupKind = ActionKind::Shot;
            warp_.followupStep = ActionStep::Charge;
            return;
        }

        int r = std::rand() % total;
        if (r < farShotWeight_) {
            warp_.followupKind = ActionKind::Shot;
            warp_.followupStep = ActionStep::Charge;
        } else {
            warp_.followupKind = ActionKind::Wave;
            warp_.followupStep = ActionStep::Charge;
        }
    }
}

void Enemy::SetupChainFromWarpContext() {
    ResetChainContext();

    if (warp_.type == WarpType::Approach) {
        chain_.active = true;
        chain_.starter = ChainStarter::WarpApproach;
        chain_.stepCount = 0;
        chain_.maxSteps = warpApproachChainMaxSteps_;
    } else if (warp_.type == WarpType::Escape) {
        chain_.active = true;
        chain_.starter = ChainStarter::WarpEscape;
        chain_.stepCount = 0;
        chain_.maxSteps = warpEscapeChainMaxSteps_;
    }
}

void Enemy::SetupSweepWarpSmashChain() {
    ResetChainContext();
    chain_.active = true;
    chain_.starter = ChainStarter::SweepWarpSmash;
    chain_.stepCount = 0;
    chain_.maxSteps = 2;
}

void Enemy::SetupWaveWarpSmashChain() {
    ResetChainContext();
    chain_.active = true;
    chain_.starter = ChainStarter::WaveWarpSmash;
    chain_.stepCount = 0;
    chain_.maxSteps = 2;
}

void Enemy::OverrideWarpFollowupByChain() {
    switch (chain_.starter) {
    case ChainStarter::SweepWarpSmash:
    case ChainStarter::WaveWarpSmash:
        warp_.followupKind = ActionKind::Smash;
        warp_.followupStep = ActionStep::Charge;
        break;
    default:
        break;
    }
}

void Enemy::ResetPostActionState() {
    postActionOption_ = PostActionOption::None;
    backWarpFollowup_ = BackWarpFollowup::None;
}

void Enemy::BeginBackWarpPostAction() {
    ResetWarpContext();

    warp_.type = WarpType::Escape;

    if (!DecideWarpTargetFarFromPlayer(warp_.targetPos)) {
        ResetPostActionState();
        return;
    }

    warp_.hasValidTarget = true;

    if (backWarpFollowup_ == BackWarpFollowup::Shot) {
        warp_.followupKind = ActionKind::Shot;
        warp_.followupStep = ActionStep::Charge;
    } else if (backWarpFollowup_ == BackWarpFollowup::Wave) {
        warp_.followupKind = ActionKind::Wave;
        warp_.followupStep = ActionStep::Charge;
    } else if (backWarpFollowup_ == BackWarpFollowup::Rush) {
        warp_.followupKind = ActionKind::Rush;
        warp_.followupStep = ActionStep::Charge;
    } else {
        warp_.followupKind = ActionKind::Shot;
        warp_.followupStep = ActionStep::Charge;
    }

    BeginAction(ActionKind::Warp, ActionStep::Start);
}

void Enemy::BeginWarpFollowup() {
    ActionKind nextKind = warp_.followupKind;
    ActionStep nextStep = warp_.followupStep;

    if (nextKind == ActionKind::None || nextStep == ActionStep::None) {
        ResetPostActionState();
        EndAttack();
        return;
    }

    if (chain_.active) {
        if (chain_.stepCount < 1) {
            chain_.stepCount = 1;
        }
    }

    ResetPostActionState();
    BeginAction(nextKind, nextStep);
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

    float distance = GetDistanceToPlayer();

    switch (chain_.starter) {
    case ChainStarter::WarpApproach:
        if (distance > approachChainContinueDistance_) {
            return false;
        }

        if (phase_ == BossPhase::Phase2 && finishedKind == ActionKind::Rush) {
            outKind = ActionKind::Sweep;
            outStep = ActionStep::Charge;
            return true;
        }

        if (finishedKind == ActionKind::Smash) {
            outKind = ActionKind::Sweep;
            outStep = ActionStep::Charge;
            return true;
        }

        return false;

    case ChainStarter::WarpEscape:
        if (finishedKind == ActionKind::Shot) {
            outKind = ActionKind::Rush;
            outStep = ActionStep::Charge;
            return true;
        }

        if (finishedKind == ActionKind::Wave) {
            outKind = ActionKind::Rush;
            outStep = ActionStep::Charge;
            return true;
        }

        return false;

    case ChainStarter::SweepWarpSmash:
        return false;

    case ChainStarter::WaveWarpSmash:
        return false;

    default:
        return false;
    }
}

bool Enemy::TryStartPostActionWarpChain(ActionKind finishedKind) {
    float distance = GetDistanceToPlayer();

    if (finishedKind == ActionKind::Sweep) {
        if (distance <= sweepWarpSmashMaxDistance_) {
            float r =
                static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
            if (r < sweepWarpSmashChance_) {
                SetupSweepWarpSmashChain();

                warp_.type = WarpType::Approach;

                if (!DecideWarpTargetNearPlayer(warp_.targetPos)) {
                    ResetChainContext();
                    ResetWarpContext();
                    return false;
                }

                warp_.hasValidTarget = true;
                OverrideWarpFollowupByChain();
                BeginAction(ActionKind::Warp, ActionStep::Start);
                return true;
            }
        }
    }

    if (finishedKind == ActionKind::Wave) {
        if (distance >= waveWarpSmashMinDistance_) {
            float r =
                static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
            if (r < waveWarpSmashChance_) {
                SetupWaveWarpSmashChain();

                warp_.type = WarpType::Approach;

                if (!DecideWarpTargetNearPlayer(warp_.targetPos)) {
                    ResetChainContext();
                    ResetWarpContext();
                    return false;
                }

                warp_.hasValidTarget = true;
                OverrideWarpFollowupByChain();
                BeginAction(ActionKind::Warp, ActionStep::Start);
                return true;
            }
        }
    }

    return false;
}

bool Enemy::TryStartBackWarpPostAction(ActionKind finishedKind) {
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

    float followupRoll =
        static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);

    if (followupRoll < backWarpShotChance_) {
        backWarpFollowup_ = BackWarpFollowup::Shot;
    } else {
        backWarpFollowup_ = BackWarpFollowup::Wave;
    }

    postActionOption_ = PostActionOption::BackWarp;
    BeginBackWarpPostAction();
    return true;
}

bool Enemy::TryContinueChain() {
    ActionKind finishedKind = action_.kind;

    if (chain_.active) {
        if (chain_.stepCount >= chain_.maxSteps) {
            ResetChainContext();
        } else {
            ActionKind nextKind = ActionKind::None;
            ActionStep nextStep = ActionStep::None;

            if (DecideNextChainAction(finishedKind, nextKind, nextStep)) {
                chain_.stepCount++;
                BeginAction(nextKind, nextStep);
                return true;
            }

            ResetChainContext();
        }
    }

    if (TryStartPostActionWarpChain(finishedKind)) {
        return true;
    }

    return false;
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

    if (stateTimer_ >= warpStartTime_) {
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
    if (warpMoveTime_ > 0.0001f) {
        t = stateTimer_ / warpMoveTime_;
    }
    if (t < 0.0f) {
        t = 0.0f;
    }
    if (t > 1.0f) {
        t = 1.0f;
    }

    // Fast-in / fast-out feels more like a violent blink than a simple lerp.
    float eased = 1.0f - std::pow(1.0f - t, 2.6f);
    tf_.position.x = warp_.departurePos.x +
                     (warp_.targetPos.x - warp_.departurePos.x) * eased;
    tf_.position.y = warp_.departurePos.y +
                     (warp_.targetPos.y - warp_.departurePos.y) * eased;
    tf_.position.z = warp_.departurePos.z +
                     (warp_.targetPos.z - warp_.departurePos.z) * eased;

    warpTrailEmitTimer_ += deltaTime;
    while (warpTrailEmitTimer_ >= warpTrailInterval_) {
        warpTrailEmitTimer_ -= warpTrailInterval_;
        float scale =
            warpTrailScaleMax_ - (warpTrailScaleMax_ - warpTrailScaleMin_) * t;
        EmitWarpTrailGhost(tf_.position, scale);
    }

    UpdateFacingToPlayer();
    LockCurrentFacing();

    if (stateTimer_ >= warpMoveTime_) {
        tf_.position = warp_.targetPos;
        ChangeActionStep(ActionStep::End);
    }
}

void Enemy::UpdateWarpEnd(float deltaTime) {
    (void)deltaTime;

    isVisible_ = true;
    warp_.collisionDisabled = false;
    UpdateFacingToPlayerWithSpeed(deltaTime, idleTurnSpeed_ * 0.55f);

    if (stateTimer_ >= warpEndTime_) {
        BeginWarpFollowup();
    }
}

// ============================================================
// ガード処理
// ============================================================
void Enemy::DecideGuardTarget() {
    int r = std::rand() % 3;

    if (r == 0) {
        guardTarget_ = GuardTarget::Face;
    } else if (r == 1) {
        guardTarget_ = GuardTarget::BodyLeft;
    } else {
        guardTarget_ = GuardTarget::BodyRight;
    }
}

void Enemy::UpdateGuardMove(float deltaTime) {
    (void)deltaTime;

    if (stateTimer_ >= guardMoveTime_) {
        action_.step = ActionStep::Hold;
        stateTimer_ = 0.0f;
    }
}

void Enemy::UpdateGuardHold(float deltaTime) {
    (void)deltaTime;

    isGuardActive_ = true;

    if (stateTimer_ >= guardHoldTime_) {
        action_.step = ActionStep::Recovery;
        stateTimer_ = 0.0f;
    }
}

void Enemy::UpdateGuardRecovery(float deltaTime) {
    (void)deltaTime;

    if (stateTimer_ >= guardRecoveryTime_) {
        guardTarget_ = GuardTarget::None;
        isGuardActive_ = false;
        EndAttack();
    }
}
