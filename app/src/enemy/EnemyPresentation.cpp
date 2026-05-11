#include "Enemy.h"
#include "ModelManager.h"

#include <algorithm>
#include <cmath>

namespace {
float Saturate(float value) {
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

DirectX::XMFLOAT4 LerpColor(const DirectX::XMFLOAT4 &from,
                            const DirectX::XMFLOAT4 &to, float t) {
    t = Saturate(t);
    return {from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t,
            from.z + (to.z - from.z) * t, from.w + (to.w - from.w) * t};
}

} // namespace

void Enemy::UpdateParts() {
    const float usedYaw = GetVisualYaw();
    float visualYaw = usedYaw;
    float visualPitch = 0.0f;
    float visualRoll = 0.0f;
    const float pulse = 0.5f + 0.5f * std::sin(runtime_.stateTimer * 18.0f);
    const float phasePulse =
        0.5f + 0.5f * std::sin(runtime_.phaseTransitionTimer * 16.0f);
    const bool isDelaySmashWhiffPunish =
        action_.kind == ActionKind::Smash &&
        action_.step == ActionStep::Recovery &&
        action_.id == ActionId::DelaySmash && !currentActionConnected_ &&
        !currentActionGuarded_;

    const float forwardX = std::sin(usedYaw);
    const float forwardZ = std::cos(usedYaw);
    const float rightX = std::cos(usedYaw);
    const float rightZ = -std::sin(usedYaw);
    const bool suppressAttackBodyMotion =
        action_.kind == ActionKind::Smash || action_.kind == ActionKind::Sweep ||
        action_.kind == ActionKind::Shot || action_.kind == ActionKind::Wave ||
        action_.kind == ActionKind::Nova;

    bodyTf_ = tf_;
    bodyTf_.position = tf_.position;
    bodyTf_.scale = {1.2f, 1.4f, 0.8f};

    visualTf_ = tf_;
    visualTf_.position = tf_.position;
    visualTf_.scale = {1.0f, 1.0f, 1.0f};

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
        const float hitT = hitReactionTimer_ / hitReactionDuration_;
        bodyTf_.scale.x += 0.18f * hitT;
        bodyTf_.scale.y -= 0.10f * hitT;
        bodyTf_.scale.z += 0.18f * hitT;
        bodyTf_.position.y += 0.06f * hitT;
        leftHandTf_.position.y += 0.10f * hitT;
        rightHandTf_.position.y += 0.10f * hitT;
        visualTf_.position.y += 0.04f * hitT;
        visualPitch -= 0.12f * hitT;
    }

    if (counterRecoilTimer_ > 0.0f) {
        float recoilProgress = counterRecoilDuration_ > 0.0001f
                                   ? 1.0f - (counterRecoilTimer_ / counterRecoilDuration_)
                                   : 1.0f;
        recoilProgress = Saturate(recoilProgress);
        const float recoil = recoilProgress * recoilProgress;
        bodyTf_.position.y += 0.015f * recoil;
        visualTf_.position.x += (-forwardX) * 0.045f * recoil;
        visualTf_.position.z += (-forwardZ) * 0.045f * recoil;
        visualPitch += counterRecoilPitchRad_ * recoil;
    }

    const bool suppressActionPresentation = (counterRecoilTimer_ > 0.0f);
    const bool isTelegraphCharge =
        action_.step == ActionStep::Charge &&
        (action_.kind == ActionKind::Smash || action_.kind == ActionKind::Sweep ||
         action_.kind == ActionKind::Shot || action_.kind == ActionKind::Wave ||
         action_.kind == ActionKind::Nova);

    if (tellActive_ || isTelegraphCharge) {
        const float chargePulse = tellActive_ ? (1.0f + 0.55f * pulse)
                                              : (0.72f + 0.42f * pulse);
        bodyTf_.scale.x += 0.12f * chargePulse;
        bodyTf_.scale.z += 0.12f * chargePulse;
        bodyTf_.scale.y -= 0.06f * chargePulse;
        bodyTf_.position.y += 0.05f * chargePulse;
        rightHandTf_.scale.x += 0.18f * chargePulse;
        rightHandTf_.scale.y += 0.18f * chargePulse;
        rightHandTf_.scale.z += 0.18f * chargePulse;
        rightHandTf_.position.y += 0.10f * chargePulse;
        visualTf_.position.y += 0.035f * chargePulse;
        visualTf_.scale.x += 0.026f * chargePulse;
        visualTf_.scale.z += 0.026f * chargePulse;
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
        visualTf_.position.y -= 0.03f;
        visualPitch -= 0.08f;
    }

