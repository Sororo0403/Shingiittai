#include "Sword.h"
#include "Input.h"
#include "ModelManager.h"

using namespace DirectX;

void Sword::Initialize(uint32_t modelId) {
    modelId_ = modelId;

    tf_.position = {0.0f, 0.0f, 0.0f};
    tf_.scale = {1.0f, 1.0f, 1.0f};
    tf_.rotation = {0, 0, 0, 1};
}

void Sword::Update(Input *input, const DirectX::XMFLOAT3 &playerPos) {
    XMVECTOR q = input->GetOrientation();
    q = XMQuaternionConjugate(q);

    XMStoreFloat4(&tf_.rotation, q);

    // 剣の向き
    XMVECTOR forward = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), q);

    XMFLOAT3 dir;
    XMStoreFloat3(&dir, forward);

    tf_.position = playerPos;

    tf_.position.x += dir.x * kSwordLength;
    tf_.position.y += dir.y * kSwordLength + kHandHeight;
    tf_.position.z += dir.z * kSwordLength;
}

void Sword::Draw(ModelManager *modelManager, const Camera &camera) {
    modelManager->Draw(modelId_, tf_, camera);
}

OBB Sword::GetOBB() const {
    OBB box;

    box.center = tf_.position;
    box.size = size_;
    box.rotation = tf_.rotation;

    return box;
}
