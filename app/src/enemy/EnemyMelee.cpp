#include "Enemy.h"

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

    if (ShouldSnapReleaseFromRead() || stateTimer_ >= currentHoldDuration_) {
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
        action_.id == ActionId::DelaySmash && !currentActionConnected_ &&
        !currentActionGuarded_;
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

    if (ShouldSnapReleaseFromRead() || stateTimer_ >= currentHoldDuration_) {
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
    if (action_.id != ActionId::DoubleSweep || isDoubleSweepSecondStage_) {
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
