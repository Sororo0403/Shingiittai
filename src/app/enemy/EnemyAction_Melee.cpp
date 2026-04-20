#include "Enemy.h"
#include "ModelManager.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

void Enemy::UpdateSmashByStep(float deltaTime) {
    switch (action_.step) {
    case ActionStep::Charge:
        UpdateSmashCharge(deltaTime);
        break;
    case ActionStep::Hold:
        UpdateSmashHold(deltaTime);
        break;
    case ActionStep::Active:
        UpdateSmashAttack(deltaTime);
        break;
    case ActionStep::Recovery:
        UpdateSmashRecovery(deltaTime);
        break;
    default:
        EndAttack();
        break;
    }
}

void Enemy::UpdateSweepByStep(float deltaTime) {
    switch (action_.step) {
    case ActionStep::Charge:
        UpdateSweepCharge(deltaTime);
        break;
    case ActionStep::Hold:
        UpdateSweepHold(deltaTime);
        break;
    case ActionStep::Active:
        UpdateSweepAttack(deltaTime);
        break;
    case ActionStep::Recovery:
        UpdateSweepRecovery(deltaTime);
        break;
    default:
        EndAttack();
        break;
    }
}

void Enemy::UpdateRushByStep(float deltaTime) {
    switch (action_.step) {
    case ActionStep::Charge:
        UpdateRushCharge(deltaTime);
        break;
    case ActionStep::Active:
        UpdateRushAttack(deltaTime);
        break;
    case ActionStep::Recovery:
        UpdateRushRecovery(deltaTime);
        break;
    default:
        EndAttack();
        break;
    }
}

// ============================================================
// 振り下ろし攻撃更新
// ============================================================
void Enemy::UpdateSmashCharge(float deltaTime) {
    float currentChargeTime = GetCurrentSmashChargeTime();

    float trackingEnd = config_.attacks.smash.melee.base.timing.trackingEndTime;
    if (trackingEnd < 0.0f) {
        trackingEnd = 0.0f;
    }
    if (trackingEnd > currentChargeTime) {
        trackingEnd = currentChargeTime;
    }

    if (!tellActive_ && stateTimer_ <= 0.0001f) {
        EnterTell(ActionKind::Smash);
    }

    if (tellActive_) {
        UpdateFacingToPlayerWithSpeed(deltaTime, chargeTurnSpeed_ * 0.55f);

        if (!IsTellFinished()) {
            return;
        }

        tellActive_ = false;
        stateTimer_ = 0.0f;
    }

    if (stateTimer_ < trackingEnd) {
        UpdateFacingToPlayerWithSpeed(deltaTime, chargeTurnSpeed_);
    } else if (!hasTrackingLocked_) {
        LockCurrentFacing();
        hasTrackingLocked_ = true;
    }

    if (stateTimer_ >= currentChargeTime) {
        if (!hasTrackingLocked_) {
            LockCurrentFacing();
            hasTrackingLocked_ = true;
        }

        if (ShouldEnterSmashHold()) {
            EnterHold(RandomRange(config_.attacks.smash.melee.holdTime.min,
                                  config_.attacks.smash.melee.holdTime.max));

            if (ShouldDoFakeCommit(ActionKind::Smash)) {
                EnterFakeCommit(ActionKind::Smash);
            }

            ChangeActionStep(ActionStep::Hold);
            return;
        }

        ChangeActionStep(ActionStep::Active);
    }
}

void Enemy::UpdateSmashHold(float deltaTime) {
    if (fakeCommitActive_) {
        UpdateFacingToPlayerWithSpeed(deltaTime, chargeTurnSpeed_ * 0.35f);

        if (!IsFakeCommitFinished()) {
            return;
        }

        EnterFreezeHold(ActionKind::Smash);
        stateTimer_ = 0.0f;
        return;
    }

    if (freezeHoldActive_) {
        if (!holdBranchDecided_ && stateTimer_ >= holdBranchDecisionTime_) {
            DecideHoldBranch(ActionKind::Smash);
        }

        if (!IsFreezeHoldFinished()) {
            return;
        }

        freezeHoldActive_ = false;
        stateTimer_ = 0.0f;
    }

    if (!holdBranchDecided_ && stateTimer_ >= holdBranchDecisionTime_) {
        DecideHoldBranch(ActionKind::Smash);
    }

    if (ShouldSnapReleaseFromRead()) {
        if (TryExecuteHoldBranch(ActionKind::Smash)) {
            return;
        }

        ChangeActionStep(ActionStep::Active);
        return;
    }

    if (stateTimer_ >= currentHoldDuration_) {
        if (TryExecuteHoldBranch(ActionKind::Smash)) {
            return;
        }

        ChangeActionStep(ActionStep::Active);
    }
}

