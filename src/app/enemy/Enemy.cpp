#include "Enemy.h"
#include "ModelManager.h"
#include "imgui.h"

#include <cmath>
#include <cstdlib>
#include <algorithm>

// ============================================================
// 初期化処理
// ============================================================
void Enemy::Initialize(uint32_t modelId) {
    modelId_ = modelId;

    tf_.position = {0.0f, 0.0f, 10.0f};
    tf_.scale = {1.0f, 1.0f, 1.0f};
    tf_.rotation = {0.0f, 0.0f, 0.0f, 1.0f};

    UpdateParts();
    ValidateAllTimings();
}

// ============================================================
// 毎フレーム更新処理
// ============================================================
void Enemy::Update(const DirectX::XMFLOAT3 &playerPos, float deltaTime,
                   bool playerGuarding) {
    if (!IsAlive()) {
        return;
    }

    playerPos_ = playerPos;
    playerGuarding_ = playerGuarding;

    float currentDistance = GetDistanceToPlayer();
    float distanceDelta = std::fabs(currentDistance - lastDistanceToPlayer_);

    if (distanceDelta < stagnantDistanceThreshold_) {
        stagnantTimer_ += deltaTime;
    } else {
        stagnantTimer_ = 0.0f;
    }
    isDistanceStagnant_ = (stagnantTimer_ >= stagnantTimeThreshold_);

    if (currentDistance <= closePressureDistance_) {
        closePressureTimer_ += deltaTime;
        if (closePressureTimer_ > closePressureTimeThreshold_) {
            closePressureTimer_ = closePressureTimeThreshold_;
        }
    } else {
        closePressureTimer_ -= deltaTime;
        if (closePressureTimer_ < 0.0f) {
            closePressureTimer_ = 0.0f;
        }
    }

    if (currentDistance > farAttackDistance_) {
        farDistanceTimer_ += deltaTime;
    } else {
        farDistanceTimer_ = 0.0f;
    }

    if (warpEscapeCooldownTimer_ > 0.0f) {
        warpEscapeCooldownTimer_ -= deltaTime;
        if (warpEscapeCooldownTimer_ < 0.0f) {
            warpEscapeCooldownTimer_ = 0.0f;
        }
    }

    lastDistanceToPlayer_ = currentDistance;

    if (action_.kind == ActionKind::None) {
        UpdateFacingToPlayerWithSpeed(deltaTime, idleTurnSpeed_);
    }

    stateTimer_ += deltaTime;

    isAttackActive_ = false;
    isGuardActive_ = false;

    UpdateByAction(deltaTime);

    UpdateBullets(deltaTime);
    UpdateWaves(deltaTime);

    UpdateParts();
}

// ============================================================
// 描画処理
// ============================================================
void Enemy::Draw(ModelManager *modelManager, const Camera &camera) {
    if (!IsAlive()) {
        return;
    }

    if (isVisible_) {
        modelManager->Draw(modelId_, bodyTf_, camera);
        modelManager->Draw(modelId_, leftHandTf_, camera);
        modelManager->Draw(modelId_, rightHandTf_, camera);
    }

    for (const auto &bullet : bullets_) {
        if (!bullet.isAlive) {
            continue;
        }

        Transform bulletTf = tf_;
        bulletTf.position = bullet.position;
        bulletTf.scale = {0.2f, 0.2f, 0.2f};

        modelManager->Draw(modelId_, bulletTf, camera);
    }

    for (const auto &wave : waves_) {
        if (!wave.isAlive) {
            continue;
        }

        Transform waveTf = tf_;
        waveTf.position = wave.position;
        waveTf.scale = {0.6f, 0.2f, 1.2f};

        modelManager->Draw(modelId_, waveTf, camera);
    }
}

// ============================================================
// 被ダメージ処理
// ============================================================
void Enemy::TakeDamage(float damage) {
    hp_ -= damage;

    if (hp_ < 0.0f) {
        hp_ = 0.0f;
    }
}

void Enemy::DestroyBullet(size_t index) {
    if (index >= bullets_.size()) {
        return;
    }

    bullets_[index].isAlive = false;
    bullets_[index].lifeTime = 0.0f;
}

void Enemy::ReflectBullet(size_t index, const DirectX::XMFLOAT3 &targetPos) {
    if (index >= bullets_.size()) {
        return;
    }

    auto &bullet = bullets_[index];
    if (!bullet.isAlive) {
        return;
    }

    float speed = std::sqrtf(bullet.velocity.x * bullet.velocity.x +
                             bullet.velocity.y * bullet.velocity.y +
                             bullet.velocity.z * bullet.velocity.z);
    if (speed < 0.0001f) {
        speed = bulletSpeed_;
    }

    float dirX = targetPos.x - bullet.position.x;
    float dirY = targetPos.y - bullet.position.y;
    float dirZ = targetPos.z - bullet.position.z;
    float len = std::sqrtf(dirX * dirX + dirY * dirY + dirZ * dirZ);
    if (len < 0.0001f) {
        dirX = 0.0f;
        dirY = 0.0f;
        dirZ = -1.0f;
        len = 1.0f;
    }

    dirX /= len;
    dirY /= len;
    dirZ /= len;

    bullet.velocity = {dirX * speed, dirY * speed, dirZ * speed};
    bullet.isReflected = true;
}

