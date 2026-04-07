#include "SwordJoyConController.h"
#include "Input.h"
#include <algorithm>
#include <cmath>
#include <numbers>

using namespace DirectX;

void SwordJoyConController::Update(
    Input* input, 
    float dt,
    const Transform& swordPos) {
    UpdateOrientation(input, dt);
    UpdateGuard(input);
    UpdateCounter();
    UpdateSlash(dt);
    UpdateSlashDir(swordPos);
}

bool SwordJoyConController::IsActive(Input *input) {
    const int guardButton = useLeftJoyCon_ ? JSL_BUTTON_ZL : JSL_BUTTON_ZR;
    return input->IsJoyConConnected(useLeftJoyCon_) &&
           (angularVelocity_ > 30.0f ||
            input->IsJsButtunPress(useLeftJoyCon_, guardButton));
}

void SwordJoyConController::UpdateOrientation(Input* input, float dt) {
    XMVECTOR q = input->GetOrientation(useLeftJoyCon_);

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

void SwordJoyConController::UpdateGuard(Input *input) {
    const int guardButton = useLeftJoyCon_ ? JSL_BUTTON_ZL : JSL_BUTTON_ZR;
    isGuard_ = input->IsJsButtunPress(useLeftJoyCon_, guardButton);
}

void SwordJoyConController::UpdateCounter() {
    if (isCounter_) {
        counterTimer_ -= 1;

        if (counterTimer_ <= 0) {
            isCounter_ = false;
            counterTimer_ = 300;
        }
    }
}

void SwordJoyConController::UpdateSlash(float dt) {
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

void SwordJoyConController::UpdateSlashDir(const Transform& swordPos) {
    if (!isSlashMode_)
        return;

    XMFLOAT2 current = {swordPos.position.x, swordPos.position.y};

    XMVECTOR currentV = XMLoadFloat2(&current);
    XMVECTOR prevV = XMLoadFloat2(&prevPos_);

    XMVECTOR delta = currentV - prevV;

    float len = XMVectorGetX(XMVector2Length(delta));
    if (len > 0.001f) {
        delta = XMVector2Normalize(delta);
        XMStoreFloat2(&slashDir_, delta);
    }

    prevPos_.x = swordPos.position.x;
    prevPos_.y = swordPos.position.y;
}
