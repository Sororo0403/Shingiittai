#include "Enemy.h"

#include <algorithm>
#include <cmath>

namespace {
float ChargeTurnScaleAfterStance(float stateTimer, float stanceTime) {
    return stateTimer < stanceTime ? 1.0f : 0.035f;
}
} // namespace

void Enemy::UpdateWaveByStep(float deltaTime) {
    switch (action_.step) {
    case ActionStep::Charge:
        UpdateWaveCharge(deltaTime);
        break;
    case ActionStep::Active:
        UpdateWaveFire(deltaTime);
        break;
    case ActionStep::Recovery:
        UpdateWaveRecovery(deltaTime);
        break;
    default:
        EndAttack();
        break;
    }
}

void Enemy::UpdateWaveCharge(float deltaTime) {
    UpdateFacingToPlayerWithSpeed(
        deltaTime,
        chargeTurnSpeed_ * ChargeTurnScaleAfterStance(stateTimer_, 0.48f));

    if (stateTimer_ >= config_.attacks.wave.chargeTime) {
        LockCurrentFacing();
        ChangeActionStep(ActionStep::Active);
    }
}

void Enemy::UpdateWaveFire(float deltaTime) {
    (void)deltaTime;
    SpawnWave();
    ChangeActionStep(ActionStep::Recovery);
}

void Enemy::UpdateWaveRecovery(float deltaTime) {
    (void)deltaTime;
    if (stateTimer_ >= config_.attacks.wave.recoveryTime + 0.18f) {
        EndAttack();
    }
}

void Enemy::SpawnWave() {
    const float usedYaw = lockedAttackYaw_;
    auto spawnWaveWithYaw = [&](float yaw) {
        EnemyWave wave{};
        const float forwardX = std::sin(yaw);
        const float forwardZ = std::cos(yaw);

        wave.position = bodyTf_.position;
        wave.position.y =
            tf_.position.y + config_.attacks.wave.spawnHeightOffset;
        wave.position.x += forwardX * config_.attacks.wave.spawnForwardOffset;
        wave.position.z += forwardZ * config_.attacks.wave.spawnForwardOffset;
        wave.direction = {forwardX, 0.0f, forwardZ};
        wave.hitBoxSize = config_.attacks.wave.attack.hitBoxSize;
        wave.speed = config_.attacks.wave.speed;
        if (phase_ != BossPhase::Phase1) {
            wave.speed *= 1.12f;
        }
        wave.traveledDistance = 0.0f;
        wave.maxDistance = config_.attacks.wave.maxDistance;
        wave.damage = config_.attacks.wave.attack.damage;
        wave.knockback = config_.attacks.wave.attack.knockback;
        wave.isAlive = true;

        waves_.push_back(wave);
    };

    if (phase_ != BossPhase::Phase1) {
        spawnWaveWithYaw(usedYaw - phase2WaveFanAngleRad_);
        spawnWaveWithYaw(usedYaw);
        spawnWaveWithYaw(usedYaw + phase2WaveFanAngleRad_);
        return;
    }

    spawnWaveWithYaw(usedYaw);
}

void Enemy::UpdateWaves(float deltaTime) {
    for (auto &wave : waves_) {
        if (!wave.isAlive) {
            continue;
        }

        const float moveX = wave.direction.x * wave.speed * deltaTime;
        const float moveZ = wave.direction.z * wave.speed * deltaTime;
        wave.position.x += moveX;
        wave.position.z += moveZ;
        wave.traveledDistance += std::sqrt(moveX * moveX + moveZ * moveZ);
        if (wave.traveledDistance >= wave.maxDistance) {
            wave.isAlive = false;
        }
    }
}

void Enemy::ConsumeWave(size_t index) {
    if (index >= waves_.size()) {
        return;
    }

    waves_[index].isAlive = false;
    waves_[index].traveledDistance = waves_[index].maxDistance;
}

void Enemy::DestroyWave(size_t index) { ConsumeWave(index); }

void Enemy::ReflectWave(size_t index, const DirectX::XMFLOAT3 &targetPos) {
    if (index >= waves_.size()) {
        return;
    }

    auto &wave = waves_[index];
    if (!wave.isAlive) {
        return;
    }

    float dirX = targetPos.x - wave.position.x;
    float dirZ = targetPos.z - wave.position.z;
    float length = std::sqrt(dirX * dirX + dirZ * dirZ);
    if (length <= 0.0001f) {
        length = 1.0f;
    }

    dirX /= length;
    dirZ /= length;
    wave.direction = {dirX, 0.0f, dirZ};
    wave.speed = (std::max)(wave.speed, config_.attacks.wave.speed);
    wave.traveledDistance = 0.0f;
    wave.isReflected = true;
}