void Enemy::DestroyWave(size_t index) {
    if (index >= waves_.size()) {
        return;
    }

    waves_[index].isAlive = false;
}

void Enemy::ReflectWave(size_t index, const DirectX::XMFLOAT3 &targetPos) {
    if (index >= waves_.size()) {
        return;
    }

    auto &wave = waves_[index];
    if (!wave.isAlive) {
        return;
    }

    float dirX = targetPos.x - wave.position.x;
    float dirY = targetPos.y - wave.position.y;
    float dirZ = targetPos.z - wave.position.z;
    float len = std::sqrtf(dirX * dirX + dirY * dirY + dirZ * dirZ);
    if (len < 0.0001f) {
        dirX = 0.0f;
        dirY = 0.0f;
        dirZ = -1.0f;
        len = 1.0f;
    }

    dirX /= len;
    dirY /= len;
    dirZ /= len;

    wave.direction = {dirX, dirY, dirZ};
    wave.traveledDistance = 0.0f;
    wave.isReflected = true;
}

// ============================================================
// 各部位Transform更新処理
// ============================================================
void Enemy::UpdateParts() {
    float usedYaw = GetVisualYaw();

    float forwardX = std::sinf(usedYaw);
    float forwardZ = std::cosf(usedYaw);

    float rightX = std::cosf(usedYaw);
    float rightZ = -std::sinf(usedYaw);

    bodyTf_ = tf_;
    bodyTf_.position = tf_.position;
    bodyTf_.scale = {1.2f, 1.4f, 0.8f};

    leftHandTf_ = tf_;
    leftHandTf_.position = tf_.position;
    leftHandTf_.position.x += (-rightX) * 1.2f;
    leftHandTf_.position.y += 0.9f;
    leftHandTf_.position.z += (-rightZ) * 1.2f;
    leftHandTf_.scale = {0.6f, 0.6f, 0.6f};

    rightHandTf_ = tf_;
    rightHandTf_.position = tf_.position;
    rightHandTf_.position.x += rightX * 1.2f;
    rightHandTf_.position.y += 0.9f;
    rightHandTf_.position.z += rightZ * 1.2f;
    rightHandTf_.scale = {0.6f, 0.6f, 0.6f};

    if (action_.kind == ActionKind::Smash) {
        if (action_.step == ActionStep::Charge) {
            rightHandTf_.position.y += 1.5f;
            rightHandTf_.position.x += (-forwardX) * 0.5f;
            rightHandTf_.position.z += (-forwardZ) * 0.5f;
        } else if (action_.step == ActionStep::Active) {
            rightHandTf_.position.y -= 0.2f;
            rightHandTf_.position.x += forwardX * 1.8f;
            rightHandTf_.position.z += forwardZ * 1.8f;
        } else if (action_.step == ActionStep::Recovery) {
            rightHandTf_.position.y += 0.3f;
            rightHandTf_.position.x += forwardX * 0.8f;
            rightHandTf_.position.z += forwardZ * 0.8f;
        }

    } else if (action_.kind == ActionKind::Sweep) {
        if (action_.step == ActionStep::Charge) {
            rightHandTf_.position.x += rightX * 1.4f;
            rightHandTf_.position.y += 0.4f;
            rightHandTf_.position.z += rightZ * 1.4f;
        } else if (action_.step == ActionStep::Active) {
            rightHandTf_.position.x += (-rightX) * 1.6f;
            rightHandTf_.position.y += 0.2f;
            rightHandTf_.position.z += (-rightZ) * 1.6f;
        } else if (action_.step == ActionStep::Recovery) {
            rightHandTf_.position.x += rightX * 0.3f;
            rightHandTf_.position.y += 0.1f;
            rightHandTf_.position.z += rightZ * 0.3f;
        }

    } else if (action_.kind == ActionKind::Shot) {
        if (action_.step == ActionStep::Charge) {
            rightHandTf_.position.y += 0.5f;
            rightHandTf_.position.x += forwardX * 0.8f;
            rightHandTf_.position.z += forwardZ * 0.8f;
        } else if (action_.step == ActionStep::Active) {
            rightHandTf_.position.y += 0.3f;
            rightHandTf_.position.x += forwardX * 1.0f;
            rightHandTf_.position.z += forwardZ * 1.0f;
        } else if (action_.step == ActionStep::Recovery) {
            rightHandTf_.position.y += 0.2f;
            rightHandTf_.position.x += forwardX * 0.4f;
            rightHandTf_.position.z += forwardZ * 0.4f;
        }

    } else if (action_.kind == ActionKind::Wave) {
        if (action_.step == ActionStep::Charge) {
            rightHandTf_.position.y += 0.8f;
            rightHandTf_.position.x += forwardX * 0.6f;
            rightHandTf_.position.z += forwardZ * 0.6f;
        } else if (action_.step == ActionStep::Active) {
            rightHandTf_.position.y += 0.4f;
            rightHandTf_.position.x += forwardX * 1.0f;
            rightHandTf_.position.z += forwardZ * 1.0f;
        } else if (action_.step == ActionStep::Recovery) {
            rightHandTf_.position.y += 0.2f;
            rightHandTf_.position.x += forwardX * 0.4f;
            rightHandTf_.position.z += forwardZ * 0.4f;
        }

    } else if (action_.kind == ActionKind::Warp) {
        if (action_.step == ActionStep::End) {
            rightHandTf_.position.y += 0.2f;
            rightHandTf_.position.x += forwardX * 0.3f;
            rightHandTf_.position.z += forwardZ * 0.3f;
        }

    } else if (action_.kind == ActionKind::Guard) {
        if (guardTarget_ == GuardTarget::Face) {
            leftHandTf_.position.x += forwardX * 0.6f;
            leftHandTf_.position.y += 0.9f;
            leftHandTf_.position.z += forwardZ * 0.6f;

        } else if (guardTarget_ == GuardTarget::BodyCenter) {
            leftHandTf_.position.x += forwardX * 0.4f;
            leftHandTf_.position.y += 0.3f;
            leftHandTf_.position.z += forwardZ * 0.4f;

        } else if (guardTarget_ == GuardTarget::BodyLeft) {
            leftHandTf_.position.x += (-rightX) * 0.2f;
            leftHandTf_.position.y += 0.3f;
            leftHandTf_.position.z += (-rightZ) * 0.2f;
        }
    }
}