void Enemy::UpdateSmashAttack(float deltaTime) {
    (void)deltaTime;

    const AttackTimingParam *timing = GetCurrentAttackTiming();
    if (!timing) {
        EndAttack();
        return;
    }

    isAttackActive_ = true;

    if (stateTimer_ >= timing->totalTime) {
        ChangeActionStep(ActionStep::Recovery);
    }
}

void Enemy::UpdateSmashRecovery(float deltaTime) {
    const bool isDelaySmashWhiff =
        (action_.id == ActionId::DelaySmash && !currentActionConnected_ &&
         !currentActionGuarded_);
    float turnSpeed = recoveryTurnSpeed_;
    if (isDelaySmashWhiff) {
        turnSpeed *= punishWindowTurnSpeedScale_;
    }
    UpdateFacingToPlayerWithSpeed(deltaTime, turnSpeed);

    const AttackTimingParam *timing = GetCurrentAttackTiming();
    if (!timing) {
        EndAttack();
        return;
    }

    float recoveryDuration = timing->totalTime - timing->recoveryStartTime;
    if (recoveryDuration < 0.0f) {
        recoveryDuration = 0.0f;
    }

    if (isDelaySmashWhiff) {
        recoveryDuration += delaySmashWhiffRecoveryBonus_;
    }

    if (stateTimer_ >= recoveryDuration) {
        FinishCurrentAction();
    }
}

// ============================================================
// 薙ぎ払い攻撃更新
// ============================================================
void Enemy::UpdateSweepCharge(float deltaTime) {
    float currentChargeTime = GetCurrentSweepChargeTime();

    float trackingEnd = config_.attacks.sweep.melee.base.timing.trackingEndTime;
    if (trackingEnd < 0.0f) {
        trackingEnd = 0.0f;
    }
    if (trackingEnd > currentChargeTime) {
        trackingEnd = currentChargeTime;
    }

    if (!tellActive_ && stateTimer_ <= 0.0001f) {
        EnterTell(ActionKind::Sweep);
    }

    if (tellActive_) {
        UpdateFacingToPlayerWithSpeed(deltaTime, chargeTurnSpeed_ * 0.50f);

        if (!IsTellFinished()) {
            return;
        }

        tellActive_ = false;
        stateTimer_ = 0.0f;
    }

    if (stateTimer_ < trackingEnd) {
        UpdateFacingToPlayerWithSpeed(deltaTime, chargeTurnSpeed_);
    } else if (!hasTrackingLocked_) {
        LockCurrentFacing();
        hasTrackingLocked_ = true;
    }

    if (stateTimer_ >= currentChargeTime) {
        if (!hasTrackingLocked_) {
            LockCurrentFacing();
            hasTrackingLocked_ = true;
        }

        if (ShouldEnterSweepHold()) {
            EnterHold(RandomRange(config_.attacks.sweep.melee.holdTime.min,
                                  config_.attacks.sweep.melee.holdTime.max));

            if (ShouldDoFakeCommit(ActionKind::Sweep)) {
                EnterFakeCommit(ActionKind::Sweep);
            }

            ChangeActionStep(ActionStep::Hold);
            return;
        }

        ChangeActionStep(ActionStep::Active);
    }
}

void Enemy::UpdateSweepHold(float deltaTime) {
    if (fakeCommitActive_) {
        UpdateFacingToPlayerWithSpeed(deltaTime, chargeTurnSpeed_ * 0.30f);

        if (!IsFakeCommitFinished()) {
            return;
        }

        EnterFreezeHold(ActionKind::Sweep);
        stateTimer_ = 0.0f;
        return;
    }

    if (freezeHoldActive_) {
        if (!holdBranchDecided_ && stateTimer_ >= holdBranchDecisionTime_) {
            DecideHoldBranch(ActionKind::Sweep);
        }

        if (!IsFreezeHoldFinished()) {
            return;
        }

        freezeHoldActive_ = false;
        stateTimer_ = 0.0f;
    }

    if (!holdBranchDecided_ && stateTimer_ >= holdBranchDecisionTime_) {
        DecideHoldBranch(ActionKind::Sweep);
    }

    if (ShouldSnapReleaseFromRead()) {
        if (TryExecuteHoldBranch(ActionKind::Sweep)) {
            return;
        }

        ChangeActionStep(ActionStep::Active);
        return;
    }

    if (stateTimer_ >= currentHoldDuration_) {
        if (TryExecuteHoldBranch(ActionKind::Sweep)) {
            return;
        }

        ChangeActionStep(ActionStep::Active);
    }
}

