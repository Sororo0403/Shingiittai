#pragma once
#include <DirectXMath.h>

struct AttackParamPreset {
    float damage = 0.0f;
    float knockback = 0.0f;
    DirectX::XMFLOAT3 hitBoxSize = {1.0f, 1.0f, 1.0f};
};

struct EnemyTuningPreset {
    float nearAttackDistance = 4.0f;
    float farAttackDistance = 4.0f;

    AttackParamPreset smash;
    float smashChargeTime = 0.6f;
    float smashAttackTime = 0.25f;
    float smashRecoveryTime = 1.0f;
    float smashAttackForwardOffset = 1.4f;
    float smashAttackHeightOffset = 0.8f;

    AttackParamPreset sweep;
    float sweepChargeTime = 0.5f;
    float sweepAttackTime = 0.3f;
    float sweepRecoveryTime = 1.0f;
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
};