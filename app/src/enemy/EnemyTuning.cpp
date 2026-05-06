#include "Enemy.h"

float Enemy::GetCurrentActionTime() const { return stateTimer_; }

float Enemy::GetCurrentMeleeChargeTime() const {
    float result = 0.0f;
    if (action_.variant == ActionVariant::Smash) {
        result = config_.attacks.smash.melee.base.chargeTime;
    } else if (action_.variant == ActionVariant::Sweep) {
        result = config_.attacks.sweep.melee.base.chargeTime;
    }

    if (result < 0.05f) {
        result = 0.05f;
    }
    return result;
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

