#include "Bullet.h"
#include "ModelManager.h"

void Bullet::Initialize(uint32_t modelId) {
    modelId_ = modelId;

    tf_.position = {0.0f, 0.0f, 10.0f};
    tf_.scale = {1.0f, 1.0f, 1.0f};
    tf_.rotation = {0, 0, 0, 1};
}

void Bullet::Update() { 
    tf_.position.z -= 0.01f;
}

void Bullet::Draw(ModelManager *modelManager, const Camera &camera) {
    modelManager->Draw(modelId_, tf_, camera);
}

OBB Bullet::GetOBB() const {
    OBB box;

    box.center = {tf_.position.x, tf_.position.y + size_.y * 0.5f,
                  tf_.position.z};
    box.size = size_;
    box.rotation = tf_.rotation;

    return box;
}