// ============================================================
// OBB生成共通処理
// ============================================================
OBB Enemy::MakeOBB(const Transform &tf, const DirectX::XMFLOAT3 &size) const {
    OBB box{};
    box.center = tf.position;
    box.center.y += size.y * 0.5f;
    box.size = size;
    box.rotation = tf.rotation;
    return box;
}

// ============================================================
// 各部位の当たり判定取得
// ============================================================
OBB Enemy::GetBodyOBB() const { return MakeOBB(bodyTf_, bodySize_); }
OBB Enemy::GetLeftHandOBB() const { return MakeOBB(leftHandTf_, handSize_); }
OBB Enemy::GetRightHandOBB() const { return MakeOBB(rightHandTf_, handSize_); }

// ============================================================
// 攻撃用OBB取得
// ============================================================
OBB Enemy::GetAttackOBB() const {
    switch (action_.kind) {
    case ActionKind::Smash:
        return GetSmashAttackOBB();
    case ActionKind::Sweep:
        return GetSweepAttackOBB();
    default:
        return OBB{};
    }
}

OBB Enemy::GetSmashAttackOBB() const {
    float usedYaw = ShouldUseLockedAttackYaw() ? lockedAttackYaw_ : facingYaw_;

    float forwardX = std::sinf(usedYaw);
    float forwardZ = std::cosf(usedYaw);

    Transform attackTf{};
    attackTf.scale = {1.0f, 1.0f, 1.0f};
    attackTf.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    attackTf.position = bodyTf_.position;
    attackTf.position.x += forwardX * smashAttackForwardOffset_;
    attackTf.position.y += smashAttackHeightOffset_;
    attackTf.position.z += forwardZ * smashAttackForwardOffset_;

    return MakeOBB(attackTf, GetCurrentAttackHitBoxSize());
}

OBB Enemy::GetSweepAttackOBB() const {
    float usedYaw = ShouldUseLockedAttackYaw() ? lockedAttackYaw_ : facingYaw_;

    float rightX = std::cosf(usedYaw);
    float rightZ = -std::sinf(usedYaw);

    Transform attackTf{};
    attackTf.scale = {1.0f, 1.0f, 1.0f};
    attackTf.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    attackTf.position = bodyTf_.position;
    attackTf.position.x += rightX * sweepAttackSideOffset_;
    attackTf.position.y += sweepAttackHeightOffset_;
    attackTf.position.z += rightZ * sweepAttackSideOffset_;

    return MakeOBB(attackTf, GetCurrentAttackHitBoxSize());
}

float Enemy::GetVisualYaw() const {
    if (ShouldUseLockedAttackYaw()) {
        return lockedAttackYaw_;
    }
    return facingYaw_;
}

float Enemy::GetCurrentAttackDamage() const {
    const AttackParam *param = GetCurrentAttackParam();
    if (!param) {
        return 0.0f;
    }
    return param->damage;
}

float Enemy::GetCurrentAttackKnockback() const {
    const AttackParam *param = GetCurrentAttackParam();
    if (!param) {
        return 0.0f;
    }
    return param->knockback;
}

DirectX::XMFLOAT3 Enemy::GetCurrentAttackHitBoxSize() const {
    const AttackParam *param = GetCurrentAttackParam();
    if (!param) {
        return {0.1f, 0.1f, 0.1f};
    }
    return param->hitBoxSize;
}

