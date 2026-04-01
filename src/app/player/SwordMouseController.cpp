#include "SwordMouseController.h"
#include "Input.h"
#include <algorithm>
#include <cmath>
#include <numbers>

using namespace DirectX;

void SwordMouseController::Update(
    Input* input,
    float dt,
    const Transform& swordPos) {
    UpdateOrientation(input, dt);
    UpdateGuard(input);
    UpdateCounter();
    UpdateSlash(input, dt);
    UpdateSlashDir(swordPos);
}

bool SwordMouseController::IsActive(Input* input) {
    return std::abs(input->GetMouseDX()) > 3 || 
           std::abs(input->GetMouseDY()) > 3 ||
           input->IsMousePress(0) || 
           input->IsMousePress(1);
}

void SwordMouseController::UpdateOrientation(Input* input, float dt) {
    float dx = static_cast<float>(input->GetMouseDX());
    float dy = static_cast<float>(input->GetMouseDY());

    // 感度
    const float sensitivity = 0.003f;

    yaw_ += dx * sensitivity;
    pitch_ += dy * sensitivity;

    // 上下向きすぎ防止
    pitch_ = std::clamp(pitch_, -1.2f, 1.2f);

    XMVECTOR qYaw = XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 0), yaw_);

    XMVECTOR qPitch = XMQuaternionRotationAxis(XMVectorSet(1, 0, 0, 0), pitch_);

    XMVECTOR q = XMQuaternionMultiply(qPitch, qYaw);
    q = XMQuaternionNormalize(q);

    XMStoreFloat4(&orientation_, q);

    float speed = std::sqrt(dx * dx + dy * dy);
    mouseSpeed_ = (dt > 0.0f) ? speed / dt : 0.0f;

    mouseDelta_ = {dx, dy};
}

void SwordMouseController::UpdateGuard(Input* input) {
    isGuard_ = input->IsMousePress(1);
}

void SwordMouseController::UpdateCounter() {
    if (isCounter_) {
        counterTimer_ -= 1;

        if (counterTimer_ <= 0) {
            isCounter_ = false;
            counterTimer_ = 300;
        }
    }
}

void SwordMouseController::UpdateSlash(Input *input, float dt) {
    float dx = static_cast<float>(input->GetMouseDX());
    float dy = static_cast<float>(input->GetMouseDY());

    mouseDelta_ = {dx, dy};

    float speed = std::sqrt(dx * dx + dy * dy);

    // Joy-Con版の angularVelocity_ の代わり
    mouseSpeed_ = speed / dt;

    if (mouseSpeed_ > kSlashHold) {
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

        if (mouseSpeed_ < kSlashHold * 0.5f && slashTimer_ > 0.1f) {
            isSlashMode_ = false;
        }
    }
}

void SwordMouseController::UpdateSlashDir(const Transform &swordPos) {
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