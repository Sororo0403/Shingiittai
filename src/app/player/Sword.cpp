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

void Sword::Update(Input *input, float deltaTime,
                   const DirectX::XMFLOAT3 &playerPos, float playerYaw,
                   float playerArmLength, float playerHandHeight) {
    UpdateOrientation(input, deltaTime);
    UpdateGuard(input);
    UpdateSlash(deltaTime);
    UpdateTransform(playerPos, playerYaw, playerArmLength, playerHandHeight);
}

void Sword::Draw(ModelManager *modelManager, const Camera &camera) {
    modelManager->Draw(modelId_, tf_, camera);
}

OBB Sword::GetOBB() const {
    OBB box;

    XMVECTOR pos = XMLoadFloat3(&tf_.position);
    XMVECTOR rot = XMLoadFloat4(&tf_.rotation);

    XMVECTOR forward = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), rot);

    XMVECTOR center = pos + forward * (kSwordLength * 0.5f);

    XMStoreFloat3(&box.center, center);

    box.size = size_;
    box.rotation = tf_.rotation;

    return box;
}

void Sword::UpdateOrientation(Input *input, float dt) {
    XMVECTOR q = input->GetOrientation();

    q = XMQuaternionConjugate(q);
    q = XMQuaternionNormalize(q);

    float dot =
        XMVectorGetX(XMQuaternionDot(q, XMLoadFloat4(&prevOrientation_)));

    dot = std::clamp(dot, -1.0f, 1.0f);

    float angleDiff = std::acos(dot) * 2.0f;

    angularVelocity_ = XMConvertToDegrees(angleDiff) / dt;

    XMStoreFloat4(&orientation_, q);
    XMStoreFloat4(&prevOrientation_, q);
}

void Sword::UpdateGuard(Input *input) {
    isGuard_ = input->IsJsButtunPress(JSL_BUTTON_ZR);
}

void Sword::UpdateSlash(float dt) {
    if (angularVelocity_ > kSlashHold) {
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

        if (angularVelocity_ < kSlashHold * 0.5f && slashTimer_ > 0.1f) {
            isSlashMode_ = false;
        }
    }
}

void Sword::UpdateTransform(const XMFLOAT3 &playerPos, float playerYaw,
                            float playerArmLength, float playerHandHeight) {
    XMVECTOR playerRot =
        XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 0), playerYaw);

    XMVECTOR joyRot = XMLoadFloat4(&orientation_);

    // 剣回転
    XMVECTOR finalRot =
        XMQuaternionNormalize(XMQuaternionMultiply(joyRot, playerRot));

    XMStoreFloat4(&tf_.rotation, finalRot);

    XMVECTOR player = XMLoadFloat3(&playerPos);

    // 肩位置
    XMVECTOR shoulderOffset = XMVectorSet(0.0f, playerHandHeight, 0.0f, 0);
    shoulderOffset = XMVector3Rotate(shoulderOffset, playerRot);

    XMVECTOR shoulderPos = player + shoulderOffset;

    // 腕ベクトル
    XMVECTOR armVec = XMVectorSet(0, 0, playerArmLength, 0);

    armVec = XMVector3Rotate(armVec, finalRot);

    XMVECTOR handPos = shoulderPos + armVec;

    XMStoreFloat3(&tf_.position, handPos);
}
