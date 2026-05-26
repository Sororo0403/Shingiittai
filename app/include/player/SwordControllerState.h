#pragma once
#include "SwordPose.h"
#include <DirectXMath.h>

struct SwordControllerState {
    void UpdateSlash(float motionSpeed, float dt) {
        if (motionSpeed > kSlashThreshold && !isSlashMode) {
            isSlashMode = true;
            slashTimer = 0.0f;
        }

        if (!isSlashMode) {
            return;
        }

        slashTimer += dt;

        if (slashTimer > kSlashTimeLimit ||
            (motionSpeed < kSlashThreshold * 0.5f && slashTimer > 0.1f)) {
            isSlashMode = false;
        }
    }

    SwordPose ToPose() const {
        SwordPose pose;
        pose.slashDir = slashDir;
        pose.orientation = orientation;
        pose.isSlashMode = isSlashMode;
        return pose;
    }

    static constexpr float kSlashThreshold = 1250.0f;
    static constexpr float kSlashTimeLimit = 0.34f;

    DirectX::XMFLOAT4 orientation{0, 0, 0, 1};
    DirectX::XMFLOAT2 slashDir{};
    bool isSlashMode = false;
    float slashTimer = 0.0f;
};
