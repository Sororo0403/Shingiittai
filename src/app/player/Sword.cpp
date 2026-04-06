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

    prevIsCounter_ = false;
    isCounterStance_ = false;
    justCountered_ = false;
    justCounterFailed_ = false;
    justCounterEarly_ = false;
    justCounterLate_ = false;
    counterStateTimer_ = 0.0f;
    counterAxis_ = SwordCounterAxis::None;
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

    UpdateCounterObservation(deltaTime);
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

void Sword::UpdateCounterObservation(float deltaTime) {
    // 1フレームだけ有効なフラグは先に落とす
    justCountered_ = false;
    justCounterFailed_ = false;
    justCounterEarly_ = false;
    justCounterLate_ = false;

    isCounterStance_ = isCounter_;

    // カウンター方向の暫定推定
    // slashDir_ から雑に4方向へ割り当てる
    // カウンター軸の暫定推定
    // slashDir_ の絶対値比較で縦 / 横だけに分ける
    if (isCounterStance_) {
        if (std::fabs(slashDir_.y) >= std::fabs(slashDir_.x)) {
            if (std::fabs(slashDir_.y) > 0.1f) {
                counterAxis_ = SwordCounterAxis::Vertical;
            } else {
                counterAxis_ = SwordCounterAxis::None;
            }
        } else {
            if (std::fabs(slashDir_.x) > 0.1f) {
                counterAxis_ = SwordCounterAxis::Horizontal;
            } else {
                counterAxis_ = SwordCounterAxis::None;
            }
        }
    } else {
        counterAxis_ = SwordCounterAxis::None;
    }

    // カウンター入力中
    if (isCounterStance_) {
        if (!prevIsCounter_) {
            counterStateTimer_ = 0.0f;
        } else {
            counterStateTimer_ += deltaTime;
        }
    } else {
        // 前フレームまでカウンター姿勢だったなら、
        // いったん「失敗して終わった」とみなす暫定実装
        if (prevIsCounter_) {
            justCounterFailed_ = true;

            if (counterStateTimer_ < counterEarlyThreshold_) {
                justCounterEarly_ = true;
            } else if (counterStateTimer_ > counterLateThreshold_) {
                justCounterLate_ = true;
            }
        }

        counterStateTimer_ = 0.0f;
    }

    prevIsCounter_ = isCounterStance_;
}

void Sword::NotifyCounterSuccess() {
    justCountered_ = true;

    // 成功したら失敗系はリセット
    justCounterFailed_ = false;
    justCounterEarly_ = false;
    justCounterLate_ = false;
}

void Sword::ImGuiDraw() {
#ifndef IMGUI_DISABLED
    ImGui::Begin("Debug");
    ImGui::Text("isSlashMode_: %s", isSlashMode_ ? "true" : "false");
    ImGui::Text("isGuard_: %s", isGuard_ ? "true" : "false");
    ImGui::Text("counter_: %s", isCounter_ ? "true" : "false");
    ImGui::Text("slashDir_: %.1f, %.1f", slashDir_.x, slashDir_.y);
    ImGui::Text("counterStance_: %s", isCounterStance_ ? "true" : "false");
    ImGui::Text("justCountered_: %s", justCountered_ ? "true" : "false");
    ImGui::Text("justCounterFailed_: %s",
                justCounterFailed_ ? "true" : "false");
    ImGui::Text("justCounterEarly_: %s", justCounterEarly_ ? "true" : "false");
    ImGui::Text("justCounterLate_: %s", justCounterLate_ ? "true" : "false");
    ImGui::Text("counterStateTimer_: %.2f", counterStateTimer_);
    const char *counterAxisName = "None";
    switch (counterAxis_) {
    case SwordCounterAxis::Vertical:
        counterAxisName = "Vertical";
        break;
    case SwordCounterAxis::Horizontal:
        counterAxisName = "Horizontal";
        break;
    default:
        break;
    }
    ImGui::Text("counterAxis_: %s", counterAxisName);
    ImGui::End();
#endif
}
