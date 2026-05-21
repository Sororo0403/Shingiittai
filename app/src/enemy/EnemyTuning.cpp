#include "Enemy.h"

#include <algorithm>

float Enemy::GetCurrentActionTime() const { return stateTimer_; }

float Enemy::GetCurrentSmashChargeTime() const {
    if (phase3GuardCounterActive_) {
        return phase3GuardCounterSmashChargeTime_;
    }
    if (action_.id == ActionId::QuickSmash) {
        return quickSmashChargeTime_;
    }

    float result = config_.attacks.smash.melee.base.chargeTime;
    if (action_.id == ActionId::DelaySmash) {
        result += config_.attacks.smash.delayExtraChargeTime;
    }

    result += GetAdaptiveChargeOffset(ActionKind::Smash);
    if (result < 0.05f) {
        result = 0.05f;
    }
    return result;
}

float Enemy::GetCurrentSweepChargeTime() const {
    if (phase3GuardCounterActive_) {
        return phase3GuardCounterSweepChargeTime_;
    }
    if (action_.id == ActionId::QuickSweep) {
        return quickSweepChargeTime_;
    }

    float result = config_.attacks.sweep.melee.base.chargeTime;

    result += GetAdaptiveChargeOffset(ActionKind::Sweep);
    if (result < 0.05f) {
        result = 0.05f;
    }
    return result;
}

bool Enemy::HasReachedTrackingEnd() const {
    const AttackTimingParam *timing = GetCurrentAttackTiming();
    return timing ? GetCurrentActionTime() >= timing->trackingEndTime : false;
}

bool Enemy::HasReachedHitStart() const {
    const AttackTimingParam *timing = GetCurrentAttackTiming();
    return timing ? GetCurrentActionTime() >= timing->activeStartTime : false;
}

bool Enemy::HasReachedHitEnd() const {
    const AttackTimingParam *timing = GetCurrentAttackTiming();
    return timing ? GetCurrentActionTime() > timing->activeEndTime : false;
}

bool Enemy::HasReachedRecoveryStart() const {
    const AttackTimingParam *timing = GetCurrentAttackTiming();
    return timing ? GetCurrentActionTime() >= timing->recoveryStartTime : false;
}

float Enemy::GetReleaseAnticipationRatio() const {
    if (IsQuickCounterAction() && action_.step == ActionStep::Charge) {
        return 1.0f;
    }
    if (phase2FeintFollowupLocked_ && phase2FeintImmediateGreen_ &&
        action_.step == ActionStep::Charge &&
        (action_.kind == ActionKind::Smash ||
         action_.kind == ActionKind::Sweep)) {
        return 1.0f;
    }

    float releaseTime = 0.0f;
    float cueWindow = 0.52f;

    if (action_.step == ActionStep::Charge) {
        switch (action_.kind) {
        case ActionKind::Smash:
            releaseTime = GetCurrentSmashChargeTime();
            break;
        case ActionKind::Sweep:
            releaseTime = GetCurrentSweepChargeTime();
            break;
        case ActionKind::BladeClash:
            releaseTime = config_.attacks.bladeClash.chargeTime;
            break;
        case ActionKind::Wave:
            releaseTime = config_.attacks.wave.chargeTime;
            break;
        case ActionKind::Cage:
            releaseTime = config_.attacks.cage.chargeTime;
            break;
        default:
            return 0.0f;
        }
    } else if (action_.step == ActionStep::Hold &&
               (action_.kind == ActionKind::Smash ||
                action_.kind == ActionKind::Sweep)) {
        releaseTime = currentHoldDuration_;
        cueWindow = 0.48f;
    } else {
        return 0.0f;
    }

    if (releaseTime <= 0.0f) {
        return 0.0f;
    }

    const float remaining = releaseTime - stateTimer_;
    if (remaining < 0.0f || remaining > cueWindow) {
        return 0.0f;
    }

    return std::clamp(1.0f - remaining / cueWindow, 0.0f, 1.0f);
}

float Enemy::GetChargeWeakPointTimeLimitForPresentation() const {
    if (action_.kind != ActionKind::Smash ||
        action_.id != ActionId::DelaySmash) {
        return 0.0f;
    }

    if (action_.step == ActionStep::Hold) {
        return currentHoldDuration_;
    }

    if (action_.step != ActionStep::Charge) {
        return 0.0f;
    }

    return GetCurrentSmashChargeTime();
}

float Enemy::GetChargeWeakPointTimeRemainingForPresentation() const {
    const float limit = GetChargeWeakPointTimeLimitForPresentation();
    if (limit <= 0.0f) {
        return 0.0f;
    }

    return (std::max)(0.0f, limit - stateTimer_);
}

ActionId Enemy::MakeDefaultActionId(ActionKind kind) const {
    switch (kind) {
    case ActionKind::Smash:
        return ActionId::Smash;
    case ActionKind::Sweep:
        return ActionId::Sweep;
    case ActionKind::BladeClash:
        return ActionId::BladeClash;
    case ActionKind::Wave:
        return ActionId::Wave;
    case ActionKind::Cage:
        return ActionId::Cage;
    case ActionKind::Warp:
        return (warp_.type == WarpType::Escape) ? ActionId::WarpEscape
                                                : ActionId::WarpApproach;
    default:
        return ActionId::None;
    }
}

void Enemy::ValidateTiming(AttackTimingParam &timing, float chargeTime) {
    if (timing.totalTime < 0.0f) {
        timing.totalTime = 0.0f;
    }
    if (timing.trackingEndTime < 0.0f) {
        timing.trackingEndTime = 0.0f;
    }
    if (timing.trackingEndTime > chargeTime) {
        timing.trackingEndTime = chargeTime;
    }
    if (timing.activeStartTime < 0.0f) {
        timing.activeStartTime = 0.0f;
    }
    if (timing.activeEndTime < timing.activeStartTime) {
        timing.activeEndTime = timing.activeStartTime;
    }
    if (timing.recoveryStartTime < timing.activeEndTime) {
        timing.recoveryStartTime = timing.activeEndTime;
    }
    if (timing.totalTime < timing.recoveryStartTime) {
        timing.totalTime = timing.recoveryStartTime;
    }
}

void Enemy::ValidateAllTimings() {
    ValidateTiming(config_.attacks.smash.melee.base.timing,
                   config_.attacks.smash.melee.base.chargeTime);
    ValidateTiming(config_.attacks.sweep.melee.base.timing,
                   config_.attacks.sweep.melee.base.chargeTime);
}