void Enemy::UpdateSweepAttack(float deltaTime) {
    (void)deltaTime;

    const AttackTimingParam *timing = GetCurrentAttackTiming();
    if (!timing) {
        EndAttack();
        return;
    }

    isAttackActive_ = true;

    if (stateTimer_ >= timing->totalTime) {
        ChangeActionStep(ActionStep::Recovery);
    }
}

void Enemy::UpdateSweepRecovery(float deltaTime) {
    UpdateFacingToPlayerWithSpeed(deltaTime, recoveryTurnSpeed_);

    if (TryBeginDoubleSweepSecondStage()) {
        return;
    }

    const AttackTimingParam *timing = GetCurrentAttackTiming();
    if (!timing) {
        EndAttack();
        return;
    }

    float recoveryDuration = timing->totalTime - timing->recoveryStartTime;
    if (recoveryDuration < 0.0f) {
        recoveryDuration = 0.0f;
    }

    if (stateTimer_ >= recoveryDuration) {
        FinishCurrentAction();
    }
}

bool Enemy::TryBeginDoubleSweepSecondStage() {
    if (action_.id != ActionId::DoubleSweep) {
        return false;
    }

    if (isDoubleSweepSecondStage_) {
        return false;
    }

    if (stateTimer_ < config_.attacks.sweep.secondDelay) {
        return false;
    }

    isDoubleSweepSecondStage_ = true;
    hasTrackingLocked_ = false;
    holdConfigured_ = false;
    currentHoldDuration_ = 0.0f;
    isAttackActive_ = false;
    stateTimer_ = 0.0f;
    action_.step = ActionStep::Charge;
    return true;
}

// ============================================================
// Rush更新
// ============================================================
void Enemy::UpdateRushCharge(float deltaTime) {
    float currentRushChargeTime = config_.attacks.rush.base.chargeTime;
    if (playerObs_.isGuarding) {
        currentRushChargeTime += rushChargeGuardTimeBonus_;
    }

    float trackingEnd = config_.attacks.rush.base.timing.trackingEndTime;
    if (trackingEnd < 0.0f) {
        trackingEnd = 0.0f;
    }
    if (trackingEnd > currentRushChargeTime) {
        trackingEnd = currentRushChargeTime;
    }

    if (stateTimer_ < trackingEnd) {
        float turnScale = playerObs_.isGuarding ? rushChargeGuardTurnScale_
                                                : rushChargeTrackingTurnScale_;
        UpdateFacingToPlayerWithSpeed(deltaTime, chargeTurnSpeed_ * turnScale);
    } else if (!hasTrackingLocked_) {
        LockCurrentFacing();
        hasTrackingLocked_ = true;
    }

    float chargeRatio = currentRushChargeTime > 0.0001f
                            ? stateTimer_ / currentRushChargeTime
                            : 1.0f;
    if (chargeRatio > 1.0f) {
        chargeRatio = 1.0f;
    }

    if (chargeRatio >= 0.35f) {
        float usedYaw = hasTrackingLocked_ ? lockedAttackYaw_ : facingYaw_;
        float forwardX = std::sinf(usedYaw);
        float forwardZ = std::cosf(usedYaw);
        float creepScale = (chargeRatio - 0.35f) / 0.65f;
        if (creepScale < 0.0f) {
            creepScale = 0.0f;
        }
        if (creepScale > 1.0f) {
            creepScale = 1.0f;
        }

        tf_.position.x += forwardX * rushChargeCreepSpeed_ * creepScale * deltaTime;
        tf_.position.z += forwardZ * rushChargeCreepSpeed_ * creepScale * deltaTime;
    }

    if (stateTimer_ >= currentRushChargeTime) {
        if (!hasTrackingLocked_) {
            LockCurrentFacing();
            hasTrackingLocked_ = true;
        }

        rushCurveDir_ = (std::rand() % 2 == 0) ? -1.0f : 1.0f;
        float curveOffsetRad =
            rushStartCurveAngleDeg_ * 3.14159265f / 180.0f * rushCurveDir_;
        rushCurrentYaw_ = lockedAttackYaw_ + curveOffsetRad;

        ChangeActionStep(ActionStep::Active);
    }
}

