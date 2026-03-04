#include "Player.h"
#include "Input.h"
#include "ModelManager.h"

void Player::Initialize(uint32_t playerModelId, uint32_t swordModelId) {
    playerModelId_ = playerModelId;

    playerTf_.position = {0, 0, 0};
    playerTf_.scale = {1, 1, 1};
    playerTf_.rotation = {0, 0, 0, 1};

    sword_.Initialize(swordModelId);
}

void Player::Update(Input *input) { sword_.Update(input); }

void Player::Draw(ModelManager *modelManager, const Camera &camera) {
    modelManager->Draw(playerModelId_, playerTf_, camera);

    sword_.Draw(modelManager, camera);
}