// ============================================================
// プレイヤーとの距離計算
// ============================================================
float Enemy::GetDistanceToPlayer() const {
    float dx = playerPos_.x - tf_.position.x;
    float dy = playerPos_.y - tf_.position.y;
    float dz = playerPos_.z - tf_.position.z;

    return std::sqrtf(dx * dx + dy * dy + dz * dz);
}

// ============================================================
// Idle更新
// ============================================================
void Enemy::UpdateIdle(float deltaTime) {
    (void)deltaTime;

    if (stateTimer_ < 0.5f) {
        return;
    }

    float distance = GetDistanceToPlayer();

    if (distance <= nearAttackDistance_) {
        int total = nearSmashWeight_ + nearSweepWeight_ + nearGuardWeight_;
        if (total <= 0) {
            total = 1;
        }

        int r = std::rand() % total;

        if (r < nearSmashWeight_) {
            BeginAction(ActionKind::Smash, ActionStep::Charge);
        } else if (r < nearSmashWeight_ + nearSweepWeight_) {
            BeginAction(ActionKind::Sweep, ActionStep::Charge);
        } else {
            BeginAction(ActionKind::Guard, ActionStep::Move);
            DecideGuardTarget();
        }

    } else if (distance > farAttackDistance_) {
        int shotWeight = farShotWeight_;
        int warpWeight = farWarpWeight_;
        int waveWeight = farWaveWeight_;

        if (isDistanceStagnant_) {
            warpWeight += stagnantWarpBonus_;
        }

        if (lastActionKind_ == ActionKind::Shot) {
            shotWeight /= 2;
        } else if (lastActionKind_ == ActionKind::Warp) {
            warpWeight /= 2;
        } else if (lastActionKind_ == ActionKind::Wave) {
            waveWeight /= 2;
        }

        if (playerGuarding_) {
            waveWeight += 10;
        }

        int total = shotWeight + warpWeight + waveWeight;
        if (total <= 0) {
            total = 1;
        }

        int r = std::rand() % total;
        if (r < shotWeight) {
            BeginAction(ActionKind::Shot, ActionStep::Charge);

        } else if (r < shotWeight + warpWeight) {
            if (PrepareWarpContext()) {
                BeginAction(ActionKind::Warp, ActionStep::Start);
            } else {
                BeginAction(ActionKind::Wave, ActionStep::Charge);
            }

        } else {
            BeginAction(ActionKind::Wave, ActionStep::Charge);
        }
    }
}

// ============================================================
// 振り下ろし攻撃更新
// ============================================================
void Enemy::UpdateSmashCharge(float deltaTime) {
    float trackingEnd = smashTiming_.trackingEndTime;
    if (trackingEnd < 0.0f) {
        trackingEnd = 0.0f;
    }
    if (trackingEnd > smashChargeTime_) {
        trackingEnd = smashChargeTime_;
    }

    if (stateTimer_ < trackingEnd) {
        UpdateFacingToPlayerWithSpeed(deltaTime, chargeTurnSpeed_);
    } else if (!hasTrackingLocked_) {
        LockCurrentFacing();
        hasTrackingLocked_ = true;
    }

    if (stateTimer_ >= smashChargeTime_) {
        if (!hasTrackingLocked_) {
            LockCurrentFacing();
            hasTrackingLocked_ = true;
        }

        ChangeActionStep(ActionStep::Active);
    }
}

void Enemy::UpdateSmashAttack(float deltaTime) {
    (void)deltaTime;

    isAttackActive_ = IsCurrentAttackInActiveWindow();

    if (IsCurrentAttackInRecoveryWindow()) {
        ChangeActionStep(ActionStep::Recovery);
    }
}

void Enemy::UpdateSmashRecovery(float deltaTime) {
    UpdateFacingToPlayerWithSpeed(deltaTime, recoveryTurnSpeed_);

    const AttackTimingParam *timing = GetCurrentAttackTiming();
    if (!timing) {
        EndAttack();
        return;
    }

    float recoveryDuration = timing->totalTime - timing->recoveryStartTime;
    if (recoveryDuration < 0.0f) {
        recoveryDuration = 0.0f;
    }

    if (stateTimer_ >= recoveryDuration) {
        FinishCurrentAction();
    }
}

// ============================================================
// 薙ぎ払い攻撃更新
// ============================================================
void Enemy::UpdateSweepCharge(float deltaTime) {
    float trackingEnd = sweepTiming_.trackingEndTime;
    if (trackingEnd < 0.0f) {
        trackingEnd = 0.0f;
    }
    if (trackingEnd > sweepChargeTime_) {
        trackingEnd = sweepChargeTime_;
    }

    if (stateTimer_ < trackingEnd) {
        UpdateFacingToPlayerWithSpeed(deltaTime, chargeTurnSpeed_);
    } else if (!hasTrackingLocked_) {
        LockCurrentFacing();
        hasTrackingLocked_ = true;
    }

    if (stateTimer_ >= sweepChargeTime_) {
        if (!hasTrackingLocked_) {
            LockCurrentFacing();
            hasTrackingLocked_ = true;
        }

        ChangeActionStep(ActionStep::Active);
    }
}

