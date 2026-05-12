#include "SwordMouseController.h"
#include "Input.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace {
constexpr float kMouseSwordOrientationScale = 0.018f;
constexpr float kMouseSwordMaxAngle = 1.05f;
constexpr float kMouseSlashMinDeltaSq = 6.0f * 6.0f;
}

SwordPose SwordMouseController::GetPose() const {
    SwordPose pose = state_.ToPose();
    pose.isMouse = true;
    return pose;
}

void SwordMouseController::Update(Input *input, float dt,
                                  const Transform &swordPos) {
    (void)swordPos;
    UpdateOrientation(input, dt);
    state_.isGuard = false;
    state_.isCounter = false;
    UpdateSlash(input, dt);
}

bool SwordMouseController::IsActive(Input *input) {
    return input->IsMousePress(0);
}

void SwordMouseController::UpdateOrientation(Input *input, float dt) {
    if (!input->IsMousePress(0)) {
        yaw_ = 0.0f;
        pitch_ = 0.0f;
        state_.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
        mouseSpeed_ = 0.0f;
        mouseDelta_ = {};
        return;
    }

    const float dx = static_cast<float>(input->GetMouseDX());
    const float dy = static_cast<float>(input->GetMouseDY());

    const float deltaLenSq = dx * dx + dy * dy;
    if (deltaLenSq < kMouseSlashMinDeltaSq) {
        yaw_ = 0.0f;
        pitch_ = 0.0f;
    } else {
        yaw_ =
            std::clamp(dx * kMouseSwordOrientationScale, -kMouseSwordMaxAngle,
                       kMouseSwordMaxAngle);
        pitch_ =
            std::clamp(dy * kMouseSwordOrientationScale, -kMouseSwordMaxAngle,
                       kMouseSwordMaxAngle);
    }

    XMVECTOR qYaw = XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 0), yaw_);
    XMVECTOR qPitch =
        XMQuaternionRotationAxis(XMVectorSet(1, 0, 0, 0), pitch_);
    XMVECTOR q = XMQuaternionNormalize(XMQuaternionMultiply(qPitch, qYaw));
    XMStoreFloat4(&state_.orientation, q);

    const float speed = std::sqrt(dx * dx + dy * dy);
    mouseSpeed_ = (dt > 0.0f) ? speed / dt : 0.0f;
    mouseDelta_ = {dx, dy};
}

void SwordMouseController::UpdateGuard(Input *input) {
    (void)input;
    state_.isGuard = false;
}

void SwordMouseController::UpdateSlash(Input *input, float dt) {
    if (!input->IsMousePress(0)) {
        state_.isSlashMode = false;
        state_.slashTimer = 0.0f;
        mouseDelta_ = {};
        mouseSpeed_ = 0.0f;
        return;
    }

    const float dx = static_cast<float>(input->GetMouseDX());
    const float dy = static_cast<float>(input->GetMouseDY());
    mouseDelta_ = {dx, dy};

    const float speed = std::sqrt(dx * dx + dy * dy);
    mouseSpeed_ = (dt > 0.0f) ? speed / dt : 0.0f;
    if (speed * speed >= kMouseSlashMinDeltaSq) {
        const float invLength = 1.0f / speed;
        state_.slashDir = {dx * invLength, -dy * invLength};
    }
    state_.UpdateSlash(mouseSpeed_, dt);
}
