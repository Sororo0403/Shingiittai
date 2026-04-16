#include "Sword.h"
#include "Camera.h"
#include "ModelManager.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace {
constexpr float kSwordVisualScaleMultiplier = 3.0f;
}

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

void Sword::Update(const Transform &transform, const SwordPose &pose,
                   float deltaTime) {
    tf_ = transform;
    isSlashMode_ = pose.isSlashMode;
    isGuard_ = pose.isGuard;
    isCounter_ = pose.isCounter;
    isMouse = pose.isMouse;
    isJoyCon = pose.isJoyCon;
    slashDir_ = pose.slashDir;
    orientation_ = pose.orientation;
    UpdateCounterObservation(deltaTime);
}

void Sword::SetRecoveryReaction(float reaction) {
    recoveryReaction_ = std::clamp(reaction, 0.0f, 1.0f);
}

OBB Sword::GetOBB() const {
    OBB box;

    float hitBoxDepth = size_.z;
    float forwardOffset = kSwordLength * 0.5f;
    if (isSlashMode_) {
        hitBoxDepth += kSlashHitDepthExtension;
        forwardOffset += kSlashHitDepthExtension * 0.5f;
    }

    XMVECTOR pos = XMLoadFloat3(&tf_.position);
    XMVECTOR rot = XMLoadFloat4(&tf_.rotation);
    XMVECTOR forward = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), rot);
    XMVECTOR center = pos + forward * forwardOffset;

    XMStoreFloat3(&box.center, center);
    box.size = size_;
    box.size.z = hitBoxDepth;
    box.rotation = tf_.rotation;
    return box;
}

OBB Sword::GetCounterOBB() const {
    OBB box;

    XMVECTOR pos = XMLoadFloat3(&tf_.position);
    XMVECTOR rot = XMLoadFloat4(&tf_.rotation);

    XMVECTOR forward = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), rot);
    XMVECTOR center = pos + forward * 0.9f;

    XMStoreFloat3(&box.center, center);

    box.size = counterSize_;
    box.rotation = tf_.rotation;

    return box;
}

void Sword::Draw(ModelManager *modelManager, const Camera &camera) {
    Transform drawTransform = tf_;
    drawTransform.scale.x *= kSwordVisualScaleMultiplier;
    drawTransform.scale.y *= kSwordVisualScaleMultiplier;
    drawTransform.scale.z *= kSwordVisualScaleMultiplier;
    if (recoveryReaction_ > 0.0f) {
        const float phase = (1.0f - recoveryReaction_) * 36.0f;
        const float pulse = std::sinf(phase);
        const float scaleBoost = 1.0f + 0.08f * recoveryReaction_ * pulse;
        drawTransform.scale.x *= scaleBoost;
        drawTransform.scale.y *= scaleBoost;
        drawTransform.scale.z *= 1.0f + 0.12f * recoveryReaction_;
    }

    modelManager->Draw(modelId_, drawTransform, camera);
    ImGuiDraw();
}

void Sword::UpdateCounterObservation(float deltaTime) {
    justCountered_ = false;
    justCounterFailed_ = false;
    justCounterEarly_ = false;
    justCounterLate_ = false;

    isCounterStance_ = isCounter_;

    if (isCounterStance_) {
        if (std::fabs(slashDir_.y) >= std::fabs(slashDir_.x)) {
            counterAxis_ = std::fabs(slashDir_.y) > 0.1f
                               ? SwordCounterAxis::Vertical
                               : SwordCounterAxis::None;
        } else {
            counterAxis_ = std::fabs(slashDir_.x) > 0.1f
                               ? SwordCounterAxis::Horizontal
                               : SwordCounterAxis::None;
        }
    } else {
        counterAxis_ = SwordCounterAxis::None;
    }

    if (isCounterStance_) {
        if (!prevIsCounter_) {
            counterStateTimer_ = 0.0f;
        } else {
            counterStateTimer_ += deltaTime;
        }
    } else {
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
    justCounterFailed_ = false;
    justCounterEarly_ = false;
    justCounterLate_ = false;
    isCounterStance_ = false;
    prevIsCounter_ = false;
    counterStateTimer_ = 0.0f;
    counterAxis_ = SwordCounterAxis::None;
}

void Sword::ImGuiDraw() {
#ifndef IMGUI_DISABLED
    ImGui::Begin("Debug");
    ImGui::Text("Sword Pos: %.2f %.2f %.2f", tf_.position.x, tf_.position.y,
                tf_.position.z);
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
