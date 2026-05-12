#include "Enemy.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {
float ChargeTurnScaleAfterStance(float stateTimer, float stanceTime) {
    return stateTimer < stanceTime ? 1.0f : 0.035f;
}
} // namespace

void Enemy::UpdateShotByStep(float deltaTime) {
    switch (action_.step) {
    case ActionStep::Charge:
        UpdateShotCharge(deltaTime);
        break;
    case ActionStep::Active:
        UpdateShotFire(deltaTime);
        break;
    case ActionStep::Recovery:
        UpdateShotRecovery(deltaTime);
        break;
    default:
        EndAttack();
        break;
    }
}

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

void Enemy::UpdateNovaByStep(float deltaTime) {
    switch (action_.step) {
    case ActionStep::Charge:
        UpdateNovaCharge(deltaTime);
        break;
    case ActionStep::Active:
        UpdateNovaActive(deltaTime);
        break;
    case ActionStep::Recovery:
        UpdateNovaRecovery(deltaTime);
        break;
    default:
        EndAttack();
        break;
    }
}

void Enemy::UpdateShotCharge(float deltaTime) {
    UpdateFacingToPlayerWithSpeed(
        deltaTime,
        chargeTurnSpeed_ * ChargeTurnScaleAfterStance(stateTimer_, 0.48f));

    if (stateTimer_ >= config_.attacks.shot.chargeTime) {
        LockCurrentFacing();
        ChangeActionStep(ActionStep::Active);
        shotsRemaining_ = 1;
        shotIntervalTimer_ = config_.attacks.shot.interval;
    }
}

void Enemy::UpdateShotFire(float deltaTime) {
    shotIntervalTimer_ += deltaTime;
    if (shotsRemaining_ > 0 &&
        shotIntervalTimer_ >= config_.attacks.shot.interval) {
        SpawnBullet();
        shotsRemaining_--;
        shotIntervalTimer_ = 0.0f;
    }

    if (shotsRemaining_ <= 0) {
        ChangeActionStep(ActionStep::Recovery);
    }
}

void Enemy::UpdateShotRecovery(float deltaTime) {
    UpdateFacingToPlayerWithSpeed(deltaTime, recoveryTurnSpeed_ * 0.25f);
    if (stateTimer_ >= config_.attacks.shot.recoveryTime + 0.18f) {
        EndAttack();
    }
}

void Enemy::SpawnBullet() {
    EnemyBullet bullet{};

    DirectX::XMFLOAT3 target = playerPos_;
    const DirectX::XMFLOAT3 &shotHandPos = leftHandTf_.position;
    const float toPlayerX = playerPos_.x - shotHandPos.x;
    const float toPlayerY = playerPos_.y - shotHandPos.y;
    const float toPlayerZ = playerPos_.z - shotHandPos.z;
    const float distance =
        std::sqrt(toPlayerX * toPlayerX + toPlayerY * toPlayerY +
                  toPlayerZ * toPlayerZ);
    const float safeBulletSpeed =
        (std::max)(config_.attacks.shot.bulletSpeed, 0.001f);
    float leadTime = (distance / safeBulletSpeed) * shotLeadTimeScale_;
    if (phase_ == BossPhase::Phase2) {
        leadTime += phase2ShotLeadBonus_;
    }
    leadTime = (std::clamp)(leadTime, 0.0f, 0.85f);

    target.x += playerObs_.velocity.x * leadTime;
    target.y += playerObs_.velocity.y * leadTime;
    target.z += playerObs_.velocity.z * leadTime;

    if (phase_ == BossPhase::Phase2) {
        const int lane = (shotsRemaining_ % 3) - 1;
        const float rightX = std::cos(facingYaw_);
        const float rightZ = -std::sin(facingYaw_);
        target.x += rightX * phase2ShotFanOffset_ * static_cast<float>(lane);
        target.z += rightZ * phase2ShotFanOffset_ * static_cast<float>(lane);
    }

    float dirX = target.x - shotHandPos.x;
    float dirY = target.y - shotHandPos.y;
    float dirZ = target.z - shotHandPos.z;
    float length = std::sqrt(dirX * dirX + dirY * dirY + dirZ * dirZ);
    if (length <= 0.0001f) {
        length = 1.0f;
    }

    dirX /= length;
    dirY /= length;
    dirZ /= length;

    bullet.position = shotHandPos;
    bullet.position.y += config_.attacks.shot.spawnHeightOffset;
    bullet.velocity = {dirX * config_.attacks.shot.bulletSpeed,
                       dirY * config_.attacks.shot.bulletSpeed,
                       dirZ * config_.attacks.shot.bulletSpeed};
    bullet.lifeTime = config_.attacks.shot.bulletLifeTime;
    bullet.isAlive = true;

    bullets_.push_back(bullet);
}

