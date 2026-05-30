#include "Enemy.h"

#include <algorithm>
#include <cmath>

namespace {
float SmoothStep01(float value) {
    const float t = std::clamp(value, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float EffectiveCombatDifficulty(float difficulty) {
    const float clamped = std::clamp(difficulty, 0.0f, 9.0f);
    const float pressure = SmoothStep01(clamped / 9.0f);
    return std::clamp(clamped + 0.55f + 0.45f * pressure, 0.0f, 9.0f);
}

float DifficultyRatio(float difficulty) {
    return EffectiveCombatDifficulty(difficulty) / 9.0f;
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

void SetActiveWindowDuration(EnemyAttackProfile &profile, float duration) {
    const float currentDuration =
        profile.timing.activeEndTime - profile.timing.activeStartTime;
    const float extension = duration - currentDuration;
    if (extension <= 0.0f) {
        return;
    }

    profile.timing.activeEndTime += extension;
    profile.timing.recoveryStartTime += extension;
    profile.timing.totalTime += extension;
}
} // namespace

void Enemy::SetDifficulty(float difficulty) {
    difficulty_ = std::clamp(difficulty, 0.0f, 9.0f);
    const float effectiveDifficulty = EffectiveCombatDifficulty(difficulty_);
    const float t = DifficultyRatio(difficulty_);
    const float highPressure = t;

    config_ = EnemyConfig{};
    config_.core.maxHp *= 0.58f + 0.095f * effectiveDifficulty;
    config_.core.maxHp *= 1.45f;
    config_.core.phase2HealthRatioThreshold = 0.80f + 0.15f * t;
    config_.core.phase3HealthRatioThreshold = 0.35f + 0.20f * t;
    config_.core.nearAttackDistance = 3.35f + 1.20f * t;

    const float damageScale = 0.62f + 0.075f * effectiveDifficulty;
    const float knockbackScale = 0.70f + 0.055f * effectiveDifficulty;
    const float hitBoxScale = 0.86f + 0.035f * effectiveDifficulty;
    const float timingScale = 1.18f - 0.045f * effectiveDifficulty -
                              0.09f * highPressure;
    ScaleAttackProfile(config_.attacks.smash.melee.base, damageScale,
                       knockbackScale, hitBoxScale, timingScale);
    ScaleAttackProfile(config_.attacks.sweep.melee.base, damageScale,
                       knockbackScale, hitBoxScale, timingScale);
    ScaleAttackProfile(config_.attacks.bladeClash.profile, damageScale,
                       knockbackScale, hitBoxScale, timingScale);
    ScaleAttackProfile(config_.attacks.arcaneLaser.profile, damageScale * 1.08f,
                       knockbackScale * 1.06f, hitBoxScale, timingScale);
    ScaleAttackProfile(config_.attacks.cataclysmLaser.profile,
                       damageScale * 1.24f, knockbackScale * 1.18f,
                       hitBoxScale, timingScale);

    constexpr float kEasyActiveWindowDuration = 2.0f;
    auto widenActiveWindow = [&](EnemyAttackProfile &profile) {
        const float currentDuration =
            profile.timing.activeEndTime - profile.timing.activeStartTime;
        const float targetDuration =
            currentDuration +
            (kEasyActiveWindowDuration - currentDuration) * (1.0f - t);
        SetActiveWindowDuration(profile, targetDuration);
    };
    widenActiveWindow(config_.attacks.smash.melee.base);
    widenActiveWindow(config_.attacks.sweep.melee.base);

    config_.attacks.smash.melee.holdTime.min = 0.12f + 0.20f * (1.0f - t);
    config_.attacks.smash.melee.holdTime.max = 0.24f + 0.38f * (1.0f - t);
    config_.attacks.sweep.melee.holdTime.min = 0.11f + 0.17f * (1.0f - t);
    config_.attacks.sweep.melee.holdTime.max = 0.21f + 0.33f * (1.0f - t);
    config_.attacks.smash.melee.holdTime.min *= 1.0f - 0.24f * highPressure;
    config_.attacks.smash.melee.holdTime.max *= 1.0f - 0.32f * highPressure;
    config_.attacks.sweep.melee.holdTime.min *= 1.0f - 0.24f * highPressure;
    config_.attacks.sweep.melee.holdTime.max *= 1.0f - 0.32f * highPressure;
    config_.attacks.smash.melee.feintChance = 0.34f;
    config_.attacks.sweep.melee.feintChance = 0.30f;
    config_.attacks.bladeClash.advanceSpeed = 2.8f + 2.0f * t;
    config_.attacks.arcaneLaser.recoveryDuration *=
        1.0f - 0.38f * highPressure;
    config_.attacks.cataclysmLaser.recoveryDuration *=
        1.0f - 0.26f * highPressure;

    const float warpTimeScale = 1.30f - 0.54f * t;
    config_.warp.startTime *= warpTimeScale;
    config_.warp.moveTime *= warpTimeScale;
    config_.warp.endTime *= warpTimeScale;

    smashTellTime_ = 0.22f - 0.10f * t;
    sweepTellTime_ = 0.20f - 0.09f * t;
    nearSmashWeight_ = 32;
    nearSweepWeight_ = 34;
    phase2NearSmashBonus_ = 7;
    phase2NearSweepBonus_ = 14;
    phase3NearSmashBonus_ = 7;
    phase3NearSweepBonus_ = 9;
    chargeTurnSpeed_ = 3.8f + 4.4f * t;
    recoveryTurnSpeed_ = 1.3f + 2.9f * t;
    idleTurnSpeed_ = 5.0f + 5.0f * t;

    quickSlashChance_ = 0.20f;
    quickSmashChargeTime_ = 0.98f - 0.34f * t;
    quickSweepChargeTime_ = 0.92f - 0.30f * t;
    directionFeintChance_ = 0.36f;
    chargeWarpFeintChance_ = 0.38f;
    farWarpSlashChance_ = 0.76f;
    farWarpSlashDistance_ = 10.6f - 1.2f * t;
    farSlashLungeSpeed_ = 58.0f + 18.0f * t;
    farSlashSmashChargeTime_ = 1.12f - 0.22f * t;
    farSlashSweepChargeTime_ = 1.04f - 0.20f * t;
    phantomWarpChance_ = 0.38f;
    phantomWarpCooldownDuration_ = 6.8f - 3.6f * t;
    tripleIaiSlashChance_ = 0.28f + 0.24f * t;
    tripleIaiSlashCooldownDuration_ = 9.6f - 3.2f * t;
    tripleIaiCloneLife_ = 1.20f + 0.30f * t;
    phantomFinalLockDuration_ = 0.38f - 0.16f * t;
    bladeClashChance_ = 0.30f;
    arcaneLaserChance_ = 0.38f + 0.22f * t;
    arcaneLaserSlashFollowupChance_ = 0.52f + 0.24f * t;
    arcaneLaserCooldownDuration_ = 6.6f - 2.4f * t;
    arcaneLaserMinDistance_ = 5.8f - 1.2f * t;
    arcaneLaserWarpDistance_ = 28.0f + 5.5f * t;
    arcaneLaserSlashMinDistance_ = 8.8f - 2.4f * t;
    cataclysmLaserChance_ = 0.28f + 0.26f * t;
    cataclysmLaserCooldownDuration_ = 12.8f - 4.2f * t;
    cataclysmLaserMinDistance_ = 10.2f - 1.8f * t;
    cataclysmLaserWarpDistance_ = 34.0f + 6.0f * t;
    sharedRangedAttackCooldownDuration_ = 5.6f - 1.2f * t;
    rangedAttackChainLockoutDuration_ = 10.5f - 2.0f * t;
    rangedAttackChainLockoutThreshold_ = 5;
    config_.attacks.arcaneLaser.range = 28.0f + 5.0f * t;
    config_.attacks.arcaneLaser.radius = 0.44f + 0.10f * t;
    config_.attacks.arcaneLaser.profile.chargeTime = 1.16f - 0.24f * t;
    config_.attacks.arcaneLaser.profile.timing.trackingEndTime =
        0.72f - 0.16f * t;
    config_.attacks.arcaneLaser.profile.timing.activeStartTime = 0.0f;
    config_.attacks.arcaneLaser.profile.timing.activeEndTime =
        2.85f - 0.32f * t;
    config_.attacks.arcaneLaser.profile.timing.recoveryStartTime =
        config_.attacks.arcaneLaser.profile.timing.activeEndTime;
    config_.attacks.arcaneLaser.profile.timing.totalTime =
        config_.attacks.arcaneLaser.profile.timing.activeEndTime +
        config_.attacks.arcaneLaser.recoveryDuration;
    config_.attacks.cataclysmLaser.range = 39.0f + 9.0f * t;
    config_.attacks.cataclysmLaser.radius = 1.85f + 0.70f * t;
    config_.attacks.cataclysmLaser.profile.chargeTime = 0.62f - 0.08f * t;
    config_.attacks.cataclysmLaser.profile.timing.trackingEndTime =
        0.95f - 0.18f * t;
    config_.attacks.cataclysmLaser.profile.timing.activeStartTime = 0.0f;
    config_.attacks.cataclysmLaser.profile.timing.activeEndTime =
        4.70f - 0.28f * t;
    config_.attacks.cataclysmLaser.profile.timing.recoveryStartTime =
        config_.attacks.cataclysmLaser.profile.timing.activeEndTime;
    config_.attacks.cataclysmLaser.profile.timing.totalTime =
        config_.attacks.cataclysmLaser.profile.timing.activeEndTime +
        config_.attacks.cataclysmLaser.recoveryDuration;

    stalkDurationMin_ = 0.70f - 0.42f * t;
    stalkDurationMax_ = 1.48f - 0.84f * t;
    stalkMoveSpeed_ = 0.92f + 1.55f * t;
    stalkPounceDistanceBonus_ = 0.35f + 0.62f * t;
    stalkPounceMinTime_ = 0.34f - 0.16f * t;
    stalkPounceChance_ = 0.52f;
    warpApproachFrontDistance_ = 2.95f - 0.62f * t;
    warpApproachBackDistance_ = 2.72f - 0.55f * t;
    warpNearChance_ = 0.18f;
    warpFarChance_ = 0.70f;
    warpCutInDistance_ = 6.1f - 2.2f * t;
    warpCutInChance_ = 0.92f;
    warpFeintChance_ = 0.34f;
    warpFeintEndTimeScale_ = 0.78f - 0.34f * t;
    warpTrailLife_ = 0.28f + 0.10f * t;
    warpTrailScaleMax_ = 0.84f + 0.48f * t;
    hitReactionDuration_ = 0.16f - 0.06f * highPressure;
    counterRecoilDuration_ = 0.62f - 0.22f * highPressure;

    ValidateAllTimings();
    if (!deathFinished_) {
        runtime_.hp = config_.core.maxHp;
    }
}

float Enemy::GetCurrentSmashChargeTime() const {
    if (farSlashActive_ && tripleIaiSlashActive_) {
        return (std::max)(0.10f, farSlashSmashChargeTime_ * 0.34f);
    }
    if (farSlashActive_) {
        return farSlashSmashChargeTime_;
    }
    if (quickSlashActive_) {
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
    if (farSlashActive_ && tripleIaiSlashActive_) {
        return (std::max)(0.10f, farSlashSweepChargeTime_ * 0.34f);
    }
    if (farSlashActive_) {
        return farSlashSweepChargeTime_;
    }
    if (quickSlashActive_) {
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
    constexpr float kNormalSlashCueWindowBonus = 0.04f;
    float cueWindow = 0.52f + kNormalSlashCueWindowBonus;
    const float highPressure = DifficultyRatio(difficulty_);

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
        cueWindow = 0.44f;
    } else {
        return 0.0f;
    }
    const float farSlashCounterWindowScale =
        farSlashActive_ ? 0.58f : 1.0f;
    cueWindow *= (1.0f - 0.42f * highPressure) * farSlashCounterWindowScale;

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
    ValidateTiming(config_.attacks.arcaneLaser.profile.timing,
                   config_.attacks.arcaneLaser.profile.chargeTime);
    ValidateTiming(config_.attacks.cataclysmLaser.profile.timing,
                   config_.attacks.cataclysmLaser.profile.chargeTime);
}

