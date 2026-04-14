#include "Enemy.h"
#include "ModelManager.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

// ============================================================
// アクションタイムの管理
// ============================================================
float Enemy::GetCurrentActionTime() const { return stateTimer_; }

float Enemy::GetCurrentSmashChargeTime() const {
    float result = smashChargeTime_;

    if (action_.id == ActionId::DelaySmash) {
        result += delaySmashExtraChargeTime_;
    }

    result += GetAdaptiveChargeOffset(ActionKind::Smash);

    if (result < 0.05f) {
        result = 0.05f;
    }
    return result;
}

float Enemy::GetCurrentSweepChargeTime() const {
    float result = sweepChargeTime_;

    if (action_.id == ActionId::DoubleSweep && isDoubleSweepSecondStage_) {
        result *= doubleSweepSecondChargeScale_;
    }

    result += GetAdaptiveChargeOffset(ActionKind::Sweep);

    if (result < 0.05f) {
        result = 0.05f;
    }
    return result;
}

bool Enemy::HasReachedTrackingEnd() const {
    const AttackTimingParam *timing = GetCurrentAttackTiming();
    if (!timing) {
        return false;
    }
    return GetCurrentActionTime() >= timing->trackingEndTime;
}

bool Enemy::HasReachedHitStart() const {
    const AttackTimingParam *timing = GetCurrentAttackTiming();
    if (!timing) {
        return false;
    }
    return GetCurrentActionTime() >= timing->activeStartTime;
}

bool Enemy::HasReachedHitEnd() const {
    const AttackTimingParam *timing = GetCurrentAttackTiming();
    if (!timing) {
        return false;
    }
    return GetCurrentActionTime() > timing->activeEndTime;
}

bool Enemy::HasReachedRecoveryStart() const {
    const AttackTimingParam *timing = GetCurrentAttackTiming();
    if (!timing) {
        return false;
    }
    return GetCurrentActionTime() >= timing->recoveryStartTime;
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

    case ActionKind::Rush:
        return ActionId::Rush;

    case ActionKind::Warp:
        return (warp_.type == WarpType::Escape) ? ActionId::WarpEscape
                                                : ActionId::WarpApproach;

    case ActionKind::Guard:
        switch (guardTarget_) {
        case GuardTarget::Face:
            return ActionId::GuardFace;
        case GuardTarget::BodyLeft:
            return ActionId::GuardBodyLeft;
        case GuardTarget::BodyRight:
            return ActionId::GuardBodyRight;
        default:
            return ActionId::None;
        }

    case ActionKind::Stalk: // 追加
        return ActionId::None;

    default:
        return ActionId::None;
    }
}

// ============================================================
// Timing検証
// ============================================================
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
    ValidateTiming(smashTiming_, smashChargeTime_);
    ValidateTiming(sweepTiming_, sweepChargeTime_);
    ValidateTiming(rushTiming_, rushChargeTime_);
}

// ============================================================
// プリセット保存用：現在値 → 構造体
// ============================================================
EnemyTuningPreset Enemy::CreateTuningPreset() const {
    EnemyTuningPreset p{};

    p.nearAttackDistance = nearAttackDistance_;
    p.farAttackDistance = farAttackDistance_;

    p.smash.damage = smashParam_.damage;
    p.smash.knockback = smashParam_.knockback;
    p.smash.hitBoxSize = smashParam_.hitBoxSize;
    p.smashChargeTime = smashChargeTime_;
    p.smashAttackForwardOffset = smashAttackForwardOffset_;
    p.smashAttackHeightOffset = smashAttackHeightOffset_;
    p.smashTiming.trackingEndTime = smashTiming_.trackingEndTime;

    p.sweep.damage = sweepParam_.damage;
    p.sweep.knockback = sweepParam_.knockback;
    p.sweep.hitBoxSize = sweepParam_.hitBoxSize;
    p.sweepChargeTime = sweepChargeTime_;
    p.sweepAttackSideOffset = sweepAttackSideOffset_;
    p.sweepAttackHeightOffset = sweepAttackHeightOffset_;
    p.sweepTiming.trackingEndTime = sweepTiming_.trackingEndTime;

    p.bullet.damage = bulletParam_.damage;
    p.bullet.knockback = bulletParam_.knockback;
    p.bullet.hitBoxSize = bulletParam_.hitBoxSize;
    p.shotChargeTime = shotChargeTime_;
    p.shotRecoveryTime = shotRecoveryTime_;
    p.shotInterval = shotInterval_;
    p.shotMinCount = shotMinCount_;
    p.shotMaxCount = shotMaxCount_;
    p.bulletSpeed = bulletSpeed_;
    p.bulletLifeTime = bulletLifeTime_;
    p.bulletSpawnHeightOffset = bulletSpawnHeightOffset_;

    p.wave.damage = waveParam_.damage;
    p.wave.knockback = waveParam_.knockback;
    p.wave.hitBoxSize = waveParam_.hitBoxSize;
    p.waveChargeTime = waveChargeTime_;
    p.waveRecoveryTime = waveRecoveryTime_;
    p.waveSpeed = waveSpeed_;
    p.waveMaxDistance = waveMaxDistance_;
    p.waveSpawnForwardOffset = waveSpawnForwardOffset_;
    p.waveSpawnHeightOffset = waveSpawnHeightOffset_;

    p.smashTiming.totalTime = smashTiming_.totalTime;
    p.smashTiming.trackingEndTime = smashTiming_.trackingEndTime;
    p.smashTiming.activeStartTime = smashTiming_.activeStartTime;
    p.smashTiming.activeEndTime = smashTiming_.activeEndTime;
    p.smashTiming.recoveryStartTime = smashTiming_.recoveryStartTime;

    p.sweepTiming.totalTime = sweepTiming_.totalTime;
    p.sweepTiming.trackingEndTime = sweepTiming_.trackingEndTime;
    p.sweepTiming.activeStartTime = sweepTiming_.activeStartTime;
    p.sweepTiming.activeEndTime = sweepTiming_.activeEndTime;
    p.sweepTiming.recoveryStartTime = sweepTiming_.recoveryStartTime;

    p.warpApproachChainMaxSteps = warpApproachChainMaxSteps_;
    p.warpEscapeChainMaxSteps = warpEscapeChainMaxSteps_;
    p.approachChainContinueDistance = approachChainContinueDistance_;
    p.escapeChainContinueDistance = escapeChainContinueDistance_;

    p.sweepWarpSmashMaxDistance = sweepWarpSmashMaxDistance_;
    p.sweepWarpSmashChance = sweepWarpSmashChance_;
    p.waveWarpSmashMinDistance = waveWarpSmashMinDistance_;
    p.waveWarpSmashChance = waveWarpSmashChance_;

    return p;
}

