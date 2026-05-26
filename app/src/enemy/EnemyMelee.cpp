#include "Enemy.h"

#include <algorithm>
#include <cmath>

namespace {
float ChargeTurnScaleAfterStance(float stateTimer, float stanceTime) {
    return stateTimer < stanceTime ? 1.0f : 0.035f;
}
} // namespace

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

void Enemy::UpdateBladeClashByStep(float deltaTime) {
    switch (action_.step) {
    case ActionStep::Charge:
        UpdateBladeClashCharge(deltaTime);
        break;
    case ActionStep::Active:
        UpdateBladeClashActive(deltaTime);
        break;
    case ActionStep::Recovery:
        UpdateBladeClashRecovery(deltaTime);
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

    if (!quickSlashActive_ && TryApplyDirectionFeint(ActionKind::Smash)) {
        return;
    }
    if (!quickSlashActive_ && TryBeginChargeWarpFeint(ActionKind::Smash)) {
        return;
    }

    const float stanceTime = (std::min)(trackingEnd, 0.46f);
    if (stateTimer_ < trackingEnd) {
        UpdateFacingToPlayerWithSpeed(
            deltaTime,
            chargeTurnSpeed_ *
                ChargeTurnScaleAfterStance(stateTimer_, stanceTime));
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
            ChangeActionStep(ActionStep::Hold);
            EnterHold(RandomRange(config_.attacks.smash.melee.holdTime.min,
                                  config_.attacks.smash.melee.holdTime.max));
            return;
        }
        ChangeActionStep(ActionStep::Active);
    }
}

void Enemy::UpdateSmashHold(float deltaTime) {
    (void)deltaTime;

    if (ShouldSnapReleaseFromRead() || stateTimer_ >= currentHoldDuration_) {
        ChangeActionStep(ActionStep::Active);
    }
}

void Enemy::UpdateSmashAttack(float deltaTime) {
    const AttackTimingParam *timing = GetCurrentAttackTiming();
    if (!timing) {
        EndAttack();
        return;
    }

    if (farSlashActive_) {
        const float forwardX = std::sin(lockedAttackYaw_);
        const float forwardZ = std::cos(lockedAttackYaw_);
        tf_.position.x += forwardX * farSlashLungeSpeed_ * deltaTime;
        tf_.position.z += forwardZ * farSlashLungeSpeed_ * deltaTime;
    }

    isAttackActive_ = stateTimer_ >= timing->activeStartTime &&
                      stateTimer_ <= timing->activeEndTime;
    if (stateTimer_ >= timing->activeEndTime) {
        ChangeActionStep(ActionStep::Recovery);
    }
}

void Enemy::UpdateSmashRecovery(float deltaTime) {
    UpdateFacingToPlayerWithSpeed(deltaTime, recoveryTurnSpeed_ * 0.25f);

    const AttackTimingParam *timing = GetCurrentAttackTiming();
    if (!timing) {
        EndAttack();
        return;
    }

    float recoveryDuration = timing->totalTime - timing->recoveryStartTime;
    if (recoveryDuration < 0.0f) {
        recoveryDuration = 0.0f;
    }
    recoveryDuration += 0.18f;

    if (stateTimer_ >= recoveryDuration) {
        EndAttack();
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

    if (!quickSlashActive_ && TryApplyDirectionFeint(ActionKind::Sweep)) {
        return;
    }
    if (!quickSlashActive_ && TryBeginChargeWarpFeint(ActionKind::Sweep)) {
        return;
    }

    const float stanceTime = (std::min)(trackingEnd, 0.42f);
    if (stateTimer_ < trackingEnd) {
        UpdateFacingToPlayerWithSpeed(
            deltaTime,
            chargeTurnSpeed_ *
                ChargeTurnScaleAfterStance(stateTimer_, stanceTime));
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
            ChangeActionStep(ActionStep::Hold);
            EnterHold(RandomRange(config_.attacks.sweep.melee.holdTime.min,
                                  config_.attacks.sweep.melee.holdTime.max));
            return;
        }
        ChangeActionStep(ActionStep::Active);
    }
}

