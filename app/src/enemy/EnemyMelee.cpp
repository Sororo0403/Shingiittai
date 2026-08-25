#include "Enemy.h"

#include <algorithm>
#include <cmath>

namespace {
float ChargeTurnScaleAfterStance(float stateTimer, float stanceTime) {
    return stateTimer < stanceTime ? 1.0f : 0.035f;
}

float SmoothStep01(float value) {
    const float t = std::clamp(value, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float EffectiveCombatDifficulty(float difficulty) {
    const float clamped = std::clamp(difficulty, 0.0f, 9.0f);
    const float pressure = SmoothStep01(clamped / 9.0f);
    return std::clamp(clamped + 0.55f + 0.45f * pressure, 0.0f, 9.0f);
}

float HighDifficultyPressure(float difficulty) {
    return EffectiveCombatDifficulty(difficulty) / 9.0f;
}

float RecoveryPadding(float basePadding, float difficulty) {
    return basePadding * (1.0f - 0.55f * HighDifficultyPressure(difficulty));
}

constexpr float kFarSlashCounterFlashDuration = 0.50f;
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

void Enemy::UpdateArcaneLaserByStep(float deltaTime) {
    switch (action_.step) {
    case ActionStep::Charge:
        UpdateArcaneLaserCharge(deltaTime);
        break;
    case ActionStep::Active:
        UpdateArcaneLaserActive(deltaTime);
        break;
    case ActionStep::Recovery:
        UpdateArcaneLaserRecovery(deltaTime);
        break;
    default:
        EndAttack();
        break;
    }
}

void Enemy::UpdateCataclysmLaserByStep(float deltaTime) {
    switch (action_.step) {
    case ActionStep::Charge:
        UpdateCataclysmLaserCharge(deltaTime);
        break;
    case ActionStep::Active:
        UpdateCataclysmLaserActive(deltaTime);
        break;
    case ActionStep::Recovery:
        UpdateCataclysmLaserRecovery(deltaTime);
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

    if (UpdateChargeTell(ActionKind::Smash, deltaTime, 0.55f)) {
        return;
    }
    if (TryChargeFeint(ActionKind::Smash)) {
        return;
    }

    const float stanceTime = (std::min)(trackingEnd, 0.30f);
    UpdateChargeTracking(deltaTime, trackingEnd, stanceTime);
    IssueReleaseCueIfReady();
    FinishSmashCharge(currentChargeTime);
}

bool Enemy::UpdateChargeTell(ActionKind kind, float deltaTime,
                             float turnSpeedScale) {
    if (!tellActive_ && stateTimer_ <= 0.0001f) {
        EnterTell(kind);
    }
    if (!tellActive_) {
        return false;
    }
    UpdateFacingToPlayerWithSpeed(deltaTime, chargeTurnSpeed_ * turnSpeedScale);
    if (!IsTellFinished()) {
        return true;
    }
    tellActive_ = false;
    stateTimer_ = 0.0f;
    return false;
}

bool Enemy::TryChargeFeint(ActionKind kind) {
    if (quickSlashActive_ || farSlashActive_) {
        return false;
    }
    return TryApplyDirectionFeint(kind) || TryBeginChargeWarpFeint(kind);
}

void Enemy::UpdateChargeTracking(float deltaTime, float trackingEnd,
                                 float stanceTime) {
    if (stateTimer_ < trackingEnd) {
        UpdateFacingToPlayerWithSpeed(
            deltaTime, chargeTurnSpeed_ *
                           ChargeTurnScaleAfterStance(stateTimer_, stanceTime));
    } else if (!hasTrackingLocked_) {
        LockCurrentFacing();
        hasTrackingLocked_ = true;
    }
}

void Enemy::FinishSmashCharge(float currentChargeTime) {
    if (stateTimer_ < currentChargeTime) {
        return;
    }
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

void Enemy::UpdateSmashHold(float deltaTime) {
    (void)deltaTime;

    IssueReleaseCueIfReady();

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
        UpdateFarSlashLunge(deltaTime);
    }

    const float attackStartTime = farSlashActive_
                                      ? kFarSlashCounterFlashDuration
                                      : timing->activeStartTime;
    const float attackEndTime =
        farSlashActive_ && farSlashLungeDuration_ > 0.0001f
            ? std::max(timing->activeEndTime,
                       kFarSlashCounterFlashDuration + farSlashLungeDuration_)
            : timing->activeEndTime;

    isAttackActive_ =
        stateTimer_ >= attackStartTime && stateTimer_ <= attackEndTime;
    if (stateTimer_ >= attackEndTime) {
        ChangeActionStep(ActionStep::Recovery);
        if (tripleIaiSlashActive_ && farSlashActive_ &&
            tripleIaiSlashesRemaining_ > 0) {
            runtime_.tripleIaiReturnCameraToCenter = true;
            isVisible_ = false;
        }
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
    recoveryDuration += RecoveryPadding(0.18f, difficulty_);
    if (tripleIaiSlashActive_ && farSlashActive_ &&
        tripleIaiSlashesRemaining_ > 0) {
        recoveryDuration = std::min(recoveryDuration, 0.035f);
    }

    if (stateTimer_ >= recoveryDuration) {
        if (TryContinueTripleIaiSlash()) {
            return;
        }
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

    if (UpdateChargeTell(ActionKind::Sweep, deltaTime, 0.50f)) {
        return;
    }
    if (TryChargeFeint(ActionKind::Sweep)) {
        return;
    }

    const float stanceTime = (std::min)(trackingEnd, 0.28f);
    UpdateChargeTracking(deltaTime, trackingEnd, stanceTime);
    IssueReleaseCueIfReady();
    FinishSweepCharge(currentChargeTime);
}

void Enemy::FinishSweepCharge(float currentChargeTime) {
    if (stateTimer_ < currentChargeTime) {
        return;
    }
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

void Enemy::UpdateSweepHold(float deltaTime) {
    (void)deltaTime;

    IssueReleaseCueIfReady();

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
        UpdateFarSlashLunge(deltaTime);
    }

    const float attackStartTime = farSlashActive_
                                      ? kFarSlashCounterFlashDuration
                                      : timing->activeStartTime;
    const float attackEndTime =
        farSlashActive_ && farSlashLungeDuration_ > 0.0001f
            ? std::max(timing->activeEndTime,
                       kFarSlashCounterFlashDuration + farSlashLungeDuration_)
            : timing->activeEndTime;

    isAttackActive_ =
        stateTimer_ >= attackStartTime && stateTimer_ <= attackEndTime;
    if (stateTimer_ >= attackEndTime) {
        ChangeActionStep(ActionStep::Recovery);
        if (tripleIaiSlashActive_ && farSlashActive_ &&
            tripleIaiSlashesRemaining_ > 0) {
            runtime_.tripleIaiReturnCameraToCenter = true;
            isVisible_ = false;
        }
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
    recoveryDuration += RecoveryPadding(0.16f, difficulty_);
    if (tripleIaiSlashActive_ && farSlashActive_ &&
        tripleIaiSlashesRemaining_ > 0) {
        recoveryDuration = std::min(recoveryDuration, 0.035f);
    }

    if (stateTimer_ >= recoveryDuration) {
        if (TryContinueTripleIaiSlash()) {
            return;
        }
        EndAttack();
    }
}

void Enemy::UpdateBladeClashCharge(float) {
    const auto &profile = config_.attacks.bladeClash.profile;

    if (!hasTrackingLocked_) {
        LockCurrentFacing();
        hasTrackingLocked_ = true;
    }

    if (stateTimer_ >= profile.chargeTime) {
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
    recoveryDuration += RecoveryPadding(0.20f, difficulty_);

    if (stateTimer_ >= recoveryDuration) {
        EndAttack();
    }
}

void Enemy::UpdateArcaneLaserCharge(float deltaTime) {
    const auto &profile = config_.attacks.arcaneLaser.profile;
    UpdateFacingToPlayerWithSpeed(deltaTime, chargeTurnSpeed_ * 0.44f);
    const float forwardX = std::sin(facingYaw_);
    const float forwardZ = std::cos(facingYaw_);
    tf_.position.x += forwardX * stalkMoveSpeed_ * 1.12f * deltaTime;
    tf_.position.z += forwardZ * stalkMoveSpeed_ * 1.12f * deltaTime;

    if (stateTimer_ >= profile.chargeTime) {
        LockCurrentFacing();
        hasTrackingLocked_ = true;
        arcaneLaserDirection_ = {std::sin(lockedAttackYaw_), 0.0f,
                                 std::cos(lockedAttackYaw_)};
        ChangeActionStep(ActionStep::Active);
    }
}

void Enemy::UpdateArcaneLaserActive(float) {
    const auto &timing = config_.attacks.arcaneLaser.profile.timing;
    isAttackActive_ = stateTimer_ >= timing.activeStartTime &&
                      stateTimer_ <= timing.activeEndTime;
    if (stateTimer_ >= timing.activeEndTime) {
        ChangeActionStep(ActionStep::Recovery);
    }
}

void Enemy::UpdateArcaneLaserRecovery(float deltaTime) {
    UpdateFacingToPlayerWithSpeed(deltaTime, recoveryTurnSpeed_ * 0.20f);

    if (stateTimer_ >= config_.attacks.arcaneLaser.recoveryDuration) {
        sharedRangedAttackCooldown_ =
            (std::max)(sharedRangedAttackCooldown_,
                       sharedRangedAttackCooldownDuration_);
        if (TryBeginLaserReengageWarp(1.0f)) {
            return;
        }
        EndAttack();
    }
}

void Enemy::UpdateCataclysmLaserCharge(float deltaTime) {
    const auto &profile = config_.attacks.cataclysmLaser.profile;
    UpdateFacingToPlayerWithSpeed(deltaTime, chargeTurnSpeed_ * 0.30f);
    const float forwardX = std::sin(facingYaw_);
    const float forwardZ = std::cos(facingYaw_);

    constexpr float kLiftDelay = 0.12f;
    constexpr float kBoostLiftTime = 0.26f;
    constexpr float kHoverHeight = 4.25f;
    const float groundY = playerPos_.y;
    const float liftDuration = std::min(
        kBoostLiftTime, std::max(0.18f, profile.chargeTime - kLiftDelay));
    const float liftRatio =
        std::clamp((stateTimer_ - kLiftDelay) / liftDuration, 0.0f, 1.0f);
    const float smoothLift =
        1.0f - (1.0f - liftRatio) * (1.0f - liftRatio) * (1.0f - liftRatio);
    tf_.position.y = groundY + kHoverHeight * smoothLift;

    const float boostPush =
        stateTimer_ >= kLiftDelay && stateTimer_ <= kLiftDelay + kBoostLiftTime
            ? 1.35f
            : 0.0f;
    const float thrustForwardScale = 0.24f + 0.42f * smoothLift + boostPush;
    tf_.position.x +=
        forwardX * stalkMoveSpeed_ * thrustForwardScale * deltaTime;
    tf_.position.z +=
        forwardZ * stalkMoveSpeed_ * thrustForwardScale * deltaTime;

    if (stateTimer_ >= profile.chargeTime) {
        LockCurrentFacing();
        hasTrackingLocked_ = true;
        cataclysmLaserDirection_ = {std::sin(lockedAttackYaw_), 0.0f,
                                    std::cos(lockedAttackYaw_)};
        ChangeActionStep(ActionStep::Active);
    }
}

void Enemy::UpdateCataclysmLaserActive(float) {
    const auto &timing = config_.attacks.cataclysmLaser.profile.timing;
    isAttackActive_ = stateTimer_ >= timing.activeStartTime &&
                      stateTimer_ <= timing.activeEndTime;
    if (stateTimer_ >= timing.activeEndTime) {
        ChangeActionStep(ActionStep::Recovery);
    }
}

void Enemy::UpdateCataclysmLaserRecovery(float deltaTime) {
    UpdateFacingToPlayerWithSpeed(deltaTime, recoveryTurnSpeed_ * 0.12f);

    const float groundY = playerPos_.y;
    const float descendStep = 6.4f * deltaTime;
    if (tf_.position.y > groundY) {
        tf_.position.y = std::max(groundY, tf_.position.y - descendStep);
    } else if (tf_.position.y < groundY) {
        tf_.position.y = std::min(groundY, tf_.position.y + descendStep);
    }

    if (stateTimer_ >= config_.attacks.cataclysmLaser.recoveryDuration) {
        tf_.position.y = groundY;
        EndAttack();
    }
}
