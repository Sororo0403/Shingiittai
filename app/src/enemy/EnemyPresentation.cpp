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

float FarDistanceVisualScale(const DirectX::XMFLOAT3 &visualPosition,
                             const DirectX::XMFLOAT3 &playerPosition) {
    constexpr float kScaleStartDistance = 6.0f;
    constexpr float kScaleFullDistance = 18.0f;
    constexpr float kMaxScaleBonus = 0.55f;

    const float dx = playerPosition.x - visualPosition.x;
    const float dy = playerPosition.y - visualPosition.y;
    const float dz = playerPosition.z - visualPosition.z;
    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    const float t = Saturate((distance - kScaleStartDistance) /
                             (kScaleFullDistance - kScaleStartDistance));
    const float eased = t * t * (3.0f - 2.0f * t);
    return 1.0f + kMaxScaleBonus * eased;
}

} // namespace

void Enemy::UpdateParts() {
    PartPresentationContext pose{};
    pose.usedYaw = GetVisualYaw();
    pose.visualYaw = pose.usedYaw;
    pose.pulse = 0.5f + 0.5f * std::sin(runtime_.stateTimer * 18.0f);
    pose.forwardX = std::sin(pose.usedYaw);
    pose.forwardZ = std::cos(pose.usedYaw);
    pose.rightX = std::cos(pose.usedYaw);
    pose.rightZ = -std::sin(pose.usedYaw);
    pose.suppressAttackBodyMotion = action_.kind == ActionKind::Smash ||
                                    action_.kind == ActionKind::Sweep ||
                                    action_.kind == ActionKind::BladeClash ||
                                    action_.kind == ActionKind::ArcaneLaser ||
                                    action_.kind == ActionKind::CataclysmLaser;
    pose.suppressActionPresentation = counterRecoilTimer_ > 0.0f;
    pose.isTelegraphCharge = action_.step == ActionStep::Charge &&
                             (action_.kind == ActionKind::Smash ||
                              action_.kind == ActionKind::Sweep);
    pose.isFarSlashFlashHold =
        farSlashActive_ && action_.step == ActionStep::Active &&
        runtime_.stateTimer <= GetFarWarpSlashStanceHoldDuration();
    pose.isFarSlashPostPierceSlash =
        farSlashActive_ && action_.step == ActionStep::Recovery;

    InitializePartTransforms(pose);
    ApplyHitPartPresentation(pose);
    ApplyChargePartPresentation(pose);
    ApplyPhasePartPresentation(pose);
    ApplyEnemyActionPartPresentation(pose);
    ApplyWarpPartPresentation(pose);
    FinalizePartTransforms(pose);
}

void Enemy::InitializePartTransforms(const PartPresentationContext &pose) {
    bodyTf_ = tf_;
    bodyTf_.position = tf_.position;
    bodyTf_.scale = {1.2f, 1.4f, 0.8f};

    visualTf_ = tf_;
    visualTf_.position = tf_.position;
    visualTf_.scale = {1.0f, 1.0f, 1.0f};

    leftHandTf_ = tf_;
    leftHandTf_.position = tf_.position;
    leftHandTf_.position.x += (-pose.rightX) * 1.2f;
    leftHandTf_.position.y += 0.9f;
    leftHandTf_.position.z += (-pose.rightZ) * 1.2f;
    leftHandTf_.scale = {0.6f, 0.6f, 0.6f};

    rightHandTf_ = tf_;
    rightHandTf_.position = tf_.position;
    rightHandTf_.position.x += pose.rightX * 1.2f;
    rightHandTf_.position.y += 0.9f;
    rightHandTf_.position.z += pose.rightZ * 1.2f;
    rightHandTf_.scale = {0.6f, 0.6f, 0.6f};
}

void Enemy::ApplyHitPartPresentation(PartPresentationContext &pose) {
    if (hitReactionTimer_ > 0.0f) {
        const float hitT = hitReactionTimer_ / hitReactionDuration_;
        bodyTf_.scale.x += 0.18f * hitT;
        bodyTf_.scale.y -= 0.10f * hitT;
        bodyTf_.scale.z += 0.18f * hitT;
        bodyTf_.position.y += 0.06f * hitT;
        leftHandTf_.position.y += 0.10f * hitT;
        rightHandTf_.position.y += 0.10f * hitT;
        visualTf_.position.y += 0.04f * hitT;
        pose.visualPitch -= 0.12f * hitT;
    }

    if (counterRecoilTimer_ > 0.0f) {
        float recoilProgress =
            counterRecoilDuration_ > 0.0001f
                ? 1.0f - (counterRecoilTimer_ / counterRecoilDuration_)
                : 1.0f;
        recoilProgress = Saturate(recoilProgress);
        const float recoil = recoilProgress * recoilProgress;
        bodyTf_.position.y += 0.015f * recoil;
        pose.visualPitch += counterRecoilPitchRad_ * recoil;
    }
}