void Enemy::UpdateRushAttack(float deltaTime) {
    isAttackActive_ = IsCurrentAttackInActiveWindow();

    if (stateTimer_ <= config_.attacks.rush.moveDuration) {
        float dx = playerPos_.x - tf_.position.x;
        float dz = playerPos_.z - tf_.position.z;
        float targetYaw = std::atan2f(dx, dz);

        float progress =
            (config_.attacks.rush.moveDuration > 0.0001f)
                ? (stateTimer_ / config_.attacks.rush.moveDuration)
                : 1.0f;
        if (progress < 0.0f) {
            progress = 0.0f;
        }
        if (progress > 1.0f) {
            progress = 1.0f;
        }

        float turnScale = 1.0f;
        float speedScale = 1.0f;

        if (progress < rushCurvePhaseRatio_) {
            turnScale = rushCurveTurnScale_;
            speedScale = rushCurveSpeedScale_;
        } else if (progress < rushBrakeStartRatio_) {
            turnScale = rushHomingTurnScale_;
            speedScale = rushHomingSpeedScale_;
        } else {
            turnScale = rushBrakeTurnScale_;
            speedScale = rushBrakeSpeedScale_;

            float distToPlayer = std::sqrtf(dx * dx + dz * dz);
            if (distToPlayer < rushBrakeDistance_) {
                float nearScale = distToPlayer / rushBrakeDistance_;
                if (nearScale < 0.20f) {
                    nearScale = 0.20f;
                }
                speedScale *= nearScale;
            }
        }

        float diff = NormalizeAngle(targetYaw - rushCurrentYaw_);
        float maxTurn = rushTurnSpeed_ * turnScale * deltaTime;

        if (diff > maxTurn) {
            diff = maxTurn;
        } else if (diff < -maxTurn) {
            diff = -maxTurn;
        }

        rushCurrentYaw_ = NormalizeAngle(rushCurrentYaw_ + diff);

        float forwardX = std::sinf(rushCurrentYaw_);
        float forwardZ = std::cosf(rushCurrentYaw_);

        tf_.position.x +=
            forwardX * config_.attacks.rush.speed * speedScale * deltaTime;
        tf_.position.z +=
            forwardZ * config_.attacks.rush.speed * speedScale * deltaTime;

        facingYaw_ = rushCurrentYaw_;
    }

    if (IsCurrentAttackInRecoveryWindow()) {
        ChangeActionStep(ActionStep::Recovery);
    }
}

void Enemy::UpdateRushRecovery(float deltaTime) {
    if (!rushFollowupEvaluated_) {
        rushFollowupEvaluated_ = true;
        rushWillSweepFollowup_ = false;

        float distance = GetDistanceToPlayer();
        if (phase_ == BossPhase::Phase2 && distance >= rushSweepMinDistance_ &&
            distance <= rushSweepMaxDistance_) {
            float followupChance = rushSweepFollowupChance_;
            followupChance += phase2RushSweepFollowupBonus_;
            if (rushFromShotCombo_) {
                followupChance += comboBRushSweepBonus_;
            }

            if (playerObs_.isGuarding) {
                followupChance += 0.08f;
            }
            if (followupChance > 0.85f) {
                followupChance = 0.85f;
            }

            float roll =
                static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
            rushWillSweepFollowup_ = (roll < followupChance);
        }
    }

    const bool isRushWhiffPunishable =
        (!rushWillSweepFollowup_ && !currentActionConnected_ &&
         !currentActionGuarded_);
    float turnSpeed = recoveryTurnSpeed_;
    if (isRushWhiffPunishable) {
        turnSpeed *= punishWindowTurnSpeedScale_;
    }
    UpdateFacingToPlayerWithSpeed(deltaTime, turnSpeed);

    if (rushWillSweepFollowup_ && stateTimer_ >= 0.06f) {
        float followupDelay =
            RandomRange(rushSweepFollowupDelayMin_, rushSweepFollowupDelayMax_);
        EndAttack();
        recoveryFollowupKind_ = ActionKind::Sweep;
        recoveryFollowupStep_ = ActionStep::Charge;
        recoveryFollowupDelayTimer_ = followupDelay;
        return;
    }

    const AttackTimingParam *timing = GetCurrentAttackTiming();
    if (!timing) {
        EndAttack();
        return;
    }

    float recoveryDuration = timing->totalTime - timing->recoveryStartTime;
    if (recoveryDuration < 0.0f) {
        recoveryDuration = 0.0f;
    }

    if (isRushWhiffPunishable) {
        recoveryDuration += rushWhiffRecoveryBonus_;
    }

    if (stateTimer_ >= recoveryDuration) {
        FinishCurrentAction();
    }
}
