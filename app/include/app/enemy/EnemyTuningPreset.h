#pragma once
#include <DirectXMath.h>

struct AttackParamPreset {
    float damage = 0.0f;
    float knockback = 0.0f;
    DirectX::XMFLOAT3 hitBoxSize = {1.0f, 1.0f, 1.0f};
};

struct AttackTimingParamPreset {
    float totalTime = 0.0f;
    float trackingEndTime = 0.0f;
    float activeStartTime = 0.0f;
    float activeEndTime = 0.0f;
    float recoveryStartTime = 0.0f;
};

struct EnemyTuningPreset {
    float enemyMaxHp = 1000.0f;
    float phase2HealthRatioThreshold = 0.60f;

    float nearAttackDistance = 4.0f;
    float farAttackDistance = 4.0f;

    AttackParamPreset smash;
    float smashChargeTime = 0.6f;
   /* float smashAttackTime = 0.25f;
    float smashRecoveryTime = 1.0f;*/
    float smashAttackForwardOffset = 1.4f;
    float smashAttackHeightOffset = 0.8f;

    AttackParamPreset sweep;
    float sweepChargeTime = 0.5f;
    /*float sweepAttackTime = 0.3f;
    float sweepRecoveryTime = 1.0f;*/
    float sweepAttackSideOffset = 0.2f;
    float sweepAttackHeightOffset = 0.8f;

    AttackParamPreset bullet;
    float shotChargeTime = 0.6f;
    float shotRecoveryTime = 0.8f;
    float shotInterval = 0.2f;
    int shotMinCount = 3;
    int shotMaxCount = 5;
    float bulletSpeed = 6.0f;
    float bulletLifeTime = 2.0f;
    float bulletSpawnHeightOffset = 0.2f;

    AttackParamPreset wave;
    float waveChargeTime = 0.6f;
    float waveRecoveryTime = 0.8f;
    float waveSpeed = 4.0f;
    float waveMaxDistance = 8.0f;
    float waveSpawnForwardOffset = 1.5f;
    float waveSpawnHeightOffset = 0.0f;

    AttackTimingParamPreset smashTiming;
    AttackTimingParamPreset sweepTiming;

    float warpStartTime = 0.2f;
    float warpMoveTime = 0.10f;
    float warpEndTime = 0.2f;

    int warpApproachChainMaxSteps = 2;
    int warpEscapeChainMaxSteps = 2;
    float approachChainContinueDistance = 5.0f;
    float escapeChainContinueDistance = 4.5f;

    float sweepWarpSmashMaxDistance = 5.0f;
    float sweepWarpSmashChance = 0.45f;
    float waveWarpSmashMinDistance = 4.5f;
    float waveWarpSmashChance = 0.50f;
};
