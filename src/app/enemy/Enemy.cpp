#include "Enemy.h"
#include "ModelManager.h"

#include <cstdlib>
#include <cmath>
void Enemy::Initialize(uint32_t modelId) {
    modelId_ = modelId;

    // ボス全体の基準位置
    tf_.position = {0.0f, 0.0f, 10.0f};
    tf_.scale = {1.0f, 1.0f, 1.0f};
    tf_.rotation = {0.0f, 0.0f, 0.0f, 1.0f};

    UpdateParts();
}

void Enemy::Update(const DirectX::XMFLOAT3 &playerPos, float deltaTime) {
    if (!IsAlive()) {
        return;
    }

    playerPos_ = playerPos;

        // Idle中はプレイヤー方向を追う
    if (state_ == EnemyState::Idle) {
        UpdateFacingToPlayer();
    }

    stateTimer_ += deltaTime;
    isAttackActive_ = false;

    switch (state_) {
    case EnemyState::Idle:
        UpdateIdle(deltaTime);
        break;

    case EnemyState::SmashCharge:
        UpdateSmashCharge(deltaTime);
        break;
    case EnemyState::SmashAttack:
        UpdateSmashAttack(deltaTime);
        break;
    case EnemyState::SmashRecovery:
        UpdateSmashRecovery(deltaTime);
        break;

    case EnemyState::SweepCharge:
        UpdateSweepCharge(deltaTime);
        break;
    case EnemyState::SweepAttack:
        UpdateSweepAttack(deltaTime);
        break;
    case EnemyState::SweepRecovery:
        UpdateSweepRecovery(deltaTime);
        break;

    case EnemyState::ShotCharge:
        UpdateShotCharge(deltaTime);
        break;
    case EnemyState::ShotFire:
        UpdateShotFire(deltaTime);
        break;
    case EnemyState::ShotRecovery:
        UpdateShotRecovery(deltaTime);
        break;

    case EnemyState::WarpStart:
        UpdateWarpStart(deltaTime);
        break;
    case EnemyState::WarpMove:
        UpdateWarpMove(deltaTime);
        break;
    case EnemyState::WarpEnd:
        UpdateWarpEnd(deltaTime);
        break;
    }
    UpdateBullets(deltaTime);
    UpdateParts();
}

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
}

void Enemy::TakeDamage(float damage) {
    hp_ -= damage;

    if (hp_ < 0.0f) {
        hp_ = 0.0f;
    }
}

void Enemy::UpdateParts() {

    // 向きの計算
    float usedYaw = facingYaw_;

    // 攻撃中は固定した向きを使う
    if (state_ == EnemyState::SmashCharge ||
        state_ == EnemyState::SmashAttack ||
        state_ == EnemyState::SmashRecovery ||
        state_ == EnemyState::SweepCharge ||
        state_ == EnemyState::SweepAttack ||
        state_ == EnemyState::SweepRecovery) {
        usedYaw = lockedAttackYaw_;
    }

    float forwardX = std::sinf(usedYaw);
    float forwardZ = std::cosf(usedYaw);

    float rightX = std::cosf(usedYaw);
    float rightZ = -std::sinf(usedYaw);

        // 胴
    bodyTf_ = tf_;
    bodyTf_.position = tf_.position;
    bodyTf_.scale = {1.2f, 1.4f, 0.8f};

    // 左手
    leftHandTf_ = tf_;
    leftHandTf_.position = tf_.position;
    leftHandTf_.position.x += (-rightX) * 1.2f;
    leftHandTf_.position.y += 0.9f;
    leftHandTf_.position.z += (-rightZ) * 1.2f;
    leftHandTf_.scale = {0.6f, 0.6f, 0.6f};

    // 右手
    rightHandTf_ = tf_;
    rightHandTf_.position = tf_.position;
    rightHandTf_.position.x += rightX * 1.2f;
    rightHandTf_.position.y += 0.9f;
    rightHandTf_.position.z += rightZ * 1.2f;
    rightHandTf_.scale = {0.6f, 0.6f, 0.6f};

    // -------------------------
    // 状態ごとの疑似アニメ
    // -------------------------
    if (state_ == EnemyState::SmashCharge) {
        // 上に持ち上げつつ、少し後ろへ引く
        rightHandTf_.position.y += 1.5f;
        rightHandTf_.position.x += (-forwardX) * 0.5f;
        rightHandTf_.position.z += (-forwardZ) * 0.5f;
    } else if (state_ == EnemyState::SmashAttack) {
        // 前下に振り下ろす
        rightHandTf_.position.y -= 0.2f;
        rightHandTf_.position.x += forwardX * 1.8f;
        rightHandTf_.position.z += forwardZ * 1.8f;
    } else if (state_ == EnemyState::SmashRecovery) {
        // 少し前に残す
        rightHandTf_.position.y += 0.3f;
        rightHandTf_.position.x += forwardX * 0.8f;
        rightHandTf_.position.z += forwardZ * 0.8f;
    } else if (state_ == EnemyState::SweepCharge) {
        // 右へ大きく引く
        rightHandTf_.position.x += rightX * 1.4f;
        rightHandTf_.position.y += 0.4f;
        rightHandTf_.position.z += rightZ * 1.4f;
    } else if (state_ == EnemyState::SweepAttack) {
        // 右から左へ横切る
        rightHandTf_.position.x += (-rightX) * 1.6f;
        rightHandTf_.position.y += 0.2f;
        rightHandTf_.position.z += (-rightZ) * 1.6f;
    } else if (state_ == EnemyState::SweepRecovery) {
        // 少し戻す
        rightHandTf_.position.x += rightX * 0.3f;
        rightHandTf_.position.y += 0.1f;
        rightHandTf_.position.z += rightZ * 0.3f;
    } else if (state_ == EnemyState::ShotCharge) {
        // 右手を少し前に出して溜める
        rightHandTf_.position.y += 0.5f;
        rightHandTf_.position.x += forwardX * 0.8f;
        rightHandTf_.position.z += forwardZ * 0.8f;
    } else if (state_ == EnemyState::ShotFire) {
        // 発射中は前に構える
        rightHandTf_.position.y += 0.3f;
        rightHandTf_.position.x += forwardX * 1.0f;
        rightHandTf_.position.z += forwardZ * 1.0f;
    } else if (state_ == EnemyState::ShotRecovery) {
        rightHandTf_.position.y += 0.2f;
        rightHandTf_.position.x += forwardX * 0.4f;
        rightHandTf_.position.z += forwardZ * 0.4f;
    } else if (state_ == EnemyState::WarpEnd) {
        rightHandTf_.position.y += 0.2f;
        rightHandTf_.position.x += forwardX * 0.3f;
        rightHandTf_.position.z += forwardZ * 0.3f;
    }
}

