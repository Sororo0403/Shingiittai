#pragma once
#include "SwordPose.h"
#include "Transform.h"
#include <DirectXMath.h>

struct SwordControllerState {
    void UpdateCounter() {
        if (!isCounter) {
            counterTimer = kCounterFrames;
            return;
        }

        if (--counterTimer <= 0) {
            isCounter = false;
            counterTimer = kCounterFrames;
        }
    }

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

    void UpdateSlashDir(const Transform &swordTransform) {
        const DirectX::XMFLOAT2 current{swordTransform.position.x,
                                        swordTransform.position.y};

        if (!isSlashMode) {
            prevPos = current;
            return;
        }

        DirectX::XMVECTOR delta = DirectX::XMVectorSubtract(
            DirectX::XMLoadFloat2(&current), DirectX::XMLoadFloat2(&prevPos));
        const float len =
            DirectX::XMVectorGetX(DirectX::XMVector2Length(delta));
        if (len > 0.001f) {
            delta = DirectX::XMVector2Normalize(delta);
            DirectX::XMStoreFloat2(&slashDir, delta);
        }

        prevPos = current;
    }

    SwordPose ToPose() const {
        SwordPose pose;
        pose.slashDir = slashDir;
        pose.orientation = orientation;
        pose.isSlashMode = isSlashMode;
        pose.isGuard = isGuard;
        pose.isCounter = isCounter;
        return pose;
    }

    static constexpr int kCounterFrames = 24;
    static constexpr float kSlashThreshold = 720.0f;
    static constexpr float kSlashTimeLimit = 1.0f;

    DirectX::XMFLOAT4 orientation{0, 0, 0, 1};
    DirectX::XMFLOAT2 prevPos{};
    DirectX::XMFLOAT2 slashDir{};
    bool isSlashMode = false;
    bool isGuard = false;
    bool isCounter = false;
    int counterTimer = kCounterFrames;
    float slashTimer = 0.0f;
};
