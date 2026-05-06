#include "SwordMouseController.h"
#include "Input.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;

SwordPose SwordMouseController::GetPose() const {
    SwordPose pose = state_.ToPose();
    pose.isMouse = true;
    return pose;
}

void SwordMouseController::Update(Input *input, float dt,
                                  const Transform &swordPos) {
    UpdateOrientation(input, dt);
    UpdateGuard(input);
    UpdateCounterFromGuardMotion();
    state_.UpdateCounter();
    if (state_.isCounter) {
        state_.isSlashMode = false;
        state_.slashTimer = 0.0f;
    } else {
        UpdateSlash(input, dt);
    }
    state_.UpdateSlashDir(swordPos);
}

bool SwordMouseController::IsActive(Input *input) {
    return std::abs(input->GetMouseDX()) > 3 ||
           std::abs(input->GetMouseDY()) > 3 || input->IsMousePress(0) ||
           input->IsMousePress(1);
}

void SwordMouseController::UpdateOrientation(Input *input, float dt) {
    const float dx = static_cast<float>(input->GetMouseDX());
    const float dy = static_cast<float>(input->GetMouseDY());

    const float sensitivity = 0.003f;
    yaw_ += dx * sensitivity;
    pitch_ += dy * sensitivity;
    pitch_ = std::clamp(pitch_, -1.2f, 1.2f);

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
    state_.isGuard = input->IsMousePress(1);
}

void SwordMouseController::UpdateCounterFromGuardMotion() {
    const bool counterMotionActive =
        state_.isGuard && mouseSpeed_ >= counterSwingThreshold_;

    if (counterMotionActive && !prevCounterMotionActive_) {
        state_.isCounter = true;
        state_.counterTimer = SwordControllerState::kCounterFrames;
        state_.isSlashMode = false;
        state_.slashTimer = 0.0f;
    }

    if (!state_.isGuard) {
        prevCounterMotionActive_ = false;
        return;
    }

    prevCounterMotionActive_ = counterMotionActive;
}

void SwordMouseController::UpdateSlash(Input *input, float dt) {
    const float dx = static_cast<float>(input->GetMouseDX());
    const float dy = static_cast<float>(input->GetMouseDY());
    mouseDelta_ = {dx, dy};

    const float speed = std::sqrt(dx * dx + dy * dy);
    mouseSpeed_ = (dt > 0.0f) ? speed / dt : 0.0f;
    state_.UpdateSlash(mouseSpeed_, dt);
}