void Enemy::ApplyChargePartPresentation(const PartPresentationContext &pose) {
    if (tellActive_ || pose.isTelegraphCharge || pose.isFarSlashFlashHold) {
        const float chargePulse = tellActive_ ? (1.0f + 0.55f * pose.pulse)
                                              : (0.72f + 0.42f * pose.pulse);
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

    if (!pose.suppressActionPresentation && farSlashActive_ &&
        (pose.isTelegraphCharge || pose.isFarSlashFlashHold)) {
        const float chargeTime = action_.kind == ActionKind::Smash
                                     ? GetCurrentSmashChargeTime()
                                     : GetCurrentSweepChargeTime();
        const float chargeT = chargeTime > 0.0001f
                                  ? Saturate(runtime_.stateTimer / chargeTime)
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
        visualTf_.position.x += pose.rightX * (shiver - 0.5f) * 0.020f;
        visualTf_.position.z += pose.rightZ * (shiver - 0.5f) * 0.020f;
    }
}

void Enemy::ApplyPhasePartPresentation(PartPresentationContext &pose) {
    if (phase_ != BossPhase::Phase1) {
        const bool phase3 = phase_ == BossPhase::Phase3;
        bodyTf_.scale.x += 0.05f;
        bodyTf_.scale.z += 0.05f;
        bodyTf_.position.y += (phase3 ? 0.06f : 0.04f) * pose.pulse;
        leftHandTf_.position.y += phase3 ? 0.07f : 0.04f;
        rightHandTf_.position.y += phase3 ? 0.09f : 0.06f;
        visualTf_.position.y += 0.03f * pose.pulse;
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
            hold *
            (0.5f + 0.5f * std::sin(runtime_.phaseTransitionTimer * 34.0f));

        bodyTf_.position.y -= 0.26f * hold - 0.10f * releaseEase;
        bodyTf_.scale.x += -0.24f * hold + 0.46f * releaseEase + 0.10f * snap;
        bodyTf_.scale.y += -0.30f * hold + 0.28f * releaseEase;
        bodyTf_.scale.z += -0.24f * hold + 0.46f * releaseEase + 0.10f * snap;

        rightHandTf_.position.y += 0.54f * hold + 0.74f * releaseEase;
        rightHandTf_.position.x +=
            (-pose.rightX) * 0.46f * hold + pose.rightX * 0.96f * releaseEase;
        rightHandTf_.position.z +=
            (-pose.rightZ) * 0.46f * hold + pose.rightZ * 0.96f * releaseEase;
        rightHandTf_.position.x += (-pose.forwardX) * 0.12f * hold;
        rightHandTf_.position.z += (-pose.forwardZ) * 0.12f * hold;
        rightHandTf_.scale.x += 0.08f * hold + 0.22f * releaseEase;
        rightHandTf_.scale.y += 0.08f * hold + 0.22f * releaseEase;
        rightHandTf_.scale.z += 0.08f * hold + 0.22f * releaseEase;

        leftHandTf_.position.y += 0.46f * hold + 0.60f * releaseEase;
        leftHandTf_.position.x +=
            pose.rightX * 0.38f * hold + (-pose.rightX) * 0.78f * releaseEase;
        leftHandTf_.position.z +=
            pose.rightZ * 0.38f * hold + (-pose.rightZ) * 0.78f * releaseEase;
        leftHandTf_.position.x += (-pose.forwardX) * 0.10f * hold;
        leftHandTf_.position.z += (-pose.forwardZ) * 0.10f * hold;
        leftHandTf_.scale.x += 0.06f * hold + 0.18f * releaseEase;
        leftHandTf_.scale.y += 0.06f * hold + 0.18f * releaseEase;
        leftHandTf_.scale.z += 0.06f * hold + 0.18f * releaseEase;

        visualTf_.position.y += -0.10f * hold + 0.20f * snap;
        visualTf_.position.x += (tremble - 0.5f * hold) * 0.018f;
        visualTf_.scale.x += -0.10f * hold + 0.20f * releaseEase + 0.08f * snap;
        visualTf_.scale.y += -0.14f * hold + 0.18f * releaseEase;
        visualTf_.scale.z += -0.10f * hold + 0.20f * releaseEase + 0.08f * snap;
        pose.visualPitch -= 0.18f * hold - 0.14f * releaseEase;
        pose.visualRoll += 0.08f * tremble + 0.12f * snap;
    }
}

void Enemy::ApplyEnemyActionPartPresentation(PartPresentationContext &pose) {
    if (pose.suppressActionPresentation) {
        return;
    }
    switch (action_.kind) {
    case ActionKind::Smash:
        ApplySmashPartPresentation(pose);
        break;
    case ActionKind::Sweep:
        ApplySweepPartPresentation(pose);
        break;
    case ActionKind::BladeClash:
        ApplyBladeClashPartPresentation(pose);
        break;
    case ActionKind::ArcaneLaser:
        ApplyArcaneLaserPartPresentation(pose);
        break;
    case ActionKind::CataclysmLaser:
        ApplyCataclysmLaserPartPresentation(pose);
        break;
    case ActionKind::Stalk:
        ApplyStalkPartPresentation(pose);
        break;
    default:
        break;
    }
}

void Enemy::ApplySmashPartPresentation(PartPresentationContext &pose) {
    if (action_.step == ActionStep::Charge ||
        action_.step == ActionStep::Hold || pose.isFarSlashFlashHold) {
        const bool isDelayBait = action_.step == ActionStep::Hold;
        bodyTf_.position.y -= 0.28f + 0.08f * pose.pulse;
        bodyTf_.scale.y += 0.24f;
        bodyTf_.scale.x += 0.16f + 0.06f * pose.pulse;
        bodyTf_.scale.z += 0.16f + 0.06f * pose.pulse;
        rightHandTf_.position.y += 2.75f + 0.28f * pose.pulse;
        rightHandTf_.position.x += (-pose.forwardX) * 1.48f;
        rightHandTf_.position.z += (-pose.forwardZ) * 1.48f;
        rightHandTf_.scale.x += 0.34f + 0.18f * pose.pulse;
        rightHandTf_.scale.y += 0.34f + 0.18f * pose.pulse;
        rightHandTf_.scale.z += 0.34f + 0.18f * pose.pulse;
        leftHandTf_.position.y += 0.36f;
        leftHandTf_.position.x += pose.forwardX * 0.18f;
        leftHandTf_.position.z += pose.forwardZ * 0.18f;
        visualTf_.position.x += (-pose.forwardX) * 0.40f;
        visualTf_.position.z += (-pose.forwardZ) * 0.40f;
        visualTf_.position.y -= 0.12f;
        pose.visualPitch -= 0.62f + 0.12f * pose.pulse;
        if (isDelayBait) {
            bodyTf_.position.y -= 0.16f;
            bodyTf_.position.x +=
                (-pose.forwardX) * (0.26f + 0.10f * pose.pulse);
            bodyTf_.position.z +=
                (-pose.forwardZ) * (0.26f + 0.10f * pose.pulse);
            bodyTf_.scale.x += 0.12f;
            bodyTf_.scale.z += 0.12f;
            rightHandTf_.position.y += 0.62f + 0.16f * pose.pulse;
            rightHandTf_.position.x +=
                (-pose.forwardX) * 0.52f + pose.rightX * 0.18f;
            rightHandTf_.position.z +=
                (-pose.forwardZ) * 0.52f + pose.rightZ * 0.18f;
            leftHandTf_.position.y += 0.22f;
            leftHandTf_.position.x += pose.forwardX * 0.28f;
            leftHandTf_.position.z += pose.forwardZ * 0.28f;
            pose.visualPitch -= 0.22f;
        }
    } else if (action_.step == ActionStep::Active ||
               pose.isFarSlashPostPierceSlash) {
        bodyTf_.position.x += pose.forwardX * 0.18f;
        bodyTf_.position.z += pose.forwardZ * 0.18f;
        bodyTf_.position.y -= 0.05f;
        rightHandTf_.position.y -= 0.45f;
        rightHandTf_.position.x += pose.forwardX * 2.10f;
        rightHandTf_.position.z += pose.forwardZ * 2.10f;
        visualTf_.position.x += pose.forwardX * 0.42f;
        visualTf_.position.z += pose.forwardZ * 0.42f;
        visualTf_.position.y += 0.06f;
        pose.visualPitch += 0.34f;
    } else if (action_.step == ActionStep::Recovery) {
        rightHandTf_.position.y += 0.3f;
        rightHandTf_.position.x += pose.forwardX * 0.8f;
        rightHandTf_.position.z += pose.forwardZ * 0.8f;

        visualTf_.position.x += pose.forwardX * 0.10f;
        visualTf_.position.z += pose.forwardZ * 0.10f;
        pose.visualPitch += 0.10f;
    }
}

void Enemy::ApplySweepPartPresentation(PartPresentationContext &pose) {
    if (action_.step == ActionStep::Charge ||
        action_.step == ActionStep::Hold || pose.isFarSlashFlashHold) {
        const bool isWideTell = action_.step == ActionStep::Hold;
        bodyTf_.position.y -= 0.24f + 0.06f * pose.pulse;
        bodyTf_.position.x += pose.rightX * (0.44f + 0.10f * pose.pulse);
        bodyTf_.position.z += pose.rightZ * (0.44f + 0.10f * pose.pulse);
        bodyTf_.scale.x += 0.20f + 0.04f * pose.pulse;
        bodyTf_.scale.z += 0.26f + 0.06f * pose.pulse;
        rightHandTf_.position.x += pose.rightX * (3.20f + 0.34f * pose.pulse);
        rightHandTf_.position.y += 0.72f + 0.18f * pose.pulse;
        rightHandTf_.position.z += pose.rightZ * (3.20f + 0.34f * pose.pulse);
        rightHandTf_.scale.x += 0.42f + 0.22f * pose.pulse;
        rightHandTf_.scale.y += 0.42f + 0.22f * pose.pulse;
        rightHandTf_.scale.z += 0.42f + 0.22f * pose.pulse;
        leftHandTf_.position.x +=
            (-pose.rightX) * 0.82f + (-pose.forwardX) * 0.18f;
        leftHandTf_.position.z +=
            (-pose.rightZ) * 0.82f + (-pose.forwardZ) * 0.18f;
        leftHandTf_.position.y += 0.16f;
        visualTf_.position.x += pose.rightX * 0.18f;
        visualTf_.position.z += pose.rightZ * 0.18f;
        pose.visualYaw += 0.62f + 0.18f * pose.pulse;
        pose.visualRoll -= 0.62f + 0.18f * pose.pulse;
        if (isWideTell) {
            bodyTf_.position.x += pose.rightX * 0.28f;
            bodyTf_.position.z += pose.rightZ * 0.28f;
            bodyTf_.scale.x += 0.10f;
            bodyTf_.scale.z += 0.16f;
            rightHandTf_.position.x += pose.rightX * 0.72f;
            rightHandTf_.position.z += pose.rightZ * 0.72f;
            leftHandTf_.position.x += (-pose.rightX) * 0.34f;
            leftHandTf_.position.z += (-pose.rightZ) * 0.34f;
            pose.visualYaw += 0.20f;
            pose.visualRoll -= 0.18f;
        }
    } else if (action_.step == ActionStep::Active ||
               pose.isFarSlashPostPierceSlash) {
        bodyTf_.position.x += (-pose.rightX) * 0.18f;
        bodyTf_.position.z += (-pose.rightZ) * 0.18f;
        rightHandTf_.position.x += (-pose.rightX) * 2.00f;
        rightHandTf_.position.y += 0.2f;
        rightHandTf_.position.z += (-pose.rightZ) * 2.00f;
        pose.visualYaw -= 0.26f;
        pose.visualRoll += 0.26f;
        visualTf_.position.x += (-pose.rightX) * 0.20f;
        visualTf_.position.z += (-pose.rightZ) * 0.20f;
    } else if (action_.step == ActionStep::Recovery) {
        rightHandTf_.position.x += pose.rightX * 0.3f;
        rightHandTf_.position.y += 0.1f;
        rightHandTf_.position.z += pose.rightZ * 0.3f;
        pose.visualRoll += 0.10f;
    }
}

void Enemy::ApplyBladeClashPartPresentation(PartPresentationContext &pose) {
    if (action_.step == ActionStep::Charge) {
        bodyTf_.position.y -= 0.18f + 0.06f * pose.pulse;
        bodyTf_.position.x += pose.forwardX * 0.18f;
        bodyTf_.position.z += pose.forwardZ * 0.18f;
        bodyTf_.scale.x += 0.18f + 0.08f * pose.pulse;
        bodyTf_.scale.z += 0.18f + 0.08f * pose.pulse;
        leftHandTf_.position.x +=
            (-pose.rightX) * 0.34f + pose.forwardX * 0.52f;
        leftHandTf_.position.z +=
            (-pose.rightZ) * 0.34f + pose.forwardZ * 0.52f;
        rightHandTf_.position.x += pose.rightX * 0.34f + pose.forwardX * 0.52f;
        rightHandTf_.position.z += pose.rightZ * 0.34f + pose.forwardZ * 0.52f;
        leftHandTf_.position.y += 0.28f;
        rightHandTf_.position.y += 0.34f;
        pose.visualPitch -= 0.22f + 0.04f * pose.pulse;
    } else if (action_.step == ActionStep::Active) {
        bodyTf_.position.x += pose.forwardX * 0.24f;
        bodyTf_.position.z += pose.forwardZ * 0.24f;
        bodyTf_.scale.x += 0.22f;
        bodyTf_.scale.z += 0.16f;
        leftHandTf_.position.x += pose.forwardX * 1.10f;
        leftHandTf_.position.z += pose.forwardZ * 1.10f;
        rightHandTf_.position.x += pose.forwardX * 1.26f;
        rightHandTf_.position.z += pose.forwardZ * 1.26f;
        leftHandTf_.scale.x += 0.26f;
        rightHandTf_.scale.x += 0.30f;
        pose.visualPitch += 0.14f;
    } else if (action_.step == ActionStep::Recovery) {
        bodyTf_.position.x += (-pose.forwardX) * 0.08f;
        bodyTf_.position.z += (-pose.forwardZ) * 0.08f;
        pose.visualPitch += 0.08f;
    }
}

void Enemy::ApplyArcaneLaserPartPresentation(PartPresentationContext &pose) {
    const float charge = GetArcaneLaserChargeRatio();
    if (action_.step == ActionStep::Charge) {
        bodyTf_.position.y -= 0.10f + 0.08f * charge;
        bodyTf_.position.x += (-pose.forwardX) * (0.18f + 0.20f * charge);
        bodyTf_.position.z += (-pose.forwardZ) * (0.18f + 0.20f * charge);
        bodyTf_.scale.x += 0.10f + 0.12f * charge;
        bodyTf_.scale.z += 0.12f + 0.16f * charge;
        rightHandTf_.position.x += pose.forwardX * (1.65f + 0.34f * charge);
        rightHandTf_.position.z += pose.forwardZ * (1.65f + 0.34f * charge);
        rightHandTf_.position.y += 0.62f + 0.24f * pose.pulse;
        rightHandTf_.scale.x += 0.24f + 0.20f * charge;
        rightHandTf_.scale.y += 0.24f + 0.20f * charge;
        rightHandTf_.scale.z += 0.24f + 0.20f * charge;
        leftHandTf_.position.x +=
            (-pose.rightX) * 0.28f + pose.forwardX * 0.26f;
        leftHandTf_.position.z +=
            (-pose.rightZ) * 0.28f + pose.forwardZ * 0.26f;
        leftHandTf_.position.y += 0.18f;
        pose.visualPitch -= 0.12f + 0.08f * charge;
    } else if (action_.step == ActionStep::Active) {
        bodyTf_.position.x += (-pose.forwardX) * 0.12f;
        bodyTf_.position.z += (-pose.forwardZ) * 0.12f;
        rightHandTf_.position.x += pose.forwardX * 2.20f;
        rightHandTf_.position.z += pose.forwardZ * 2.20f;
        rightHandTf_.position.y += 0.62f;
        rightHandTf_.scale.x += 0.48f;
        rightHandTf_.scale.y += 0.48f;
        rightHandTf_.scale.z += 0.48f;
        pose.visualPitch -= 0.06f;
    } else if (action_.step == ActionStep::Recovery) {
        rightHandTf_.position.x += pose.forwardX * 0.58f;
        rightHandTf_.position.z += pose.forwardZ * 0.58f;
        rightHandTf_.position.y += 0.22f;
        pose.visualPitch += 0.05f;
    }
}

void Enemy::ApplyCataclysmLaserPartPresentation(PartPresentationContext &pose) {
    const float charge = GetCataclysmLaserChargeRatio();
    const float heavyPulse = pose.pulse * (0.65f + 0.35f * charge);
    if (action_.step == ActionStep::Charge) {
        bodyTf_.position.y += 0.78f + 1.16f * charge + 0.10f * heavyPulse;
        bodyTf_.position.x += (-pose.forwardX) * (0.34f + 0.42f * charge);
        bodyTf_.position.z += (-pose.forwardZ) * (0.34f + 0.42f * charge);
        bodyTf_.scale.x += 0.22f + 0.26f * charge;
        bodyTf_.scale.y += 0.06f + 0.08f * charge;
        bodyTf_.scale.z += 0.24f + 0.34f * charge;
        rightHandTf_.position.x += pose.forwardX * (2.25f + 0.72f * charge);
        rightHandTf_.position.z += pose.forwardZ * (2.25f + 0.72f * charge);
        rightHandTf_.position.y += 1.38f + 0.82f * charge + 0.42f * heavyPulse;
        rightHandTf_.scale.x += 0.54f + 0.44f * charge;
        rightHandTf_.scale.y += 0.54f + 0.44f * charge;
        rightHandTf_.scale.z += 0.54f + 0.44f * charge;
        leftHandTf_.position.x +=
            (-pose.rightX) * 0.38f + pose.forwardX * 0.42f;
        leftHandTf_.position.z +=
            (-pose.rightZ) * 0.38f + pose.forwardZ * 0.42f;
        leftHandTf_.position.y += 0.82f + 0.72f * charge;
        visualTf_.position.y += 0.78f + 1.12f * charge + 0.08f * heavyPulse;
        visualTf_.scale.x += 0.06f * charge;
        visualTf_.scale.z += 0.08f * charge;
        pose.visualPitch -= 0.24f + 0.18f * charge;
    } else if (action_.step == ActionStep::Active) {
        bodyTf_.position.y += 2.10f + 0.14f * heavyPulse;
        bodyTf_.position.x += (-pose.forwardX) * 0.22f;
        bodyTf_.position.z += (-pose.forwardZ) * 0.22f;
        bodyTf_.scale.x += 0.18f;
        bodyTf_.scale.z += 0.26f;
        rightHandTf_.position.x += pose.forwardX * 2.85f;
        rightHandTf_.position.z += pose.forwardZ * 2.85f;
        rightHandTf_.position.y += 2.52f + 0.18f * heavyPulse;
        rightHandTf_.scale.x += 0.78f;
        rightHandTf_.scale.y += 0.78f;
        rightHandTf_.scale.z += 0.78f;
        leftHandTf_.position.y += 1.52f + 0.12f * heavyPulse;
        visualTf_.position.y += 2.08f + 0.12f * heavyPulse;
        pose.visualPitch -= 0.12f;
    } else if (action_.step == ActionStep::Recovery) {
        rightHandTf_.position.x += pose.forwardX * 0.88f;
        rightHandTf_.position.z += pose.forwardZ * 0.88f;
        rightHandTf_.position.y += 0.80f;
        bodyTf_.position.y += 0.58f;
        visualTf_.position.y += 0.52f;
        pose.visualPitch += 0.08f;
    }
}

void Enemy::ApplyStalkPartPresentation(PartPresentationContext &pose) {
    rightHandTf_.position.y += 0.35f;
    leftHandTf_.position.y += 0.20f;
    rightHandTf_.position.x += pose.forwardX * 0.35f;
    rightHandTf_.position.z += pose.forwardZ * 0.35f;
    visualTf_.position.x += pose.forwardX * 0.06f;
    visualTf_.position.z += pose.forwardZ * 0.06f;
    pose.visualRoll += 0.04f * pose.pulse;
}

void Enemy::ApplyWarpPartPresentation(PartPresentationContext &pose) {
    if (action_.kind == ActionKind::Warp && action_.step == ActionStep::Start) {
        const float startTime = GetCurrentWarpStartTime();
        const float t = startTime > 0.0001f
                            ? Saturate(runtime_.stateTimer / startTime)
                            : 1.0f;
        const float windup = std::sin(t * 3.14159265f);
        const float snapT = Saturate((t - 0.34f) / 0.66f);
        const float snap = std::pow(snapT, 2.65f);
        const float shimmer =
            std::sin(runtime_.stateTimer * 72.0f) * (0.35f + 0.65f * snap);

        visualTf_.scale.x += 0.18f * windup - 0.42f * snap;
        visualTf_.scale.y += 0.10f * windup + 0.54f * snap;
        visualTf_.scale.z -= 0.12f * windup + 0.34f * snap;
        visualTf_.position.y += 0.04f * windup + 0.18f * snap;
        visualTf_.position.x += pose.rightX * shimmer * 0.045f;
        visualTf_.position.z += pose.rightZ * shimmer * 0.045f;
        pose.visualYaw += shimmer * 0.10f + snap * 0.12f;
        pose.visualRoll += shimmer * 0.18f - snap * 0.20f;
    } else if (action_.kind == ActionKind::Warp &&
               action_.step == ActionStep::End) {
        const float t = Saturate(runtime_.stateTimer / 0.26f);
        const float returnT = t * t * (3.0f - 2.0f * t);
        const float distortion = std::pow(1.0f - returnT, 2.20f);
        const float overshoot =
            std::sin(t * 3.14159265f) * std::pow(1.0f - t, 0.65f);
        const float shimmer =
            std::sin(runtime_.stateTimer * 64.0f) * (1.0f - returnT);

        visualTf_.scale.x += -0.36f * distortion + 0.20f * overshoot;
        visualTf_.scale.y += 0.48f * distortion - 0.18f * overshoot;
        visualTf_.scale.z += -0.30f * distortion + 0.18f * overshoot;
        visualTf_.position.y += 0.16f * distortion - 0.05f * overshoot;
        visualTf_.position.x += pose.rightX * shimmer * 0.040f;
        visualTf_.position.z += pose.rightZ * shimmer * 0.040f;
        pose.visualYaw += shimmer * 0.08f - distortion * 0.08f;
        pose.visualRoll +=
            shimmer * 0.14f + distortion * 0.16f - overshoot * 0.12f;
    }
}

void Enemy::FinalizePartTransforms(PartPresentationContext &pose) {
    if (pose.suppressAttackBodyMotion) {
        bodyTf_.position.x = tf_.position.x;
        bodyTf_.position.z = tf_.position.z;
        pose.visualYaw = pose.usedYaw;
        visualTf_.position.x = tf_.position.x;
        visualTf_.position.z = tf_.position.z;
        pose.visualPitch = 0.0f;
        pose.visualRoll = 0.0f;
    }

    pose.visualPitch += cinematicPitch_;
    pose.visualRoll += cinematicRoll_;

    DirectX::XMVECTOR hitboxRot =
        DirectX::XMQuaternionRotationRollPitchYaw(0.0f, pose.usedYaw, 0.0f);
    DirectX::XMStoreFloat4(&bodyTf_.rotation, hitboxRot);
    DirectX::XMStoreFloat4(&leftHandTf_.rotation, hitboxRot);
    DirectX::XMStoreFloat4(&rightHandTf_.rotation, hitboxRot);

    DirectX::XMVECTOR visualRot = DirectX::XMQuaternionRotationRollPitchYaw(
        pose.visualPitch, pose.visualYaw, pose.visualRoll);
    DirectX::XMStoreFloat4(&visualTf_.rotation, visualRot);
}

void Enemy::Draw(ModelManager *modelManager, const Camera &camera,
                 float visualScale) {
    if (runtime_.deathFinished) {
        return;
    }
    const float actionPulse =
        0.5f + 0.5f * std::sin(runtime_.stateTimer * 12.0f);
    DirectX::XMFLOAT4 actionTint{};
    float actionIntensity = 0.0f;
    float actionNoise = 0.0f;
    ResolveActionDrawStyle(actionPulse, actionTint, actionIntensity,
                           actionNoise);
    ApplyActionDrawStyleModifiers(actionPulse, actionTint, actionIntensity,
                                  actionNoise);
    bool isHitFlashing = false;
    const ModelDrawEffect hitEffect = BuildEnemyHitEffect(isHitFlashing);
    const ModelDrawEffect baseEffect = BuildEnemyBaseEffect(
        hitEffect, isHitFlashing, actionTint, actionIntensity, actionNoise);

    const float warpAlpha = GetWarpVisualAlpha();
    if (isVisible_ && warpAlpha > 0.001f) {
        DrawEnemyVisual(modelManager, camera, visualTf_, warpAlpha, visualScale,
                        actionPulse, isHitFlashing, baseEffect);
    }
    for (const auto &clone : tripleIaiClones_) {
        if (clone.isActive) {
            DrawEnemyVisual(modelManager, camera, clone.visual, 1.0f,
                            visualScale, actionPulse, isHitFlashing,
                            baseEffect);
        }
    }
    DrawEnemyAfterimages(modelManager, camera, visualScale);
    modelManager->ClearDrawEffect();
}

void Enemy::ResolveActionDrawStyle(float actionPulse,
                                   DirectX::XMFLOAT4 &actionTint,
                                   float &actionIntensity,
                                   float &actionNoise) const {
    const DirectX::XMFLOAT4 phaseTint =
        phase_ == BossPhase::Phase3
            ? DirectX::XMFLOAT4{0.72f, 0.50f, 0.16f, 0.20f}
        : phase_ == BossPhase::Phase2
            ? DirectX::XMFLOAT4{0.56f, 0.50f, 0.42f, 0.16f}
            : DirectX::XMFLOAT4{0.72f, 0.76f, 0.72f, 0.12f};
    actionTint = phaseTint;
    actionIntensity = 0.025f + 0.010f * actionPulse;
    actionNoise = 0.08f;

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
        actionIntensity = 0.0f;
        actionNoise = 0.0f;
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
}

void Enemy::ApplyActionDrawStyleModifiers(float actionPulse,
                                          DirectX::XMFLOAT4 &actionTint,
                                          float &actionIntensity,
                                          float &actionNoise) const {
    const bool isTelegraphCharge = action_.step == ActionStep::Charge &&
                                   (action_.kind == ActionKind::Smash ||
                                    action_.kind == ActionKind::Sweep);
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
}

ModelDrawEffect Enemy::BuildEnemyHitEffect(bool &isHitFlashing) const {
    float hitReactionFlash = 0.0f;
    if (hitReactionDuration_ > 0.0001f) {
        hitReactionFlash = std::clamp(
            runtime_.hitReactionTimer / hitReactionDuration_, 0.0f, 1.0f);
    }
    const float damageFlash =
        damageFlashDuration_ > 0.0001f
            ? std::clamp(runtime_.damageFlashTimer / damageFlashDuration_, 0.0f,
                         1.0f)
            : 0.0f;
    const float hitFlash = (std::max)(hitReactionFlash, damageFlash * 0.48f);
    isHitFlashing = hitFlash > 0.0f;
    ModelDrawEffect hitEffect{};
    if (isHitFlashing) {
        const bool hasHitReactionFlash = hitReactionFlash > 0.0f;
        const float flashElapsed =
            hasHitReactionFlash
                ? hitReactionDuration_ - runtime_.hitReactionTimer
                : damageFlashDuration_ - runtime_.damageFlashTimer;
        const float hitStrobe =
            std::sinf(flashElapsed * 45.0f) > 0.0f ? 1.0f : 0.0f;
        const float hitFlashAmount = hitFlash * hitStrobe;
        hitEffect.enabled = hitFlashAmount > 0.0f;
        hitEffect.additiveBlend = false;
        hitEffect.color =
            hasHitReactionFlash
                ? LerpColor({1.0f, 0.94f, 0.78f, 0.92f},
                            {1.0f, 1.0f, 1.0f, 0.98f}, hitFlash)
                : LerpColor({1.0f, 0.82f, 0.50f, 0.42f},
                            {1.0f, 0.95f, 0.74f, 0.52f}, damageFlash);
        hitEffect.intensity = hasHitReactionFlash
                                  ? 0.46f + 0.62f * hitFlashAmount
                                  : 0.16f + 0.22f * hitFlashAmount;
        hitEffect.fresnelPower = 1.8f;
        hitEffect.noiseAmount = 0.08f;
        hitEffect.time = flashElapsed;
        hitEffect.surfaceTint = hasHitReactionFlash
                                    ? 0.40f + 0.24f * hitFlashAmount
                                    : 0.09f + 0.14f * hitFlashAmount;
        hitEffect.alphaBoost = hasHitReactionFlash ? 0.62f : 0.34f;
    }

    return hitEffect;
}

ModelDrawEffect Enemy::BuildEnemyBaseEffect(const ModelDrawEffect &hitEffect,
                                            bool isHitFlashing,
                                            const DirectX::XMFLOAT4 &actionTint,
                                            float actionIntensity,
                                            float actionNoise) const {
    ModelDrawEffect baseEffect{};
    if (isHitFlashing) {
        baseEffect = hitEffect;
    } else {
        const bool isTelegraphCharge = action_.step == ActionStep::Charge &&
                                       (action_.kind == ActionKind::Smash ||
                                        action_.kind == ActionKind::Sweep);
        baseEffect.enabled = false;
        baseEffect.additiveBlend = false;
        baseEffect.color = actionTint;
        baseEffect.intensity =
            (isTelegraphCharge || action_.step == ActionStep::Active)
                ? actionIntensity * 0.32f
                : actionIntensity * 0.50f;
        baseEffect.fresnelPower = 2.5f;
        baseEffect.noiseAmount = actionNoise;
        baseEffect.time = runtime_.stateTimer;
    }

    return baseEffect;
}

void Enemy::DrawEnemyVisual(ModelManager *modelManager, const Camera &camera,
                            const Transform &visual, float alpha,
                            float visualScale, float actionPulse,
                            bool isHitFlashing,
                            const ModelDrawEffect &baseEffect) const {
    auto applyBaseEffect = [&](float effectAlpha) {
        ModelDrawEffect effect = baseEffect;
        if (effectAlpha < 0.999f) {
            effect.enabled = true;
            effect.blendOverride = ModelDrawEffectBlendOverride::Alpha;
            effect.alphaMultiplier = std::clamp(effectAlpha, 0.0f, 1.0f);
            if (!baseEffect.enabled) {
                effect.intensity = 0.0f;
            }
        }
        modelManager->SetDrawEffect(effect);
    };

    Transform scaledVisual = visual;
    const float distanceVisualScale =
        visualScale * FarDistanceVisualScale(visual.position, playerPos_);
    scaledVisual.scale.x *= distanceVisualScale;
    scaledVisual.scale.y *= distanceVisualScale;
    scaledVisual.scale.z *= distanceVisualScale;
    applyBaseEffect(alpha);
    if (alpha > 0.999f && distanceVisualScale > 1.01f && !isHitFlashing) {
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
    applyBaseEffect(alpha);
    modelManager->Draw(modelId_, scaledVisual, camera);
}

void Enemy::DrawEnemyAfterimages(ModelManager *modelManager,
                                 const Camera &camera,
                                 float visualScale) const {
    for (const auto &trail : afterimageGhosts_) {
        if (!trail.isActive) {
            continue;
        }
        const float alpha =
            trail.maxLife > 0.0001f
                ? std::clamp(trail.life / trail.maxLife, 0.0f, 1.0f)
                : 0.0f;
        Transform trailVisual = trail.visual;
        const float distanceVisualScale =
            visualScale *
            FarDistanceVisualScale(trailVisual.position, playerPos_);
        trailVisual.scale.x *= distanceVisualScale * (0.98f + 0.04f * alpha);
        trailVisual.scale.y *= distanceVisualScale * (0.98f + 0.04f * alpha);
        trailVisual.scale.z *= distanceVisualScale * (0.98f + 0.04f * alpha);

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
}

void Enemy::ApplyVictoryDefeatPose(float ratio,
                                   const DirectX::XMFLOAT3 &startPosition,
                                   const DirectX::XMFLOAT3 &playerPosition) {
    ratio = (std::clamp)(ratio, 0.0f, 1.0f);
    const int frame = (std::min)(3, static_cast<int>(std::floor(ratio * 4.0f)));

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
