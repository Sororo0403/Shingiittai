#include "SwordMouseController.h"
#include "Input.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace {
constexpr float kMouseSwordOrientationScale = 0.0055f;
constexpr float kMouseSlashSpeedScale = 0.90f;
constexpr float kMouseSwordMaxAngle = 1.18f;
constexpr float kMouseSlashMinDeltaSq = 6.0f * 6.0f;
}

SwordPose SwordMouseController::GetPose() const {
    return state_.ToPose();
}

void SwordMouseController::Update(Input *input, float dt,
                                  const Transform &swordPos) {
    (void)swordPos;
    UpdateOrientation(input, dt);
    UpdateSlash(input, dt);
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
}

void SwordMouseController::UpdateSlash(Input *input, float dt) {
    const float dx = static_cast<float>(input->GetMouseDX());
    const float dy = static_cast<float>(input->GetMouseDY());

    const float speed = std::sqrt(dx * dx + dy * dy);
    const float mouseSpeed = (dt > 0.0f) ? speed / dt : 0.0f;
    if (speed * speed >= kMouseSlashMinDeltaSq) {
        const float invLength = 1.0f / speed;
        state_.slashDir = {dx * invLength, -dy * invLength};
    }
    state_.UpdateSlash(mouseSpeed * kMouseSlashSpeedScale, dt);
}