OBB Enemy::MakeOBB(const Transform &tf, const DirectX::XMFLOAT3 &size) const {
    OBB box{};
    box.center = tf.position;
    box.center.y += size.y * 0.5f;
    box.size = size;
    box.rotation = tf.rotation;
    return box;
}

OBB Enemy::GetBodyOBB() const { return MakeOBB(bodyTf_, bodySize_); }

OBB Enemy::GetLeftHandOBB() const { return MakeOBB(leftHandTf_, handSize_); }

OBB Enemy::GetRightHandOBB() const { return MakeOBB(rightHandTf_, handSize_); }

OBB Enemy::GetAttackOBB() const {
    OBB box{};

    float usedYaw = lockedAttackYaw_;
    float forwardX = std::sinf(usedYaw);
    float forwardZ = std::cosf(usedYaw);
    float rightX = std::cosf(usedYaw);
    float rightZ = -std::sinf(usedYaw);

    if (state_ == EnemyState::SmashAttack) {
        // 振り下ろし：前方に強い
        box.center = bodyTf_.position;
        box.center.y += 0.8f;
        box.center.x += forwardX * 1.4f;
        box.center.z += forwardZ * 1.4f;
        box.size = {1.5f, 1.8f, 1.5f};
    } else if (state_ == EnemyState::SweepAttack) {
        // 薙ぎ払い：横方向に広い
        box.center = bodyTf_.position;
        box.center.y += 0.8f;
        box.center.x += rightX * 0.2f;
        box.center.z += rightZ * 0.2f;
        box.size = {3.2f, 1.2f, 1.4f};
    } else {
        box.center = rightHandTf_.position;
        box.size = {0.1f, 0.1f, 0.1f};
    }

    box.rotation = bodyTf_.rotation;
    return box;
}

// プレイヤーとの距離を計算
float Enemy::GetDistanceToPlayer() const {
    float dx = playerPos_.x - tf_.position.x;
    float dy = playerPos_.y - tf_.position.y;
    float dz = playerPos_.z - tf_.position.z;

    return std::sqrtf(dx * dx + dy * dy + dz * dz);
}

void Enemy::UpdateIdle(float deltaTime) {
    deltaTime = deltaTime;
    if (stateTimer_ < 0.5f) {
        return;
    }

    float distance = GetDistanceToPlayer();

    if (distance <= nearAttackDistance_) {
        LockCurrentFacing();

        if (useSmashNext_) {
            state_ = EnemyState::SmashCharge;
        } else {
            state_ = EnemyState::SweepCharge;
        }

        useSmashNext_ = !useSmashNext_;
        stateTimer_ = 0.0f;
    } else if (distance > farAttackDistance_) {
        LockCurrentFacing();

        if (useShotNext_) {
            state_ = EnemyState::ShotCharge;
        } else {
            DecideWarpTargetNearPlayer();
            state_ = EnemyState::WarpStart;
        }

        useShotNext_ = !useShotNext_;
        stateTimer_ = 0.0f;
    }
}