void Enemy::UpdateSweepHold(float deltaTime) {
    (void)deltaTime;

    if (ShouldSnapReleaseFromRead() || stateTimer_ >= currentHoldDuration_) {
        ChangeActionStep(ActionStep::Active);
    }
}

void Enemy::UpdateSweepAttack(float deltaTime) {
    const AttackTimingParam *timing = GetCurrentAttackTiming();
    if (!timing) {
        EndAttack();
        return;
    }

    if (farSlashActive_) {
        const float forwardX = std::sin(lockedAttackYaw_);
        const float forwardZ = std::cos(lockedAttackYaw_);
        tf_.position.x += forwardX * farSlashLungeSpeed_ * 0.92f * deltaTime;
        tf_.position.z += forwardZ * farSlashLungeSpeed_ * 0.92f * deltaTime;
    }

    isAttackActive_ = stateTimer_ >= timing->activeStartTime &&
                      stateTimer_ <= timing->activeEndTime;
    if (stateTimer_ >= timing->activeEndTime) {
        ChangeActionStep(ActionStep::Recovery);
    }
}

void Enemy::UpdateSweepRecovery(float deltaTime) {
    UpdateFacingToPlayerWithSpeed(deltaTime, recoveryTurnSpeed_ * 0.25f);

    const AttackTimingParam *timing = GetCurrentAttackTiming();
    if (!timing) {
        EndAttack();
        return;
    }

    float recoveryDuration = timing->totalTime - timing->recoveryStartTime;
    if (recoveryDuration < 0.0f) {
        recoveryDuration = 0.0f;
    }
    recoveryDuration += 0.16f;

    if (stateTimer_ >= recoveryDuration) {
        EndAttack();
    }
}

void Enemy::UpdateBladeClashCharge(float deltaTime) {
    const auto &profile = config_.attacks.bladeClash.profile;
    float trackingEnd = profile.timing.trackingEndTime;
    if (trackingEnd < 0.0f) {
        trackingEnd = 0.0f;
    }
    if (trackingEnd > profile.chargeTime) {
        trackingEnd = profile.chargeTime;
    }

    if (stateTimer_ < trackingEnd) {
        UpdateFacingToPlayerWithSpeed(deltaTime, chargeTurnSpeed_ * 0.85f);
        const float toPlayerX = playerPos_.x - tf_.position.x;
        const float toPlayerZ = playerPos_.z - tf_.position.z;
        const float distSq = toPlayerX * toPlayerX + toPlayerZ * toPlayerZ;
        if (distSq > 2.20f * 2.20f) {
            const float dist = std::sqrt(distSq);
            tf_.position.x +=
                (toPlayerX / dist) * config_.attacks.bladeClash.advanceSpeed *
                deltaTime;
            tf_.position.z +=
                (toPlayerZ / dist) * config_.attacks.bladeClash.advanceSpeed *
                deltaTime;
        }
    } else if (!hasTrackingLocked_) {
        LockCurrentFacing();
        hasTrackingLocked_ = true;
    }

    if (stateTimer_ >= profile.chargeTime) {
        if (!hasTrackingLocked_) {
            LockCurrentFacing();
            hasTrackingLocked_ = true;
        }
        ChangeActionStep(ActionStep::Active);
    }
}

void Enemy::UpdateBladeClashActive(float) {
    const AttackTimingParam &timing = config_.attacks.bladeClash.profile.timing;
    isAttackActive_ = stateTimer_ >= timing.activeStartTime &&
                      stateTimer_ <= timing.activeEndTime;
    if (stateTimer_ >= timing.activeEndTime) {
        ChangeActionStep(ActionStep::Recovery);
    }
}

void Enemy::UpdateBladeClashRecovery(float deltaTime) {
    UpdateFacingToPlayerWithSpeed(deltaTime, recoveryTurnSpeed_ * 0.35f);

    const AttackTimingParam &timing = config_.attacks.bladeClash.profile.timing;
    float recoveryDuration = timing.totalTime - timing.recoveryStartTime;
    if (recoveryDuration < 0.0f) {
        recoveryDuration = 0.0f;
    }
    recoveryDuration += 0.20f;

    if (stateTimer_ >= recoveryDuration) {
        EndAttack();
    }
}

