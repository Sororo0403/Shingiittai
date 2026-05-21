#include "Enemy.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {
float ChargeTurnScaleAfterStance(float stateTimer, float stanceTime) {
    return stateTimer < stanceTime ? 1.0f : 0.035f;
}
} // namespace

void Enemy::UpdateBladeClashByStep(float deltaTime) {
    switch (action_.step) {
    case ActionStep::Charge:
        UpdateBladeClashCharge(deltaTime);
        break;
    case ActionStep::Active:
        UpdateBladeClashActive(deltaTime);
        break;
    case ActionStep::Recovery:
        UpdateBladeClashRecovery(deltaTime);
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

void Enemy::UpdateCageByStep(float deltaTime) {
    switch (action_.step) {
    case ActionStep::Charge:
        UpdateCageCharge(deltaTime);
        break;
    case ActionStep::Active:
        UpdateCageActive(deltaTime);
        break;
    case ActionStep::Recovery:
        UpdateCageRecovery(deltaTime);
        break;
    default:
        EndAttack();
        break;
    }
}

void Enemy::UpdateBladeClashCharge(float deltaTime) {
    UpdateFacingToPlayerWithSpeed(
        deltaTime,
        chargeTurnSpeed_ * ChargeTurnScaleAfterStance(stateTimer_, 0.36f));

    const float dx = playerPos_.x - tf_.position.x;
    const float dz = playerPos_.z - tf_.position.z;
    const float distanceSq = dx * dx + dz * dz;
    if (distanceSq > 2.65f * 2.65f) {
        const float distance = std::sqrt(distanceSq);
        const float moveSpeed = phase_ == BossPhase::Phase3
                                    ? 3.68f
                                    : phase_ == BossPhase::Phase2 ? 3.55f
                                                                  : 3.18f;
        tf_.position.x += (dx / distance) * moveSpeed * deltaTime;
        tf_.position.z += (dz / distance) * moveSpeed * deltaTime;
        ClampToArena();
    }

    if (phase2BladeClashStandby_) {
        return;
    }

    if (stateTimer_ >= config_.attacks.bladeClash.chargeTime) {
        LockCurrentFacing();
        dualCounterStage_ = 0;
        dualCounterStageResolved_ = false;
        ChangeActionStep(ActionStep::Active);
    }
}

void Enemy::UpdateBladeClashActive(float deltaTime) {
    (void)deltaTime;
    isAttackActive_ = IsBladeClashWindow();
    if (stateTimer_ >= config_.attacks.bladeClash.activeTime) {
        ChangeActionStep(ActionStep::Recovery);
    }
}

void Enemy::UpdateBladeClashRecovery(float deltaTime) {
    UpdateFacingToPlayerWithSpeed(deltaTime, recoveryTurnSpeed_ * 0.25f);
    if (stateTimer_ >= config_.attacks.bladeClash.recoveryTime + 0.18f) {
        EndAttack();
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
        if (TryBranchFromRecovery(ActionKind::Wave)) {
            return;
        }
        EndAttack();
    }
}

void Enemy::UpdateCageCharge(float deltaTime) {
    UpdateFacingToPlayerWithSpeed(
        deltaTime,
        chargeTurnSpeed_ * ChargeTurnScaleAfterStance(stateTimer_, 0.46f));

    if (stateTimer_ >= config_.attacks.cage.chargeTime) {
        LockCurrentFacing();
        ChangeActionStep(ActionStep::Active);
    }
}

void Enemy::UpdateCageActive(float deltaTime) {
    (void)deltaTime;
    if (!cageTrapSpawned_) {
        SpawnCageTrap();
        cageTrapSpawned_ = true;
    }

    if (stateTimer_ >= config_.attacks.cage.activeTime) {
        ChangeActionStep(ActionStep::Recovery);
    }
}

void Enemy::UpdateCageRecovery(float deltaTime) {
    UpdateFacingToPlayerWithSpeed(deltaTime, recoveryTurnSpeed_ * 0.20f);
    if (stateTimer_ >= config_.attacks.cage.recoveryTime + 0.14f) {
        if (TryBranchFromRecovery(ActionKind::Cage)) {
            return;
        }
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

void Enemy::SpawnCageTrap() {
    cage_ = EnemyCage{};
    cage_.center = playerPos_;
    cage_.center.y = tf_.position.y;
    cage_.radius = config_.attacks.cage.radius;
    cage_.targetRadius = config_.attacks.cage.radius;
    cage_.height = config_.attacks.cage.height;
    cage_.lifeTime = config_.attacks.cage.duration;
    cage_.maxLifeTime = config_.attacks.cage.duration;
    cage_.maxBreakValue = config_.attacks.cage.breakValue;
    cage_.breakValue = cage_.maxBreakValue;
    cage_.pulseTimer = config_.attacks.cage.pulseInterval * 0.72f;
    cage_.seamWindow = 0.52f;
    cage_.barCount = (std::max)(6, config_.attacks.cage.barCount);
    cage_.isActive = true;

    const float toEnemyX = tf_.position.x - cage_.center.x;
    const float toEnemyZ = tf_.position.z - cage_.center.z;
    if (toEnemyX * toEnemyX + toEnemyZ * toEnemyZ > 0.0001f) {
        cage_.seamAngle = std::atan2(toEnemyX, toEnemyZ);
    } else {
        cage_.seamAngle = facingYaw_;
    }

    if (phase_ != BossPhase::Phase1) {
        cage_.radius *= 0.94f;
        cage_.targetRadius *= 0.94f;
        cage_.breakValue += 0.6f;
        cage_.maxBreakValue = cage_.breakValue;
        cage_.lifeTime += 0.45f;
        cage_.maxLifeTime = cage_.lifeTime;
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

void Enemy::UpdateCageTrap(float deltaTime) {
    cage_.pulseJustFired = false;
    if (!cage_.isActive) {
        return;
    }

    cage_.lifeTime -= deltaTime;
    if (cage_.lifeTime <= 0.0f) {
        cage_.lifeTime = 0.0f;
        cage_.isActive = false;
        return;
    }

    if (cage_.hitCooldown > 0.0f) {
        cage_.hitCooldown -= deltaTime;
        if (cage_.hitCooldown < 0.0f) {
            cage_.hitCooldown = 0.0f;
        }
    }

    cage_.targetRadius =
        (std::max)(config_.attacks.cage.minRadius,
                   cage_.targetRadius -
                       config_.attacks.cage.shrinkSpeed * deltaTime);
    cage_.radius += (cage_.targetRadius - cage_.radius) *
                    (std::min)(1.0f, deltaTime * 5.6f);

    cage_.pulseTimer -= deltaTime;
    if (cage_.pulseTimer <= 0.0f) {
        cage_.pulseJustFired = true;
        cage_.pulseTimer += (std::max)(0.12f, config_.attacks.cage.pulseInterval);
    }
}

void Enemy::ConsumeWave(size_t index) {
    if (index >= waves_.size()) {
        return;
    }

    waves_[index].isAlive = false;
    waves_[index].traveledDistance = waves_[index].maxDistance;
}

bool Enemy::DamageCage(float amount, bool hitSeam) {
    if (!cage_.isActive || amount <= 0.0f || cage_.hitCooldown > 0.0f) {
        return false;
    }

    float damage = amount;
    if (hitSeam) {
        damage *= config_.attacks.cage.seamBonusDamage;
    }
    cage_.breakValue -= damage;
    cage_.hitCooldown = hitSeam ? 0.05f : 0.11f;

    if (cage_.breakValue <= 0.0f) {
        cage_.breakValue = 0.0f;
        cage_.isActive = false;
        cage_.justBroken = true;
        return true;
    }
    return false;
}

bool Enemy::ConsumeCageBreakFlash() {
    const bool result = cage_.justBroken;
    cage_.justBroken = false;
    return result;
}

bool Enemy::ConsumeCagePulse() {
    const bool result = cage_.pulseJustFired;
    cage_.pulseJustFired = false;
    return result;
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
