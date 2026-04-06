#include "Enemy.h"
#include "ModelManager.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

// ============================================================
// 各部位Transform更新処理
// ============================================================
void Enemy::UpdateParts() {
    float usedYaw = GetVisualYaw();

    float forwardX = std::sinf(usedYaw);
    float forwardZ = std::cosf(usedYaw);

    float rightX = std::cosf(usedYaw);
    float rightZ = -std::sinf(usedYaw);

    bodyTf_ = tf_;
    bodyTf_.position = tf_.position;
    bodyTf_.scale = {1.2f, 1.4f, 0.8f};

    leftHandTf_ = tf_;
    leftHandTf_.position = tf_.position;
    leftHandTf_.position.x += (-rightX) * 1.2f;
    leftHandTf_.position.y += 0.9f;
    leftHandTf_.position.z += (-rightZ) * 1.2f;
    leftHandTf_.scale = {0.6f, 0.6f, 0.6f};

    rightHandTf_ = tf_;
    rightHandTf_.position = tf_.position;
    rightHandTf_.position.x += rightX * 1.2f;
    rightHandTf_.position.y += 0.9f;
    rightHandTf_.position.z += rightZ * 1.2f;
    rightHandTf_.scale = {0.6f, 0.6f, 0.6f};

    if (action_.kind == ActionKind::Smash) {
        if (action_.step == ActionStep::Charge ||
            action_.step == ActionStep::Hold) {
            rightHandTf_.position.y += 1.5f;
            rightHandTf_.position.x += (-forwardX) * 0.5f;
            rightHandTf_.position.z += (-forwardZ) * 0.5f;
        } else if (action_.step == ActionStep::Active) {
            rightHandTf_.position.y -= 0.2f;
            rightHandTf_.position.x += forwardX * 1.8f;
            rightHandTf_.position.z += forwardZ * 1.8f;
        } else if (action_.step == ActionStep::Recovery) {
            rightHandTf_.position.y += 0.3f;
            rightHandTf_.position.x += forwardX * 0.8f;
            rightHandTf_.position.z += forwardZ * 0.8f;
        }

    } else if (action_.kind == ActionKind::Sweep) {
        if (action_.step == ActionStep::Charge ||
            action_.step == ActionStep::Hold) {
            rightHandTf_.position.x += rightX * 1.4f;
            rightHandTf_.position.y += 0.4f;
            rightHandTf_.position.z += rightZ * 1.4f;
        } else if (action_.step == ActionStep::Active) {
            rightHandTf_.position.x += (-rightX) * 1.6f;
            rightHandTf_.position.y += 0.2f;
            rightHandTf_.position.z += (-rightZ) * 1.6f;
        } else if (action_.step == ActionStep::Recovery) {
            rightHandTf_.position.x += rightX * 0.3f;
            rightHandTf_.position.y += 0.1f;
            rightHandTf_.position.z += rightZ * 0.3f;
        }

    } else if (action_.kind == ActionKind::Shot) {
        if (action_.step == ActionStep::Charge) {
            rightHandTf_.position.y += 0.5f;
            rightHandTf_.position.x += forwardX * 0.8f;
            rightHandTf_.position.z += forwardZ * 0.8f;
        } else if (action_.step == ActionStep::Active) {
            rightHandTf_.position.y += 0.3f;
            rightHandTf_.position.x += forwardX * 1.0f;
            rightHandTf_.position.z += forwardZ * 1.0f;
        } else if (action_.step == ActionStep::Recovery) {
            rightHandTf_.position.y += 0.2f;
            rightHandTf_.position.x += forwardX * 0.4f;
            rightHandTf_.position.z += forwardZ * 0.4f;
        }

    } else if (action_.kind == ActionKind::Wave) {
        if (action_.step == ActionStep::Charge) {
            rightHandTf_.position.y += 0.8f;
            rightHandTf_.position.x += forwardX * 0.6f;
            rightHandTf_.position.z += forwardZ * 0.6f;
        } else if (action_.step == ActionStep::Active) {
            rightHandTf_.position.y += 0.4f;
            rightHandTf_.position.x += forwardX * 1.0f;
            rightHandTf_.position.z += forwardZ * 1.0f;
        } else if (action_.step == ActionStep::Recovery) {
            rightHandTf_.position.y += 0.2f;
            rightHandTf_.position.x += forwardX * 0.4f;
            rightHandTf_.position.z += forwardZ * 0.4f;
        }

    } else if (action_.kind == ActionKind::Rush) {
        if (action_.step == ActionStep::Charge) {
            rightHandTf_.position.y += 0.4f;
            rightHandTf_.position.x += forwardX * 0.8f;
            rightHandTf_.position.z += forwardZ * 0.8f;
        } else if (action_.step == ActionStep::Active) {
            rightHandTf_.position.y += 0.1f;
            rightHandTf_.position.x += forwardX * 1.6f;
            rightHandTf_.position.z += forwardZ * 1.6f;
        } else if (action_.step == ActionStep::Recovery) {
            rightHandTf_.position.y += 0.2f;
            rightHandTf_.position.x += forwardX * 0.6f;
            rightHandTf_.position.z += forwardZ * 0.6f;
        }

    } else if (action_.kind == ActionKind::Warp) {
        if (action_.step == ActionStep::End) {
            rightHandTf_.position.y += 0.2f;
            rightHandTf_.position.x += forwardX * 0.3f;
            rightHandTf_.position.z += forwardZ * 0.3f;
        }

    } else if (action_.kind == ActionKind::Guard) {
        if (guardTarget_ == GuardTarget::Face) {
            leftHandTf_.position.x += forwardX * 0.6f;
            leftHandTf_.position.y += 0.9f;
            leftHandTf_.position.z += forwardZ * 0.6f;

        } else if (guardTarget_ == GuardTarget::BodyLeft) {
            leftHandTf_.position.x += (-rightX) * 0.35f;
            leftHandTf_.position.y += 0.3f;
            leftHandTf_.position.z += (-rightZ) * 0.35f;

        } else if (guardTarget_ == GuardTarget::BodyRight) {
            leftHandTf_.position.x += rightX * 0.35f;
            leftHandTf_.position.y += 0.3f;
            leftHandTf_.position.z += rightZ * 0.35f;
        }
    }
}

