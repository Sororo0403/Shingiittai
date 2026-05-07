#include "SwordJoyConController.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;

SwordPose SwordJoyConController::GetPose() const {
    SwordPose pose = state_.ToPose();
    pose.isJoyCon = true;
    return pose;
}

void SwordJoyConController::Update(JoyCon *joyCon, float dt,
                                   const Transform &swordPos) {
    UpdateOrientation(joyCon, dt);
    UpdateGuard(joyCon);
    state_.isCounter = false;
    UpdateSlash(dt);
    state_.UpdateSlashDir(swordPos);
}

bool SwordJoyConController::IsActive(const JoyCon *joyCon) const {
    if (joyCon == nullptr || !joyCon->IsConnected()) {
        return false;
    }

    const int guardButton = joyCon->IsLeft() ? JSL_BUTTON_ZL : JSL_BUTTON_ZR;
    return angularVelocity_ > 30.0f || joyCon->IsButtonPress(guardButton);
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
    if (joyCon == nullptr || !joyCon->IsConnected()) {
        state_.isGuard = false;
        return;
    }

    const int guardButton = joyCon->IsLeft() ? JSL_BUTTON_ZL : JSL_BUTTON_ZR;
    state_.isGuard = joyCon->IsButtonPress(guardButton);
}

void SwordJoyConController::UpdateSlash(float dt) {
    state_.UpdateSlash(angularVelocity_, dt);
}
