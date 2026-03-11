#include "Player.h"
#include "Input.h"
#include "ModelManager.h"

void Player::Initialize(uint32_t playerModelId, uint32_t swordModelId) {
    modelId_ = playerModelId;

    tf_.position = {0, 0, 0};
    tf_.scale = {1, 1, 1};
    tf_.rotation = {0, 0, 0, 1};

    sword_.Initialize(swordModelId);
}

void Player::Update(Input *input, float deltaTime) {
    UpdateMovement(input, deltaTime);

    sword_.Update(input, deltaTime, tf_.position);
}

void Player::Draw(ModelManager *modelManager, const Camera &camera) {
    modelManager->Draw(modelId_, tf_, camera);

    sword_.Draw(modelManager, camera);
}

void Player::UpdateMovement(Input *input, float deltaTime) {
    float moveX = 0.0f;
    float moveZ = 0.0f;

    if (input->IsKeyPress(DIK_W)) {
        moveZ += 1.0f;
    }
    if (input->IsKeyPress(DIK_S)) {
        moveZ -= 1.0f;
    }
    if (input->IsKeyPress(DIK_A)) {
        moveX -= 1.0f;
    }
    if (input->IsKeyPress(DIK_D)) {
        moveX += 1.0f;
    }

    tf_.position.x += moveX * moveSpeed_ * deltaTime;
    tf_.position.z += moveZ * moveSpeed_ * deltaTime;
}

OBB Player::GetOBB() const {
    OBB box{};
    box.center = tf_.position;
    box.size = {1.0f, 2.0f, 1.0f};
    box.rotation = tf_.rotation;
    return box;
}