void Enemy::UpdateSweepAttack(float deltaTime) {
    (void)deltaTime;

    isAttackActive_ = IsCurrentAttackInActiveWindow();

    if (IsCurrentAttackInRecoveryWindow()) {
        ChangeActionStep(ActionStep::Recovery);
    }
}

void Enemy::UpdateSweepRecovery(float deltaTime) {
    UpdateFacingToPlayerWithSpeed(deltaTime, recoveryTurnSpeed_);

    const AttackTimingParam *timing = GetCurrentAttackTiming();
    if (!timing) {
        EndAttack();
        return;
    }

    float recoveryDuration = timing->totalTime - timing->recoveryStartTime;
    if (recoveryDuration < 0.0f) {
        recoveryDuration = 0.0f;
    }

    if (stateTimer_ >= recoveryDuration) {
        FinishCurrentAction();
    }
}

// ============================================================
// 向き更新処理
// ============================================================
void Enemy::UpdateFacingToPlayer() {
    float dx = playerPos_.x - tf_.position.x;
    float dz = playerPos_.z - tf_.position.z;

    facingYaw_ = std::atan2f(dx, dz);
}

void Enemy::LockCurrentFacing() { lockedAttackYaw_ = facingYaw_; }

float Enemy::NormalizeAngle(float angle) const {
    while (angle > 3.14159265f) {
        angle -= 6.28318530f;
    }
    while (angle < -3.14159265f) {
        angle += 6.28318530f;
    }
    return angle;
}

