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
    //swordJoyConController_.Update(input, deltaTime, tf_);
    swordMouseController_.Update(input, deltaTime, tf_);
    UpdateTransform(playerPos, playerRotation, playerArmLength,
                    playerHandHeight);


    //isSlashMode_ = swordJoyConController_.GetIsSlashMode();
    //isGuard_ = swordJoyConController_.GetIsGuard();
    //isCounter_ = swordJoyConController_.GetCounter();
    //slashDir_ = swordJoyConController_.GetSlashDir();
    //orientation_ = swordJoyConController_.GetOrientation();

    isSlashMode_ = swordMouseController_.GetIsSlashMode();
    isGuard_ = swordMouseController_.GetIsGuard();
    isCounter_ = swordMouseController_.GetCounter();
    slashDir_ = swordMouseController_.GetSlashDir();
    orientation_ = swordMouseController_.GetOrientation();
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

void Sword::SetCounter(bool isCounter) {
    swordMouseController_.SetCounter(isCounter);
}

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
