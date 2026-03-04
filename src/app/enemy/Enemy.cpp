#include "Enemy.h"
#include "ModelManager.h"

void Enemy::Initialize(uint32_t modelId) {
    modelId_ = modelId;

    tf_.position = {0.0f, 0.0f, 10.0f};
    tf_.scale = {1.0f, 1.0f, 1.0f};
    tf_.rotation = {0, 0, 0, 1};
}

void Enemy::Update() {
    if (!IsAlive())
        return;
}

void Enemy::Draw(ModelManager *modelManager, const Camera &camera) {
    if (!IsAlive())
        return;

    modelManager->Draw(modelId_, tf_, camera);
}

void Enemy::TakeDamage(float damage) {
    hp_ -= damage;

    if (hp_ < 0.0f) {
        hp_ = 0.0f;
    }
}
