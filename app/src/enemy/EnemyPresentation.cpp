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
    const float forwardX = std::sin(usedYaw);
    const float forwardZ = std::cos(usedYaw);
    const float rightX = std::cos(usedYaw);
    const float rightZ = -std::sin(usedYaw);
    const bool suppressAttackBodyMotion =
        action_.kind == ActionKind::Smash || action_.kind == ActionKind::Sweep ||
        action_.kind == ActionKind::BladeClash ||
        action_.kind == ActionKind::ArcaneLaser ||
        action_.kind == ActionKind::CataclysmLaser;

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
        (action_.kind == ActionKind::Smash || action_.kind == ActionKind::Sweep);
    const bool isFarSlashFlashHold =
        farSlashActive_ && action_.step == ActionStep::Active &&
        runtime_.stateTimer <= GetFarWarpSlashStanceHoldDuration();
    const bool isFarSlashPostPierceSlash =
        farSlashActive_ && action_.step == ActionStep::Recovery;

    if (tellActive_ || isTelegraphCharge || isFarSlashFlashHold) {
        const float chargePulse = tellActive_ ? (1.0f + 0.55f * pulse)
                                              : (0.72f + 0.42f * pulse);
        constexpr float bodyChargeScale = 0.24f;
        constexpr float handChargeScale = 0.36f;
        bodyTf_.scale.x += bodyChargeScale * chargePulse;
        bodyTf_.scale.z += bodyChargeScale * chargePulse;
        bodyTf_.scale.y -= 0.12f * chargePulse;
        bodyTf_.position.y += 0.02f * chargePulse;
        rightHandTf_.scale.x += handChargeScale * chargePulse;
        rightHandTf_.scale.y += handChargeScale * chargePulse;
        rightHandTf_.scale.z += handChargeScale * chargePulse;
        rightHandTf_.position.y += 0.18f * chargePulse;
        visualTf_.position.y += 0.055f * chargePulse;
        visualTf_.scale.x += 0.050f * chargePulse;
        visualTf_.scale.z += 0.050f * chargePulse;
    }

    if (!suppressActionPresentation && farSlashActive_ &&
        (isTelegraphCharge || isFarSlashFlashHold)) {
        const float chargeTime =
            action_.kind == ActionKind::Smash ? GetCurrentSmashChargeTime()
                                              : GetCurrentSweepChargeTime();
        const float chargeT =
            chargeTime > 0.0001f ? Saturate(runtime_.stateTimer / chargeTime)
                                  : 1.0f;
        const float coil = 0.35f + 0.65f * chargeT;
        const float shiver =
            coil * (0.5f + 0.5f * std::sin(runtime_.stateTimer * 42.0f));
        bodyTf_.position.y -= 0.10f * coil;
        bodyTf_.scale.x -= 0.16f * coil;
        bodyTf_.scale.y += 0.18f * coil;
        bodyTf_.scale.z -= 0.16f * coil;
        rightHandTf_.position.y += 0.34f * coil;
        rightHandTf_.scale.x += 0.16f * coil;
        rightHandTf_.scale.y += 0.16f * coil;
        rightHandTf_.scale.z += 0.16f * coil;
        visualTf_.position.y -= 0.055f * coil;
        visualTf_.scale.x -= 0.045f * coil;
        visualTf_.scale.y += 0.075f * coil;
        visualTf_.scale.z -= 0.045f * coil;
        visualTf_.position.x += rightX * (shiver - 0.5f) * 0.020f;
        visualTf_.position.z += rightZ * (shiver - 0.5f) * 0.020f;
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

    if (!suppressActionPresentation && action_.kind == ActionKind::Smash) {
        if (action_.step == ActionStep::Charge ||
            action_.step == ActionStep::Hold || isFarSlashFlashHold) {
            const bool isDelayBait = action_.step == ActionStep::Hold;
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
        } else if (action_.step == ActionStep::Active ||
                   isFarSlashPostPierceSlash) {
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
        if (action_.step == ActionStep::Charge ||
            action_.step == ActionStep::Hold || isFarSlashFlashHold) {
            const bool isWideTell = action_.step == ActionStep::Hold;
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
        } else if (action_.step == ActionStep::Active ||
                   isFarSlashPostPierceSlash) {
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
        if (action_.step == ActionStep::Charge) {
            bodyTf_.position.y -= 0.18f + 0.06f * pulse;
            bodyTf_.position.x += forwardX * 0.18f;
            bodyTf_.position.z += forwardZ * 0.18f;
            bodyTf_.scale.x += 0.18f + 0.08f * pulse;
            bodyTf_.scale.z += 0.18f + 0.08f * pulse;
            leftHandTf_.position.x += (-rightX) * 0.34f + forwardX * 0.52f;
            leftHandTf_.position.z += (-rightZ) * 0.34f + forwardZ * 0.52f;
            rightHandTf_.position.x += rightX * 0.34f + forwardX * 0.52f;
            rightHandTf_.position.z += rightZ * 0.34f + forwardZ * 0.52f;
            leftHandTf_.position.y += 0.28f;
            rightHandTf_.position.y += 0.34f;
            visualPitch -= 0.22f + 0.04f * pulse;
        } else if (action_.step == ActionStep::Active) {
            bodyTf_.position.x += forwardX * 0.24f;
            bodyTf_.position.z += forwardZ * 0.24f;
            bodyTf_.scale.x += 0.22f;
            bodyTf_.scale.z += 0.16f;
            leftHandTf_.position.x += forwardX * 1.10f;
            leftHandTf_.position.z += forwardZ * 1.10f;
            rightHandTf_.position.x += forwardX * 1.26f;
            rightHandTf_.position.z += forwardZ * 1.26f;
            leftHandTf_.scale.x += 0.26f;
            rightHandTf_.scale.x += 0.30f;
            visualPitch += 0.14f;
        } else if (action_.step == ActionStep::Recovery) {
            bodyTf_.position.x += (-forwardX) * 0.08f;
            bodyTf_.position.z += (-forwardZ) * 0.08f;
            visualPitch += 0.08f;
        }
    } else if (!suppressActionPresentation &&
               action_.kind == ActionKind::ArcaneLaser) {
        const float charge = GetArcaneLaserChargeRatio();
        if (action_.step == ActionStep::Charge) {
            bodyTf_.position.y -= 0.10f + 0.08f * charge;
            bodyTf_.position.x += (-forwardX) * (0.18f + 0.20f * charge);
            bodyTf_.position.z += (-forwardZ) * (0.18f + 0.20f * charge);
            bodyTf_.scale.x += 0.10f + 0.12f * charge;
            bodyTf_.scale.z += 0.12f + 0.16f * charge;
            rightHandTf_.position.x += forwardX * (1.65f + 0.34f * charge);
            rightHandTf_.position.z += forwardZ * (1.65f + 0.34f * charge);
            rightHandTf_.position.y += 0.62f + 0.24f * pulse;
            rightHandTf_.scale.x += 0.24f + 0.20f * charge;
            rightHandTf_.scale.y += 0.24f + 0.20f * charge;
            rightHandTf_.scale.z += 0.24f + 0.20f * charge;
            leftHandTf_.position.x += (-rightX) * 0.28f + forwardX * 0.26f;
            leftHandTf_.position.z += (-rightZ) * 0.28f + forwardZ * 0.26f;
            leftHandTf_.position.y += 0.18f;
            visualPitch -= 0.12f + 0.08f * charge;
        } else if (action_.step == ActionStep::Active) {
            bodyTf_.position.x += (-forwardX) * 0.12f;
            bodyTf_.position.z += (-forwardZ) * 0.12f;
            rightHandTf_.position.x += forwardX * 2.20f;
            rightHandTf_.position.z += forwardZ * 2.20f;
            rightHandTf_.position.y += 0.62f;
            rightHandTf_.scale.x += 0.48f;
            rightHandTf_.scale.y += 0.48f;
            rightHandTf_.scale.z += 0.48f;
            visualPitch -= 0.06f;
        } else if (action_.step == ActionStep::Recovery) {
            rightHandTf_.position.x += forwardX * 0.58f;
            rightHandTf_.position.z += forwardZ * 0.58f;
            rightHandTf_.position.y += 0.22f;
            visualPitch += 0.05f;
        }
    } else if (!suppressActionPresentation &&
               action_.kind == ActionKind::CataclysmLaser) {
        const float charge = GetCataclysmLaserChargeRatio();
        const float heavyPulse = pulse * (0.65f + 0.35f * charge);
        if (action_.step == ActionStep::Charge) {
            bodyTf_.position.y += 0.78f + 1.16f * charge + 0.10f * heavyPulse;
            bodyTf_.position.x += (-forwardX) * (0.34f + 0.42f * charge);
            bodyTf_.position.z += (-forwardZ) * (0.34f + 0.42f * charge);
            bodyTf_.scale.x += 0.22f + 0.26f * charge;
            bodyTf_.scale.y += 0.06f + 0.08f * charge;
            bodyTf_.scale.z += 0.24f + 0.34f * charge;
            rightHandTf_.position.x += forwardX * (2.25f + 0.72f * charge);
            rightHandTf_.position.z += forwardZ * (2.25f + 0.72f * charge);
            rightHandTf_.position.y += 1.38f + 0.82f * charge +
                                       0.42f * heavyPulse;
            rightHandTf_.scale.x += 0.54f + 0.44f * charge;
            rightHandTf_.scale.y += 0.54f + 0.44f * charge;
            rightHandTf_.scale.z += 0.54f + 0.44f * charge;
            leftHandTf_.position.x += (-rightX) * 0.38f + forwardX * 0.42f;
            leftHandTf_.position.z += (-rightZ) * 0.38f + forwardZ * 0.42f;
            leftHandTf_.position.y += 0.82f + 0.72f * charge;
            visualTf_.position.y += 0.78f + 1.12f * charge +
                                    0.08f * heavyPulse;
            visualTf_.scale.x += 0.06f * charge;
            visualTf_.scale.z += 0.08f * charge;
            visualPitch -= 0.24f + 0.18f * charge;
        } else if (action_.step == ActionStep::Active) {
            bodyTf_.position.y += 2.10f + 0.14f * heavyPulse;
            bodyTf_.position.x += (-forwardX) * 0.22f;
            bodyTf_.position.z += (-forwardZ) * 0.22f;
            bodyTf_.scale.x += 0.18f;
            bodyTf_.scale.z += 0.26f;
            rightHandTf_.position.x += forwardX * 2.85f;
            rightHandTf_.position.z += forwardZ * 2.85f;
            rightHandTf_.position.y += 2.52f + 0.18f * heavyPulse;
            rightHandTf_.scale.x += 0.78f;
            rightHandTf_.scale.y += 0.78f;
            rightHandTf_.scale.z += 0.78f;
            leftHandTf_.position.y += 1.52f + 0.12f * heavyPulse;
            visualTf_.position.y += 2.08f + 0.12f * heavyPulse;
            visualPitch -= 0.12f;
        } else if (action_.step == ActionStep::Recovery) {
            rightHandTf_.position.x += forwardX * 0.88f;
            rightHandTf_.position.z += forwardZ * 0.88f;
            rightHandTf_.position.y += 0.80f;
            bodyTf_.position.y += 0.58f;
            visualTf_.position.y += 0.52f;
            visualPitch += 0.08f;
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
        visualYaw = usedYaw;
        visualTf_.position.x = tf_.position.x;
        visualTf_.position.z = tf_.position.z;
        visualPitch = 0.0f;
        visualRoll = 0.0f;
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

    float hitReactionFlash = 0.0f;
    if (hitReactionDuration_ > 0.0001f) {
        hitReactionFlash =
            std::clamp(runtime_.hitReactionTimer / hitReactionDuration_, 0.0f, 1.0f);
    }
    const float damageFlash =
        damageFlashDuration_ > 0.0001f
            ? std::clamp(runtime_.damageFlashTimer / damageFlashDuration_, 0.0f,
                         1.0f)
            : 0.0f;
    const float hitFlash = (std::max)(hitReactionFlash, damageFlash * 0.48f);
    const bool isHitFlashing = hitFlash > 0.0f;
    const float actionPulse =
        0.5f + 0.5f * std::sin(runtime_.stateTimer * 12.0f);
    const DirectX::XMFLOAT4 phaseTint =
        phase_ == BossPhase::Phase3
            ? DirectX::XMFLOAT4{0.72f, 0.50f, 0.16f, 0.20f}
            : phase_ == BossPhase::Phase2
                  ? DirectX::XMFLOAT4{0.56f, 0.50f, 0.42f, 0.16f}
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
        actionTint = {0.96f, 0.78f, 0.34f, 0.26f};
        actionIntensity = 0.072f + 0.034f * actionPulse;
        actionNoise = 0.14f;
        break;
    case ActionKind::Warp:
        actionTint = {0.46f, 0.72f, 0.92f, 0.24f};
        actionIntensity = 0.075f + 0.030f * actionPulse;
        actionNoise = 0.16f;
        break;
    case ActionKind::Stalk:
        actionTint = {0.58f, 0.60f, 0.52f, 0.12f};
        actionIntensity = 0.030f + 0.010f * actionPulse;
        actionNoise = 0.06f;
        break;
    case ActionKind::ArcaneLaser:
        actionTint = {0.26f, 0.96f, 0.78f, 0.30f};
        actionIntensity = 0.084f + 0.038f * actionPulse;
        actionNoise = 0.18f;
        break;
    case ActionKind::CataclysmLaser:
        actionTint = {0.18f, 0.86f, 1.0f, 0.36f};
        actionIntensity = 0.128f + 0.062f * actionPulse;
        actionNoise = 0.24f;
        break;
    default:
        break;
    }

    const bool isTelegraphCharge =
        action_.step == ActionStep::Charge &&
        (action_.kind == ActionKind::Smash || action_.kind == ActionKind::Sweep);
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
        const DirectX::XMFLOAT4 transitionTint =
            phase_ == BossPhase::Phase3
                ? DirectX::XMFLOAT4{0.76f, 0.52f, 0.18f, 0.24f}
                : DirectX::XMFLOAT4{0.62f, 0.52f, 0.38f, 0.24f};
        actionTint = LerpColor(actionTint, transitionTint, phaseRatio);
        actionIntensity += 0.12f * phaseRatio;
        actionNoise += 0.06f * phaseRatio;
    }

    ModelDrawEffect hitEffect{};
    if (isHitFlashing) {
        const bool hasHitReactionFlash = hitReactionFlash > 0.0f;
        const float flashElapsed =
            hasHitReactionFlash ? hitReactionDuration_ - runtime_.hitReactionTimer
                                : damageFlashDuration_ - runtime_.damageFlashTimer;
        const float hitStrobe =
            std::sinf(flashElapsed * 45.0f) > 0.0f
                ? 1.0f
                : 0.0f;
        const float hitFlashAmount = hitFlash * hitStrobe;
        hitEffect.enabled = hitFlashAmount > 0.0f;
        hitEffect.additiveBlend = false;
        hitEffect.color =
            hasHitReactionFlash
                ? LerpColor({1.0f, 0.94f, 0.78f, 0.92f},
                            {1.0f, 1.0f, 1.0f, 0.98f}, hitFlash)
                : LerpColor({1.0f, 0.82f, 0.50f, 0.42f},
                            {1.0f, 0.95f, 0.74f, 0.52f}, damageFlash);
        hitEffect.intensity =
            hasHitReactionFlash ? 0.46f + 0.62f * hitFlashAmount
                                : 0.16f + 0.22f * hitFlashAmount;
        hitEffect.fresnelPower = 1.8f;
        hitEffect.noiseAmount = 0.08f;
        hitEffect.time = flashElapsed;
        hitEffect.surfaceTint =
            hasHitReactionFlash ? 0.40f + 0.24f * hitFlashAmount
                                : 0.09f + 0.14f * hitFlashAmount;
        hitEffect.alphaBoost = hasHitReactionFlash ? 0.62f : 0.34f;
    }

    if (isHitFlashing) {
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

    if (isVisible_) {
        drawEnemyVisual(visualTf_);
    }

    for (const auto &clone : tripleIaiClones_) {
        if (!clone.isActive) {
            continue;
        }

        drawEnemyVisual(clone.visual);
    }

    for (const auto &trail : afterimageGhosts_) {
        if (!trail.isActive) {
            continue;
        }
        const float alpha = trail.maxLife > 0.0001f
                                ? std::clamp(trail.life / trail.maxLife, 0.0f,
                                             1.0f)
                                : 0.0f;
        Transform trailVisual = trail.visual;
        trailVisual.scale.x *= visualScale * (0.98f + 0.04f * alpha);
        trailVisual.scale.y *= visualScale * (0.98f + 0.04f * alpha);
        trailVisual.scale.z *= visualScale * (0.98f + 0.04f * alpha);

        ModelDrawEffect trailEffect{};
        trailEffect.enabled = true;
        trailEffect.additiveBlend = true;
        trailEffect.disableCulling = true;
        trailEffect.color = {0.46f, 0.82f, 1.0f, 0.34f * alpha};
        trailEffect.intensity = 0.30f * alpha;
        trailEffect.fresnelPower = 1.45f;
        trailEffect.noiseAmount = 0.18f;
        trailEffect.time = runtime_.stateTimer + (1.0f - alpha) * 0.35f;
        modelManager->SetDrawEffect(trailEffect);
        modelManager->Draw(modelId_, trailVisual, camera);
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

