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

float EaseOutCubic(float t) {
    float u = 1.0f - Saturate(t);
    return 1.0f - u * u * u;
}

float EaseOutBack(float t) {
    t = Saturate(t);
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.0f;
    float u = t - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
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

    if (introActive_) {
        const float introPulse = std::sin(GetIntroRatio() * 3.14159265f);
        const IntroPhase introPhase = GetIntroPhase();

        if (introPhase == IntroPhase::SecondSlash) {
            float t = introSecondSlashDuration_ > 0.0001f
                          ? runtime_.introTimer / introSecondSlashDuration_
                          : 1.0f;
            t = Saturate(t);
            const float slashT = EaseOutCubic(t);

            bodyTf_.position.y -= 0.05f * (1.0f - slashT);
            bodyTf_.position.x += forwardX * 0.06f * slashT;
            bodyTf_.position.z += forwardZ * 0.06f * slashT;
            bodyTf_.scale.x += 0.10f * (1.0f - t);
            bodyTf_.scale.z += 0.10f * (1.0f - t);

            rightHandTf_.position.y += 0.72f * (1.0f - t);
            rightHandTf_.position.x +=
                rightX * 0.22f + (-forwardX) * 0.24f * (1.0f - t);
            rightHandTf_.position.z +=
                rightZ * 0.22f + (-forwardZ) * 0.24f * (1.0f - t);
            leftHandTf_.position.y += 0.18f * (1.0f - t);
            leftHandTf_.position.x += (-rightX) * 0.12f;
            leftHandTf_.position.z += (-rightZ) * 0.12f;

            visualTf_.position.x += forwardX * (introSlashLunge_ * 0.92f) * slashT;
            visualTf_.position.z += forwardZ * (introSlashLunge_ * 0.92f) * slashT;
            visualPitch -= 0.14f * (1.0f - t);
            visualRoll -= 0.06f * (1.0f - t);
        } else if (introPhase == IntroPhase::SpinSlash) {
            const float phaseTime = runtime_.introTimer - introSecondSlashDuration_;
            float t = introSpinSlashDuration_ > 0.0001f
                          ? phaseTime / introSpinSlashDuration_
                          : 1.0f;
            t = Saturate(t);
            const float spinT = EaseOutBack(t);
            const float spinYaw = 6.28318530f * introSpinTurns_ * spinT;

            bodyTf_.position.y += introSpinLift_ * std::sin(t * 3.14159265f);
            bodyTf_.scale.x += 0.08f * (1.0f - t);
            bodyTf_.scale.z += 0.12f * (1.0f - t);

            rightHandTf_.position.y += 0.45f + 0.22f * introPulse;
            rightHandTf_.position.x += rightX * 0.32f;
            rightHandTf_.position.z += rightZ * 0.32f;
            leftHandTf_.position.y += 0.18f;

            visualTf_.position.x += forwardX * introSpinLunge_ * t;
            visualTf_.position.z += forwardZ * introSpinLunge_ * t;
            visualTf_.position.y +=
                introSpinLift_ * 0.55f * std::sin(t * 3.14159265f);
            visualTf_.scale.x += introVisualScaleBoost_ * (1.0f - t);
            visualTf_.scale.z += introVisualScaleBoost_ * (1.0f - t);
            visualYaw += spinYaw;
            visualPitch -= 0.10f * (1.0f - t);
            visualRoll += 0.16f * std::sin(t * 6.28318530f);
        } else {
            const float phaseTime =
                runtime_.introTimer -
                (introSecondSlashDuration_ + introSpinSlashDuration_);
            float t = introSettleDuration_ > 0.0001f
                          ? phaseTime / introSettleDuration_
                          : 1.0f;
            t = Saturate(t);
            const float settle = 1.0f - t;

            bodyTf_.position.y -= 0.02f;
            bodyTf_.position.x += forwardX * 0.10f;
            bodyTf_.position.z += forwardZ * 0.10f;
            bodyTf_.scale.x += introImpactSquash_ * 0.30f;
            bodyTf_.scale.y -= introImpactSquash_ * 0.34f;
            bodyTf_.scale.z += introImpactSquash_ * 0.30f;

            rightHandTf_.position.y += 0.08f;
            rightHandTf_.position.x += forwardX * 0.40f + rightX * 0.10f;
            rightHandTf_.position.z += forwardZ * 0.40f + rightZ * 0.10f;
            leftHandTf_.position.y += 0.04f;

            visualTf_.position.x += forwardX * 0.16f;
            visualTf_.position.z += forwardZ * 0.16f;
            visualTf_.position.y += 0.02f;
            visualPitch += 0.16f;
            visualRoll -= 0.04f * settle;
        }
    }

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

    if (tellActive_) {
        bodyTf_.scale.x += 0.10f * pulse;
        bodyTf_.scale.z += 0.10f * pulse;
        bodyTf_.scale.y -= 0.06f * pulse;
        bodyTf_.position.y += 0.05f * pulse;
        rightHandTf_.scale.x += 0.12f * pulse;
        rightHandTf_.scale.y += 0.12f * pulse;
        rightHandTf_.scale.z += 0.12f * pulse;
        visualTf_.position.y += 0.03f * pulse;
        visualTf_.scale.x += 0.02f * pulse;
        visualTf_.scale.z += 0.02f * pulse;
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

    ModelDrawEffect hitEffect{};
    if (isHitFlashing) {
        hitEffect.enabled = true;
        hitEffect.additiveBlend = false;
        hitEffect.color = {1.00f, 0.22f, 0.22f, 0.92f};
        hitEffect.intensity = 0.75f + 0.85f * hitFlash;
        hitEffect.fresnelPower = 2.6f;
        hitEffect.noiseAmount = 0.10f;
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
        warpEffect.color = {0.70f, 0.04f, 0.22f, 0.82f};
        warpEffect.intensity = (action_.step == ActionStep::Move) ? 2.05f : 1.70f;
        warpEffect.fresnelPower = 4.6f;
        warpEffect.noiseAmount = 0.88f;
        warpEffect.time = stateTimer_;

        if (isHitFlashing) {
            warpEffect.color = {1.00f, 0.18f, 0.28f, 0.92f};
            warpEffect.intensity += 0.75f * hitFlash;
            warpEffect.noiseAmount += 0.10f * hitFlash;
        }

        modelManager->SetDrawEffect(warpEffect);
    } else if (isHitFlashing) {
        modelManager->SetDrawEffect(hitEffect);
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

    if (action_.kind == ActionKind::Warp || isHitFlashing) {
        modelManager->ClearDrawEffect();
    }

    for (const auto &bullet : bullets_) {
        if (!bullet.isAlive) {
            continue;
        }

        Transform bulletTf = tf_;
        bulletTf.position = bullet.position;
        bulletTf.scale = {0.2f, 0.2f, 0.2f};
        modelManager->Draw(effectModelId, bulletTf, camera);
    }

    for (const auto &wave : waves_) {
        if (!wave.isAlive) {
            continue;
        }

        Transform waveTf = tf_;
        waveTf.position = wave.position;
        waveTf.scale = {0.6f, 0.2f, 1.2f};
        modelManager->Draw(effectModelId, waveTf, camera);
    }
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
