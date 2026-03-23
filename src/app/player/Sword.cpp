#include "Sword.h"
#include "Camera.h"
#include "Input.h"
#include "imgui.h"
#include "ModelManager.h"
#include <algorithm>
#include <cmath>
#include <numbers>

using namespace DirectX;

void Sword::Initialize(uint32_t modelId) {
    modelId_ = modelId;

    tf_.position = {0, 0, 0};
    tf_.scale = {1, 1, 1};
    tf_.rotation = {0, 0, 0, 1};
}

void Sword::Update(Input *input, float deltaTime, const XMFLOAT3 &playerPos,
                   const XMFLOAT4 &playerRotation, float playerArmLength,
                   float playerHandHeight) {
    UpdateOrientation(input, deltaTime);
    UpdateGuard(input);
    UpdateSlash(deltaTime);
    UpdateCounter();
    UpdateSlashDir();
    UpdateTransform(playerPos, playerRotation, playerArmLength,
                    playerHandHeight);
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

void Sword::UpdateCounter() {
    if (isCounter_) {
        counterTimer_ -= 1;

        if (counterTimer_ <= 0) {
            isCounter_ = false;
            counterTimer_ = 300;
        }
    }
}

void Sword::UpdateSlashDir() { 
    if (!isSlashMode_) return;

    XMFLOAT2 current = { tf_.position.x, tf_.position.y };

    XMVECTOR currentV = XMLoadFloat2(&current);
    XMVECTOR prevV = XMLoadFloat2(&prevPos_);

    XMVECTOR delta = currentV - prevV;

    float len = XMVectorGetX(XMVector2Length(delta));
    if (len > 0.001f) {
        delta = XMVector2Normalize(delta);
        XMStoreFloat2(&slashDir_, delta);
    }

    prevPos_.x = tf_.position.x;
    prevPos_.y = tf_.position.y;
}

void Sword::Draw(ModelManager *modelManager, const Camera &camera) {
    modelManager->Draw(modelId_, tf_, camera);
    ImGuiDraw();
}


void Sword::UpdateTransform(const XMFLOAT3 &playerPos,
                            const XMFLOAT4 &playerRotation,
                            float playerArmLength, float playerHandHeight) {
    XMVECTOR playerRot = XMQuaternionNormalize(XMLoadFloat4(&playerRotation));
    XMVECTOR joyRot = XMQuaternionNormalize(XMLoadFloat4(&orientation_));

    XMVECTOR finalRot = XMQuaternionMultiply(joyRot, playerRot);

    XMStoreFloat4(&tf_.rotation, finalRot);

    XMVECTOR player = XMLoadFloat3(&playerPos);

    XMVECTOR shoulderOffset = XMVectorSet(0, playerHandHeight, 0, 0);
    XMVECTOR shoulderPos = player + shoulderOffset;

    XMVECTOR armVec = XMVectorSet(0, 0, playerArmLength, 0);
    armVec = XMVector3Rotate(armVec, finalRot);

    XMVECTOR handPos = shoulderPos + armVec;

    XMStoreFloat3(&tf_.position, handPos);
}

void Sword::SetCounter(bool isCounter) { isCounter_ = isCounter; }

void Sword::ImGuiDraw() {
#ifndef IMGUI_DISABLED
    ImGui::Begin("Debug");
    ImGui::Text("isSlashMode_: %s", isSlashMode_ ? "true" : "false");
    ImGui::Text("isGuard_: %s", isGuard_ ? "true" : "false");
    ImGui::Text("counter_: %s", isCounter_ ? "true" : "false");
    ImGui::Text("slashDir_: %.1f, %.1f", slashDir_.x, slashDir_.y);
    ImGui::End();
#endif
}
