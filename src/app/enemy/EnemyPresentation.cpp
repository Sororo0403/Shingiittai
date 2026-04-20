#include "Enemy.h"
#include "ModelManager.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

static float Saturate(float value) {
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

static float EaseOutCubic(float t) {
    float u = 1.0f - Saturate(t);
    return 1.0f - u * u * u;
}

static float EaseOutBack(float t) {
    t = Saturate(t);
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.0f;
    float u = t - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}

// ============================================================
// 各部位Transform更新処理
// ============================================================
void Enemy::UpdateParts() {
    float usedYaw = GetVisualYaw();
    float visualYaw = usedYaw;
    float visualPitch = 0.0f;
    float visualRoll = 0.0f;
    float pulse = 0.5f + 0.5f * std::sinf(runtime_.stateTimer * 18.0f);
    float phasePulse =
        0.5f + 0.5f * std::sinf(runtime_.phaseTransitionTimer * 16.0f);
    const bool isDelaySmashWhiffPunish =
        (runtime_.action.kind == ActionKind::Smash &&
         runtime_.action.step == ActionStep::Recovery &&
         runtime_.action.id == ActionId::DelaySmash &&
         !runtime_.currentActionConnected && !runtime_.currentActionGuarded);
    const bool isRushWhiffPunish =
        (runtime_.action.kind == ActionKind::Rush &&
         runtime_.action.step == ActionStep::Recovery &&
         !runtime_.rushWillSweepFollowup && !runtime_.currentActionConnected &&
         !runtime_.currentActionGuarded);

    float forwardX = std::sinf(usedYaw);
    float forwardZ = std::cosf(usedYaw);

    float rightX = std::cosf(usedYaw);
    float rightZ = -std::sinf(usedYaw);

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
        float introT = GetIntroRatio();
        float introPulse = std::sinf(introT * 3.14159265f);
        IntroPhase introPhase = GetIntroPhase();

        if (introPhase == IntroPhase::SecondSlash) {
            float t = introSecondSlashDuration_ > 0.0001f
                          ? runtime_.introTimer / introSecondSlashDuration_
                          : 1.0f;
            t = Saturate(t);
            float slashT = EaseOutCubic(t);

            bodyTf_.position.y -= 0.05f * (1.0f - slashT);
            bodyTf_.position.x += forwardX * 0.06f * slashT;
            bodyTf_.position.z += forwardZ * 0.06f * slashT;
            bodyTf_.scale.x += 0.10f * (1.0f - t);
            bodyTf_.scale.z += 0.10f * (1.0f - t);

            rightHandTf_.position.y += 0.72f * (1.0f - t);
            rightHandTf_.position.x += rightX * 0.22f + (-forwardX) * 0.24f * (1.0f - t);
            rightHandTf_.position.z += rightZ * 0.22f + (-forwardZ) * 0.24f * (1.0f - t);
            leftHandTf_.position.y += 0.18f * (1.0f - t);
            leftHandTf_.position.x += (-rightX) * 0.12f;
            leftHandTf_.position.z += (-rightZ) * 0.12f;

            visualTf_.position.x += forwardX * (introSlashLunge_ * 0.92f) * slashT;
            visualTf_.position.z += forwardZ * (introSlashLunge_ * 0.92f) * slashT;
            visualPitch -= 0.14f * (1.0f - t);
            visualRoll -= 0.06f * (1.0f - t);
        } else if (introPhase == IntroPhase::SpinSlash) {
            float phaseTime =
                runtime_.introTimer - introSecondSlashDuration_;
            float t = introSpinSlashDuration_ > 0.0001f
                          ? phaseTime / introSpinSlashDuration_
                          : 1.0f;
            t = Saturate(t);
            float spinT = EaseOutBack(t);
            float spinYaw =
                6.28318530f * introSpinTurns_ * spinT;

            bodyTf_.position.y += introSpinLift_ * std::sinf(t * 3.14159265f);
            bodyTf_.scale.x += 0.08f * (1.0f - t);
            bodyTf_.scale.z += 0.12f * (1.0f - t);

            rightHandTf_.position.y += 0.45f + 0.22f * introPulse;
            rightHandTf_.position.x += rightX * 0.32f;
            rightHandTf_.position.z += rightZ * 0.32f;
            leftHandTf_.position.y += 0.18f;

            visualTf_.position.x += forwardX * introSpinLunge_ * t;
            visualTf_.position.z += forwardZ * introSpinLunge_ * t;
            visualTf_.position.y += introSpinLift_ * 0.55f *
                                    std::sinf(t * 3.14159265f);
            visualTf_.scale.x += introVisualScaleBoost_ * (1.0f - t);
            visualTf_.scale.z += introVisualScaleBoost_ * (1.0f - t);
            visualYaw += spinYaw;
            visualPitch -= 0.10f * (1.0f - t);
            visualRoll += 0.16f * std::sinf(t * 6.28318530f);
        } else {
            float phaseTime =
                runtime_.introTimer -
                (introSecondSlashDuration_ + introSpinSlashDuration_);
            float t = introSettleDuration_ > 0.0001f
                          ? phaseTime / introSettleDuration_
                          : 1.0f;
            t = Saturate(t);
            float settle = 1.0f - t;

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
        float hitT = hitReactionTimer_ / hitReactionDuration_;
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
                                   ? 1.0f - (counterRecoilTimer_ /
                                             counterRecoilDuration_)
                                   : 1.0f;
        recoilProgress = Saturate(recoilProgress);
        float recoil = recoilProgress * recoilProgress;

        bodyTf_.position.y += 0.015f * recoil;
        visualTf_.position.x += (-forwardX) * 0.045f * recoil;
        visualTf_.position.z += (-forwardZ) * 0.045f * recoil;
        visualPitch += counterRecoilPitchRad_ * recoil;
    }

    const bool suppressActionPresentation = (counterRecoilTimer_ > 0.0f);

    if (runtime_.tellActive) {
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

    if (runtime_.fakeCommitActive) {
        rightHandTf_.position.y += 0.28f * pulse;
        rightHandTf_.position.x += forwardX * 0.24f * pulse;
        rightHandTf_.position.z += forwardZ * 0.24f * pulse;
        rightHandTf_.scale.x += 0.10f * pulse;
        rightHandTf_.scale.y += 0.10f * pulse;
        rightHandTf_.scale.z += 0.10f * pulse;
    }

    if (runtime_.freezeHoldActive) {
        bodyTf_.position.y -= 0.05f;
        bodyTf_.scale.x += 0.06f;
        bodyTf_.scale.z += 0.06f;
        rightHandTf_.scale.x += 0.12f;
        rightHandTf_.scale.y += 0.12f;
        rightHandTf_.scale.z += 0.12f;

        visualTf_.position.y -= 0.03f;
        visualPitch -= 0.08f;
    }

    if (runtime_.phase == BossPhase::Phase2) {
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

        visualTf_.position.y += 0.08f * phasePulse;
        visualTf_.scale.x += 0.06f + 0.04f * phasePulse;
        visualTf_.scale.y += 0.03f;
        visualTf_.scale.z += 0.06f + 0.04f * phasePulse;
        visualPitch -= 0.12f;
        visualRoll += 0.10f * phasePulse;
    }

    if (!suppressActionPresentation &&
        runtime_.action.kind == ActionKind::None &&
        runtime_.isMargitComboATransition) {
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

    if (!suppressActionPresentation &&
        runtime_.action.kind == ActionKind::None &&
        runtime_.isMargitComboBTransition) {
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

    if (!suppressActionPresentation &&
        runtime_.action.kind == ActionKind::Smash) {
        if (runtime_.action.step == ActionStep::Charge ||
            runtime_.action.step == ActionStep::Hold) {
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
        } else if (runtime_.action.step == ActionStep::Active) {
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
        } else if (runtime_.action.step == ActionStep::Recovery) {
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

    } else if (!suppressActionPresentation &&
               runtime_.action.kind == ActionKind::Sweep) {
        if (runtime_.action.step == ActionStep::Charge ||
            runtime_.action.step == ActionStep::Hold) {
            bodyTf_.position.x += rightX * 0.12f;
            bodyTf_.position.z += rightZ * 0.12f;

            rightHandTf_.position.x += rightX * 2.00f;
            rightHandTf_.position.y += 0.25f;
            rightHandTf_.position.z += rightZ * 2.00f;

            leftHandTf_.position.x += (-rightX) * 0.25f;
            leftHandTf_.position.z += (-rightZ) * 0.25f;
            visualYaw += 0.18f;
            visualRoll -= 0.18f;
        } else if (runtime_.action.step == ActionStep::Active) {
            bodyTf_.position.x += (-rightX) * 0.18f;
            bodyTf_.position.z += (-rightZ) * 0.18f;

            rightHandTf_.position.x += (-rightX) * 2.00f;
            rightHandTf_.position.y += 0.2f;
            rightHandTf_.position.z += (-rightZ) * 2.00f;
            visualYaw -= 0.26f;
            visualRoll += 0.26f;
            visualTf_.position.x += (-rightX) * 0.20f;
            visualTf_.position.z += (-rightZ) * 0.20f;
        } else if (runtime_.action.step == ActionStep::Recovery) {
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

    } else if (!suppressActionPresentation && action_.kind == ActionKind::Rush) {
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
            visualTf_.position.x += forwardX * 0.16f;
            visualTf_.position.z += forwardZ * 0.16f;
            visualTf_.position.y -= 0.05f;
            visualPitch -= 0.24f;
        } else if (action_.step == ActionStep::Active) {
            bodyTf_.position.x += forwardX * 0.25f;
            bodyTf_.position.z += forwardZ * 0.25f;
            bodyTf_.position.y += 0.05f;

            rightHandTf_.position.y += 0.1f;
            rightHandTf_.position.x += forwardX * 1.6f;
            rightHandTf_.position.z += forwardZ * 1.6f;
            visualTf_.position.x += forwardX * 0.52f;
            visualTf_.position.z += forwardZ * 0.52f;
            visualPitch += 0.18f;
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

            visualTf_.position.x += forwardX * 0.14f;
            visualTf_.position.z += forwardZ * 0.14f;
            visualPitch += 0.10f;
        }

    } else if (!suppressActionPresentation && action_.kind == ActionKind::Warp) {
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
                } else if (warp_.approachSlot ==
                           WarpApproachSlot::LongFront) {
                    bodyTf_.position.y -= 0.10f;
                    bodyTf_.scale.z += 0.10f;
                    rightHandTf_.position.x += forwardX * 0.28f;
                    rightHandTf_.position.z += forwardZ * 0.28f;
                }
            }
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
                } else if (warp_.approachSlot ==
                           WarpApproachSlot::LongFront) {
                    bodyTf_.position.y -= 0.08f;
                    bodyTf_.position.x += forwardX * 0.12f;
                    bodyTf_.position.z += forwardZ * 0.12f;
                    rightHandTf_.position.x += forwardX * 0.35f;
                    rightHandTf_.position.z += forwardZ * 0.35f;
                }
            }
        }

    } else if (!suppressActionPresentation && action_.kind == ActionKind::Guard) {
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

    DirectX::XMVECTOR visualRot = DirectX::XMQuaternionRotationRollPitchYaw(
        visualPitch, visualYaw, visualRoll);
    DirectX::XMStoreFloat4(&visualTf_.rotation, visualRot);
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
