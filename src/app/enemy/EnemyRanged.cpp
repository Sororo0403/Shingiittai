#include "Enemy.h"

#include <cmath>
#include <cstdlib>

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

void Enemy::UpdateShotCharge(float deltaTime) {
    UpdateFacingToPlayerWithSpeed(deltaTime, chargeTurnSpeed_);

    if (stateTimer_ >= config_.attacks.shot.chargeTime) {
        ChangeActionStep(ActionStep::Active);
        shotsRemaining_ =
            config_.attacks.shot.minCount +
            (std::rand() % (config_.attacks.shot.maxCount -
                            config_.attacks.shot.minCount + 1));
        shotIntervalTimer_ = 0.0f;
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
    UpdateFacingToPlayerWithSpeed(deltaTime, recoveryTurnSpeed_ * 1.25f);
    if (stateTimer_ >= config_.attacks.shot.recoveryTime) {
        FinishCurrentAction();
    }
}

void Enemy::SpawnBullet() {
    EnemyBullet bullet{};

    float dirX = playerPos_.x - rightHandTf_.position.x;
    float dirY = playerPos_.y - rightHandTf_.position.y;
    float dirZ = playerPos_.z - rightHandTf_.position.z;
    float length = std::sqrt(dirX * dirX + dirY * dirY + dirZ * dirZ);
    if (length <= 0.0001f) {
        length = 1.0f;
    }

    dirX /= length;
    dirY /= length;
    dirZ /= length;

    bullet.position = rightHandTf_.position;
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
    UpdateFacingToPlayerWithSpeed(deltaTime, chargeTurnSpeed_);

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
    if (stateTimer_ >= config_.attacks.wave.recoveryTime) {
        FinishCurrentAction();
    }
}

void Enemy::SpawnWave() {
    EnemyWave wave{};

    const float usedYaw = lockedAttackYaw_;
    const float forwardX = std::sin(usedYaw);
    const float forwardZ = std::cos(usedYaw);

    wave.position = bodyTf_.position;
    wave.position.y = tf_.position.y + config_.attacks.wave.spawnHeightOffset;
    wave.position.x += forwardX * config_.attacks.wave.spawnForwardOffset;
    wave.position.z += forwardZ * config_.attacks.wave.spawnForwardOffset;
    wave.direction = {forwardX, 0.0f, forwardZ};
    wave.speed = config_.attacks.wave.speed;
    wave.traveledDistance = 0.0f;
    wave.maxDistance = config_.attacks.wave.maxDistance;
    wave.isAlive = true;

    waves_.push_back(wave);
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