// Smashは右手を使った振り下ろし攻撃
void Enemy::UpdateSmashCharge(float deltaTime) {
    deltaTime = deltaTime;
    // 右手を上に持ち上げる
    // ここでは UpdateParts 後に位置上書きされるので、
    // 後で UpdateParts 内で state を見て反映する
    if (stateTimer_ >= 0.6f) {
        state_ = EnemyState::SmashAttack;
        stateTimer_ = 0.0f;
    }
}

void Enemy::UpdateSmashAttack(float deltaTime) {
    deltaTime = deltaTime;
    // 攻撃判定ON
    isAttackActive_ = true;

    if (stateTimer_ >= 0.25f) {
        state_ = EnemyState::SmashRecovery;
        stateTimer_ = 0.0f;
    }
}

void Enemy::UpdateSmashRecovery(float deltaTime) {
    deltaTime = deltaTime;
    if (stateTimer_ >= 1.0f) {
        state_ = EnemyState::Idle;
        stateTimer_ = 0.0f;
    }
}

// Sweepは左手を使った横薙ぎ攻撃
void Enemy::UpdateSweepCharge(float deltaTime) {
    deltaTime = deltaTime;
    if (stateTimer_ >= 0.5f) {
        state_ = EnemyState::SweepAttack;
        stateTimer_ = 0.0f;
    }
}

void Enemy::UpdateSweepAttack(float deltaTime) {
    deltaTime = deltaTime;
    isAttackActive_ = true;

    if (stateTimer_ >= 0.3f) {
        state_ = EnemyState::SweepRecovery;
        stateTimer_ = 0.0f;
    }
}

void Enemy::UpdateSweepRecovery(float deltaTime) {
    deltaTime = deltaTime;
    if (stateTimer_ >= 1.0f) {
        state_ = EnemyState::Idle;
        stateTimer_ = 0.0f;
    }
}

// プレイヤーの位置をもとに向きを更新
void Enemy::UpdateFacingToPlayer() {
    float dx = playerPos_.x - tf_.position.x;
    float dz = playerPos_.z - tf_.position.z;

    // 真横・真後ろも含めて yaw を計算
    facingYaw_ = std::atan2f(dx, dz);
}

void Enemy::LockCurrentFacing() { lockedAttackYaw_ = facingYaw_; }

// Shot
void Enemy::UpdateShotCharge(float deltaTime) {
    deltaTime = deltaTime;
    if (stateTimer_ >= 0.6f) {
        state_ = EnemyState::ShotFire;
        stateTimer_ = 0.0f;

        shotsRemaining_ = 3 + (std::rand() % 3); // 3～5発
        shotIntervalTimer_ = 0.0f;
    }
}

void Enemy::UpdateShotFire(float deltaTime) {
    shotIntervalTimer_ += deltaTime;

    if (shotsRemaining_ > 0 && shotIntervalTimer_ >= 0.2f) {
        SpawnBullet();
        shotsRemaining_--;
        shotIntervalTimer_ = 0.0f;
    }

    if (shotsRemaining_ <= 0) {
        state_ = EnemyState::ShotRecovery;
        stateTimer_ = 0.0f;
    }
}

void Enemy::UpdateShotRecovery(float deltaTime) {
    deltaTime = deltaTime;
    if (stateTimer_ >= 0.8f) {
        state_ = EnemyState::Idle;
        stateTimer_ = 0.0f;
    }
}

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
    bullet.position.y += 0.2f;

    float speed = 6.0f;
    bullet.velocity = {dirX * speed, dirY * speed, dirZ * speed};
    bullet.lifeTime = 2.0f;
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

// ワープのターゲットをプレイヤーの近くにランダムに決める
void Enemy::DecideWarpTargetNearPlayer() {
    // プレイヤーの周囲にランダム角度で出る
    float angle = (std::rand() % 360) * 3.14159265f / 180.0f;

    float radius = 2.0f; // プレイヤー近くに出る
    warpTargetPos_ = playerPos_;
    warpTargetPos_.x += std::cosf(angle) * radius;
    warpTargetPos_.z += std::sinf(angle) * radius;

    // 今は平地前提
    warpTargetPos_.y = tf_.position.y;
}

void Enemy::UpdateWarpStart(float deltaTime) {
    deltaTime = deltaTime;
    // 消える準備
    isVisible_ = false;

    if (stateTimer_ >= 0.2f) {
        state_ = EnemyState::WarpMove;
        stateTimer_ = 0.0f;
    }
}

void Enemy::UpdateWarpMove(float deltaTime) {
    deltaTime = deltaTime;
    // 実際に座標移動
    tf_.position = warpTargetPos_;

    // ワープ先でプレイヤー方向を向き直す
    UpdateFacingToPlayer();
    LockCurrentFacing();

    state_ = EnemyState::WarpEnd;
    stateTimer_ = 0.0f;
}

void Enemy::UpdateWarpEnd(float deltaTime) {
    deltaTime = deltaTime;
    isVisible_ = true;

    if (stateTimer_ >= 0.2f) {
        state_ = EnemyState::Idle;
        stateTimer_ = 0.0f;
    }
}