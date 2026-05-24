#include "Enemy.h"

#include <algorithm>

float Enemy::GetCurrentSmashChargeTime() const {
    float result = config_.attacks.smash.melee.base.chargeTime;
    if (result < 0.05f) {
        result = 0.05f;
    }
    return result;
}

float Enemy::GetCurrentSweepChargeTime() const {
    float result = config_.attacks.sweep.melee.base.chargeTime;
    if (result < 0.05f) {
        result = 0.05f;
    }
    return result;
}

float Enemy::GetReleaseAnticipationRatio() const {
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
        case ActionKind::Wave:
            releaseTime = config_.attacks.wave.chargeTime;
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

ActionId Enemy::MakeDefaultActionId(ActionKind kind) const {
    switch (kind) {
    case ActionKind::Smash:
        return ActionId::Smash;
    case ActionKind::Sweep:
        return ActionId::Sweep;
    case ActionKind::Wave:
        return ActionId::Wave;
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

