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
    float pulse = 0.5f + 0.5f * std::sinf(stateTimer_ * 18.0f);
    float phasePulse = 0.5f + 0.5f * std::sinf(phaseTransitionTimer_ * 16.0f);
    const bool isDelaySmashWhiffPunish =
        (action_.kind == ActionKind::Smash &&
         action_.step == ActionStep::Recovery &&
         action_.id == ActionId::DelaySmash && !currentActionConnected_ &&
         !currentActionGuarded_);
    const bool isRushWhiffPunish =
        (action_.kind == ActionKind::Rush &&
         action_.step == ActionStep::Recovery && !rushWillSweepFollowup_ &&
         !currentActionConnected_ && !currentActionGuarded_);

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

    if (hitReactionTimer_ > 0.0f) {
        float hitT = hitReactionTimer_ / hitReactionDuration_;
        bodyTf_.scale.x += 0.18f * hitT;
        bodyTf_.scale.y -= 0.10f * hitT;
        bodyTf_.scale.z += 0.18f * hitT;
        bodyTf_.position.y += 0.06f * hitT;

        leftHandTf_.position.y += 0.10f * hitT;
        rightHandTf_.position.y += 0.10f * hitT;
    }

    if (tellActive_) {
        bodyTf_.scale.x += 0.10f * pulse;
        bodyTf_.scale.z += 0.10f * pulse;
        bodyTf_.scale.y -= 0.06f * pulse;
        bodyTf_.position.y += 0.05f * pulse;

        rightHandTf_.scale.x += 0.12f * pulse;
        rightHandTf_.scale.y += 0.12f * pulse;
        rightHandTf_.scale.z += 0.12f * pulse;
    }

    if (fakeCommitActive_) {
        rightHandTf_.position.y += 0.28f * pulse;
        rightHandTf_.position.x += forwardX * 0.24f * pulse;
        rightHandTf_.position.z += forwardZ * 0.24f * pulse;
        rightHandTf_.scale.x += 0.10f * pulse;
        rightHandTf_.scale.y += 0.10f * pulse;
        rightHandTf_.scale.z += 0.10f * pulse;
    }

    if (freezeHoldActive_) {
        bodyTf_.position.y -= 0.05f;
        bodyTf_.scale.x += 0.06f;
        bodyTf_.scale.z += 0.06f;
        rightHandTf_.scale.x += 0.12f;
        rightHandTf_.scale.y += 0.12f;
        rightHandTf_.scale.z += 0.12f;
    }

    if (phase_ == BossPhase::Phase2) {
        bodyTf_.scale.x += 0.05f;
        bodyTf_.scale.z += 0.05f;
        bodyTf_.position.y += 0.04f * pulse;
        leftHandTf_.position.y += 0.04f;
        rightHandTf_.position.y += 0.06f;
    }

    if (phaseTransitionActive_) {
        float t = phaseTransitionDuration_ > 0.0001f
                      ? phaseTransitionTimer_ / phaseTransitionDuration_
                      : 1.0f;
        if (t > 1.0f) {
            t = 1.0f;
        }

        bodyTf_.position.y -= 0.18f + 0.06f * phasePulse;
        bodyTf_.scale.x += 0.18f + 0.10f * phasePulse;
        bodyTf_.scale.y -= 0.14f * (1.0f - t * 0.35f);
        bodyTf_.scale.z += 0.18f + 0.10f * phasePulse;

        rightHandTf_.position.y += 0.95f + 0.18f * phasePulse;
        rightHandTf_.position.x += rightX * 0.55f;
        rightHandTf_.position.z += rightZ * 0.55f;
        rightHandTf_.position.x += (-forwardX) * 0.18f;
        rightHandTf_.position.z += (-forwardZ) * 0.18f;
        rightHandTf_.scale.x += 0.18f + 0.08f * phasePulse;
        rightHandTf_.scale.y += 0.18f + 0.08f * phasePulse;
        rightHandTf_.scale.z += 0.18f + 0.08f * phasePulse;

        leftHandTf_.position.y += 0.68f + 0.12f * phasePulse;
        leftHandTf_.position.x += (-rightX) * 0.42f;
        leftHandTf_.position.z += (-rightZ) * 0.42f;
        leftHandTf_.position.x += forwardX * 0.12f;
        leftHandTf_.position.z += forwardZ * 0.12f;
        leftHandTf_.scale.x += 0.12f;
        leftHandTf_.scale.y += 0.12f;
        leftHandTf_.scale.z += 0.12f;
    }

    if (action_.kind == ActionKind::None && isMargitComboATransition_) {
        bodyTf_.position.y -= 0.08f;
        bodyTf_.position.x += rightX * 0.22f;
        bodyTf_.position.z += rightZ * 0.22f;
        bodyTf_.scale.x += 0.12f;
        bodyTf_.scale.z += 0.06f;

        rightHandTf_.position.y += 1.10f + 0.12f * pulse;
        rightHandTf_.position.x += rightX * 1.65f;
        rightHandTf_.position.z += rightZ * 1.65f;
        rightHandTf_.position.x += (-forwardX) * 0.20f;
        rightHandTf_.position.z += (-forwardZ) * 0.20f;
        rightHandTf_.scale.x += 0.12f;
        rightHandTf_.scale.y += 0.12f;
        rightHandTf_.scale.z += 0.12f;

        leftHandTf_.position.x += (-rightX) * 0.60f;
        leftHandTf_.position.z += (-rightZ) * 0.60f;
        leftHandTf_.position.x += forwardX * 0.12f;
        leftHandTf_.position.z += forwardZ * 0.12f;
        leftHandTf_.position.y += 0.14f;
    }

    if (action_.kind == ActionKind::None && isMargitComboBTransition_) {
        bodyTf_.position.y -= 0.12f;
        bodyTf_.position.x += forwardX * 0.10f;
        bodyTf_.position.z += forwardZ * 0.10f;
        bodyTf_.scale.y -= 0.08f;
        bodyTf_.scale.z += 0.10f;

        rightHandTf_.position.y += 0.32f + 0.08f * pulse;
        rightHandTf_.position.x += forwardX * 1.10f;
        rightHandTf_.position.z += forwardZ * 1.10f;
        rightHandTf_.position.x += rightX * 0.18f;
        rightHandTf_.position.z += rightZ * 0.18f;
        rightHandTf_.scale.x += 0.10f;
        rightHandTf_.scale.y += 0.10f;
        rightHandTf_.scale.z += 0.10f;

        leftHandTf_.position.y -= 0.08f;
        leftHandTf_.position.x += (-rightX) * 0.30f;
        leftHandTf_.position.z += (-rightZ) * 0.30f;
        leftHandTf_.position.x += (-forwardX) * 0.10f;
        leftHandTf_.position.z += (-forwardZ) * 0.10f;
    }

    if (action_.kind == ActionKind::Smash) {
        if (action_.step == ActionStep::Charge ||
            action_.step == ActionStep::Hold) {
            bodyTf_.position.y -= 0.10f;
            bodyTf_.scale.y += 0.08f;

            rightHandTf_.position.y += 1.90f;
            rightHandTf_.position.x += (-forwardX) * 0.90f;
            rightHandTf_.position.z += (-forwardZ) * 0.90f;
            rightHandTf_.scale.x += 0.08f;
            rightHandTf_.scale.y += 0.08f;
            rightHandTf_.scale.z += 0.08f;

            leftHandTf_.position.y += 0.15f;
        } else if (action_.step == ActionStep::Active) {
            bodyTf_.position.x += forwardX * 0.18f;
            bodyTf_.position.z += forwardZ * 0.18f;
            bodyTf_.position.y -= 0.05f;

            rightHandTf_.position.y -= 0.45f;
            rightHandTf_.position.x += forwardX * 2.10f;
            rightHandTf_.position.z += forwardZ * 2.10f;
        } else if (action_.step == ActionStep::Recovery) {
            rightHandTf_.position.y += 0.3f;
            rightHandTf_.position.x += forwardX * 0.8f;
            rightHandTf_.position.z += forwardZ * 0.8f;

            if (isDelaySmashWhiffPunish) {
                bodyTf_.position.y -= 0.14f;
                bodyTf_.position.x += forwardX * 0.10f;
                bodyTf_.position.z += forwardZ * 0.10f;
                bodyTf_.scale.y -= 0.10f;
                bodyTf_.scale.x += 0.08f;

                rightHandTf_.position.y -= 0.30f;
                rightHandTf_.position.x += forwardX * 0.40f;
                rightHandTf_.position.z += forwardZ * 0.40f;
                leftHandTf_.position.y -= 0.18f;
                leftHandTf_.position.x += (-rightX) * 0.22f;
                leftHandTf_.position.z += (-rightZ) * 0.22f;
            }
        }

    } else if (action_.kind == ActionKind::Sweep) {
        if (action_.step == ActionStep::Charge ||
            action_.step == ActionStep::Hold) {
            bodyTf_.position.x += rightX * 0.12f;
            bodyTf_.position.z += rightZ * 0.12f;

            rightHandTf_.position.x += rightX * 2.00f;
            rightHandTf_.position.y += 0.25f;
            rightHandTf_.position.z += rightZ * 2.00f;

            leftHandTf_.position.x += (-rightX) * 0.25f;
            leftHandTf_.position.z += (-rightZ) * 0.25f;
        } else if (action_.step == ActionStep::Active) {
            bodyTf_.position.x += (-rightX) * 0.18f;
            bodyTf_.position.z += (-rightZ) * 0.18f;

            rightHandTf_.position.x += (-rightX) * 2.00f;
            rightHandTf_.position.y += 0.2f;
            rightHandTf_.position.z += (-rightZ) * 2.00f;
        } else if (action_.step == ActionStep::Recovery) {
            rightHandTf_.position.x += rightX * 0.3f;
            rightHandTf_.position.y += 0.1f;
            rightHandTf_.position.z += rightZ * 0.3f;
        }

    } else if (action_.kind == ActionKind::Shot) {
        if (action_.step == ActionStep::Charge) {
            bodyTf_.scale.x -= 0.04f;
            bodyTf_.scale.z -= 0.04f;
            bodyTf_.scale.y += 0.06f;

            rightHandTf_.position.y += 0.45f;
            rightHandTf_.position.x += forwardX * 1.30f;
            rightHandTf_.position.z += forwardZ * 1.30f;
            rightHandTf_.scale.x += 0.08f + 0.08f * pulse;
            rightHandTf_.scale.y += 0.08f + 0.08f * pulse;
            rightHandTf_.scale.z += 0.08f + 0.08f * pulse;

            leftHandTf_.position.x += (-rightX) * 0.35f;
            leftHandTf_.position.z += (-rightZ) * 0.35f;
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
            bodyTf_.scale.x += 0.06f + 0.06f * pulse;
            bodyTf_.scale.z += 0.06f + 0.06f * pulse;
            rightHandTf_.scale.x += 0.10f + 0.06f * pulse;
            rightHandTf_.scale.y += 0.10f + 0.06f * pulse;
            rightHandTf_.scale.z += 0.10f + 0.06f * pulse;
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
            bodyTf_.position.y -= 0.18f;
            bodyTf_.scale.y -= 0.10f;
            bodyTf_.scale.z += 0.12f;
            bodyTf_.position.x += forwardX * 0.08f;
            bodyTf_.position.z += forwardZ * 0.08f;

            if (rushFromShotCombo_) {
                bodyTf_.position.y -= 0.04f;
                bodyTf_.position.x += forwardX * 0.08f;
                bodyTf_.position.z += forwardZ * 0.08f;
                rightHandTf_.position.x += rightX * 0.22f;
                rightHandTf_.position.z += rightZ * 0.22f;
            }

            leftHandTf_.position.y -= 0.10f;
            rightHandTf_.position.y -= 0.05f;

            leftHandTf_.position.x += forwardX * 0.35f;
            leftHandTf_.position.z += forwardZ * 0.35f;
            rightHandTf_.position.x += forwardX * 0.55f;
            rightHandTf_.position.z += forwardZ * 0.55f;
        } else if (action_.step == ActionStep::Active) {
            bodyTf_.position.x += forwardX * 0.25f;
            bodyTf_.position.z += forwardZ * 0.25f;
            bodyTf_.position.y += 0.05f;

            rightHandTf_.position.y += 0.1f;
            rightHandTf_.position.x += forwardX * 1.6f;
            rightHandTf_.position.z += forwardZ * 1.6f;
        } else if (action_.step == ActionStep::Recovery) {
            rightHandTf_.position.y += 0.2f;
            rightHandTf_.position.x += forwardX * 0.6f;
            rightHandTf_.position.z += forwardZ * 0.6f;

            if (isRushWhiffPunish) {
                bodyTf_.position.y -= 0.12f;
                bodyTf_.scale.y -= 0.08f;
                bodyTf_.scale.x += 0.10f;
                bodyTf_.scale.z += 0.06f;

                rightHandTf_.position.y -= 0.18f;
                rightHandTf_.position.x += forwardX * 0.18f;
                rightHandTf_.position.z += forwardZ * 0.18f;
                leftHandTf_.position.y -= 0.22f;
                leftHandTf_.position.x += (-rightX) * 0.28f;
                leftHandTf_.position.z += (-rightZ) * 0.28f;
            }
        }

    } else if (action_.kind == ActionKind::Warp) {
        if (action_.step == ActionStep::Start) {
            if (warp_.type == WarpType::Approach) {
                if (warp_.approachSlot == WarpApproachSlot::FrontLeft) {
                    bodyTf_.position.x += (-rightX) * 0.16f;
                    bodyTf_.position.z += (-rightZ) * 0.16f;
                    rightHandTf_.position.x += (-rightX) * 0.32f;
                    rightHandTf_.position.z += (-rightZ) * 0.32f;
                } else if (warp_.approachSlot == WarpApproachSlot::FrontRight) {
                    bodyTf_.position.x += rightX * 0.16f;
                    bodyTf_.position.z += rightZ * 0.16f;
                    rightHandTf_.position.x += rightX * 0.32f;
                    rightHandTf_.position.z += rightZ * 0.32f;
                } else if (warp_.approachSlot == WarpApproachSlot::LongFront) {
                    bodyTf_.position.y -= 0.02f;
                    bodyTf_.scale.z += 0.10f;
                    rightHandTf_.position.x += forwardX * 0.28f;
                    rightHandTf_.position.z += forwardZ * 0.28f;
                }
            }

            // メルゼナ風：消える前に少し締まる
            bodyTf_.scale.x *= 0.96f;
            bodyTf_.scale.y *= 0.92f;
            bodyTf_.scale.z *= 0.96f;

        } else if (action_.step == ActionStep::Move) {
            // メルゼナ風：Move中は本体感を落として影が滑る感じ
            bodyTf_.scale.x *= 0.88f;
            bodyTf_.scale.y *= 0.80f;
            bodyTf_.scale.z *= 0.88f;

            leftHandTf_.scale.x *= 0.82f;
            leftHandTf_.scale.y *= 0.76f;
            leftHandTf_.scale.z *= 0.82f;

            rightHandTf_.scale.x *= 0.82f;
            rightHandTf_.scale.y *= 0.76f;
            rightHandTf_.scale.z *= 0.82f;

            bodyTf_.position.y -= 0.01f;

        } else if (action_.step == ActionStep::End) {
            rightHandTf_.position.y += 0.2f;
            rightHandTf_.position.x += forwardX * 0.3f;
            rightHandTf_.position.z += forwardZ * 0.3f;

            if (warp_.type == WarpType::Approach) {
                if (warp_.approachSlot == WarpApproachSlot::FrontLeft) {
                    bodyTf_.position.x += (-rightX) * 0.18f;
                    bodyTf_.position.z += (-rightZ) * 0.18f;
                    rightHandTf_.position.x += (-rightX) * 0.28f;
                    rightHandTf_.position.z += (-rightZ) * 0.28f;
                } else if (warp_.approachSlot == WarpApproachSlot::FrontRight) {
                    bodyTf_.position.x += rightX * 0.18f;
                    bodyTf_.position.z += rightZ * 0.18f;
                    rightHandTf_.position.x += rightX * 0.28f;
                    rightHandTf_.position.z += rightZ * 0.28f;
                } else if (warp_.approachSlot == WarpApproachSlot::LongFront) {
                    bodyTf_.position.y -= 0.01f;
                    bodyTf_.position.x += forwardX * 0.12f;
                    bodyTf_.position.z += forwardZ * 0.12f;
                    rightHandTf_.position.x += forwardX * 0.35f;
                    rightHandTf_.position.z += forwardZ * 0.35f;
                }
            }

            // メルゼナ風：arrivalで本体が少し戻ってくる
            bodyTf_.scale.x *= 1.03f;
            bodyTf_.scale.y *= 1.02f;
            bodyTf_.scale.z *= 1.03f;

            leftHandTf_.scale.x *= 1.02f;
            leftHandTf_.scale.y *= 1.01f;
            leftHandTf_.scale.z *= 1.02f;

            rightHandTf_.scale.x *= 1.02f;
            rightHandTf_.scale.y *= 1.01f;
            rightHandTf_.scale.z *= 1.02f;
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
    } else if (action_.kind == ActionKind::Stalk) {
        rightHandTf_.position.y += 0.35f;
        leftHandTf_.position.y += 0.20f;

        rightHandTf_.position.x += forwardX * 0.35f;
        rightHandTf_.position.z += forwardZ * 0.35f;
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

void Enemy::UpdatePresentationEvents() {
    ActionStep currentWarpStep = ActionStep::None;
    if (action_.kind == ActionKind::Warp) {
        currentWarpStep = action_.step;
    }

    if (currentWarpStep == ActionStep::Start &&
        prevPresentationWarpStep_ != ActionStep::Start) {
        EnemyElectricRingSpawnRequest req{};
        req.worldPos =
            warp_.hasDeparturePos ? warp_.departurePos : tf_.position;
        req.isWarpEnd = false;
        electricRingSpawnRequests_.push_back(req);
    }

    if (currentWarpStep == ActionStep::End &&
        prevPresentationWarpStep_ != ActionStep::End) {
        EnemyElectricRingSpawnRequest req{};
        req.worldPos = warp_.targetPos;
        req.isWarpEnd = true;
        electricRingSpawnRequests_.push_back(req);
    }

    prevPresentationWarpStep_ = currentWarpStep;
}

std::vector<EnemyElectricRingSpawnRequest>
Enemy::ConsumeElectricRingSpawnRequests() {
    std::vector<EnemyElectricRingSpawnRequest> out = electricRingSpawnRequests_;
    electricRingSpawnRequests_.clear();
    return out;
}
