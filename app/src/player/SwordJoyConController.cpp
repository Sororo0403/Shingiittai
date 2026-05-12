#include "SwordJoyConController.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace {
constexpr float kJoyConSlashSensitivity = 2.05f;
}

SwordPose SwordJoyConController::GetPose() const {
    SwordPose pose = state_.ToPose();
    pose.isJoyCon = true;
    return pose;
}

void SwordJoyConController::ResetTracking(JoyCon *joyCon) {
    XMVECTOR q = XMQuaternionIdentity();
    if (joyCon != nullptr && joyCon->IsConnected()) {
        q = XMQuaternionNormalize(XMQuaternionConjugate(joyCon->GetOrientation()));
    }

    XMStoreFloat4(&state_.orientation, q);
    XMStoreFloat4(&prevOrientation_, q);

    XMVECTOR tip = XMVector3Rotate(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), q);
    XMStoreFloat3(&prevTipDirection_, tip);

    angularVelocity_ = 0.0f;
    state_.isSlashMode = false;
    state_.slashTimer = 0.0f;
    state_.slashDir = {};
}

void SwordJoyConController::Update(JoyCon *joyCon, float dt,
                                   const Transform &swordPos) {
    (void)swordPos;
    UpdateOrientation(joyCon, dt);
    state_.isGuard = false;
    state_.isCounter = false;
    state_.counterTimer = SwordControllerState::kCounterFrames;
    UpdateSlash(dt);
    UpdateSlashDirFromOrientation();
}

bool SwordJoyConController::IsActive(const JoyCon *joyCon) const {
    if (joyCon == nullptr || !joyCon->IsConnected()) {
        return false;
    }

    return angularVelocity_ > 30.0f;
}

void SwordJoyConController::UpdateOrientation(JoyCon *joyCon, float dt) {
    if (joyCon == nullptr || !joyCon->IsConnected()) {
        angularVelocity_ = 0.0f;
        return;
    }

    XMVECTOR q =
        XMQuaternionNormalize(XMQuaternionConjugate(joyCon->GetOrientation()));
    float dot =
        XMVectorGetX(XMQuaternionDot(q, XMLoadFloat4(&prevOrientation_)));
    dot = std::clamp(dot, -1.0f, 1.0f);

    const float angleDiff = std::acos(dot) * 2.0f;
    angularVelocity_ = dt > 0.0f ? XMConvertToDegrees(angleDiff) / dt : 0.0f;

    XMStoreFloat4(&state_.orientation, q);
    XMStoreFloat4(&prevOrientation_, q);
}

void SwordJoyConController::UpdateGuard(JoyCon *joyCon) {
    (void)joyCon;
    state_.isGuard = false;
}

void SwordJoyConController::UpdateCounter(JoyCon *joyCon) {
    (void)joyCon;
    state_.isCounter = false;
    state_.counterTimer = SwordControllerState::kCounterFrames;
}

void SwordJoyConController::UpdateSlash(float dt) {
    state_.UpdateSlash(angularVelocity_ * kJoyConSlashSensitivity, dt);
}

void SwordJoyConController::UpdateSlashDirFromOrientation() {
    XMVECTOR orientation = XMLoadFloat4(&state_.orientation);
    XMVECTOR tip =
        XMVector3Rotate(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), orientation);
    XMFLOAT3 currentTip{};
    XMStoreFloat3(&currentTip, tip);

    if (!state_.isSlashMode) {
        prevTipDirection_ = currentTip;
        return;
    }

    const float dx = currentTip.x - prevTipDirection_.x;
    const float dy = currentTip.y - prevTipDirection_.y;
    const float lenSq = dx * dx + dy * dy;
    if (lenSq > 0.000025f) {
        const float invLen = 1.0f / std::sqrt(lenSq);
        state_.slashDir = {dx * invLen, dy * invLen};
    }

    prevTipDirection_ = currentTip;
}
