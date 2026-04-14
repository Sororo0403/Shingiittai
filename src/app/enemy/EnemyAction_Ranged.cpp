#include "Enemy.h"
#include "ModelManager.h"
#include "imgui.h"

#include <algorithm>
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

// ============================================================
// 弾攻撃更新
// ============================================================
void Enemy::UpdateShotCharge(float deltaTime) {
    UpdateFacingToPlayerWithSpeed(deltaTime, chargeTurnSpeed_);

    if (stateTimer_ >= shotChargeTime_) {
        ChangeActionStep(ActionStep::Active);

        shotsRemaining_ =
            shotMinCount_ + (std::rand() % (shotMaxCount_ - shotMinCount_ + 1));

        shotIntervalTimer_ = 0.0f;
    }
}

void Enemy::UpdateShotFire(float deltaTime) {
    shotIntervalTimer_ += deltaTime;

    if (shotsRemaining_ > 0 && shotIntervalTimer_ >= shotInterval_) {
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

    if (stateTimer_ >= shotRecoveryTime_) {
        FinishCurrentAction();
    }
}

// ============================================================
// 弾生成・弾更新
// ============================================================
void Enemy::SpawnBullet() {
    EnemyBullet bullet{};

    float dirX = playerPos_.x - rightHandTf_.position.x;
    float dirY = playerPos_.y - rightHandTf_.position.y;
    float dirZ = playerPos_.z - rightHandTf_.position.z;

    float len = std::sqrtf(dirX * dirX + dirY * dirY + dirZ * dirZ);
    if (len <= 0.0001f) {
        len = 1.0f;
    }

    dirX /= len;
    dirY /= len;
    dirZ /= len;

    bullet.position = rightHandTf_.position;
    bullet.position.y += bulletSpawnHeightOffset_;

    bullet.velocity = {dirX * bulletSpeed_, dirY * bulletSpeed_,
                       dirZ * bulletSpeed_};
    bullet.lifeTime = bulletLifeTime_;
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

// ============================================================
// 波攻撃更新
// ============================================================
void Enemy::UpdateWaveCharge(float deltaTime) {
    UpdateFacingToPlayerWithSpeed(deltaTime, chargeTurnSpeed_);

    if (stateTimer_ >= waveChargeTime_) {
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

    if (stateTimer_ >= waveRecoveryTime_) {
        FinishCurrentAction();
    }
}

// ============================================================
// 波生成・波更新
// ============================================================
void Enemy::SpawnWave() {
    EnemyWave wave{};

    float usedYaw = lockedAttackYaw_;
    float forwardX = std::sinf(usedYaw);
    float forwardZ = std::cosf(usedYaw);

    wave.position = bodyTf_.position;
    wave.position.y = tf_.position.y + waveSpawnHeightOffset_;
    wave.position.x += forwardX * waveSpawnForwardOffset_;
    wave.position.z += forwardZ * waveSpawnForwardOffset_;

    wave.direction = {forwardX, 0.0f, forwardZ};
    wave.speed = waveSpeed_;
    wave.traveledDistance = 0.0f;
    wave.maxDistance = waveMaxDistance_;
    wave.isAlive = true;

    waves_.push_back(wave);
}

void Enemy::UpdateWaves(float deltaTime) {
    for (auto &wave : waves_) {
        if (!wave.isAlive) {
            continue;
        }

        float moveX = wave.direction.x * wave.speed * deltaTime;
        float moveZ = wave.direction.z * wave.speed * deltaTime;

        wave.position.x += moveX;
        wave.position.z += moveZ;

        float moved = std::sqrtf(moveX * moveX + moveZ * moveZ);
        wave.traveledDistance += moved;

        if (wave.traveledDistance >= wave.maxDistance) {
            wave.isAlive = false;
        }
    }
}
