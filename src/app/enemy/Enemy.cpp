#include "Enemy.h"
#include "ModelManager.h"
#include "imgui.h"

void Enemy::Initialize(uint32_t modelId) {
    modelId_ = modelId;

    tf_.position = {0.0f, 0.0f, 10.0f};
    tf_.scale = {1.0f, 1.0f, 1.0f};
    tf_.rotation = {0, 0, 0, 1};
}

void Enemy::Update(const Transform &playerTf) {
    if (!IsAlive())
        return;

    SweepAttack(playerTf.position);
}

void Enemy::Draw(ModelManager *modelManager, const Camera &camera) {
    if (!IsAlive())
        return;

    modelManager->Draw(modelId_, tf_, camera);
    ImGuiDraw();
}

void Enemy::TakeDamage(float damage) {
    hp_ -= damage;

    if (hp_ < 0.0f) {
        hp_ = 0.0f;
    }
}

void Enemy::SweepAttack(const DirectX::XMFLOAT3 &playerPos) {

     if (waitTimer_ > 700) {
        waitTimer_ = 0;
    }

     waitTimer_++;

     if (waitTimer_ > 120 && waitTimer_ < 360) {
         phase_ = 1;
     } else if (waitTimer_ > 360 && waitTimer_ < 480) {
         phase_ = 2;
     } else if (waitTimer_ > 480 && waitTimer_ < 510) {
         phase_ = 3;
     } else {
         phase_ = 0;
     }

    // プレイヤー方向ベクトル
    DirectX::XMFLOAT3 dir;
    dir.x = playerPos.x - tf_.position.x;
    dir.y = 0.0f; // Yは無視
    dir.z = playerPos.z - tf_.position.z;

    // 距離
    float length = sqrtf(dir.x * dir.x + dir.z * dir.z);

     if (phase_ == 1) {

        // 一定距離より遠いなら移動
        if (length > stopDistance_) {

            // 正規化
            dir.x /= length;
            dir.z /= length;

            // 移動
            tf_.position.x += dir.x * moveSpeed_;
            tf_.position.z += dir.z * moveSpeed_;

            // 向きをプレイヤーに合わせる（Y回転）
            float angle = atan2f(dir.x, dir.z);
            tf_.rotation = {0, sinf(angle * 0.5f), 0, cosf(angle * 0.5f)};
        }
    }

    if (phase_ == 2) {
        isAttacking_ = true;
    }

     // ===== 2: 後退 =====
    if (phase_ == 3) {
        isAttacking_ = false;
        // 後ろに下がる（逆方向）
        tf_.position.x -= dir.x * (moveSpeed_ - 0.025f);
        tf_.position.z -= dir.z * (moveSpeed_ - 0.025f);

    }

}

OBB Enemy::GetOBB() const {
    OBB box;

    box.center = {tf_.position.x, tf_.position.y + size_.y * 0.5f,
                  tf_.position.z};
    box.size = size_;
    box.rotation = tf_.rotation;

    return box;
}

OBB Enemy::GetAttackOBB() const { 
    OBB box;

    // 前方向（Z+を前とする）
    float forwardX = 2.0f * (tf_.rotation.x * tf_.rotation.z +
                             tf_.rotation.w * tf_.rotation.y);
    float forwardZ = 1.0f - 2.0f * (tf_.rotation.x * tf_.rotation.x +
                                    tf_.rotation.y * tf_.rotation.y);

    // 前に少しずらす
    float offset = 2.0f;

    box.center = {tf_.position.x + forwardX * offset,
                  tf_.position.y + attackSize_.y * 0.5f,
                  tf_.position.z + forwardZ * offset};

    box.size = attackSize_;
    box.rotation = tf_.rotation;

    return box;
}

void Enemy::SetIsHit(bool isHit) { isHit_ = isHit; }

void Enemy::ImGuiDraw() {
#ifndef IMGUI_DISABLED
    ImGui::Begin("Debug");
    ImGui::Text("isEnemyAttackHit: %s", isHit_ ? "true" : "false");
    ImGui::End();
#endif
}