    if (phase_ == BossPhase::Phase2) {
        bodyTf_.scale.x += 0.05f;
        bodyTf_.scale.z += 0.05f;
        bodyTf_.position.y += 0.04f * pulse;
        leftHandTf_.position.y += 0.04f;
        rightHandTf_.position.y += 0.06f;
        visualTf_.position.y += 0.03f * pulse;
        visualTf_.scale.x += 0.03f;
        visualTf_.scale.z += 0.03f;
    }

    if (phaseTransitionActive_) {
        float t = phaseTransitionDuration_ > 0.0001f
                      ? runtime_.phaseTransitionTimer / phaseTransitionDuration_
                      : 1.0f;
        if (t > 1.0f) {
            t = 1.0f;
        }

        bodyTf_.position.y -= 0.18f + 0.06f * phasePulse;
        bodyTf_.scale.x += 0.18f + 0.10f * phasePulse;
        bodyTf_.scale.y -= 0.14f * (1.0f - t * 0.35f);
        bodyTf_.scale.z += 0.18f + 0.10f * phasePulse;

        rightHandTf_.position.y += 0.95f + 0.18f * phasePulse;
        rightHandTf_.position.x += rightX * 0.55f + (-forwardX) * 0.18f;
        rightHandTf_.position.z += rightZ * 0.55f + (-forwardZ) * 0.18f;
        rightHandTf_.scale.x += 0.18f + 0.08f * phasePulse;
        rightHandTf_.scale.y += 0.18f + 0.08f * phasePulse;
        rightHandTf_.scale.z += 0.18f + 0.08f * phasePulse;

        leftHandTf_.position.y += 0.68f + 0.12f * phasePulse;
        leftHandTf_.position.x += (-rightX) * 0.42f + forwardX * 0.12f;
        leftHandTf_.position.z += (-rightZ) * 0.42f + forwardZ * 0.12f;
        leftHandTf_.scale.x += 0.12f;
        leftHandTf_.scale.y += 0.12f;
        leftHandTf_.scale.z += 0.12f;

        visualTf_.position.y += 0.08f * phasePulse;
        visualTf_.scale.x += 0.06f + 0.04f * phasePulse;
        visualTf_.scale.y += 0.03f;
        visualTf_.scale.z += 0.06f + 0.04f * phasePulse;
        visualPitch -= 0.12f;
        visualRoll += 0.10f * phasePulse;
    }

    if (!suppressActionPresentation && action_.kind == ActionKind::None &&
        runtime_.isMargitComboATransition) {
        bodyTf_.position.y -= 0.08f;
        bodyTf_.position.x += rightX * 0.22f;
        bodyTf_.position.z += rightZ * 0.22f;
        bodyTf_.scale.x += 0.12f;
        bodyTf_.scale.z += 0.06f;

        rightHandTf_.position.y += 1.10f + 0.12f * pulse;
        rightHandTf_.position.x += rightX * 1.65f + (-forwardX) * 0.20f;
        rightHandTf_.position.z += rightZ * 1.65f + (-forwardZ) * 0.20f;
        rightHandTf_.scale.x += 0.12f;
        rightHandTf_.scale.y += 0.12f;
        rightHandTf_.scale.z += 0.12f;

        leftHandTf_.position.x += (-rightX) * 0.60f + forwardX * 0.12f;
        leftHandTf_.position.z += (-rightZ) * 0.60f + forwardZ * 0.12f;
        leftHandTf_.position.y += 0.14f;
    }

