#include "SwordMouseController.h"
#include "Input.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace {
constexpr float kMouseSwordOrientationScale = 0.0065f;
constexpr float kMouseSwordMaxAngle = 1.18f;
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
    (void)input;
    return true;
}

void SwordMouseController::UpdateOrientation(Input *input, float dt) {
    (void)dt;
    if (input->IsMouseTrigger(0)) {
        yaw_ = 0.0f;
        pitch_ = 0.0f;
    }

    const float dx = static_cast<float>(input->GetMouseDX());
    const float dy = static_cast<float>(input->GetMouseDY());

    yaw_ = std::clamp(yaw_ + dx * kMouseSwordOrientationScale,
                      -kMouseSwordMaxAngle, kMouseSwordMaxAngle);
    pitch_ = std::clamp(pitch_ + dy * kMouseSwordOrientationScale,
                        -kMouseSwordMaxAngle, kMouseSwordMaxAngle);

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
