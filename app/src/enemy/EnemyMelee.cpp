#include "Enemy.h"

void Enemy::UpdateMeleeByStep(float deltaTime) {
    switch (action_.step) {
    case ActionStep::Charge:
        UpdateMeleeCharge(deltaTime);
        break;
    case ActionStep::Active:
        UpdateMeleeAttack(deltaTime);
        break;
    case ActionStep::Recovery:
        UpdateMeleeRecovery(deltaTime);
        break;
    default:
        EndAttack();
        break;
    }
}

void Enemy::UpdateMeleeCharge(float deltaTime) {
    float currentChargeTime = GetCurrentMeleeChargeTime();
    const AttackTimingParam *timing = GetCurrentAttackTiming();
    if (!timing) {
        EndAttack();
        return;
    }

    float trackingEnd = timing->trackingEndTime;
    if (trackingEnd < 0.0f) {
        trackingEnd = 0.0f;
    }
    if (trackingEnd > currentChargeTime) {
        trackingEnd = currentChargeTime;
    }

    if (!tellActive_ && stateTimer_ <= 0.0001f) {
        EnterTell(action_.variant);
    }

    if (tellActive_) {
        const float tellTurnScale =
            action_.variant == ActionVariant::Smash ? 0.55f : 0.50f;
        UpdateFacingToPlayerWithSpeed(deltaTime, chargeTurnSpeed_ * tellTurnScale);
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

        ChangeActionStep(ActionStep::Active);
    }
}

void Enemy::UpdateMeleeAttack(float deltaTime) {
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

void Enemy::UpdateMeleeRecovery(float deltaTime) {
    UpdateFacingToPlayerWithSpeed(deltaTime, recoveryTurnSpeed_);

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