// ============================================================
// プリセット読込用：構造体 → 現在値
// ============================================================
void Enemy::ApplyTuningPreset(const EnemyTuningPreset &p) {
    nearAttackDistance_ = p.nearAttackDistance;
    farAttackDistance_ = p.farAttackDistance;

    smashParam_.damage = p.smash.damage;
    smashParam_.knockback = p.smash.knockback;
    smashParam_.hitBoxSize = p.smash.hitBoxSize;
    smashChargeTime_ = p.smashChargeTime;
    smashAttackForwardOffset_ = p.smashAttackForwardOffset;
    smashAttackHeightOffset_ = p.smashAttackHeightOffset;
    smashTiming_.totalTime = p.smashTiming.totalTime;
    smashTiming_.activeStartTime = p.smashTiming.activeStartTime;
    smashTiming_.activeEndTime = p.smashTiming.activeEndTime;
    smashTiming_.recoveryStartTime = p.smashTiming.recoveryStartTime;
    smashTiming_.trackingEndTime = p.smashTiming.trackingEndTime;

    sweepParam_.damage = p.sweep.damage;
    sweepParam_.knockback = p.sweep.knockback;
    sweepParam_.hitBoxSize = p.sweep.hitBoxSize;
    sweepChargeTime_ = p.sweepChargeTime;
    sweepAttackSideOffset_ = p.sweepAttackSideOffset;
    sweepAttackHeightOffset_ = p.sweepAttackHeightOffset;
    sweepTiming_.totalTime = p.sweepTiming.totalTime;
    sweepTiming_.activeStartTime = p.sweepTiming.activeStartTime;
    sweepTiming_.activeEndTime = p.sweepTiming.activeEndTime;
    sweepTiming_.recoveryStartTime = p.sweepTiming.recoveryStartTime;
    sweepTiming_.trackingEndTime = p.sweepTiming.trackingEndTime;

    bulletParam_.damage = p.bullet.damage;
    bulletParam_.knockback = p.bullet.knockback;
    bulletParam_.hitBoxSize = p.bullet.hitBoxSize;
    shotChargeTime_ = p.shotChargeTime;
    shotRecoveryTime_ = p.shotRecoveryTime;
    shotInterval_ = p.shotInterval;
    shotMinCount_ = p.shotMinCount;
    shotMaxCount_ = p.shotMaxCount;
    bulletSpeed_ = p.bulletSpeed;
    bulletLifeTime_ = p.bulletLifeTime;
    bulletSpawnHeightOffset_ = p.bulletSpawnHeightOffset;

    waveParam_.damage = p.wave.damage;
    waveParam_.knockback = p.wave.knockback;
    waveParam_.hitBoxSize = p.wave.hitBoxSize;
    waveChargeTime_ = p.waveChargeTime;
    waveRecoveryTime_ = p.waveRecoveryTime;
    waveSpeed_ = p.waveSpeed;
    waveMaxDistance_ = p.waveMaxDistance;
    waveSpawnForwardOffset_ = p.waveSpawnForwardOffset;
    waveSpawnHeightOffset_ = p.waveSpawnHeightOffset;

    warpApproachChainMaxSteps_ = p.warpApproachChainMaxSteps;
    warpEscapeChainMaxSteps_ = p.warpEscapeChainMaxSteps;
    approachChainContinueDistance_ = p.approachChainContinueDistance;
    escapeChainContinueDistance_ = p.escapeChainContinueDistance;

    sweepWarpSmashMaxDistance_ = p.sweepWarpSmashMaxDistance;
    sweepWarpSmashChance_ = p.sweepWarpSmashChance;
    waveWarpSmashMinDistance_ = p.waveWarpSmashMinDistance;
    waveWarpSmashChance_ = p.waveWarpSmashChance;

    if (nearAttackDistance_ > farAttackDistance_) {
        farAttackDistance_ = nearAttackDistance_;
    }

    ValidateAllTimings();
}

// ============================================================
// プリセット初期化
// ============================================================
void Enemy::ResetTuningPreset() { ApplyTuningPreset(EnemyTuningPreset{}); }