void Enemy::UpdateFacingToPlayerWithSpeed(float deltaTime, float turnSpeed) {
    float dx = playerPos_.x - tf_.position.x;
    float dz = playerPos_.z - tf_.position.z;

    float targetYaw = std::atan2f(dx, dz);
    float diff = NormalizeAngle(targetYaw - facingYaw_);

    float maxStep = turnSpeed * deltaTime;

    if (diff > maxStep) {
        diff = maxStep;
    } else if (diff < -maxStep) {
        diff = -maxStep;
    }

    facingYaw_ = NormalizeAngle(facingYaw_ + diff);
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
    (void)deltaTime;

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
// ワープ処理
// ============================================================
bool Enemy::DecideWarpTargetNearPlayer(DirectX::XMFLOAT3 &outTarget) const {
    float angle = (std::rand() % 360) * 3.14159265f / 180.0f;

    float t = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    float radius =
        warpNearRadiusMin_ + (warpNearRadiusMax_ - warpNearRadiusMin_) * t;

    outTarget = playerPos_;
    outTarget.x += std::cosf(angle) * radius;
    outTarget.z += std::sinf(angle) * radius;
    outTarget.y = tf_.position.y;

    return true;
}

bool Enemy::DecideWarpTargetFarFromPlayer(DirectX::XMFLOAT3 &outTarget) const {
    float angle = (std::rand() % 360) * 3.14159265f / 180.0f;

    float t = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    float radius =
        warpFarRadiusMin_ + (warpFarRadiusMax_ - warpFarRadiusMin_) * t;

    outTarget = playerPos_;
    outTarget.x += std::cosf(angle) * radius;
    outTarget.z += std::sinf(angle) * radius;
    outTarget.y = tf_.position.y;

    return true;
}

bool Enemy::PrepareWarpContext() {
    ResetWarpContext();

    int approachWeight = warpApproachWeight_;
    int escapeWeight = 0;

    if (isDistanceStagnant_) {
        approachWeight += stagnantWarpBonus_;
    }

    if (farDistanceTimer_ >= farDistanceWarpTimeThreshold_) {
        approachWeight += farDistanceWarpBonus_;
    }

    bool canUseEscapeWarp =
        (closePressureTimer_ >= closePressureTimeThreshold_) &&
        (warpEscapeCooldownTimer_ <= 0.0f);

    if (canUseEscapeWarp) {
        escapeWeight = warpEscapeWeight_;
    }

    int warpTypeTotal = approachWeight + escapeWeight;
    if (warpTypeTotal <= 0) {
        warp_.type = WarpType::Approach;
    } else {
        int wr = std::rand() % warpTypeTotal;
        warp_.type =
            (wr < approachWeight) ? WarpType::Approach : WarpType::Escape;
    }

    bool ok = false;
    if (warp_.type == WarpType::Escape) {
        ok = DecideWarpTargetFarFromPlayer(warp_.targetPos);
    } else {
        warp_.type = WarpType::Approach;
        ok = DecideWarpTargetNearPlayer(warp_.targetPos);
    }

    if (!ok) {
        ResetWarpContext();
        return false;
    }

    warp_.hasValidTarget = true;

    // 既に連携起点が仕込まれている場合は、その意図を優先
    if (chain_.active && (chain_.starter == ChainStarter::SweepWarpSmash ||
                          chain_.starter == ChainStarter::WaveWarpSmash)) {
        OverrideWarpFollowupByChain();
    } else {
        DecideWarpFollowupFromContext();
        SetupChainFromWarpContext();
    }

    return true;
}

void Enemy::DecideWarpFollowupFromContext() {
    if (warp_.type == WarpType::Approach) {
        int total = nearSmashWeight_ + nearSweepWeight_;
        if (total <= 0) {
            warp_.followupKind = ActionKind::Smash;
            warp_.followupStep = ActionStep::Charge;
            return;
        }

        int r = std::rand() % total;
        if (r < nearSmashWeight_) {
            warp_.followupKind = ActionKind::Smash;
            warp_.followupStep = ActionStep::Charge;
        } else {
            warp_.followupKind = ActionKind::Sweep;
            warp_.followupStep = ActionStep::Charge;
        }
    } else if (warp_.type == WarpType::Escape) {
        int total = farShotWeight_ + farWaveWeight_;
        if (total <= 0) {
            warp_.followupKind = ActionKind::Shot;
            warp_.followupStep = ActionStep::Charge;
            return;
        }

        int r = std::rand() % total;
        if (r < farShotWeight_) {
            warp_.followupKind = ActionKind::Shot;
            warp_.followupStep = ActionStep::Charge;
        } else {
            warp_.followupKind = ActionKind::Wave;
            warp_.followupStep = ActionStep::Charge;
        }
    }
}

void Enemy::SetupChainFromWarpContext() {
    ResetChainContext();

    if (warp_.type == WarpType::Approach) {
        chain_.active = true;
        chain_.starter = ChainStarter::WarpApproach;
        chain_.stepCount = 0;
        chain_.maxSteps = warpApproachChainMaxSteps_;
    } else if (warp_.type == WarpType::Escape) {
        chain_.active = true;
        chain_.starter = ChainStarter::WarpEscape;
        chain_.stepCount = 0;
        chain_.maxSteps = warpEscapeChainMaxSteps_;
    }
}

void Enemy::SetupSweepWarpSmashChain() {
    ResetChainContext();
    chain_.active = true;
    chain_.starter = ChainStarter::SweepWarpSmash;
    chain_.stepCount = 0;
    chain_.maxSteps = 2;
}

void Enemy::SetupWaveWarpSmashChain() {
    ResetChainContext();
    chain_.active = true;
    chain_.starter = ChainStarter::WaveWarpSmash;
    chain_.stepCount = 0;
    chain_.maxSteps = 2;
}

void Enemy::OverrideWarpFollowupByChain() {
    switch (chain_.starter) {
    case ChainStarter::SweepWarpSmash:
    case ChainStarter::WaveWarpSmash:
        warp_.followupKind = ActionKind::Smash;
        warp_.followupStep = ActionStep::Charge;
        break;
    default:
        break;
    }
}

void Enemy::BeginWarpFollowup() {
    ActionKind nextKind = warp_.followupKind;
    ActionStep nextStep = warp_.followupStep;

    if (nextKind == ActionKind::None || nextStep == ActionStep::None) {
        EndAttack();
        return;
    }

    if (chain_.active) {
        if (chain_.stepCount < 1) {
            chain_.stepCount = 1;
        }
    }

    BeginAction(nextKind, nextStep);
}

void Enemy::ResetWarpContext() { warp_ = WarpContext{}; }

void Enemy::ResetChainContext() { chain_ = ChainContext{}; }

bool Enemy::DecideNextChainAction(ActionKind finishedKind, ActionKind &outKind,
                                  ActionStep &outStep) const {
    outKind = ActionKind::None;
    outStep = ActionStep::None;

    if (!chain_.active) {
        return false;
    }

    float distance = GetDistanceToPlayer();

    switch (chain_.starter) {
    case ChainStarter::WarpApproach:
        // 接近Warp -> Smash -> Sweep
        if (distance > approachChainContinueDistance_) {
            return false;
        }

        if (finishedKind == ActionKind::Smash) {
            outKind = ActionKind::Sweep;
            outStep = ActionStep::Charge;
            return true;
        }

        // Sweepで締める
        return false;

    case ChainStarter::WarpEscape:
        if (finishedKind == ActionKind::Shot) {
            if (playerGuarding_ || distance >= escapeChainContinueDistance_) {
                outKind = ActionKind::Wave;
                outStep = ActionStep::Charge;
                return true;
            }
        }
        return false;

    case ChainStarter::SweepWarpSmash:
        // Sweep -> Warp -> Smash
        // Warp後Smashで締める
        return false;

    case ChainStarter::WaveWarpSmash:
        // Wave -> Warp -> Smash
        // Warp後Smashで締める
        return false;

    default:
        return false;
    }
}

bool Enemy::TryStartPostActionWarpChain(ActionKind finishedKind) {
    float distance = GetDistanceToPlayer();

    // Sweep -> Warp -> Smash
    if (finishedKind == ActionKind::Sweep) {
        if (distance <= sweepWarpSmashMaxDistance_) {
            float r =
                static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
            if (r < sweepWarpSmashChance_) {
                SetupSweepWarpSmashChain();

                warp_.type = WarpType::Approach;

                if (!DecideWarpTargetNearPlayer(warp_.targetPos)) {
                    ResetChainContext();
                    ResetWarpContext();
                    return false;
                }

                warp_.hasValidTarget = true;
                OverrideWarpFollowupByChain();
                BeginAction(ActionKind::Warp, ActionStep::Start);
                return true;
            }
        }
    }

    // Wave -> 接近Warp -> Smash
    if (finishedKind == ActionKind::Wave) {
        if (distance >= waveWarpSmashMinDistance_) {
            float r =
                static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
            if (r < waveWarpSmashChance_) {
                SetupWaveWarpSmashChain();

                warp_.type = WarpType::Approach;

                if (!DecideWarpTargetNearPlayer(warp_.targetPos)) {
                    ResetChainContext();
                    ResetWarpContext();
                    return false;
                }

                warp_.hasValidTarget = true;
                OverrideWarpFollowupByChain();
                BeginAction(ActionKind::Warp, ActionStep::Start);
                return true;
            }
        }
    }

    return false;
}

bool Enemy::TryContinueChain() {
    ActionKind finishedKind = action_.kind;

    // まず既存Chain継続
    if (chain_.active) {
        if (chain_.stepCount >= chain_.maxSteps) {
            ResetChainContext();
        } else {
            ActionKind nextKind = ActionKind::None;
            ActionStep nextStep = ActionStep::None;

            if (DecideNextChainAction(finishedKind, nextKind, nextStep)) {
                chain_.stepCount++;
                BeginAction(nextKind, nextStep);
                return true;
            }

            ResetChainContext();
        }
    }

    // 次に行動終了後の新規Warp連携を試す
    if (TryStartPostActionWarpChain(finishedKind)) {
        return true;
    }

    return false;
}

void Enemy::FinishCurrentAction() {
    if (TryContinueChain()) {
        return;
    }

    EndAttack();
}

void Enemy::UpdateWarpStart(float deltaTime) {
    (void)deltaTime;

    isVisible_ = false;
    warp_.collisionDisabled = true;

    if (stateTimer_ >= warpStartTime_) {
        ChangeActionStep(ActionStep::Move);
    }
}

void Enemy::UpdateWarpMove(float deltaTime) {
    (void)deltaTime;

    if (!warp_.hasValidTarget) {
        EndAttack();
        return;
    }

    tf_.position = warp_.targetPos;

    UpdateFacingToPlayer();
    LockCurrentFacing();

    ChangeActionStep(ActionStep::End);
}

void Enemy::UpdateWarpEnd(float deltaTime) {
    (void)deltaTime;

    isVisible_ = true;
    warp_.collisionDisabled = false;

    if (stateTimer_ >= warpEndTime_) {
        BeginWarpFollowup();
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

// ============================================================
// ガード処理
// ============================================================
void Enemy::DecideGuardTarget() {
    int r = std::rand() % 3;

    if (r == 0) {
        guardTarget_ = GuardTarget::Face;
    } else if (r == 1) {
        guardTarget_ = GuardTarget::BodyCenter;
    } else {
        guardTarget_ = GuardTarget::BodyLeft;
    }
}

void Enemy::UpdateGuardMove(float deltaTime) {
    (void)deltaTime;

    if (stateTimer_ >= guardMoveTime_) {
        action_.step = ActionStep::Hold;
        stateTimer_ = 0.0f;
    }
}

void Enemy::UpdateGuardHold(float deltaTime) {
    (void)deltaTime;

    isGuardActive_ = true;

    if (stateTimer_ >= guardHoldTime_) {
        action_.step = ActionStep::Recovery;
        stateTimer_ = 0.0f;
    }
}

void Enemy::UpdateGuardRecovery(float deltaTime) {
    (void)deltaTime;

    if (stateTimer_ >= guardRecoveryTime_) {
        guardTarget_ = GuardTarget::None;
        isGuardActive_ = false;
        EndAttack();
    }
}

// ============================================================
// アクションタイムの管理
// ============================================================
float Enemy::GetCurrentActionTime() const { return stateTimer_; }

const AttackTimingParam *Enemy::GetCurrentAttackTiming() const {
    switch (action_.kind) {
    case ActionKind::Smash:
        return &smashTiming_;
    case ActionKind::Sweep:
        return &sweepTiming_;
    default:
        return nullptr;
    }
}

AttackParam *Enemy::GetCurrentAttackParam() {
    switch (action_.kind) {
    case ActionKind::Smash:
        return &smashParam_;
    case ActionKind::Sweep:
        return &sweepParam_;
    case ActionKind::Shot:
        return &bulletParam_;
    case ActionKind::Wave:
        return &waveParam_;
    default:
        return nullptr;
    }
}

const AttackParam *Enemy::GetCurrentAttackParam() const {
    switch (action_.kind) {
    case ActionKind::Smash:
        return &smashParam_;
    case ActionKind::Sweep:
        return &sweepParam_;
    case ActionKind::Shot:
        return &bulletParam_;
    case ActionKind::Wave:
        return &waveParam_;
    default:
        return nullptr;
    }
}

bool Enemy::IsCurrentAttackInActiveWindow() const {
    const AttackTimingParam *timing = GetCurrentAttackTiming();
    if (!timing) {
        return false;
    }

    float t = GetCurrentActionTime();
    return (t >= timing->activeStartTime && t <= timing->activeEndTime);
}

bool Enemy::IsCurrentAttackInRecoveryWindow() const {
    const AttackTimingParam *timing = GetCurrentAttackTiming();
    if (!timing) {
        return false;
    }

    return GetCurrentActionTime() >= timing->recoveryStartTime;
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

// ============================================================
// 行動開始・行動遷移
// ============================================================
void Enemy::BeginAction(ActionKind kind, ActionStep step) {
    lastActionKind_ = kind;

    if (kind == ActionKind::Warp) {
        stagnantTimer_ = 0.0f;
        isDistanceStagnant_ = false;

        if (warp_.type == WarpType::Escape) {
            warpEscapeCooldownTimer_ = warpEscapeCooldown_;
        }
    } else {
        ResetWarpContext();
    }

    action_.kind = kind;
    action_.step = step;

    hasTrackingLocked_ = false;
    isAttackActive_ = false;
    stateTimer_ = 0.0f;
}

void Enemy::ChangeActionStep(ActionStep step) {
    action_.step = step;
    isAttackActive_ = false;
    stateTimer_ = 0.0f;
}

void Enemy::EndAttack() {
    action_.kind = ActionKind::None;
    action_.step = ActionStep::None;

    ResetWarpContext();
    ResetChainContext();
    isVisible_ = true;

    hasTrackingLocked_ = false;
    isAttackActive_ = false;
    stateTimer_ = 0.0f;
}

// ============================================================
// 現在の攻撃が、向き固定して攻撃判定を出すタイプか
// ============================================================
bool Enemy::ShouldUseLockedAttackYaw() const {
    switch (action_.kind) {
    case ActionKind::Smash:
    case ActionKind::Sweep:
        return true;
    default:
        return false;
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
}

// ============================================================
// action ベース更新
// ============================================================
void Enemy::UpdateByAction(float deltaTime) {
    if (action_.kind == ActionKind::None) {
        UpdateIdle(deltaTime);
        return;
    }

    switch (action_.kind) {
    case ActionKind::Smash:
        UpdateSmashByStep(deltaTime);
        break;
    case ActionKind::Sweep:
        UpdateSweepByStep(deltaTime);
        break;
    case ActionKind::Shot:
        UpdateShotByStep(deltaTime);
        break;
    case ActionKind::Wave:
        UpdateWaveByStep(deltaTime);
        break;
    case ActionKind::Warp:
        UpdateWarpByStep(deltaTime);
        break;
    case ActionKind::Guard:
        UpdateGuardByStep(deltaTime);
        break;
    default:
        UpdateIdle(deltaTime);
        break;
    }
}

void Enemy::UpdateSmashByStep(float deltaTime) {
    switch (action_.step) {
    case ActionStep::Charge:
        UpdateSmashCharge(deltaTime);
        break;
    case ActionStep::Active:
        UpdateSmashAttack(deltaTime);
        break;
    case ActionStep::Recovery:
        UpdateSmashRecovery(deltaTime);
        break;
    default:
        EndAttack();
        break;
    }
}

void Enemy::UpdateSweepByStep(float deltaTime) {
    switch (action_.step) {
    case ActionStep::Charge:
        UpdateSweepCharge(deltaTime);
        break;
    case ActionStep::Active:
        UpdateSweepAttack(deltaTime);
        break;
    case ActionStep::Recovery:
        UpdateSweepRecovery(deltaTime);
        break;
    default:
        EndAttack();
        break;
    }
}

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

void Enemy::UpdateWarpByStep(float deltaTime) {
    switch (action_.step) {
    case ActionStep::Start:
        UpdateWarpStart(deltaTime);
        break;
    case ActionStep::Move:
        UpdateWarpMove(deltaTime);
        break;
    case ActionStep::End:
        UpdateWarpEnd(deltaTime);
        break;
    default:
        EndAttack();
        break;
    }
}

void Enemy::UpdateGuardByStep(float deltaTime) {
    switch (action_.step) {
    case ActionStep::Move:
        UpdateGuardMove(deltaTime);
        break;
    case ActionStep::Hold:
        UpdateGuardHold(deltaTime);
        break;
    case ActionStep::Recovery:
        UpdateGuardRecovery(deltaTime);
        break;
    default:
        EndAttack();
        break;
    }
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
