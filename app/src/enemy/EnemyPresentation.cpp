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

float GetPhase3PhantomWarpMoveTime(bool finalWarp) {
    return finalWarp ? 0.24f : 0.20f;
}

float GetPhase3PhantomWarpEndTime(bool finalWarp) {
    return finalWarp ? 0.18f : 0.42f;
}

} // namespace

void Enemy::UpdateParts() {
    const float usedYaw = GetVisualYaw();
    float visualYaw = usedYaw;
    float visualPitch = 0.0f;
    float visualRoll = 0.0f;
    const float pulse = 0.5f + 0.5f * std::sin(runtime_.stateTimer * 18.0f);
    const float slowPulse = std::sin(runtime_.stateTimer * 7.0f);
    const float forwardX = std::sin(usedYaw);
    const float forwardZ = std::cos(usedYaw);
    const float rightX = std::cos(usedYaw);
    const float rightZ = -std::sin(usedYaw);
    const bool suppressAttackBodyMotion =
        action_.kind == ActionKind::Smash || action_.kind == ActionKind::Sweep ||
        action_.kind == ActionKind::BladeClash ||
        action_.kind == ActionKind::Wave || action_.kind == ActionKind::Cage;

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
        visualPitch += counterRecoilPitchRad_ * recoil;
    }

    const bool suppressActionPresentation = (counterRecoilTimer_ > 0.0f);
    const bool isTelegraphCharge =
        action_.step == ActionStep::Charge &&
        (action_.kind == ActionKind::Smash || action_.kind == ActionKind::Sweep ||
         action_.kind == ActionKind::BladeClash ||
         action_.kind == ActionKind::Wave || action_.kind == ActionKind::Cage);

    if (tellActive_ || isTelegraphCharge) {
        const float chargePulse = tellActive_ ? (1.0f + 0.55f * pulse)
                                              : (0.72f + 0.42f * pulse);
        const bool isMeleeCharge =
            action_.kind == ActionKind::Smash || action_.kind == ActionKind::Sweep;
        const float bodyChargeScale = isMeleeCharge ? 0.24f : 0.12f;
        const float handChargeScale = isMeleeCharge ? 0.36f : 0.18f;
        bodyTf_.scale.x += bodyChargeScale * chargePulse;
        bodyTf_.scale.z += bodyChargeScale * chargePulse;
        bodyTf_.scale.y -= (isMeleeCharge ? 0.12f : 0.06f) * chargePulse;
        bodyTf_.position.y += (isMeleeCharge ? 0.02f : 0.05f) * chargePulse;
        rightHandTf_.scale.x += handChargeScale * chargePulse;
        rightHandTf_.scale.y += handChargeScale * chargePulse;
        rightHandTf_.scale.z += handChargeScale * chargePulse;
        rightHandTf_.position.y += (isMeleeCharge ? 0.18f : 0.10f) * chargePulse;
        visualTf_.position.y += (isMeleeCharge ? 0.055f : 0.035f) * chargePulse;
        visualTf_.scale.x += (isMeleeCharge ? 0.050f : 0.026f) * chargePulse;
        visualTf_.scale.z += (isMeleeCharge ? 0.050f : 0.026f) * chargePulse;
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

    if (phase_ != BossPhase::Phase1) {
        const bool phase3 = phase_ == BossPhase::Phase3;
        bodyTf_.scale.x += 0.05f;
        bodyTf_.scale.z += 0.05f;
        bodyTf_.position.y += (phase3 ? 0.06f : 0.04f) * pulse;
        leftHandTf_.position.y += phase3 ? 0.07f : 0.04f;
        rightHandTf_.position.y += phase3 ? 0.09f : 0.06f;
        visualTf_.position.y += 0.03f * pulse;
        visualTf_.scale.x += phase3 ? 0.05f : 0.03f;
        visualTf_.scale.z += phase3 ? 0.05f : 0.03f;
    }

    if (phaseTransitionActive_) {
        float t = phaseTransitionDuration_ > 0.0001f
                      ? runtime_.phaseTransitionTimer / phaseTransitionDuration_
                      : 1.0f;
        if (t > 1.0f) {
            t = 1.0f;
        }
        constexpr float kReleaseStart = 0.88f;
        constexpr float kReleaseDuration = 0.05f;
        const float charge = Saturate(t / kReleaseStart);
        const float chargeEase = charge * charge * (3.0f - 2.0f * charge);
        const float release = Saturate((t - kReleaseStart) / kReleaseDuration);
        const float releaseEase = release * release * (3.0f - 2.0f * release);
        const float hold = chargeEase * (1.0f - releaseEase);
        const float snap = std::sin(release * 3.14159265f);
        const float tremble =
            hold * (0.5f + 0.5f * std::sin(runtime_.phaseTransitionTimer * 34.0f));

        bodyTf_.position.y -= 0.26f * hold - 0.10f * releaseEase;
        bodyTf_.scale.x += -0.24f * hold + 0.46f * releaseEase + 0.10f * snap;
        bodyTf_.scale.y += -0.30f * hold + 0.28f * releaseEase;
        bodyTf_.scale.z += -0.24f * hold + 0.46f * releaseEase + 0.10f * snap;

        rightHandTf_.position.y += 0.54f * hold + 0.74f * releaseEase;
        rightHandTf_.position.x += (-rightX) * 0.46f * hold +
                                   rightX * 0.96f * releaseEase;
        rightHandTf_.position.z += (-rightZ) * 0.46f * hold +
                                   rightZ * 0.96f * releaseEase;
        rightHandTf_.position.x += (-forwardX) * 0.12f * hold;
        rightHandTf_.position.z += (-forwardZ) * 0.12f * hold;
        rightHandTf_.scale.x += 0.08f * hold + 0.22f * releaseEase;
        rightHandTf_.scale.y += 0.08f * hold + 0.22f * releaseEase;
        rightHandTf_.scale.z += 0.08f * hold + 0.22f * releaseEase;

        leftHandTf_.position.y += 0.46f * hold + 0.60f * releaseEase;
        leftHandTf_.position.x += rightX * 0.38f * hold +
                                  (-rightX) * 0.78f * releaseEase;
        leftHandTf_.position.z += rightZ * 0.38f * hold +
                                  (-rightZ) * 0.78f * releaseEase;
        leftHandTf_.position.x += (-forwardX) * 0.10f * hold;
        leftHandTf_.position.z += (-forwardZ) * 0.10f * hold;
        leftHandTf_.scale.x += 0.06f * hold + 0.18f * releaseEase;
        leftHandTf_.scale.y += 0.06f * hold + 0.18f * releaseEase;
        leftHandTf_.scale.z += 0.06f * hold + 0.18f * releaseEase;

        visualTf_.position.y += -0.10f * hold + 0.20f * snap;
        visualTf_.position.x += (tremble - 0.5f * hold) * 0.018f;
        visualTf_.scale.x += -0.10f * hold + 0.20f * releaseEase + 0.08f * snap;
        visualTf_.scale.y += -0.14f * hold + 0.18f * releaseEase;
        visualTf_.scale.z += -0.10f * hold + 0.20f * releaseEase + 0.08f * snap;
        visualPitch -= 0.18f * hold - 0.14f * releaseEase;
        visualRoll += 0.08f * tremble + 0.12f * snap;
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
            const bool isDelayBait =
                action_.step == ActionStep::Hold || fakeCommitActive_ ||
                freezeHoldActive_;
            bodyTf_.position.y -= 0.28f + 0.08f * pulse;
            bodyTf_.scale.y += 0.24f;
            bodyTf_.scale.x += 0.16f + 0.06f * pulse;
            bodyTf_.scale.z += 0.16f + 0.06f * pulse;
            rightHandTf_.position.y += 2.75f + 0.28f * pulse;
            rightHandTf_.position.x += (-forwardX) * 1.48f;
            rightHandTf_.position.z += (-forwardZ) * 1.48f;
            rightHandTf_.scale.x += 0.34f + 0.18f * pulse;
            rightHandTf_.scale.y += 0.34f + 0.18f * pulse;
            rightHandTf_.scale.z += 0.34f + 0.18f * pulse;
            leftHandTf_.position.y += 0.36f;
            leftHandTf_.position.x += forwardX * 0.18f;
            leftHandTf_.position.z += forwardZ * 0.18f;
            visualTf_.position.x += (-forwardX) * 0.40f;
            visualTf_.position.z += (-forwardZ) * 0.40f;
            visualTf_.position.y -= 0.12f;
            visualPitch -= 0.62f + 0.12f * pulse;
            if (isDelayBait) {
                bodyTf_.position.y -= 0.16f;
                bodyTf_.position.x += (-forwardX) * (0.26f + 0.10f * pulse);
                bodyTf_.position.z += (-forwardZ) * (0.26f + 0.10f * pulse);
                bodyTf_.scale.x += 0.12f;
                bodyTf_.scale.z += 0.12f;
                rightHandTf_.position.y += 0.62f + 0.16f * pulse;
                rightHandTf_.position.x += (-forwardX) * 0.52f + rightX * 0.18f;
                rightHandTf_.position.z += (-forwardZ) * 0.52f + rightZ * 0.18f;
                leftHandTf_.position.y += 0.22f;
                leftHandTf_.position.x += forwardX * 0.28f;
                leftHandTf_.position.z += forwardZ * 0.28f;
                visualPitch -= 0.22f;
            }
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


            visualTf_.position.x += forwardX * 0.10f;
            visualTf_.position.z += forwardZ * 0.10f;
            visualPitch += 0.10f;
        }
    } else if (!suppressActionPresentation && action_.kind == ActionKind::Sweep) {
        if (action_.step == ActionStep::Charge || action_.step == ActionStep::Hold) {
            const bool isWideTell =
                action_.step == ActionStep::Hold || fakeCommitActive_ ||
                freezeHoldActive_;
            bodyTf_.position.y -= 0.24f + 0.06f * pulse;
            bodyTf_.position.x += rightX * (0.44f + 0.10f * pulse);
            bodyTf_.position.z += rightZ * (0.44f + 0.10f * pulse);
            bodyTf_.scale.x += 0.20f + 0.04f * pulse;
            bodyTf_.scale.z += 0.26f + 0.06f * pulse;
            rightHandTf_.position.x += rightX * (3.20f + 0.34f * pulse);
            rightHandTf_.position.y += 0.72f + 0.18f * pulse;
            rightHandTf_.position.z += rightZ * (3.20f + 0.34f * pulse);
            rightHandTf_.scale.x += 0.42f + 0.22f * pulse;
            rightHandTf_.scale.y += 0.42f + 0.22f * pulse;
            rightHandTf_.scale.z += 0.42f + 0.22f * pulse;
            leftHandTf_.position.x += (-rightX) * 0.82f + (-forwardX) * 0.18f;
            leftHandTf_.position.z += (-rightZ) * 0.82f + (-forwardZ) * 0.18f;
            leftHandTf_.position.y += 0.16f;
            visualTf_.position.x += rightX * 0.18f;
            visualTf_.position.z += rightZ * 0.18f;
            visualYaw += 0.62f + 0.18f * pulse;
            visualRoll -= 0.62f + 0.18f * pulse;
            if (isWideTell) {
                bodyTf_.position.x += rightX * 0.28f;
                bodyTf_.position.z += rightZ * 0.28f;
                bodyTf_.scale.x += 0.10f;
                bodyTf_.scale.z += 0.16f;
                rightHandTf_.position.x += rightX * 0.72f;
                rightHandTf_.position.z += rightZ * 0.72f;
                leftHandTf_.position.x += (-rightX) * 0.34f;
                leftHandTf_.position.z += (-rightZ) * 0.34f;
                visualYaw += 0.20f;
                visualRoll -= 0.18f;
            }
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
    } else if (!suppressActionPresentation &&
               action_.kind == ActionKind::BladeClash) {
        const float bladeClashPulse =
            0.5f + 0.5f * std::sin(bladeClashPresentationTime_ * 48.0f);
        const float bladeClashTremble = (bladeClashPulse - 0.5f) * 2.0f;
        if (action_.step == ActionStep::Charge) {
            bodyTf_.scale.x -= 0.04f;
            bodyTf_.scale.z -= 0.04f;
            bodyTf_.scale.y += 0.06f;
            leftHandTf_.position.y += 0.48f;
            leftHandTf_.position.x += forwardX * 1.02f + (-rightX) * 0.48f;
            leftHandTf_.position.z += forwardZ * 1.02f + (-rightZ) * 0.48f;
            leftHandTf_.scale.x += 0.20f + 0.14f * pulse;
            leftHandTf_.scale.y += 0.20f + 0.14f * pulse;
            leftHandTf_.scale.z += 0.20f + 0.14f * pulse;
            rightHandTf_.position.y += 0.44f;
            rightHandTf_.position.x += forwardX * 0.72f + rightX * 0.52f;
            rightHandTf_.position.z += forwardZ * 0.72f + rightZ * 0.52f;
            rightHandTf_.scale.x += 0.10f + 0.08f * pulse;
            rightHandTf_.scale.y += 0.10f + 0.08f * pulse;
            rightHandTf_.scale.z += 0.10f + 0.08f * pulse;
            visualTf_.position.x += (-forwardX) * 0.10f;
            visualTf_.position.z += (-forwardZ) * 0.10f;
            visualPitch -= 0.12f;
        } else if (action_.step == ActionStep::Active) {
            const float tremble = bladeClashTremble;
            bodyTf_.position.y -= 0.10f;
            bodyTf_.scale.x *= 0.96f;
            bodyTf_.scale.y *= 0.92f;
            bodyTf_.scale.z *= 1.06f;
            rightHandTf_.position.y += 0.20f + 0.012f * bladeClashPulse;
            rightHandTf_.position.x += forwardX * 0.30f + rightX * 0.06f;
            rightHandTf_.position.z += forwardZ * 0.30f + rightZ * 0.06f;
            leftHandTf_.position.y += 0.12f + 0.010f * bladeClashPulse;
            leftHandTf_.position.x += (-rightX) * 0.10f + (-forwardX) * 0.04f;
            leftHandTf_.position.z += (-rightZ) * 0.10f + (-forwardZ) * 0.04f;
            visualTf_.position.x += rightX * 0.018f * tremble;
            visualTf_.position.z += rightZ * 0.018f * tremble;
            visualTf_.position.y -= 0.04f;
            visualYaw += 0.014f * tremble;
            visualRoll += 0.030f * tremble;
            visualTf_.scale.x *= 0.985f;
            visualTf_.scale.y *= 0.970f;
            visualTf_.scale.z *= 1.020f;
        } else if (action_.step == ActionStep::Recovery) {
            leftHandTf_.position.y += 0.16f;
            leftHandTf_.position.x += forwardX * 0.54f;
            leftHandTf_.position.z += forwardZ * 0.54f;
            visualPitch += 0.06f;
        }
    } else if (!suppressActionPresentation && action_.kind == ActionKind::Wave) {
        if (action_.step == ActionStep::Charge) {
            bodyTf_.position.y -= 0.12f + 0.04f * pulse;
            rightHandTf_.position.y += 0.44f;
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
            visualPitch += 0.26f;
            leftHandTf_.position.y += 0.34f;
            leftHandTf_.position.x += (-rightX) * 0.36f;
            leftHandTf_.position.z += (-rightZ) * 0.36f;
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
    } else if (!suppressActionPresentation && action_.kind == ActionKind::Cage) {
        if (action_.step == ActionStep::Charge) {
            bodyTf_.position.y -= 0.10f + 0.05f * pulse;
            bodyTf_.scale.x += 0.08f + 0.08f * pulse;
            bodyTf_.scale.z += 0.08f + 0.08f * pulse;
            leftHandTf_.position.y += 0.86f + 0.18f * pulse;
            leftHandTf_.position.x += (-rightX) * 0.32f + forwardX * 0.28f;
            leftHandTf_.position.z += (-rightZ) * 0.32f + forwardZ * 0.28f;
            rightHandTf_.position.y += 0.94f + 0.20f * pulse;
            rightHandTf_.position.x += rightX * 0.32f + forwardX * 0.28f;
            rightHandTf_.position.z += rightZ * 0.32f + forwardZ * 0.28f;
            leftHandTf_.scale.x += 0.16f + 0.10f * pulse;
            leftHandTf_.scale.y += 0.16f + 0.10f * pulse;
            leftHandTf_.scale.z += 0.16f + 0.10f * pulse;
            rightHandTf_.scale.x += 0.16f + 0.10f * pulse;
            rightHandTf_.scale.y += 0.16f + 0.10f * pulse;
            rightHandTf_.scale.z += 0.16f + 0.10f * pulse;
            visualTf_.position.y += 0.05f * pulse;
            visualPitch += 0.18f;
        } else if (action_.step == ActionStep::Active) {
            leftHandTf_.position.y += 0.70f;
            rightHandTf_.position.y += 0.70f;
            leftHandTf_.position.x += (-rightX) * 0.52f;
            leftHandTf_.position.z += (-rightZ) * 0.52f;
            rightHandTf_.position.x += rightX * 0.52f;
            rightHandTf_.position.z += rightZ * 0.52f;
            bodyTf_.scale.x += 0.10f;
            bodyTf_.scale.z += 0.10f;
            visualTf_.position.y += 0.08f;
        } else if (action_.step == ActionStep::Recovery) {
            leftHandTf_.position.y += 0.22f;
            rightHandTf_.position.y += 0.22f;
            visualPitch += 0.05f;
        }
    } else if (!suppressActionPresentation && action_.kind == ActionKind::Warp) {
        if (action_.step == ActionStep::Start) {
            const float warpStartTime =
                warp_.phase3PhantomChain
                    ? (warp_.phase3PhantomFinal ? 0.13f : 0.11f)
                    : config_.warp.startTime;
            const float startT =
                Saturate(stateTimer_ / (std::max)(warpStartTime, 0.0001f));
            const float vanish = startT * startT;
            const float shimmer =
                std::sin(startT * 12.56637061f) * (1.0f - startT);
            if (warp_.type == WarpType::Approach) {
                if (warp_.approachSlot == WarpApproachSlot::Front) {
                    bodyTf_.position.y += 0.07f;
                    bodyTf_.position.x += (-forwardX) * 0.16f;
                    bodyTf_.position.z += (-forwardZ) * 0.16f;
                    rightHandTf_.position.x += (-forwardX) * 0.24f;
                    rightHandTf_.position.z += (-forwardZ) * 0.24f;
                    visualTf_.position.x += (-forwardX) * (0.10f + 0.16f * vanish);
                    visualTf_.position.z += (-forwardZ) * (0.10f + 0.16f * vanish);
                    visualPitch += 0.10f * vanish;
                } else if (warp_.approachSlot == WarpApproachSlot::Back) {
                    bodyTf_.position.y -= 0.10f;
                    bodyTf_.scale.z += 0.10f;
                    rightHandTf_.position.x += forwardX * 0.28f;
                    rightHandTf_.position.z += forwardZ * 0.28f;
                    visualTf_.position.x += forwardX * (0.12f + 0.20f * vanish);
                    visualTf_.position.z += forwardZ * (0.12f + 0.20f * vanish);
                    visualPitch -= 0.12f * vanish;
                }
            }

            // メルゼナ風：消える前に少し締まる
            bodyTf_.scale.x *= 0.96f;
            bodyTf_.scale.y *= 0.92f;
            bodyTf_.scale.z *= 0.96f;
            visualTf_.scale.x *= 1.0f - 0.22f * vanish;
            visualTf_.scale.y *= 1.0f + 0.16f * vanish;
            visualTf_.scale.z *= 1.0f - 0.42f * vanish;
            visualTf_.position.y += warpArrivalPreviewHeight_ * (0.5f + vanish);
            visualTf_.position.x += rightX * shimmer * 0.08f;
            visualTf_.position.z += rightZ * shimmer * 0.08f;
            visualYaw += 0.35f * slowPulse * vanish + shimmer * 0.38f;
            visualRoll += shimmer * 0.18f;

        } else if (action_.step == ActionStep::Move) {
            const float warpMoveTime =
                warp_.phase3PhantomChain
                    ? GetPhase3PhantomWarpMoveTime(warp_.phase3PhantomFinal)
                    : config_.warp.moveTime;
            const float moveT =
                Saturate(stateTimer_ / (std::max)(warpMoveTime, 0.0001f));
            const float streak = 1.0f - std::abs(moveT * 2.0f - 1.0f);
            // Move中は本体を見せず、出発/到着の残光だけにする
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
            visualTf_.scale.x *= warpMoveGhostScaleX_;
            visualTf_.scale.y *= warpMoveGhostScaleY_;
            visualTf_.scale.z *= warpMoveGhostScaleZ_;
            visualTf_.position.y += warpArrivalPreviewHeight_ + 0.18f * streak;
            visualPitch -= 0.22f * streak;
            visualYaw += 0.85f * slowPulse;

        } else if (action_.step == ActionStep::End) {
            const float warpEndTime =
                warp_.phase3PhantomChain
                    ? GetPhase3PhantomWarpEndTime(warp_.phase3PhantomFinal)
                    : config_.warp.endTime;
            const float warpArrivalTime =
                warp_.phase3PhantomChain && !warp_.phase3PhantomFinal
                    ? 0.14f
                    : warpEndTime;
            const float endT =
                Saturate(stateTimer_ / (std::max)(warpArrivalTime, 0.0001f));
            const float arrival = 1.0f - (1.0f - endT) * (1.0f - endT);
            rightHandTf_.position.y += 0.2f;
            rightHandTf_.position.x += forwardX * 0.3f;
            rightHandTf_.position.z += forwardZ * 0.3f;

            if (warp_.type == WarpType::Approach) {
                if (warp_.approachSlot == WarpApproachSlot::Front) {
                    bodyTf_.position.y += 0.08f;
                    bodyTf_.position.x += (-forwardX) * 0.10f;
                    bodyTf_.position.z += (-forwardZ) * 0.10f;
                    rightHandTf_.position.x += (-forwardX) * 0.25f;
                    rightHandTf_.position.z += (-forwardZ) * 0.25f;
                    visualTf_.position.x += (-forwardX) * warpArrivalEchoOffset_ *
                                            (1.0f - arrival);
                    visualTf_.position.z += (-forwardZ) * warpArrivalEchoOffset_ *
                                            (1.0f - arrival);
                    visualPitch += 0.14f * (1.0f - arrival);
                } else if (warp_.approachSlot == WarpApproachSlot::Back) {
                    bodyTf_.position.y -= 0.08f;
                    bodyTf_.position.x += forwardX * 0.12f;
                    bodyTf_.position.z += forwardZ * 0.12f;
                    rightHandTf_.position.x += forwardX * 0.35f;
                    rightHandTf_.position.z += forwardZ * 0.35f;
                    visualTf_.position.x += forwardX * warpArrivalEchoOffset_ *
                                            (1.0f - arrival);
                    visualTf_.position.z += forwardZ * warpArrivalEchoOffset_ *
                                            (1.0f - arrival);
                    visualPitch -= 0.14f * (1.0f - arrival);
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
            const float arrivalScale =
                1.0f + (warpArrivalPreviewScale_ - 1.0f) * (1.0f - arrival);
            visualTf_.scale.x *= arrivalScale;
            visualTf_.scale.z *= arrivalScale;
            visualTf_.position.y += warpArrivalPreviewHeight_ * (1.0f - arrival);
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
        const bool keepBladeClashPressure =
            action_.kind == ActionKind::BladeClash &&
            action_.step == ActionStep::Active;
        bodyTf_.position.x = tf_.position.x;
        bodyTf_.position.z = tf_.position.z;
        visualYaw = usedYaw;
        if (!keepBladeClashPressure) {
            visualTf_.position.x = tf_.position.x;
            visualTf_.position.z = tf_.position.z;
            visualPitch = 0.0f;
            visualRoll = 0.0f;
        } else {
            visualRoll *= 0.18f;
        }
    }

    visualPitch += cinematicPitch_;
    visualRoll += cinematicRoll_;

    DirectX::XMVECTOR hitboxRot =
        DirectX::XMQuaternionRotationRollPitchYaw(0.0f, usedYaw, 0.0f);
    DirectX::XMStoreFloat4(&bodyTf_.rotation, hitboxRot);
    DirectX::XMStoreFloat4(&leftHandTf_.rotation, hitboxRot);
    DirectX::XMStoreFloat4(&rightHandTf_.rotation, hitboxRot);

    DirectX::XMVECTOR visualRot =
        DirectX::XMQuaternionRotationRollPitchYaw(visualPitch, visualYaw, visualRoll);
    DirectX::XMStoreFloat4(&visualTf_.rotation, visualRot);
}

void Enemy::Draw(ModelManager *modelManager, const Camera &camera,
                 float visualScale) {
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
        phase_ == BossPhase::Phase3
            ? DirectX::XMFLOAT4{0.42f, 0.86f, 0.62f, 0.22f}
            : phase_ == BossPhase::Phase2
                  ? DirectX::XMFLOAT4{0.78f, 0.58f, 0.42f, 0.18f}
                  : DirectX::XMFLOAT4{0.72f, 0.76f, 0.72f, 0.12f};
    DirectX::XMFLOAT4 actionTint = phaseTint;
    float actionIntensity = 0.025f + 0.010f * actionPulse;
    float actionNoise = 0.08f;

    switch (action_.kind) {
    case ActionKind::Smash:
        actionTint = {0.86f, 0.54f, 0.32f, 0.20f};
        actionIntensity = 0.055f + 0.020f * actionPulse;
        actionNoise = 0.10f;
        break;
    case ActionKind::Sweep:
        actionTint = {0.86f, 0.66f, 0.38f, 0.18f};
        actionIntensity = 0.050f + 0.018f * actionPulse;
        actionNoise = 0.10f;
        break;
    case ActionKind::BladeClash:
        actionTint = {0.28f, 1.0f, 0.58f, 0.18f};
        actionIntensity = 0.052f + 0.018f * actionPulse;
        actionNoise = 0.10f;
        break;
    case ActionKind::Wave:
        actionTint = {0.58f, 0.72f, 0.54f, 0.14f};
        actionIntensity = 0.045f + 0.015f * actionPulse;
        actionNoise = 0.08f;
        break;
    case ActionKind::Cage:
        actionTint = {0.42f, 0.96f, 0.88f, 0.18f};
        actionIntensity = 0.052f + 0.020f * actionPulse;
        actionNoise = 0.10f;
        break;
    case ActionKind::Warp:
        actionTint = {0.72f, 0.58f, 0.42f, 0.18f};
        actionIntensity = 0.060f + 0.018f * actionPulse;
        actionNoise = 0.10f;
        break;
    case ActionKind::Stalk:
        actionTint = {0.58f, 0.60f, 0.52f, 0.12f};
        actionIntensity = 0.030f + 0.010f * actionPulse;
        actionNoise = 0.06f;
        break;
    default:
        break;
    }

    const bool isTelegraphCharge =
        action_.step == ActionStep::Charge &&
        (action_.kind == ActionKind::Smash || action_.kind == ActionKind::Sweep ||
         action_.kind == ActionKind::BladeClash ||
         action_.kind == ActionKind::Wave || action_.kind == ActionKind::Cage);
    if (isTelegraphCharge) {
        actionTint = LerpColor(actionTint, {0.82f, 0.72f, 0.42f, 0.22f},
                               0.08f + 0.08f * actionPulse);
        actionIntensity = actionIntensity * 0.55f + 0.012f * actionPulse;
        actionNoise += 0.02f + 0.01f * actionPulse;
    }

    if (IsPunishableRecovery()) {
        const float recoveryPulse =
            0.5f + 0.5f * std::sin(runtime_.stateTimer * 22.0f);
        actionTint = LerpColor(actionTint, {0.86f, 0.74f, 0.34f, 0.28f},
                               0.55f + 0.25f * recoveryPulse);
        actionIntensity += 0.10f + 0.05f * recoveryPulse;
        actionNoise += 0.05f + 0.02f * recoveryPulse;
    }

    if (phaseTransitionActive_) {
        const float phaseRatio = GetPhaseTransitionRatio();
        actionTint = LerpColor(actionTint, {0.86f, 0.58f, 0.32f, 0.28f},
                               phaseRatio);
        actionIntensity += 0.12f * phaseRatio;
        actionNoise += 0.06f * phaseRatio;
    }

    ModelDrawEffect hitEffect{};
    if (isHitFlashing) {
        hitEffect.enabled = false;
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

    const float phase3NonFinalEndVisibleTime = 0.16f;
    const bool showNormalDuringPhase3WarpSettle =
        action_.kind == ActionKind::Warp && action_.step == ActionStep::End &&
        warp_.phase3PhantomChain && !warp_.phase3PhantomFinal &&
        stateTimer_ >= phase3NonFinalEndVisibleTime;

    if (action_.kind == ActionKind::Warp && !showNormalDuringPhase3WarpSettle) {
        warpEffect.enabled = true;
        warpEffect.additiveBlend = true;
        warpEffect.color = actionTint;
        warpEffect.intensity = (action_.step == ActionStep::End) ? 0.86f : 0.58f;
        warpEffect.fresnelPower = 1.9f;
        warpEffect.noiseAmount = (action_.step == ActionStep::Start) ? 0.56f : 0.38f;
        warpEffect.time = stateTimer_;
        if (warp_.phase3PhantomChain) {
            warpEffect.color =
                warp_.phase3PhantomFinal
                    ? DirectX::XMFLOAT4{0.42f, 1.0f, 0.58f, 0.92f}
                    : DirectX::XMFLOAT4{0.64f, 0.74f, 1.0f, 0.82f};
            warpEffect.intensity += warp_.phase3PhantomFinal ? 0.44f : 0.30f;
            warpEffect.noiseAmount += 0.16f;
        }

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
        actionEffect.enabled = false;
        actionEffect.additiveBlend = false;
        actionEffect.color = actionTint;
        actionEffect.intensity = (isTelegraphCharge || action_.step == ActionStep::Active)
                                     ? actionIntensity * 0.32f
                                     : actionIntensity * 0.50f;
        actionEffect.fresnelPower = 2.5f;
        actionEffect.noiseAmount = actionNoise;
        actionEffect.time = runtime_.stateTimer;
        modelManager->SetDrawEffect(actionEffect);
    }

    auto drawEnemyVisual = [&](const Transform &visual) {
        Transform scaledVisual = visual;
        scaledVisual.scale.x *= visualScale;
        scaledVisual.scale.y *= visualScale;
        scaledVisual.scale.z *= visualScale;
        if (visualScale > 1.01f && !isHitFlashing) {
            Transform rimVisual = scaledVisual;
            rimVisual.scale.x *= 1.015f;
            rimVisual.scale.y *= 1.012f;
            rimVisual.scale.z *= 1.015f;
            ModelDrawEffect rimEffect{};
            rimEffect.enabled = true;
            rimEffect.additiveBlend = true;
            rimEffect.disableCulling = true;
            rimEffect.color = {0.72f, 0.50f, 0.24f, 0.14f};
            rimEffect.intensity = 0.12f + 0.04f * actionPulse;
            rimEffect.fresnelPower = 1.35f;
            rimEffect.noiseAmount = 0.04f;
            rimEffect.time = runtime_.stateTimer;
            modelManager->SetDrawEffect(rimEffect);
            modelManager->Draw(modelId_, rimVisual, camera);
            ModelDrawEffect finishEffect{};
            finishEffect.enabled = true;
            finishEffect.color = {0.78f, 0.70f, 0.56f, 0.10f};
            finishEffect.intensity = 0.07f;
            finishEffect.fresnelPower = 1.45f;
            finishEffect.time = runtime_.stateTimer;
            modelManager->SetDrawEffect(finishEffect);
        }
        modelManager->Draw(modelId_, scaledVisual, camera);
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
            trailVisual.scale.x *= trail.scale * 1.02f;
            trailVisual.scale.y *= trail.scale * 1.10f;
            trailVisual.scale.z *= trail.scale * 0.62f;
            trailVisual.position.y += warpArrivalPreviewHeight_;
            drawEnemyVisual(trailVisual);
        }
    }

    modelManager->ClearDrawEffect();

    if (cage_.isActive) {
        const int barCount = (std::max)(6, cage_.barCount);
        const float lifeRatio =
            cage_.maxLifeTime > 0.0001f
                ? Saturate(cage_.lifeTime / cage_.maxLifeTime)
                : 0.0f;
        const float breakRatio =
            cage_.maxBreakValue > 0.0001f
                ? Saturate(cage_.breakValue / cage_.maxBreakValue)
                : 0.0f;
        const float pulseWave =
            0.5f + 0.5f * std::sin(runtime_.stateTimer * 13.5f);
        const float seamPulse =
            0.5f + 0.5f * std::sin(runtime_.stateTimer * 28.0f);
        const float ringY[2] = {0.18f, cage_.height};
        const float segmentLength =
            (2.0f * 3.14159265f * cage_.radius) /
            static_cast<float>(barCount);

        auto drawCagePiece = [&](Transform tf, const DirectX::XMFLOAT4 &color,
                                 float intensity, float noise) {
            ModelDrawEffect cageEffect{};
            cageEffect.enabled = true;
            cageEffect.additiveBlend = true;
            cageEffect.disableCulling = true;
            cageEffect.color = color;
            cageEffect.intensity = intensity;
            cageEffect.fresnelPower = 1.18f;
            cageEffect.noiseAmount = noise;
            cageEffect.time = runtime_.stateTimer;
            modelManager->SetDrawEffect(cageEffect);
            modelManager->Draw(effectModelId, tf, camera);
        };

        for (int i = 0; i < barCount; ++i) {
            const float angle =
                (static_cast<float>(i) / static_cast<float>(barCount)) *
                6.28318530f;
            float angleDiff = NormalizeAngle(angle - cage_.seamAngle);
            const bool isSeamBar = std::fabs(angleDiff) <= cage_.seamWindow;
            const float x = cage_.center.x + std::sin(angle) * cage_.radius;
            const float z = cage_.center.z + std::cos(angle) * cage_.radius;

            Transform barTf = tf_;
            barTf.position = {x, cage_.center.y + cage_.height * 0.50f, z};
            barTf.scale =
                isSeamBar
                    ? DirectX::XMFLOAT3{0.12f, cage_.height * 0.70f, 0.12f}
                    : DirectX::XMFLOAT3{0.075f, cage_.height * 0.58f, 0.075f};
            DirectX::XMStoreFloat4(
                &barTf.rotation,
                DirectX::XMQuaternionRotationRollPitchYaw(0.0f, angle, 0.0f));
            const DirectX::XMFLOAT4 barColor =
                isSeamBar
                    ? DirectX::XMFLOAT4{1.0f, 0.94f, 0.52f,
                                        0.62f + 0.24f * seamPulse}
                    : DirectX::XMFLOAT4{0.42f, 0.96f, 0.88f,
                                        0.30f + 0.18f * pulseWave};
            const float barIntensity =
                (isSeamBar ? 1.12f + 0.38f * seamPulse : 0.54f) +
                (1.0f - breakRatio) * 0.24f;
            drawCagePiece(barTf, barColor, barIntensity, 0.18f);

            for (float y : ringY) {
                Transform ringTf = tf_;
                ringTf.position = {x, cage_.center.y + y, z};
                ringTf.scale =
                    {0.065f, 0.052f, segmentLength * (isSeamBar ? 0.62f : 0.48f)};
                DirectX::XMStoreFloat4(
                    &ringTf.rotation,
                    DirectX::XMQuaternionRotationRollPitchYaw(0.0f,
                                                              angle + 1.57079633f,
                                                              0.0f));
                drawCagePiece(ringTf, barColor,
                              isSeamBar ? 0.84f + 0.22f * seamPulse : 0.42f,
                              0.16f);
            }
        }

        Transform pulseRing = tf_;
        pulseRing.position = cage_.center;
        pulseRing.position.y += 0.08f;
        const float pulseScale =
            cage_.radius * (1.72f + 0.08f * pulseWave) *
            (0.82f + 0.18f * lifeRatio);
        pulseRing.scale = {pulseScale, 0.045f, pulseScale};
        DirectX::XMStoreFloat4(
            &pulseRing.rotation,
            DirectX::XMQuaternionRotationRollPitchYaw(0.0f, runtime_.stateTimer,
                                                      0.0f));
        drawCagePiece(pulseRing, {0.36f, 0.95f, 0.92f, 0.26f},
                      0.34f + 0.24f * pulseWave, 0.20f);
    }

    modelManager->ClearDrawEffect();

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

void Enemy::ApplyVictoryDefeatPose(
    float ratio, const DirectX::XMFLOAT3 &startPosition,
    const DirectX::XMFLOAT3 &playerPosition) {
    ratio = (std::clamp)(ratio, 0.0f, 1.0f);
    const int frame =
        (std::min)(3, static_cast<int>(std::floor(ratio * 4.0f)));

    float awayX = startPosition.x - playerPosition.x;
    float awayZ = startPosition.z - playerPosition.z;
    float awayLen = std::sqrt(awayX * awayX + awayZ * awayZ);
    if (awayLen < 0.0001f) {
        awayLen = 1.0f;
        awayZ = 1.0f;
    }
    awayX /= awayLen;
    awayZ /= awayLen;
    const float rightX = awayZ;
    const float rightZ = -awayX;

    float backDistance = 0.0f;
    float lift = 0.0f;
    float lateral = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
    float fallPose = 0.0f;
    switch (frame) {
    case 0:
        backDistance = 0.08f;
        lift = 0.16f;
        lateral = 0.00f;
        pitch = -0.08f;
        roll = 0.00f;
        fallPose = 0.00f;
        break;
    case 1:
        backDistance = 1.30f;
        lift = 1.10f;
        lateral = 0.24f;
        pitch = -0.58f;
        roll = 0.22f;
        fallPose = 0.34f;
        break;
    case 2:
        backDistance = 2.75f;
        lift = 0.48f;
        lateral = -0.18f;
        pitch = -1.15f;
        roll = -0.16f;
        fallPose = 0.70f;
        break;
    default:
        backDistance = 3.55f;
        lift = -0.30f;
        lateral = 0.04f;
        pitch = -1.68f;
        roll = 0.03f;
        fallPose = 1.00f;
        break;
    }

    tf_.position = startPosition;
    tf_.position.x += awayX * backDistance + rightX * lateral;
    tf_.position.z += awayZ * backDistance + rightZ * lateral;
    tf_.position.y += lift;
    tf_.scale = {1.0f + 0.05f * fallPose, 1.0f - 0.18f * fallPose,
                 1.0f + 0.10f * fallPose};

    const float yaw = std::atan2(-awayX, -awayZ);
    DirectX::XMStoreFloat4(
        &tf_.rotation,
        DirectX::XMQuaternionRotationRollPitchYaw(pitch, yaw, roll));

    bodyTf_ = tf_;
    visualTf_ = tf_;
    leftHandTf_ = tf_;
    rightHandTf_ = tf_;

    bodyTf_.position.y -= 0.18f * fallPose;
    visualTf_.position.y += 0.12f * (1.0f - fallPose);
    leftHandTf_.position.x += (-rightX) * (1.0f + 0.85f * fallPose) -
                              awayX * (0.18f + 0.50f * fallPose);
    leftHandTf_.position.z += (-rightZ) * (1.0f + 0.85f * fallPose) -
                              awayZ * (0.18f + 0.50f * fallPose);
    leftHandTf_.position.y += 0.58f - 0.72f * fallPose;
    rightHandTf_.position.x += rightX * (1.15f + 0.95f * fallPose) +
                               awayX * (0.10f + 0.38f * fallPose);
    rightHandTf_.position.z += rightZ * (1.15f + 0.95f * fallPose) +
                               awayZ * (0.10f + 0.38f * fallPose);
    rightHandTf_.position.y += 0.84f - 0.80f * fallPose;
}

