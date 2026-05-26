#include "Enemy.h"

#include <algorithm>
#include <cmath>

namespace {
float DifficultyRatio(float difficulty) {
    return std::clamp(difficulty, 0.0f, 9.0f) / 9.0f;
}

void ScaleAttackProfile(EnemyAttackProfile &profile, float damageScale,
                        float knockbackScale, float hitBoxScale,
                        float timingScale) {
    profile.attack.damage *= damageScale;
    profile.attack.knockback *= knockbackScale;
    profile.attack.hitBoxSize.x *= hitBoxScale;
    profile.attack.hitBoxSize.y *= hitBoxScale;
    profile.attack.hitBoxSize.z *= hitBoxScale;
    profile.chargeTime *= timingScale;
    profile.timing.totalTime *= timingScale;
    profile.timing.trackingEndTime *= timingScale;
    profile.timing.activeStartTime *= timingScale;
    profile.timing.activeEndTime *= timingScale;
    profile.timing.recoveryStartTime *= timingScale;
}
} // namespace

void Enemy::SetDifficulty(float difficulty) {
    difficulty_ = std::clamp(difficulty, 0.0f, 9.0f);
    const float t = DifficultyRatio(difficulty_);

    config_ = EnemyConfig{};
    config_.core.maxHp *= 0.58f + 0.095f * difficulty_;
    config_.core.phase2HealthRatioThreshold = 0.62f + 0.18f * t;
    config_.core.phase3HealthRatioThreshold = 0.25f + 0.20f * t;
    config_.core.nearAttackDistance = 3.35f + 1.20f * t;

    const float damageScale = 0.62f + 0.075f * difficulty_;
    const float knockbackScale = 0.70f + 0.055f * difficulty_;
    const float hitBoxScale = 0.86f + 0.035f * difficulty_;
    const float timingScale = 1.34f - 0.055f * difficulty_;
    ScaleAttackProfile(config_.attacks.smash.melee.base, damageScale,
                       knockbackScale, hitBoxScale, timingScale);
    ScaleAttackProfile(config_.attacks.sweep.melee.base, damageScale,
                       knockbackScale, hitBoxScale, timingScale);
    ScaleAttackProfile(config_.attacks.bladeClash.profile, damageScale,
                       knockbackScale, hitBoxScale, timingScale);

    config_.attacks.smash.melee.holdTime.min = 0.18f + 0.28f * (1.0f - t);
    config_.attacks.smash.melee.holdTime.max = 0.36f + 0.54f * (1.0f - t);
    config_.attacks.sweep.melee.holdTime.min = 0.16f + 0.24f * (1.0f - t);
    config_.attacks.sweep.melee.holdTime.max = 0.32f + 0.48f * (1.0f - t);
    config_.attacks.smash.melee.feintChance = 0.34f;
    config_.attacks.sweep.melee.feintChance = 0.30f;
    config_.attacks.bladeClash.advanceSpeed = 2.8f + 2.0f * t;

    const float warpTimeScale = 1.30f - 0.46f * t;
    config_.warp.startTime *= warpTimeScale;
    config_.warp.moveTime *= warpTimeScale;
    config_.warp.endTime *= warpTimeScale;

    smashTellTime_ = 0.30f - 0.14f * t;
    sweepTellTime_ = 0.28f - 0.13f * t;
    nearSmashWeight_ = 32;
    nearSweepWeight_ = 34;
    phase2NearSmashBonus_ = 7;
    phase2NearSweepBonus_ = 14;
    phase3NearSmashBonus_ = 7;
    phase3NearSweepBonus_ = 9;
    chargeTurnSpeed_ = 3.8f + 4.4f * t;
    recoveryTurnSpeed_ = 1.3f + 2.1f * t;
    idleTurnSpeed_ = 5.0f + 5.0f * t;

    quickSlashChance_ = 0.34f;
    quickSmashChargeTime_ = 0.96f - 0.42f * t;
    quickSweepChargeTime_ = 0.88f - 0.38f * t;
    directionFeintChance_ = 0.36f;
    chargeWarpFeintChance_ = 0.38f;
    farWarpSlashChance_ = 0.62f;
    farWarpSlashDistance_ = 12.2f - 2.4f * t;
    farSlashLungeSpeed_ = 32.0f + 20.0f * t;
    phantomWarpChance_ = 0.28f;
    phantomWarpCooldownDuration_ = 8.2f - 4.4f * t;
    phantomFinalLockDuration_ = 0.38f - 0.16f * t;
    bladeClashChance_ = 0.30f;

    stalkDurationMin_ = 0.70f - 0.36f * t;
    stalkDurationMax_ = 1.48f - 0.74f * t;
    stalkMoveSpeed_ = 0.92f + 1.28f * t;
    stalkPounceDistanceBonus_ = 0.35f + 0.62f * t;
    stalkPounceMinTime_ = 0.34f - 0.16f * t;
    stalkPounceChance_ = 0.52f;
    warpApproachFrontDistance_ = 2.95f - 0.62f * t;
    warpApproachBackDistance_ = 2.72f - 0.55f * t;
    warpNearChance_ = 0.24f;
    warpFarChance_ = 0.56f;
    warpCutInDistance_ = 7.4f - 2.0f * t;
    warpCutInChance_ = 0.78f;
    warpFeintChance_ = 0.34f;
    warpFeintEndTimeScale_ = 0.78f - 0.34f * t;
    warpTrailLife_ = 0.04f + 0.04f * t;
    warpTrailScaleMax_ = 0.68f + 0.38f * t;

    ValidateAllTimings();
    if (!deathFinished_) {
        runtime_.hp = config_.core.maxHp;
    }
}

float Enemy::GetCurrentSmashChargeTime() const {
    if (quickSlashActive_ || farSlashActive_) {
        return quickSmashChargeTime_;
    }
    if (warpFeintImmediate_) {
        return (std::max)(0.18f, quickSmashChargeTime_ * 0.72f);
    }
    float result = config_.attacks.smash.melee.base.chargeTime;
    if (result < 0.05f) {
        result = 0.05f;
    }
    return result;
}

float Enemy::GetCurrentSweepChargeTime() const {
    if (quickSlashActive_ || farSlashActive_) {
        return quickSweepChargeTime_;
    }
    if (warpFeintImmediate_) {
        return (std::max)(0.18f, quickSweepChargeTime_ * 0.72f);
    }
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
    ValidateTiming(config_.attacks.bladeClash.profile.timing,
                   config_.attacks.bladeClash.profile.chargeTime);
}

