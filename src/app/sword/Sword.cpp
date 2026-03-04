#include "Sword.h"
#include "Input.h"
#include "ModelManager.h"

using namespace DirectX;

void Sword::Initialize(uint32_t modelId) {
    modelId_ = modelId;

    tf_.position = {0.0f, 0.0f, 3.0f};
    tf_.scale = {1.0f, 1.0f, 1.0f};
    tf_.rotation = {0, 0, 0, 1};
}

void Sword::Update(Input *input) {
    XMVECTOR q = input->GetOrientation();
    q = XMQuaternionConjugate(q);

    XMStoreFloat4(&tf_.rotation, q);
}

void Sword::Draw(ModelManager *modelManager, const Camera &camera) {
    modelManager->Draw(modelId_, tf_, camera);
}