void Enemy::UpdateBullets(float deltaTime) {
    for (auto &bullet : bullets_) {
        if (!bullet.isAlive) {
            continue;
        }

        bullet.position.x += bullet.velocity.x * deltaTime;
        bullet.position.y += bullet.velocity.y * deltaTime;
        bullet.position.z += bullet.velocity.z * deltaTime;
        bullet.lifeTime -= deltaTime;
        if (bullet.lifeTime <= 0.0f) {
            bullet.isAlive = false;
        }
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

void Enemy::UpdateNovaCharge(float deltaTime) {
    UpdateFacingToPlayerWithSpeed(
        deltaTime,
        chargeTurnSpeed_ * ChargeTurnScaleAfterStance(stateTimer_, 0.64f));
    if (stateTimer_ >= config_.attacks.nova.chargeTime) {
        LockCurrentFacing();
        runtime_.novaSkyBulletsSpawned = false;
        runtime_.novaRingsSpawned = 0;
        runtime_.novaRingTimer = 0.0f;
        ChangeActionStep(ActionStep::Active);
    }
}

void Enemy::UpdateNovaActive(float deltaTime) {
    UpdateFacingToPlayerWithSpeed(deltaTime, recoveryTurnSpeed_ * 0.35f);

    if (!runtime_.novaSkyBulletsSpawned) {
        SpawnNovaSkyBullets();
        runtime_.novaSkyBulletsSpawned = true;
    }

    runtime_.novaRingTimer -= deltaTime;
    while (runtime_.novaRingsSpawned < config_.attacks.nova.ringCount &&
           runtime_.novaRingTimer <= 0.0f) {
        SpawnNovaRing(runtime_.novaRingsSpawned);
        ++runtime_.novaRingsSpawned;
        runtime_.novaRingTimer += config_.attacks.nova.ringInterval;
    }

    if (stateTimer_ >= config_.attacks.nova.activeTime &&
        runtime_.novaRingsSpawned >= config_.attacks.nova.ringCount) {
        ChangeActionStep(ActionStep::Recovery);
    }
}

void Enemy::UpdateNovaRecovery(float deltaTime) {
    UpdateFacingToPlayerWithSpeed(deltaTime, recoveryTurnSpeed_ * 0.20f);
    if (stateTimer_ >= config_.attacks.nova.recoveryTime + 0.25f) {
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
        wave.speed = config_.attacks.wave.speed;
        if (phase_ == BossPhase::Phase2) {
            wave.speed *= 1.12f;
        }
        wave.traveledDistance = 0.0f;
        wave.maxDistance = config_.attacks.wave.maxDistance;
        wave.isAlive = true;

        waves_.push_back(wave);
    };

    if (phase_ == BossPhase::Phase2) {
        spawnWaveWithYaw(usedYaw - phase2WaveFanAngleRad_);
        spawnWaveWithYaw(usedYaw);
        spawnWaveWithYaw(usedYaw + phase2WaveFanAngleRad_);
        return;
    }

    spawnWaveWithYaw(usedYaw);
}

void Enemy::SpawnNovaRing(int ringIndex) {
    const int waveCount = (std::max)(1, config_.attacks.nova.wavesPerRing);
    const float angleOffset =
        (ringIndex % 2 == 0) ? 0.0f : (3.14159265f / static_cast<float>(waveCount));
    const float spawnRadius = config_.attacks.nova.firstRingRadius +
                              config_.attacks.nova.ringRadiusStep *
                                  static_cast<float>(ringIndex);

    for (int i = 0; i < waveCount; ++i) {
        const float angle = angleOffset +
                            (6.28318530f * static_cast<float>(i)) /
                                static_cast<float>(waveCount);
        const float dirX = std::sin(angle);
        const float dirZ = std::cos(angle);

        EnemyWave wave{};
        wave.position = bodyTf_.position;
        wave.position.x += dirX * spawnRadius;
        wave.position.y = tf_.position.y + config_.attacks.wave.spawnHeightOffset;
        wave.position.z += dirZ * spawnRadius;
        wave.direction = {dirX, 0.0f, dirZ};
        wave.speed = config_.attacks.nova.waveSpeed *
                     (1.0f + 0.08f * static_cast<float>(ringIndex));
        wave.traveledDistance = 0.0f;
        wave.maxDistance = config_.attacks.nova.waveMaxDistance;
        wave.isAlive = true;
        waves_.push_back(wave);
    }
}

void Enemy::SpawnNovaSkyBullets() {
    const int bulletCount = (std::max)(1, config_.attacks.nova.skyBulletCount);
    for (int i = 0; i < bulletCount; ++i) {
        const float angle = (6.28318530f * static_cast<float>(i)) /
                            static_cast<float>(bulletCount);
        const float dirX = std::sin(angle);
        const float dirZ = std::cos(angle);

        EnemyBullet bullet{};
        bullet.position = bodyTf_.position;
        bullet.position.x += dirX * 0.42f;
        bullet.position.y =
            tf_.position.y + config_.attacks.nova.skyBulletHeightOffset;
        bullet.position.z += dirZ * 0.42f;

        const float up = (i % 2 == 0) ? 0.34f : 0.18f;
        bullet.velocity = {dirX * config_.attacks.nova.bulletSpeed,
                           up * config_.attacks.nova.bulletSpeed,
                           dirZ * config_.attacks.nova.bulletSpeed};
        bullet.lifeTime = config_.attacks.nova.bulletLifeTime;
        bullet.isAlive = true;
        bullets_.push_back(bullet);
    }
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

void Enemy::ConsumeBullet(size_t index) {
    if (index >= bullets_.size()) {
        return;
    }

    bullets_[index].isAlive = false;
    bullets_[index].lifeTime = 0.0f;
}

void Enemy::DestroyBullet(size_t index) { ConsumeBullet(index); }

void Enemy::ReflectBullet(size_t index, const DirectX::XMFLOAT3 &targetPos) {
    if (index >= bullets_.size()) {
        return;
    }

    auto &bullet = bullets_[index];
    if (!bullet.isAlive) {
        return;
    }

    float dirX = targetPos.x - bullet.position.x;
    float dirY = targetPos.y - bullet.position.y;
    float dirZ = targetPos.z - bullet.position.z;
    float length = std::sqrt(dirX * dirX + dirY * dirY + dirZ * dirZ);
    if (length <= 0.0001f) {
        length = 1.0f;
    }

    dirX /= length;
    dirY /= length;
    dirZ /= length;
    bullet.velocity = {dirX * config_.attacks.shot.bulletSpeed,
                       dirY * config_.attacks.shot.bulletSpeed,
                       dirZ * config_.attacks.shot.bulletSpeed};
    bullet.isReflected = true;
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
    wave.speed = config_.attacks.wave.speed;
    wave.traveledDistance = 0.0f;
    wave.isReflected = true;
}
