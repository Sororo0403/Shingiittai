#include "Sword.h"
#include "Input.h"
#include "ModelManager.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;

void Sword::Initialize(uint32_t modelId) {
    modelId_ = modelId;

    tf_.position = {0.0f, 0.0f, 0.0f};
    tf_.scale = {1.0f, 1.0f, 1.0f};
    tf_.rotation = {0, 0, 0, 1};
}

void Sword::Update(Input *input, float dt, const DirectX::XMFLOAT3 &playerPos) {
    XMVECTOR q = input->GetOrientation();
    q = XMQuaternionConjugate(q);

    float dot =
        XMVectorGetX(XMQuaternionDot(q, XMLoadFloat4(&prevOrientation_)));
    dot = std::clamp(dot, -1.0f, 1.0f);
    float angleDiff = std::acos(dot) * 2.0f;

    float angularVelocity = XMConvertToDegrees(angleDiff) / dt;

    if (angularVelocity > kSlashHold) {
        if (!isSlashMode_) {
            isSlashMode_ = true;
            slashTimer_ = 0.0f;
        }
    }

    if (isSlashMode_) {
        slashTimer_ += dt;
        if (slashTimer_ > kTimeLimit) {
            isSlashMode_ = false;
        }

        if (angularVelocity < kSlashHold * 0.5f && slashTimer_ > 1.0f) {
            isSlashMode_ = false;
        }
    }

    XMStoreFloat4(&prevOrientation_, q);
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

    XMVECTOR pos = XMLoadFloat3(&tf_.position);
    XMVECTOR rot = XMLoadFloat4(&tf_.rotation);

    // 剣の向き
    XMVECTOR forward = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), rot);

    // 中心位置 = 柄 + 剣の半分
    XMVECTOR center = pos + forward * (kSwordLength * 0.5f);

    XMStoreFloat3(&box.center, center);

    box.size = size_;
    box.rotation = tf_.rotation;

    return box;
}