// ============================================================
// 向き更新処理
// ============================================================
void Enemy::UpdateFacingToPlayer() {
    float dx = playerPos_.x - tf_.position.x;
    float dz = playerPos_.z - tf_.position.z;

    facingYaw_ = std::atan2f(dx, dz);
}

void Enemy::LockCurrentFacing() { lockedAttackYaw_ = facingYaw_; }

float Enemy::NormalizeAngle(float angle) const {
    while (angle > 3.14159265f) {
        angle -= 6.28318530f;
    }
    while (angle < -3.14159265f) {
        angle += 6.28318530f;
    }
    return angle;
}

void Enemy::UpdateFacingToPlayerWithSpeed(float deltaTime, float turnSpeed) {
    float dx = playerPos_.x - tf_.position.x;
    float dz = playerPos_.z - tf_.position.z;

    float targetYaw = std::atan2f(dx, dz);
    float diff = NormalizeAngle(targetYaw - facingYaw_);

    float maxStep = turnSpeed * deltaTime;

    if (diff > maxStep) {
        diff = maxStep;
    } else if (diff < -maxStep) {
        diff = -maxStep;
    }

    facingYaw_ = NormalizeAngle(facingYaw_ + diff);
}

float Enemy::GetVisualYaw() const {
    if (action_.kind == ActionKind::Rush) {
        return rushCurrentYaw_;
    }

    if (ShouldUseLockedAttackYaw()) {
        return lockedAttackYaw_;
    }
    return facingYaw_;
}