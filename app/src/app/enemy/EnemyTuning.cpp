#include "Enemy.h"

float Enemy::GetCurrentActionTime() const { return stateTimer_; }

float Enemy::GetCurrentSmashChargeTime() const {
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
    float result = config_.attacks.sweep.melee.base.chargeTime;
    if (action_.id == ActionId::DoubleSweep && isDoubleSweepSecondStage_) {
        result *= config_.attacks.sweep.secondChargeScale;
    }

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

ActionId Enemy::MakeDefaultActionId(ActionKind kind) const {
    switch (kind) {
    case ActionKind::Smash:
        return ActionId::Smash;
    case ActionKind::Sweep:
        return ActionId::Sweep;
    case ActionKind::Shot:
        return ActionId::Shot;
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

EnemyTuningPreset Enemy::CreateTuningPreset() const {
    EnemyTuningPreset p{};

    p.enemyMaxHp = config_.core.maxHp;
    p.phase2HealthRatioThreshold = config_.core.phase2HealthRatioThreshold;
    p.nearAttackDistance = config_.core.nearAttackDistance;
    p.farAttackDistance = config_.core.farAttackDistance;

    p.smash.damage = config_.attacks.smash.melee.base.attack.damage;
    p.smash.knockback = config_.attacks.smash.melee.base.attack.knockback;
    p.smash.hitBoxSize = config_.attacks.smash.melee.base.attack.hitBoxSize;
    p.smashChargeTime = config_.attacks.smash.melee.base.chargeTime;
    p.smashAttackForwardOffset = config_.attacks.smash.attackForwardOffset;
    p.smashAttackHeightOffset = config_.attacks.smash.attackHeightOffset;
    p.smashTiming.totalTime = config_.attacks.smash.melee.base.timing.totalTime;
    p.smashTiming.trackingEndTime =
        config_.attacks.smash.melee.base.timing.trackingEndTime;
    p.smashTiming.activeStartTime =
        config_.attacks.smash.melee.base.timing.activeStartTime;
    p.smashTiming.activeEndTime =
        config_.attacks.smash.melee.base.timing.activeEndTime;
    p.smashTiming.recoveryStartTime =
        config_.attacks.smash.melee.base.timing.recoveryStartTime;

    p.sweep.damage = config_.attacks.sweep.melee.base.attack.damage;
    p.sweep.knockback = config_.attacks.sweep.melee.base.attack.knockback;
    p.sweep.hitBoxSize = config_.attacks.sweep.melee.base.attack.hitBoxSize;
    p.sweepChargeTime = config_.attacks.sweep.melee.base.chargeTime;
    p.sweepAttackSideOffset = config_.attacks.sweep.attackSideOffset;
    p.sweepAttackHeightOffset = config_.attacks.sweep.attackHeightOffset;
    p.sweepTiming.totalTime = config_.attacks.sweep.melee.base.timing.totalTime;
    p.sweepTiming.trackingEndTime =
        config_.attacks.sweep.melee.base.timing.trackingEndTime;
    p.sweepTiming.activeStartTime =
        config_.attacks.sweep.melee.base.timing.activeStartTime;
    p.sweepTiming.activeEndTime =
        config_.attacks.sweep.melee.base.timing.activeEndTime;
    p.sweepTiming.recoveryStartTime =
        config_.attacks.sweep.melee.base.timing.recoveryStartTime;

    p.bullet.damage = config_.attacks.shot.attack.damage;
    p.bullet.knockback = config_.attacks.shot.attack.knockback;
    p.bullet.hitBoxSize = config_.attacks.shot.attack.hitBoxSize;
    p.shotChargeTime = config_.attacks.shot.chargeTime;
    p.shotRecoveryTime = config_.attacks.shot.recoveryTime;
    p.shotInterval = config_.attacks.shot.interval;
    p.shotMinCount = config_.attacks.shot.minCount;
    p.shotMaxCount = config_.attacks.shot.maxCount;
    p.bulletSpeed = config_.attacks.shot.bulletSpeed;
    p.bulletLifeTime = config_.attacks.shot.bulletLifeTime;
    p.bulletSpawnHeightOffset = config_.attacks.shot.spawnHeightOffset;

    p.wave.damage = config_.attacks.wave.attack.damage;
    p.wave.knockback = config_.attacks.wave.attack.knockback;
    p.wave.hitBoxSize = config_.attacks.wave.attack.hitBoxSize;
    p.waveChargeTime = config_.attacks.wave.chargeTime;
    p.waveRecoveryTime = config_.attacks.wave.recoveryTime;
    p.waveSpeed = config_.attacks.wave.speed;
    p.waveMaxDistance = config_.attacks.wave.maxDistance;
    p.waveSpawnForwardOffset = config_.attacks.wave.spawnForwardOffset;
    p.waveSpawnHeightOffset = config_.attacks.wave.spawnHeightOffset;

    p.warpStartTime = config_.warp.startTime;
    p.warpMoveTime = config_.warp.moveTime;
    p.warpEndTime = config_.warp.endTime;
    p.warpApproachChainMaxSteps = config_.chain.warpApproachMaxSteps;
    p.warpEscapeChainMaxSteps = config_.chain.warpEscapeMaxSteps;
    p.approachChainContinueDistance = config_.chain.approachContinueDistance;
    p.escapeChainContinueDistance = config_.chain.escapeContinueDistance;
    p.sweepWarpSmashMaxDistance = config_.chain.sweepWarpSmashMaxDistance;
    p.sweepWarpSmashChance = config_.chain.sweepWarpSmashChance;
    p.waveWarpSmashMinDistance = config_.chain.waveWarpSmashMinDistance;
    p.waveWarpSmashChance = config_.chain.waveWarpSmashChance;

    return p;
}

void Enemy::ApplyTuningPreset(const EnemyTuningPreset &p) {
    config_.core.maxHp = (p.enemyMaxHp < 1.0f) ? 1.0f : p.enemyMaxHp;
    hp_ = config_.core.maxHp;

    config_.core.phase2HealthRatioThreshold = p.phase2HealthRatioThreshold;
    if (config_.core.phase2HealthRatioThreshold < 0.05f) {
        config_.core.phase2HealthRatioThreshold = 0.05f;
    }
    if (config_.core.phase2HealthRatioThreshold > 0.95f) {
        config_.core.phase2HealthRatioThreshold = 0.95f;
    }

    config_.core.nearAttackDistance = p.nearAttackDistance;
    config_.core.farAttackDistance = p.farAttackDistance;

    config_.attacks.smash.melee.base.attack.damage = p.smash.damage;
    config_.attacks.smash.melee.base.attack.knockback = p.smash.knockback;
    config_.attacks.smash.melee.base.attack.hitBoxSize = p.smash.hitBoxSize;
    config_.attacks.smash.melee.base.chargeTime = p.smashChargeTime;
    config_.attacks.smash.attackForwardOffset = p.smashAttackForwardOffset;
    config_.attacks.smash.attackHeightOffset = p.smashAttackHeightOffset;
    config_.attacks.smash.melee.base.timing.totalTime = p.smashTiming.totalTime;
    config_.attacks.smash.melee.base.timing.activeStartTime =
        p.smashTiming.activeStartTime;
    config_.attacks.smash.melee.base.timing.activeEndTime =
        p.smashTiming.activeEndTime;
    config_.attacks.smash.melee.base.timing.recoveryStartTime =
        p.smashTiming.recoveryStartTime;
    config_.attacks.smash.melee.base.timing.trackingEndTime =
        p.smashTiming.trackingEndTime;

    config_.attacks.sweep.melee.base.attack.damage = p.sweep.damage;
    config_.attacks.sweep.melee.base.attack.knockback = p.sweep.knockback;
    config_.attacks.sweep.melee.base.attack.hitBoxSize = p.sweep.hitBoxSize;
    config_.attacks.sweep.melee.base.chargeTime = p.sweepChargeTime;
    config_.attacks.sweep.attackSideOffset = p.sweepAttackSideOffset;
    config_.attacks.sweep.attackHeightOffset = p.sweepAttackHeightOffset;
    config_.attacks.sweep.melee.base.timing.totalTime = p.sweepTiming.totalTime;
    config_.attacks.sweep.melee.base.timing.activeStartTime =
        p.sweepTiming.activeStartTime;
    config_.attacks.sweep.melee.base.timing.activeEndTime =
        p.sweepTiming.activeEndTime;
    config_.attacks.sweep.melee.base.timing.recoveryStartTime =
        p.sweepTiming.recoveryStartTime;
    config_.attacks.sweep.melee.base.timing.trackingEndTime =
        p.sweepTiming.trackingEndTime;

    config_.attacks.shot.attack.damage = p.bullet.damage;
    config_.attacks.shot.attack.knockback = p.bullet.knockback;
    config_.attacks.shot.attack.hitBoxSize = p.bullet.hitBoxSize;
    config_.attacks.shot.chargeTime = p.shotChargeTime;
    config_.attacks.shot.recoveryTime = p.shotRecoveryTime;
    config_.attacks.shot.interval = p.shotInterval;
    config_.attacks.shot.minCount = p.shotMinCount;
    config_.attacks.shot.maxCount = p.shotMaxCount;
    config_.attacks.shot.bulletSpeed = p.bulletSpeed;
    config_.attacks.shot.bulletLifeTime = p.bulletLifeTime;
    config_.attacks.shot.spawnHeightOffset = p.bulletSpawnHeightOffset;

    config_.attacks.wave.attack.damage = p.wave.damage;
    config_.attacks.wave.attack.knockback = p.wave.knockback;
    config_.attacks.wave.attack.hitBoxSize = p.wave.hitBoxSize;
    config_.attacks.wave.chargeTime = p.waveChargeTime;
    config_.attacks.wave.recoveryTime = p.waveRecoveryTime;
    config_.attacks.wave.speed = p.waveSpeed;
    config_.attacks.wave.maxDistance = p.waveMaxDistance;
    config_.attacks.wave.spawnForwardOffset = p.waveSpawnForwardOffset;
    config_.attacks.wave.spawnHeightOffset = p.waveSpawnHeightOffset;

    config_.warp.startTime = (p.warpStartTime < 0.0f) ? 0.0f : p.warpStartTime;
    config_.warp.moveTime = (p.warpMoveTime < 0.0f) ? 0.0f : p.warpMoveTime;
    config_.warp.endTime = (p.warpEndTime < 0.0f) ? 0.0f : p.warpEndTime;

    config_.chain.warpApproachMaxSteps = p.warpApproachChainMaxSteps;
    config_.chain.warpEscapeMaxSteps = p.warpEscapeChainMaxSteps;
    config_.chain.approachContinueDistance = p.approachChainContinueDistance;
    config_.chain.escapeContinueDistance = p.escapeChainContinueDistance;
    config_.chain.sweepWarpSmashMaxDistance = p.sweepWarpSmashMaxDistance;
    config_.chain.sweepWarpSmashChance = p.sweepWarpSmashChance;
    config_.chain.waveWarpSmashMinDistance = p.waveWarpSmashMinDistance;
    config_.chain.waveWarpSmashChance = p.waveWarpSmashChance;

    if (config_.core.nearAttackDistance > config_.core.farAttackDistance) {
        config_.core.farAttackDistance = config_.core.nearAttackDistance;
    }

    ValidateAllTimings();
}

void Enemy::ResetTuningPreset() { ApplyTuningPreset(EnemyTuningPreset{}); }