    if (!suppressActionPresentation && action_.kind == ActionKind::Smash) {
        if (action_.step == ActionStep::Charge || action_.step == ActionStep::Hold) {
            bodyTf_.position.y -= 0.10f;
            bodyTf_.scale.y += 0.08f;
            rightHandTf_.position.y += 1.90f;
            rightHandTf_.position.x += (-forwardX) * 0.90f;
            rightHandTf_.position.z += (-forwardZ) * 0.90f;
            rightHandTf_.scale.x += 0.08f;
            rightHandTf_.scale.y += 0.08f;
            rightHandTf_.scale.z += 0.08f;
            leftHandTf_.position.y += 0.15f;
            visualTf_.position.x += (-forwardX) * 0.18f;
            visualTf_.position.z += (-forwardZ) * 0.18f;
            visualTf_.position.y -= 0.04f;
            visualPitch -= 0.28f;
        } else if (action_.step == ActionStep::Active) {
            bodyTf_.position.x += forwardX * 0.18f;
            bodyTf_.position.z += forwardZ * 0.18f;
            bodyTf_.position.y -= 0.05f;
            rightHandTf_.position.y -= 0.45f;
            rightHandTf_.position.x += forwardX * 2.10f;
            rightHandTf_.position.z += forwardZ * 2.10f;
            visualTf_.position.x += forwardX * 0.42f;
            visualTf_.position.z += forwardZ * 0.42f;
            visualTf_.position.y += 0.06f;
            visualPitch += 0.34f;
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

            visualTf_.position.x += forwardX * 0.10f;
            visualTf_.position.z += forwardZ * 0.10f;
            visualPitch += 0.10f;
        }
    } else if (!suppressActionPresentation && action_.kind == ActionKind::Sweep) {
        if (action_.step == ActionStep::Charge || action_.step == ActionStep::Hold) {
            bodyTf_.position.x += rightX * 0.12f;
            bodyTf_.position.z += rightZ * 0.12f;
            rightHandTf_.position.x += rightX * 2.00f;
            rightHandTf_.position.y += 0.25f;
            rightHandTf_.position.z += rightZ * 2.00f;
            leftHandTf_.position.x += (-rightX) * 0.25f;
            leftHandTf_.position.z += (-rightZ) * 0.25f;
            visualYaw += 0.18f;
            visualRoll -= 0.18f;
        } else if (action_.step == ActionStep::Active) {
            bodyTf_.position.x += (-rightX) * 0.18f;
            bodyTf_.position.z += (-rightZ) * 0.18f;
            rightHandTf_.position.x += (-rightX) * 2.00f;
            rightHandTf_.position.y += 0.2f;
            rightHandTf_.position.z += (-rightZ) * 2.00f;
            visualYaw -= 0.26f;
            visualRoll += 0.26f;
            visualTf_.position.x += (-rightX) * 0.20f;
            visualTf_.position.z += (-rightZ) * 0.20f;
        } else if (action_.step == ActionStep::Recovery) {
            rightHandTf_.position.x += rightX * 0.3f;
            rightHandTf_.position.y += 0.1f;
            rightHandTf_.position.z += rightZ * 0.3f;
            visualRoll += 0.10f;
        }
    } else if (!suppressActionPresentation && action_.kind == ActionKind::Shot) {
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
            visualTf_.position.x += (-forwardX) * 0.10f;
            visualTf_.position.z += (-forwardZ) * 0.10f;
            visualPitch -= 0.12f;
        } else if (action_.step == ActionStep::Active) {
            rightHandTf_.position.y += 0.3f;
            rightHandTf_.position.x += forwardX * 1.0f;
            rightHandTf_.position.z += forwardZ * 1.0f;
            visualTf_.position.x += forwardX * 0.12f;
            visualTf_.position.z += forwardZ * 0.12f;
        } else if (action_.step == ActionStep::Recovery) {
            rightHandTf_.position.y += 0.2f;
            rightHandTf_.position.x += forwardX * 0.4f;
            rightHandTf_.position.z += forwardZ * 0.4f;
            visualPitch += 0.06f;
        }
    } else if (!suppressActionPresentation && action_.kind == ActionKind::Wave) {
        if (action_.step == ActionStep::Charge) {
            rightHandTf_.position.y += 0.8f;
            rightHandTf_.position.x += forwardX * 0.6f;
            rightHandTf_.position.z += forwardZ * 0.6f;
            bodyTf_.scale.x += 0.06f + 0.06f * pulse;
            bodyTf_.scale.z += 0.06f + 0.06f * pulse;
            rightHandTf_.scale.x += 0.10f + 0.06f * pulse;
            rightHandTf_.scale.y += 0.10f + 0.06f * pulse;
            rightHandTf_.scale.z += 0.10f + 0.06f * pulse;
            visualTf_.position.y += 0.04f * pulse;
            visualTf_.scale.x += 0.02f * pulse;
            visualTf_.scale.z += 0.02f * pulse;
            visualPitch -= 0.10f;
        } else if (action_.step == ActionStep::Active) {
            rightHandTf_.position.y += 0.4f;
            rightHandTf_.position.x += forwardX * 1.0f;
            rightHandTf_.position.z += forwardZ * 1.0f;
            visualTf_.position.x += forwardX * 0.14f;
            visualTf_.position.z += forwardZ * 0.14f;
        } else if (action_.step == ActionStep::Recovery) {
            rightHandTf_.position.y += 0.2f;
            rightHandTf_.position.x += forwardX * 0.4f;
            rightHandTf_.position.z += forwardZ * 0.4f;
            visualPitch += 0.06f;
        }
    } else if (!suppressActionPresentation && action_.kind == ActionKind::Nova) {
        const float novaPulse = 0.5f + 0.5f * std::sin(runtime_.stateTimer * 28.0f);
        if (action_.step == ActionStep::Charge) {
            bodyTf_.position.y -= 0.14f;
            bodyTf_.scale.x += 0.18f + 0.12f * novaPulse;
            bodyTf_.scale.z += 0.18f + 0.12f * novaPulse;
            bodyTf_.scale.y -= 0.08f;
            rightHandTf_.position.y += 1.25f + 0.16f * novaPulse;
            leftHandTf_.position.y += 1.00f + 0.12f * novaPulse;
            rightHandTf_.position.x += rightX * 0.50f + (-forwardX) * 0.26f;
            rightHandTf_.position.z += rightZ * 0.50f + (-forwardZ) * 0.26f;
            leftHandTf_.position.x += (-rightX) * 0.50f + (-forwardX) * 0.18f;
            leftHandTf_.position.z += (-rightZ) * 0.50f + (-forwardZ) * 0.18f;
            visualTf_.position.y += 0.08f * novaPulse;
            visualPitch -= 0.18f;
            visualRoll += 0.14f * novaPulse;
        } else if (action_.step == ActionStep::Active) {
            const float impactTime = config_.attacks.nova.impactTime;
            const float riseT =
                impactTime > 0.0001f
                    ? std::clamp(runtime_.stateTimer / impactTime, 0.0f, 1.0f)
                    : 1.0f;
            const float afterImpactT =
                std::clamp((runtime_.stateTimer - impactTime) / 0.22f, 0.0f, 1.0f);
            const float jumpHeight =
                runtime_.stateTimer < impactTime
                    ? std::sin(riseT * 1.57079633f) * 1.95f
                    : (1.0f - afterImpactT) * 1.95f;
            const float impactSquash = 1.0f - afterImpactT;
            visualTf_.position.y += jumpHeight;
            bodyTf_.position.y += jumpHeight * 0.35f - 0.14f * impactSquash;
            bodyTf_.scale.x += 0.34f + 0.28f * impactSquash;
            bodyTf_.scale.z += 0.34f + 0.28f * impactSquash;
            bodyTf_.scale.y -= 0.08f * impactSquash;
            rightHandTf_.position.y += 1.70f + jumpHeight * 0.35f;
            leftHandTf_.position.y += 1.52f + jumpHeight * 0.35f;
            rightHandTf_.position.x += rightX * 0.95f + forwardX * 0.24f;
            rightHandTf_.position.z += rightZ * 0.95f + forwardZ * 0.24f;
            leftHandTf_.position.x += (-rightX) * 0.95f + forwardX * 0.24f;
            leftHandTf_.position.z += (-rightZ) * 0.95f + forwardZ * 0.24f;
            visualTf_.scale.x += 0.08f * novaPulse + 0.10f * impactSquash;
            visualTf_.scale.z += 0.08f * novaPulse + 0.10f * impactSquash;
            visualPitch += 0.20f - 0.28f * riseT;
        } else if (action_.step == ActionStep::Recovery) {
            bodyTf_.position.y -= 0.06f;
            rightHandTf_.position.y += 0.36f;
            leftHandTf_.position.y += 0.30f;
            visualPitch += 0.08f;
        }
    } else if (!suppressActionPresentation && action_.kind == ActionKind::Warp) {
        if (action_.step == ActionStep::Start) {
            if (warp_.type == WarpType::Approach) {
                if (warp_.approachSlot == WarpApproachSlot::BackLeft) {
                    bodyTf_.position.x += (-rightX) * 0.16f;
                    bodyTf_.position.z += (-rightZ) * 0.16f;
                    rightHandTf_.position.x += (-rightX) * 0.32f;
                    rightHandTf_.position.z += (-rightZ) * 0.32f;
                } else if (warp_.approachSlot == WarpApproachSlot::BackRight) {
                    bodyTf_.position.x += rightX * 0.16f;
                    bodyTf_.position.z += rightZ * 0.16f;
                    rightHandTf_.position.x += rightX * 0.32f;
                    rightHandTf_.position.z += rightZ * 0.32f;
                } else if (warp_.approachSlot == WarpApproachSlot::DirectBack) {
                    bodyTf_.position.y -= 0.10f;
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
                if (warp_.approachSlot == WarpApproachSlot::BackLeft) {
                    bodyTf_.position.x += (-rightX) * 0.18f;
                    bodyTf_.position.z += (-rightZ) * 0.18f;
                    rightHandTf_.position.x += (-rightX) * 0.28f;
                    rightHandTf_.position.z += (-rightZ) * 0.28f;
                } else if (warp_.approachSlot == WarpApproachSlot::BackRight) {
                    bodyTf_.position.x += rightX * 0.18f;
                    bodyTf_.position.z += rightZ * 0.18f;
                    rightHandTf_.position.x += rightX * 0.28f;
                    rightHandTf_.position.z += rightZ * 0.28f;
                } else if (warp_.approachSlot == WarpApproachSlot::DirectBack) {
                    bodyTf_.position.y -= 0.08f;
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
    } else if (!suppressActionPresentation && action_.kind == ActionKind::Stalk) {
        rightHandTf_.position.y += 0.35f;
        leftHandTf_.position.y += 0.20f;
        rightHandTf_.position.x += forwardX * 0.35f;
        rightHandTf_.position.z += forwardZ * 0.35f;
        visualTf_.position.x += forwardX * 0.06f;
        visualTf_.position.z += forwardZ * 0.06f;
        visualRoll += 0.04f * pulse;
    }

    if (suppressAttackBodyMotion) {
        bodyTf_.position.x = tf_.position.x;
        bodyTf_.position.z = tf_.position.z;
        visualTf_.position.x = tf_.position.x;
        visualTf_.position.z = tf_.position.z;
        visualYaw = usedYaw;
        visualPitch = 0.0f;
        visualRoll = 0.0f;
    }

    DirectX::XMVECTOR hitboxRot =
        DirectX::XMQuaternionRotationRollPitchYaw(0.0f, usedYaw, 0.0f);
    DirectX::XMStoreFloat4(&bodyTf_.rotation, hitboxRot);
    DirectX::XMStoreFloat4(&leftHandTf_.rotation, hitboxRot);
    DirectX::XMStoreFloat4(&rightHandTf_.rotation, hitboxRot);

    DirectX::XMVECTOR visualRot =
        DirectX::XMQuaternionRotationRollPitchYaw(visualPitch, visualYaw, visualRoll);
    DirectX::XMStoreFloat4(&visualTf_.rotation, visualRot);
}

void Enemy::Draw(ModelManager *modelManager, const Camera &camera) {
    if (runtime_.deathFinished) {
        return;
    }

    float hitFlash = 0.0f;
    if (hitReactionDuration_ > 0.0001f) {
        hitFlash =
            std::clamp(runtime_.hitReactionTimer / hitReactionDuration_, 0.0f, 1.0f);
    }
    const bool isHitFlashing = hitFlash > 0.0f;
    const float actionPulse =
        0.5f + 0.5f * std::sin(runtime_.stateTimer * 12.0f);
    const DirectX::XMFLOAT4 phaseTint =
        phase_ == BossPhase::Phase2
            ? DirectX::XMFLOAT4{1.0f, 0.42f, 0.16f, 0.52f}
            : DirectX::XMFLOAT4{0.72f, 0.48f, 0.30f, 0.34f};
    DirectX::XMFLOAT4 actionTint = phaseTint;
    float actionIntensity = 0.10f + 0.035f * actionPulse;
    float actionNoise = 0.28f;

    switch (action_.kind) {
    case ActionKind::Smash:
        actionTint = {1.0f, 0.36f, 0.10f, 0.58f};
        actionIntensity = 0.32f + 0.12f * actionPulse;
        actionNoise = 0.38f;
        break;
    case ActionKind::Sweep:
        actionTint = {1.0f, 0.62f, 0.18f, 0.54f};
        actionIntensity = 0.24f + 0.10f * actionPulse;
        actionNoise = 0.34f;
        break;
    case ActionKind::Shot:
        actionTint = {0.68f, 0.78f, 0.78f, 0.46f};
        actionIntensity = 0.20f + 0.10f * actionPulse;
        actionNoise = 0.30f;
        break;
    case ActionKind::Wave:
        actionTint = {0.54f, 0.78f, 0.50f, 0.44f};
        actionIntensity = 0.22f + 0.10f * actionPulse;
        actionNoise = 0.34f;
        break;
    case ActionKind::Nova:
        actionTint = {1.0f, 0.28f, 0.06f, 0.82f};
        actionIntensity = 0.58f + 0.18f * actionPulse;
        actionNoise = 0.42f;
        break;
    case ActionKind::Warp:
        actionTint = {0.90f, 0.48f, 0.20f, 0.70f};
        actionIntensity = 0.42f + 0.12f * actionPulse;
        actionNoise = 0.36f;
        break;
    case ActionKind::Stalk:
        actionTint = {0.52f, 0.56f, 0.42f, 0.38f};
        actionIntensity = 0.14f + 0.04f * actionPulse;
        actionNoise = 0.24f;
        break;
    default:
        break;
    }

    const bool isTelegraphCharge =
        action_.step == ActionStep::Charge &&
        (action_.kind == ActionKind::Smash || action_.kind == ActionKind::Sweep ||
         action_.kind == ActionKind::Shot || action_.kind == ActionKind::Wave ||
         action_.kind == ActionKind::Nova);
    if (isTelegraphCharge) {
        actionTint = LerpColor(actionTint, {1.0f, 0.82f, 0.24f, 0.86f},
                               0.12f + 0.12f * actionPulse);
        actionIntensity = actionIntensity * 0.42f + 0.05f * actionPulse;
        actionNoise += 0.06f + 0.04f * actionPulse;
    }

    if (IsPunishableRecovery()) {
        const float recoveryPulse =
            0.5f + 0.5f * std::sin(runtime_.stateTimer * 22.0f);
        actionTint = LerpColor(actionTint, {1.0f, 0.92f, 0.24f, 0.88f},
                               0.55f + 0.25f * recoveryPulse);
        actionIntensity += 0.44f + 0.30f * recoveryPulse;
        actionNoise += 0.18f + 0.10f * recoveryPulse;
    }

    if (phaseTransitionActive_) {
        const float phaseRatio = GetPhaseTransitionRatio();
        actionTint = LerpColor(actionTint, {1.0f, 0.48f, 0.12f, 0.76f},
                               phaseRatio);
        actionIntensity += 0.46f * phaseRatio;
        actionNoise += 0.28f * phaseRatio;
    }

    ModelDrawEffect hitEffect{};
    if (isHitFlashing) {
        hitEffect.enabled = true;
        hitEffect.additiveBlend = false;
        hitEffect.color = LerpColor({1.0f, 0.30f, 0.08f, 0.78f},
                                    {1.0f, 0.78f, 0.28f, 0.86f},
                                    actionPulse);
        hitEffect.intensity = 0.42f + 0.72f * hitFlash;
        hitEffect.fresnelPower = 1.8f;
        hitEffect.noiseAmount = 0.30f;
        hitEffect.time = runtime_.stateTimer;
    }

    const uint32_t effectModelId =
        (projectileModelId_ != 0) ? projectileModelId_ : modelId_;
    const bool isWarpMoveHidden =
        action_.kind == ActionKind::Warp && action_.step == ActionStep::Move;
    ModelDrawEffect warpEffect{};

    if (action_.kind == ActionKind::Warp) {
        warpEffect.enabled = true;
        warpEffect.additiveBlend = true;
        warpEffect.color = actionTint;
        warpEffect.intensity = (action_.step == ActionStep::Move) ? 0.92f : 0.62f;
        warpEffect.fresnelPower = 1.9f;
        warpEffect.noiseAmount = 0.42f;
        warpEffect.time = stateTimer_;

        if (isHitFlashing) {
            warpEffect.color = LerpColor(warpEffect.color,
                                         {1.0f, 0.72f, 0.20f, 0.88f},
                                         hitFlash);
            warpEffect.intensity += 0.38f * hitFlash;
            warpEffect.noiseAmount += 0.10f * hitFlash;
        }

        modelManager->SetDrawEffect(warpEffect);
    } else if (isHitFlashing) {
        modelManager->SetDrawEffect(hitEffect);
    } else {
        ModelDrawEffect actionEffect{};
        actionEffect.enabled = true;
        actionEffect.additiveBlend = false;
        actionEffect.color = actionTint;
        actionEffect.intensity = (isTelegraphCharge || action_.step == ActionStep::Active)
                                     ? actionIntensity * 0.52f
                                     : actionIntensity;
        actionEffect.fresnelPower = 2.5f;
        actionEffect.noiseAmount = actionNoise;
        actionEffect.time = runtime_.stateTimer;
        modelManager->SetDrawEffect(actionEffect);
    }

    auto drawEnemyVisual = [&](const Transform &visual) {
        modelManager->Draw(modelId_, visual, camera);
    };

    if (isVisible_ && !isWarpMoveHidden) {
        drawEnemyVisual(visualTf_);
    }

    if (action_.kind == ActionKind::Warp) {
        for (const auto &trail : warpTrailGhosts_) {
            if (!trail.isActive || trail.life <= 0.0f) {
                continue;
            }

            Transform trailVisual = visualTf_;
            trailVisual.position = trail.position;
            trailVisual.scale.x *= 0.90f;
            trailVisual.scale.y *= 0.95f;
            trailVisual.scale.z *= 0.60f;
            drawEnemyVisual(trailVisual);
        }
    }

    modelManager->ClearDrawEffect();

    for (const auto &bullet : bullets_) {
        if (!bullet.isAlive) {
            continue;
        }

        Transform bulletTf = tf_;
        bulletTf.position = bullet.position;
        bulletTf.scale = {0.2f, 0.2f, 0.2f};
        ModelDrawEffect bulletEffect{};
        bulletEffect.enabled = true;
        bulletEffect.additiveBlend = true;
        bulletEffect.color =
            bullet.isReflected ? DirectX::XMFLOAT4{1.0f, 0.95f, 0.20f, 0.90f}
                               : DirectX::XMFLOAT4{0.74f, 0.84f, 0.78f, 0.82f};
        bulletEffect.intensity = bullet.isReflected ? 1.15f : 0.82f;
        bulletEffect.fresnelPower = 1.4f;
        bulletEffect.noiseAmount = 0.26f;
        bulletEffect.time = stateTimer_ + bullet.lifeTime;
        modelManager->SetDrawEffect(bulletEffect);
        modelManager->Draw(effectModelId, bulletTf, camera);
    }

    for (const auto &wave : waves_) {
        if (!wave.isAlive) {
            continue;
        }

        Transform waveTf = tf_;
        waveTf.position = wave.position;
        waveTf.scale = {0.6f, 0.2f, 1.2f};
        const float waveYaw = std::atan2(wave.direction.x, wave.direction.z);
        DirectX::XMStoreFloat4(
            &waveTf.rotation,
            DirectX::XMQuaternionRotationRollPitchYaw(0.0f, waveYaw, 0.0f));
        ModelDrawEffect waveEffect{};
        waveEffect.enabled = true;
        waveEffect.additiveBlend = true;
        waveEffect.color =
            wave.isReflected ? DirectX::XMFLOAT4{1.0f, 0.58f, 0.18f, 0.90f}
                             : DirectX::XMFLOAT4{0.54f, 0.82f, 0.48f, 0.82f};
        waveEffect.intensity = wave.isReflected ? 1.12f : 0.88f;
        waveEffect.fresnelPower = 1.2f;
        waveEffect.noiseAmount = 0.32f;
        waveEffect.time = stateTimer_ + wave.traveledDistance;
        modelManager->SetDrawEffect(waveEffect);
        modelManager->Draw(effectModelId, waveTf, camera);
    }

    modelManager->ClearDrawEffect();
}

