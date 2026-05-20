#include "GameScene.h"
#include "Input.h"
#include <cmath>

using namespace DirectX;

static constexpr float kPi = 3.14159265f;
static constexpr float kTwoPi = 6.28318530f;
static constexpr float kMinVectorLength = 0.0001f;
static constexpr float kBladeClashWinGuardBreakLead = 0.52f;
static constexpr float kBladeClashWinGuardBreakCameraMoveStart = 0.34f;
static constexpr float kBladeClashWinActionSlow = 0.95f;

static float Clamp(float value, float minValue, float maxValue) {
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return value;
}

static float Clamp01(float value) {
    return Clamp(value, 0.0f, 1.0f);
}

static float SaturatedAlpha(float speed, float deltaTime) {
    return Clamp01(speed * deltaTime);
}

static float WrapRadians(float angle) {
    while (angle > kPi) {
        angle -= kTwoPi;
    }
    while (angle < -kPi) {
        angle += kTwoPi;
    }
    return angle;
}

static XMFLOAT2 NormalizeXZ(float x, float z) {
    float length = std::sqrt(x * x + z * z);
    if (length < kMinVectorLength) {
        length = 1.0f;
    }

    return {x / length, z / length};
}

static float DistanceXZ(const XMFLOAT3 &a, const XMFLOAT3 &b) {
    const float dx = a.x - b.x;
    const float dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

static XMFLOAT3 Lerp(const XMFLOAT3 &from, const XMFLOAT3 &to, float alpha) {
    return {from.x + (to.x - from.x) * alpha,
            from.y + (to.y - from.y) * alpha,
            from.z + (to.z - from.z) * alpha};
}

void GameScene::UpdateCamera(Input *input) {
    // ===== 騾壼�E��E�繧�E�繝｡繝ｩ =====

    // 繝ｭ繝�Eけ繧�E�繝ｳ蛻・�E�譖ｿ縺・
    if (input->IsKeyTrigger(DIK_Q) ||
        input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_RIGHT_SHOULDER)) {
        isLockOn_ = !isLockOn_;
    }

    float yawInput = 0.0f;
    float pitchInput = 0.0f;

#ifdef _DEBUG
    if (input->IsKeyPress(DIK_LEFT)) {
        yawInput -= 1.0f;
    }
    if (input->IsKeyPress(DIK_RIGHT)) {
        yawInput += 1.0f;
    }
    if (input->IsKeyPress(DIK_UP)) {
        pitchInput += 1.0f;
    }
    if (input->IsKeyPress(DIK_DOWN)) {
        pitchInput -= 1.0f;
    }
#endif

    if (input->IsGamepadConnected() && player_.UsesGamepadCameraLook()) {
        yawInput += input->GetGamepadRightStickX();
        pitchInput += input->GetGamepadRightStickY();
    }

    cameraYaw_ += yawInput * cameraLookSensitivity_;
    cameraPitch_ += pitchInput * cameraLookSensitivity_;
    cameraPitch_ = Clamp(cameraPitch_, cameraPitchMin_, cameraPitchMax_);

    // 箝�E譛驥崎ｦ・�E�壹�E�E��薙〒繧�E�繝｡繝ｩ遒ｺ螳・
    UpdateBattleCamera();

    // 箝�E譛驥崎ｦ・�E�夊｡悟�E譖ｴ譁E��
    camera_.UpdateMatrices();
}

void GameScene::UpdateBattleCamera() {
    const auto &playerTf = player_.GetTransform();
    const auto &enemyTf = enemy_.GetTransform();

    const DirectX::XMFLOAT3 &playerPos = playerTf.position;
    const DirectX::XMFLOAT3 &enemyPos = enemyTf.position;

    if (victorySequenceActive_) {
        float toEnemyX = enemyPos.x - playerPos.x;
        float toEnemyZ = enemyPos.z - playerPos.z;
        const XMFLOAT2 line = NormalizeXZ(toEnemyX, toEnemyZ);
        const float sideX = line.y;
        const float sideZ = -line.x;
        const DirectX::XMFLOAT3 frontCameraPos = {
            enemyPos.x - line.x * 8.2f,
            enemyPos.y + 2.25f,
            enemyPos.z - line.y * 8.2f};
        const DirectX::XMFLOAT3 frontLookAt = {
            enemyPos.x,
            enemyPos.y + 1.45f,
            enemyPos.z};
        const DirectX::XMFLOAT3 fallCameraPos = {
            enemyPos.x - line.x * 7.6f + sideX * 4.2f,
            enemyPos.y + 0.92f,
            enemyPos.z - line.y * 7.6f + sideZ * 4.2f};
        const DirectX::XMFLOAT3 fallLookAt = {
            enemyPos.x + line.x * 0.55f,
            enemyPos.y + 0.95f,
            enemyPos.z + line.y * 0.55f};
        const float introMoveT = Clamp01(victorySequenceTimer_ / 0.72f);
        const float cutT = introMoveT * introMoveT * (3.0f - 2.0f * introMoveT);
        const DirectX::XMFLOAT3 desiredCameraPos =
            Lerp(frontCameraPos, fallCameraPos, cutT);
        const DirectX::XMFLOAT3 desiredLookAt = Lerp(frontLookAt, fallLookAt, cutT);
        const float alpha =
            victorySequenceTimer_ < 0.82f ? 1.0f
                                           : SaturatedAlpha(9.5f, ctx_->deltaTime);
        lockOnOrbitCameraPos_ =
            Lerp(lockOnOrbitCameraPos_, desiredCameraPos, alpha);
        lockOnLookAt_ = Lerp(lockOnLookAt_, desiredLookAt, alpha);
        targetFovDeg_ = 52.0f + 4.0f * cutT;
        currentFovDeg_ +=
            (targetFovDeg_ - currentFovDeg_) *
            SaturatedAlpha(5.8f, ctx_->deltaTime);
        camera_.SetPerspectiveFovDeg(currentFovDeg_);
        camera_.SetPosition(lockOnOrbitCameraPos_);
        camera_.LookAt(lockOnLookAt_);
        return;
    }
    if (battleIntroActive_) {
        float toEnemyX = enemyPos.x - playerPos.x;
        float toEnemyZ = enemyPos.z - playerPos.z;
        const XMFLOAT2 line = NormalizeXZ(toEnemyX, toEnemyZ);
        const float sideX = line.y;
        const float sideZ = -line.x;
        const float settle =
            Clamp01((battleIntroTimer_ - 2.86f) /
                    (battleIntroDuration_ - 2.86f));
        const float settleEase = settle * settle * (3.0f - 2.0f * settle);
        const float revealCharge = Clamp01(battleIntroTimer_ / 2.36f);
        const float revealEase =
            revealCharge * revealCharge * (3.0f - 2.0f * revealCharge);
        const float flashKick =
            Clamp01(1.0f - std::fabs(battleIntroTimer_ - 2.36f) / 0.24f);
        const float threatShake =
            (1.0f - settleEase) * revealEase *
            (0.5f + 0.5f * std::sinf(sceneLightTime_ * 22.0f));

        const DirectX::XMFLOAT3 summonCameraPos = {
            enemyPos.x - line.x * 10.8f + sideX * 1.65f,
            enemyPos.y + 3.05f,
            enemyPos.z - line.y * 10.8f + sideZ * 1.65f};
        const DirectX::XMFLOAT3 revealCameraPos = {
            enemyPos.x - line.x * 8.7f + sideX * 0.70f,
            enemyPos.y + 2.86f + flashKick * 0.10f,
            enemyPos.z - line.y * 8.7f + sideZ * 0.70f};
        const DirectX::XMFLOAT3 playCameraPos = {
            playerPos.x - line.x * 6.0f + sideX * 1.15f,
            playerPos.y + 2.35f,
            playerPos.z - line.y * 6.0f + sideZ * 1.15f};

        const DirectX::XMFLOAT3 summonLookAt = {
            enemyPos.x,
            enemyPos.y + 1.45f + 0.22f * revealEase,
            enemyPos.z};
        const DirectX::XMFLOAT3 revealLookAt = {
            enemyPos.x + line.x * 0.12f,
            enemyPos.y + 1.62f + flashKick * 0.08f,
            enemyPos.z + line.y * 0.12f};
        const DirectX::XMFLOAT3 playLookAt = {
            playerPos.x * 0.34f + enemyPos.x * 0.66f,
            playerPos.y * 0.26f + enemyPos.y * 0.74f + 1.36f,
            playerPos.z * 0.34f + enemyPos.z * 0.66f};

        const DirectX::XMFLOAT3 heroCameraPos =
            Lerp(summonCameraPos, revealCameraPos, revealEase);
        const DirectX::XMFLOAT3 heroLookAt =
            Lerp(summonLookAt, revealLookAt, revealEase);
        DirectX::XMFLOAT3 desiredCameraPos =
            Lerp(heroCameraPos, playCameraPos, settleEase);
        DirectX::XMFLOAT3 desiredLookAt =
            Lerp(heroLookAt, playLookAt, settleEase);
        desiredCameraPos.x += sideX * (flashKick * 0.14f + threatShake * 0.055f);
        desiredCameraPos.y += flashKick * 0.05f + threatShake * 0.024f;
        desiredCameraPos.z += sideZ * (flashKick * 0.14f + threatShake * 0.055f);
        lockOnOrbitCameraPos_ = desiredCameraPos;
        lockOnLookAt_ = desiredLookAt;
        const float heroFov = 58.0f - 4.0f * revealEase + 2.0f * flashKick;
        currentFovDeg_ = heroFov + (66.0f - heroFov) * settleEase;
        camera_.SetPerspectiveFovDeg(currentFovDeg_);
        camera_.SetPosition(lockOnOrbitCameraPos_);
        camera_.LookAt(lockOnLookAt_);
        return;
    }
    if (defeatSequenceActive_) {
        float toEnemyX = enemyPos.x - playerPos.x;
        float toEnemyZ = enemyPos.z - playerPos.z;
        const XMFLOAT2 line = NormalizeXZ(toEnemyX, toEnemyZ);
        const float sideX = line.y;
        const float sideZ = -line.x;
        const float ratio =
            Clamp01(defeatSequenceTimer_ / defeatSequenceDuration_);
        const float fall =
            Clamp01((defeatSequenceTimer_ - 0.34f) / 1.22f);
        const DirectX::XMFLOAT3 startCameraPos = {
            playerPos.x - line.x * 5.0f + sideX * 1.85f,
            playerPos.y + 1.85f,
            playerPos.z - line.y * 5.0f + sideZ * 1.85f};
        const DirectX::XMFLOAT3 fallCameraPos = {
            playerPos.x - line.x * 4.3f + sideX * 3.2f,
            playerPos.y + 0.78f,
            playerPos.z - line.y * 4.3f + sideZ * 3.2f};
        const DirectX::XMFLOAT3 startLookAt = {
            playerPos.x, playerPos.y + 1.05f, playerPos.z};
        const DirectX::XMFLOAT3 fallLookAt = {
            playerPos.x + line.x * 0.42f,
            playerPos.y + 0.46f - 0.16f * fall,
            playerPos.z + line.y * 0.42f};
        const float cutT = fall * fall * (3.0f - 2.0f * fall);
        const DirectX::XMFLOAT3 desiredCameraPos =
            Lerp(startCameraPos, fallCameraPos, cutT);
        const DirectX::XMFLOAT3 desiredLookAt = Lerp(startLookAt, fallLookAt, cutT);
        const float alpha = defeatSequenceTimer_ < 0.12f
                                ? 1.0f
                                : SaturatedAlpha(8.2f, ctx_->deltaTime);
        lockOnOrbitCameraPos_ =
            Lerp(lockOnOrbitCameraPos_, desiredCameraPos, alpha);
        lockOnLookAt_ = Lerp(lockOnLookAt_, desiredLookAt, alpha);
        targetFovDeg_ = 58.0f + 8.0f * ratio;
        currentFovDeg_ +=
            (targetFovDeg_ - currentFovDeg_) *
            SaturatedAlpha(5.8f, ctx_->deltaTime);
        camera_.SetPerspectiveFovDeg(currentFovDeg_);
        camera_.SetPosition(lockOnOrbitCameraPos_);
        camera_.LookAt(lockOnLookAt_);
        return;
    }

    // 謨�E�陦悟虚迥�E�諷九ｒ蜿門�E�・
    const ActionKind enemyActionKind = enemy_.GetActionKind();
    const ActionStep enemyActionStep = enemy_.GetActionStep();

    const bool isEnemyWarpStart = (enemyActionKind == ActionKind::Warp &&
                                   enemyActionStep == ActionStep::Start);

    const bool isEnemyWarpMove = (enemyActionKind == ActionKind::Warp &&
                                  enemyActionStep == ActionStep::Move);

    const bool isEnemyWarpEnd = (enemyActionKind == ActionKind::Warp &&
                                 enemyActionStep == ActionStep::End);
    const bool isEnemyMeleeAction =
        enemyActionKind == ActionKind::Smash ||
        enemyActionKind == ActionKind::Sweep;
    const bool isEnemyPressureAction =
        isEnemyMeleeAction &&
        (enemyActionStep == ActionStep::Charge ||
         enemyActionStep == ActionStep::Active ||
         enemyActionStep == ActionStep::Recovery);
    const bool isEnemyWideAction =
        enemyActionKind == ActionKind::Wave ||
        enemyActionKind == ActionKind::Cage;
    const bool isEnemyPhaseTransition =
        enemy_.IsPhaseTransitionActive() && !bladeClashFinishActive_;
    const float enemyPhaseTransitionRatio = enemy_.GetPhaseTransitionRatio();
    const float chargeFocus = Clamp01(chargeWeakPointFocusRatio_);

    // =========================
    // FOV繧�E�繝ｼ繧�E�繝�Eヨ豎ｺ螳・
    // =========================
    targetFovDeg_ = normalFovDeg_;

    if (isLockOn_) {
        targetFovDeg_ = lockOnFovDeg_;
    }

    if (isEnemyWarpStart || isEnemyWarpMove || isEnemyWarpEnd) {
        targetFovDeg_ = warpFovDeg_;
    }

    if (isEnemyWideAction) {
        targetFovDeg_ = 76.5f;
    } else if (isEnemyPressureAction) {
        targetFovDeg_ = 71.5f;
    } else if (bladeClashActive_) {
        const float clashAdvantage = Clamp(bladeClashGauge_, -1.0f, 1.0f);
        targetFovDeg_ = 72.0f - Clamp01(clashAdvantage) * 4.0f +
                        Clamp01(-clashAdvantage) * 5.0f;
    } else if (enemyActionKind == ActionKind::BladeClash) {
        targetFovDeg_ = 74.5f;
    }

    if (isEnemyPhaseTransition) {
        targetFovDeg_ = phaseTransitionFovDeg_;
    }
    if (chargeFocus > 0.0f) {
        targetFovDeg_ = targetFovDeg_ * (1.0f - chargeFocus) +
                        58.0f * chargeFocus;
    }
    if (playerViewCamera_) {
        targetFovDeg_ = isLockOn_ ? 82.0f : normalFovDeg_;
        if (isEnemyWideAction) {
            targetFovDeg_ = 86.0f;
        } else if (isEnemyPressureAction) {
            targetFovDeg_ = 80.0f;
        } else if (bladeClashActive_) {
            const float clashAdvantage = Clamp(bladeClashGauge_, -1.0f, 1.0f);
            targetFovDeg_ = 74.0f - Clamp01(clashAdvantage) * 3.0f +
                            Clamp01(-clashAdvantage) * 4.0f;
        } else if (enemyActionKind == ActionKind::BladeClash) {
            targetFovDeg_ = 84.0f;
        }
        if (isEnemyWarpStart || isEnemyWarpMove || isEnemyWarpEnd) {
            targetFovDeg_ = warpFovDeg_;
        }
        if (isEnemyPhaseTransition) {
            targetFovDeg_ = phaseTransitionFovDeg_;
        }
        if (chargeFocus > 0.0f) {
            targetFovDeg_ = targetFovDeg_ * (1.0f - chargeFocus) +
                            62.0f * chargeFocus;
        }
    }

    float usedFovLerpSpeed = fovLerpSpeed_;
    if (isEnemyPhaseTransition) {
        usedFovLerpSpeed = phaseTransitionFovLerpSpeed_;
    }
    const float fovAlpha = SaturatedAlpha(usedFovLerpSpeed, ctx_->deltaTime);
    currentFovDeg_ += (targetFovDeg_ - currentFovDeg_) * fovAlpha;
    camera_.SetPerspectiveFovDeg(currentFovDeg_ + combatFeedback_.GetFovKickDeg());

    if (bladeClashActive_) {
        const DirectX::XMFLOAT3 playerPosNow = player_.GetTransform().position;
        const DirectX::XMFLOAT3 enemyPosNow = enemy_.GetTransform().position;
        const XMFLOAT2 line = NormalizeXZ(enemyPosNow.x - playerPosNow.x,
                                          enemyPosNow.z - playerPosNow.z);
        const XMFLOAT3 right = {line.y, 0.0f, -line.x};
        const float advantage = Clamp(bladeClashGauge_, -1.0f, 1.0f);
        const float playerPush = Clamp01(advantage);
        const float enemyPush = Clamp01(-advantage);
        const float pressureEase =
            std::abs(advantage) * std::abs(advantage) *
            (3.0f - 2.0f * std::abs(advantage));
        const XMFLOAT3 mid = {
            bladeClashCenter_.x + line.x * advantage * 0.38f,
            bladeClashCenter_.y + 0.06f + playerPush * 0.05f - enemyPush * 0.03f,
            bladeClashCenter_.z + line.y * advantage * 0.38f};
        const float push = Clamp01(bladeClashCameraPush_);
        const float pulse = push * push * (3.0f - 2.0f * push);
        const float impact = Clamp01(bladeClashImpactPulse_);
        const float tension = 0.35f + std::abs(bladeClashGauge_) * 0.40f;
        const float shakePhase = sceneLightTime_ * (34.0f + 18.0f * impact);
        const float shake =
            (0.018f + 0.034f * impact + enemyPush * 0.018f) * tension;
        const XMFLOAT3 playerShoulder = {
            playerPosNow.x + line.x * 0.48f + right.x * 0.22f,
            playerPosNow.y + 0.92f,
            playerPosNow.z + line.y * 0.48f + right.z * 0.22f};
        const float cameraBackDistance =
            4.56f + enemyPush * 0.92f - playerPush * 0.82f;
        const float cameraSideDistance =
            2.10f + 0.16f * pulse + playerPush * 0.34f - enemyPush * 0.20f;
        const float cameraHeight =
            0.70f + 0.10f * pulse - playerPush * 0.08f + enemyPush * 0.24f;
        const float pushIn = playerPush * (0.54f + 0.36f * pressureEase);
        const float recoilBack = enemyPush * (0.30f + 0.44f * pressureEase);
        DirectX::XMFLOAT3 cameraPos = {
            playerPosNow.x - line.x * cameraBackDistance +
                line.x * pushIn - line.x * recoilBack +
                right.x * cameraSideDistance +
                right.x * std::sinf(shakePhase) * shake,
            playerPosNow.y + cameraHeight +
                std::cosf(shakePhase * 1.31f) * shake * 0.42f,
            playerPosNow.z - line.y * cameraBackDistance +
                line.y * pushIn - line.y * recoilBack +
                right.z * cameraSideDistance +
                right.z * std::sinf(shakePhase) * shake};
        DirectX::XMFLOAT3 lookAt = {
            playerShoulder.x * 0.34f + mid.x * 0.66f +
                line.x * (0.12f + 0.44f * playerPush - 0.22f * enemyPush),
            playerShoulder.y * 0.28f + mid.y * 0.72f + 0.14f +
                0.05f * impact + playerPush * 0.03f - enemyPush * 0.04f,
            playerShoulder.z * 0.34f + mid.z * 0.66f +
                line.y * (0.12f + 0.44f * playerPush - 0.22f * enemyPush)};
        cameraYaw_ = std::atan2f(line.x, line.y);
        combatFeedback_.ApplyCameraImpulse(cameraPos, lookAt, sceneLightTime_);
        camera_.SetPosition(cameraPos);
        camera_.LookAt(lookAt);
        return;
    }

    if (bladeClashFinishActive_) {
        const XMFLOAT2 line =
            bladeClashFinishPlayerWon_
                ? NormalizeXZ(bladeClashDirection_.x, bladeClashDirection_.z)
                : NormalizeXZ(enemyPos.x - playerPos.x,
                              enemyPos.z - playerPos.z);
        const XMFLOAT3 right = {line.y, 0.0f, -line.x};
        const float ratio =
            Clamp01(bladeClashFinishTimer_ / bladeClashFinishDuration_);
        const float strike = std::sinf(Clamp01(ratio / 0.18f) * kPi);
        const float hold = 1.0f - Clamp01((ratio - 0.10f) / 0.30f);
        const XMFLOAT3 center =
            bladeClashFinishPlayerWon_
                ? XMFLOAT3{bladeClashFinishCenter_.x,
                           bladeClashFinishCenter_.y + 0.06f,
                           bladeClashFinishCenter_.z}
                : XMFLOAT3{(playerPos.x + enemyPos.x) * 0.5f,
                           playerPos.y + 1.18f,
                           (playerPos.z + enemyPos.z) * 0.5f};
        if (bladeClashFinishPlayerWon_) {
            const float winActionTimer =
                (std::max)(0.0f, bladeClashFinishTimer_ -
                                      kBladeClashWinGuardBreakLead) /
                kBladeClashWinActionSlow;
            const float winDuration =
                (std::max)(0.001f, bladeClashFinishDuration_ -
                                      kBladeClashWinGuardBreakLead) /
                kBladeClashWinActionSlow;
            const float winRatio = Clamp01(winActionTimer / winDuration);
            const float cameraBlend = Clamp01(
                (bladeClashFinishTimer_ -
                 kBladeClashWinGuardBreakCameraMoveStart) /
                (kBladeClashWinGuardBreakLead -
                 kBladeClashWinGuardBreakCameraMoveStart));
            const XMFLOAT3 guardBreakCamera = {
                bladeClashFinishEnemyStart_.x - line.x * 6.60f +
                    right.x * 5.85f,
                bladeClashFinishEnemyStart_.y + 1.72f,
                bladeClashFinishEnemyStart_.z - line.y * 6.60f +
                    right.z * 5.85f};
            const XMFLOAT3 guardBreakLookAt = {
                bladeClashFinishEnemyStart_.x - line.x * 0.18f,
                bladeClashFinishEnemyStart_.y + 1.22f,
                bladeClashFinishEnemyStart_.z - line.y * 0.18f};
            if (bladeClashFinal_) {
                const float launchT =
                    Clamp01((winActionTimer - 0.36f) / 0.82f);
                const float catchT =
                    Clamp01((winActionTimer - 0.78f) / 0.62f);
                const float crashT =
                    Clamp01((winActionTimer - 2.14f) / 0.36f);
                const float catchEase =
                    catchT * catchT * (3.0f - 2.0f * catchT);
                const XMFLOAT3 enemyChest = {
                    enemyPos.x, enemyPos.y + 1.18f, enemyPos.z};
                const XMFLOAT3 startFocus = {
                    bladeClashFinishEnemyStart_.x + line.x * 0.45f,
                    bladeClashFinishEnemyStart_.y + 1.22f,
                    bladeClashFinishEnemyStart_.z + line.y * 0.45f};
                const XMFLOAT3 chaseFocus = {
                    enemyChest.x - line.x * 0.86f,
                    enemyChest.y + 0.18f,
                    enemyChest.z - line.y * 0.86f};
                const float shake =
                    (0.030f + 0.062f * launchT +
                     0.120f * std::sinf(crashT * kPi));
                const float phase = sceneLightTime_ * 49.0f;
                const XMFLOAT3 startCamera = {
                    bladeClashFinishPlayerStart_.x - line.x * 3.60f +
                        right.x * 2.35f,
                    bladeClashFinishPlayerStart_.y + 1.35f,
                    bladeClashFinishPlayerStart_.z - line.y * 3.60f +
                        right.z * 2.35f};
                const XMFLOAT3 chaseCamera = {
                    enemyPos.x - line.x * (5.95f + 0.70f * launchT) +
                        right.x * 2.25f,
                    enemyPos.y + 1.72f + 0.18f * launchT,
                    enemyPos.z - line.y * (5.95f + 0.70f * launchT) +
                        right.z * 2.25f};
                XMFLOAT3 cameraPos = Lerp(startCamera, chaseCamera, catchEase);
                cameraPos.x += right.x * std::sinf(phase) * shake;
                cameraPos.y += std::cosf(phase * 1.27f) * shake * 0.45f;
                cameraPos.z += right.z * std::sinf(phase) * shake;
                XMFLOAT3 lookAt = Lerp(startFocus, chaseFocus, catchEase);
                if (bladeClashFinishTimer_ < kBladeClashWinGuardBreakLead) {
                    cameraPos = Lerp(guardBreakCamera, cameraPos, cameraBlend);
                    lookAt = Lerp(guardBreakLookAt, lookAt, cameraBlend);
                }
                cameraYaw_ = std::atan2f(line.x, line.y);
                targetFovDeg_ = 66.0f + 7.0f * catchEase +
                                2.0f * std::sinf(crashT * kPi);
                currentFovDeg_ += (targetFovDeg_ - currentFovDeg_) *
                                  SaturatedAlpha(8.0f, ctx_->deltaTime);
                camera_.SetPerspectiveFovDeg(currentFovDeg_ +
                                             combatFeedback_.GetFovKickDeg());
                combatFeedback_.ApplyCameraImpulse(cameraPos, lookAt,
                                                   sceneLightTime_);
                camera_.SetPosition(cameraPos);
                camera_.LookAt(lookAt);
                return;
            }
            const float cutT =
                Clamp01(winActionTimer / 0.30f);
            const float slideT =
                Clamp01((winActionTimer - 0.18f) / 0.86f);
            const float cutEase = 1.0f - std::pow(1.0f - cutT, 4.0f);
            const float slideEase =
                1.0f - std::pow(1.0f - slideT, 2.0f);
            const float dashEase =
                Clamp01(0.54f * cutEase + 0.30f * slideEase);
            const float slide =
                1.0f - std::pow(1.0f - Clamp01(winRatio / 0.58f), 2.0f);
            const XMFLOAT3 cinematicPlayer = {
                bladeClashFinishPlayerStart_.x +
                    (bladeClashFinishPlayerEnd_.x -
                     bladeClashFinishPlayerStart_.x) *
                        dashEase,
                bladeClashFinishPlayerStart_.y +
                    (bladeClashFinishPlayerEnd_.y -
                     bladeClashFinishPlayerStart_.y) *
                        dashEase,
                bladeClashFinishPlayerStart_.z +
                    (bladeClashFinishPlayerEnd_.z -
                     bladeClashFinishPlayerStart_.z) *
                        dashEase};
            const XMFLOAT3 enemyChest = {
                enemyPos.x, enemyPos.y + 1.16f, enemyPos.z};
            const XMFLOAT3 lookAtBase = {
                enemyChest.x + line.x * (1.35f + 0.82f * slide),
                enemyChest.y + 0.20f,
                enemyChest.z + line.y * (1.35f + 0.82f * slide)};
            const float shake = (0.064f * strike + 0.014f * hold) * 0.74f;
            const float phase = sceneLightTime_ * 44.0f;
            XMFLOAT3 cameraPos = {
                cinematicPlayer.x + line.x * (5.65f + 1.05f * slide) +
                    right.x * (2.05f + 0.46f * slide) +
                    right.x * std::sinf(phase) * shake,
                cinematicPlayer.y + 1.60f +
                    std::cosf(phase * 1.27f) * shake * 0.5f,
                cinematicPlayer.z + line.y * (5.65f + 1.05f * slide) +
                    right.z * (2.05f + 0.46f * slide) +
                    right.z * std::sinf(phase) * shake};
            XMFLOAT3 lookAt = {
                lookAtBase.x + right.x * 0.12f,
                lookAtBase.y,
                lookAtBase.z + right.z * 0.12f};
            if (bladeClashFinishTimer_ < kBladeClashWinGuardBreakLead) {
                cameraPos = Lerp(guardBreakCamera, cameraPos, cameraBlend);
                lookAt = Lerp(guardBreakLookAt, lookAt, cameraBlend);
            }
            cameraYaw_ = std::atan2f(line.x, line.y);
            targetFovDeg_ = 68.0f;
            currentFovDeg_ += (targetFovDeg_ - currentFovDeg_) *
                              SaturatedAlpha(7.0f, ctx_->deltaTime);
            camera_.SetPerspectiveFovDeg(currentFovDeg_ +
                                         combatFeedback_.GetFovKickDeg());
            combatFeedback_.ApplyCameraImpulse(cameraPos, lookAt,
                                               sceneLightTime_);
            camera_.SetPosition(cameraPos);
            camera_.LookAt(lookAt);
            return;
        }
        const float leanT = Clamp01(bladeClashFinishTimer_ / 0.78f);
        const float recoilT =
            Clamp01((bladeClashFinishTimer_ - 0.78f) / 0.36f);
        const float slideT =
            Clamp01((bladeClashFinishTimer_ - 1.12f) / 0.52f);
        const float leanEase = leanT * leanT * (3.0f - 2.0f * leanT);
        const float recoilEase = 1.0f - std::pow(1.0f - recoilT, 3.0f);
        const float settleT = Clamp01((slideT - 0.74f) / 0.26f);
        const float settleEase = settleT * settleT * (3.0f - 2.0f * settleT);
        const float slideEase =
            slideT < 0.18f
                ? 0.06f * std::pow(Clamp01(slideT / 0.18f), 2.0f)
                : slideT < 0.74f
                      ? 0.06f +
                            0.88f *
                                (1.0f -
                                 std::pow(1.0f -
                                              Clamp01((slideT - 0.18f) / 0.56f),
                                          5.0f))
                      : 0.94f + 0.06f * settleEase;
        const float retreat =
            0.24f * leanEase + 0.56f * recoilEase +
            (15.80f - 0.80f) * slideEase;
        XMFLOAT3 cinematicPlayer = {
            bladeClashFinishPlayerStart_.x - bladeClashDirection_.x * retreat,
            bladeClashFinishPlayerStart_.y,
            bladeClashFinishPlayerStart_.z - bladeClashDirection_.z * retreat};
        cinematicPlayer.y += std::sinf(recoilT * kPi) * 0.10f;
        cinematicPlayer.y += std::sinf(slideT * kPi) * 0.92f;
        const float catchPrep =
            Clamp01((bladeClashFinishTimer_ - 0.92f) / 0.28f);
        const float catchLaunch =
            Clamp01((bladeClashFinishTimer_ - 1.12f) / 0.34f);
        const float catchPrepEase =
            catchPrep * catchPrep * (3.0f - 2.0f * catchPrep);
        const float catchLaunchEase =
            1.0f - std::pow(1.0f - catchLaunch, 4.0f);
        const float catchT = Clamp01(0.18f * catchPrepEase +
                                     0.82f * catchLaunchEase);
        const float crashT = Clamp01((bladeClashFinishTimer_ - 1.52f) / 0.18f);
        const float crash = std::sinf(crashT * kPi);
        const float shake =
            (0.070f * strike + 0.018f * hold + 0.120f * crash) * 0.92f;
        const float phase = sceneLightTime_ * 44.0f;
        const XMFLOAT3 sideCamera = {
            center.x + right.x * 5.35f - line.x * 1.15f,
            center.y + 1.10f,
            center.z + right.z * 5.35f - line.y * 1.15f};
        const XMFLOAT3 frontCamera = {
            cinematicPlayer.x - line.x * (5.10f + 1.20f * catchT) +
                right.x * (0.78f - 0.38f * catchT),
            cinematicPlayer.y + 1.35f + 0.46f * catchT,
            cinematicPlayer.z - line.y * (5.10f + 1.20f * catchT) +
                right.z * (0.78f - 0.38f * catchT)};
        XMFLOAT3 cameraPos = Lerp(sideCamera, frontCamera, catchT);
        cameraPos.x += right.x * std::sinf(phase) * shake;
        cameraPos.y += std::cosf(phase * 1.27f) * shake * 0.5f;
        cameraPos.z += right.z * std::sinf(phase) * shake;
        const XMFLOAT3 sideLookAt = {
            center.x + line.x * 0.12f,
            center.y + 0.04f,
            center.z + line.y * 0.12f};
        const XMFLOAT3 frontLookAt = {
            cinematicPlayer.x + line.x * (0.74f - 0.30f * catchT),
            cinematicPlayer.y + 1.02f + 0.12f * catchT,
            cinematicPlayer.z + line.y * (0.74f - 0.30f * catchT)};
        XMFLOAT3 lookAt = Lerp(sideLookAt, frontLookAt, catchT);
        cameraYaw_ = std::atan2f(line.x, line.y);
        targetFovDeg_ = 72.0f + 6.0f * catchT;
        currentFovDeg_ += (targetFovDeg_ - currentFovDeg_) *
                          SaturatedAlpha(7.0f, ctx_->deltaTime);
        camera_.SetPerspectiveFovDeg(currentFovDeg_ +
                                     combatFeedback_.GetFovKickDeg());
        combatFeedback_.ApplyCameraImpulse(cameraPos, lookAt, sceneLightTime_);
        camera_.SetPosition(cameraPos);
        camera_.LookAt(lookAt);
        return;
    }

    if (playerViewCamera_) {
        const DirectX::XMFLOAT3 playerEye = {
            playerPos.x, playerPos.y + playerViewEyeHeight_, playerPos.z};

        if (isLockOn_) {
            float dx = enemyPos.x - playerPos.x;
            float dz = enemyPos.z - playerPos.z;
            float targetYaw = std::atan2f(dx, dz);
            float diff = WrapRadians(targetYaw - cameraYaw_);

            float inputMagnitude = 0.0f;
#ifdef _DEBUG
            Input *input = ctx_->input;
            if (input->IsKeyPress(DIK_LEFT) || input->IsKeyPress(DIK_RIGHT)) {
                inputMagnitude = 1.0f;
            }
#endif
            if (ctx_->input != nullptr && ctx_->input->IsGamepadConnected() &&
                player_.UsesGamepadCameraLook()) {
                inputMagnitude =
                    (std::max)(inputMagnitude,
                               std::abs(ctx_->input->GetGamepadRightStickX()));
            }

            const float assistScale = inputMagnitude > 0.0f ? 0.42f : 1.0f;
            const float applied =
                Clamp(diff * 7.2f * assistScale * ctx_->deltaTime,
                      -8.0f * ctx_->deltaTime, 8.0f * ctx_->deltaTime);
            cameraYaw_ += applied;
        }

        const float viewCosPitch = std::cosf(cameraPitch_);
        const DirectX::XMFLOAT3 viewForward = {
            std::sinf(cameraYaw_) * viewCosPitch,
            std::sinf(cameraPitch_),
            std::cosf(cameraYaw_) * viewCosPitch};
        const DirectX::XMFLOAT3 viewRight = {std::cosf(cameraYaw_), 0.0f,
                                             -std::sinf(cameraYaw_)};

        DirectX::XMFLOAT3 cameraPos = {
            playerEye.x + viewForward.x * playerViewForwardOffset_ +
                viewRight.x * playerViewSideOffset_,
            playerEye.y + viewForward.y * playerViewForwardOffset_,
            playerEye.z + viewForward.z * playerViewForwardOffset_ +
                viewRight.z * playerViewSideOffset_};

        DirectX::XMFLOAT3 lookAt{};
        if (isLockOn_) {
            const float enemyLookHeight =
                isEnemyWideAction ? playerViewLockOnLookHeight_ + 0.22f
                                  : playerViewLockOnLookHeight_;
            DirectX::XMFLOAT3 desiredLookAt = {
                enemyPos.x, enemyPos.y + enemyLookHeight, enemyPos.z};

            const float lookAlpha = SaturatedAlpha(16.0f, ctx_->deltaTime);
            lockOnLookAt_ = Lerp(lockOnLookAt_, desiredLookAt, lookAlpha);
            lookAt = lockOnLookAt_;
        } else {
            lookAt = {
                cameraPos.x + viewForward.x * playerViewLookAhead_,
                cameraPos.y + viewForward.y * playerViewLookAhead_,
                cameraPos.z + viewForward.z * playerViewLookAhead_};
            lockOnLookAt_ = lookAt;
        }

        if (isEnemyPhaseTransition) {
            DirectX::XMFLOAT3 transitionLookAt = {
                enemyPos.x, enemyPos.y + phaseTransitionLookAtHeight_,
                enemyPos.z};
            lookAt = Lerp(lookAt, transitionLookAt, enemyPhaseTransitionRatio);
        }
        if (chargeFocus > 0.0f) {
            const DirectX::XMFLOAT3 focusLookAt = {
                enemyPos.x, enemyPos.y + 1.70f, enemyPos.z};
            lookAt = Lerp(lookAt, focusLookAt, chargeFocus);
        }

        combatFeedback_.ApplyCameraImpulse(cameraPos, lookAt, sceneLightTime_);
        camera_.SetPosition(cameraPos);
        camera_.LookAt(lookAt);
        return;
    }

    // =========================
    // 繝ｭ繝�Eけ繧�E�繝ｳ荳�E�縺�E�縺・yaw 陬懷勧
    // =========================
    if (isLockOn_) {
        DirectX::XMFLOAT3 assistTarget = enemyPos;

        float assistStrength = lockOnAssistStrength_;
        float assistMaxStep = lockOnAssistMaxStep_;

        if (isEnemyWarpStart) {
            assistStrength = warpStartAssistStrength_;
            assistMaxStep = warpStartAssistMaxStep_;
        } else if (isEnemyWarpMove) {
            // 繝ｯ繝ｼ繝礼�E��E�蜍穂ｸ�E�縺�E�辟｡送E�E↓謖ｯ繧雁屓縺輔�E縺・
            assistStrength = 0.0f;
            assistMaxStep = 0.0f;
        } else if (isEnemyWarpEnd) {
            assistStrength = warpEndAssistStrength_;
            assistMaxStep = warpEndAssistMaxStep_;
        } else if (isEnemyPhaseTransition) {
            assistStrength = lockOnAssistStrength_ * 1.35f;
            assistMaxStep = lockOnAssistMaxStep_ * 1.35f;
        }

        float dx = assistTarget.x - playerPos.x;
        float dz = assistTarget.z - playerPos.z;

        float targetYaw = std::atan2f(dx, dz);
        float diff = WrapRadians(targetYaw - cameraYaw_);

        float inputMagnitude = 0.0f;
#ifdef _DEBUG
        Input *input = ctx_->input;
        if (input->IsKeyPress(DIK_LEFT) || input->IsKeyPress(DIK_RIGHT)) {
            inputMagnitude = 1.0f;
        }
#endif
        if (ctx_->input != nullptr && ctx_->input->IsGamepadConnected() &&
            player_.UsesGamepadCameraLook()) {
            const float stick = std::abs(ctx_->input->GetGamepadRightStickX());
            if (stick > inputMagnitude) {
                inputMagnitude = stick;
            }
        }

        float assistScale = 1.0f;
        if (inputMagnitude > 0.0f) {
            assistScale = lockOnInputReduce_;
        }

        float maxStep = assistMaxStep * assistScale * ctx_->deltaTime;
        float applied = diff * assistStrength * assistScale * ctx_->deltaTime;

        applied = Clamp(applied, -maxStep, maxStep);

        cameraYaw_ += applied;
    }

    // =========================
    // yaw / pitch 縺九ｉ蝓�E�貁E�E�E��E�繧剁E��懊ａE
    // =========================
    float cosPitch = std::cosf(cameraPitch_);
    DirectX::XMFLOAT3 forward = {std::sinf(cameraYaw_) * cosPitch,
                                 std::sinf(cameraPitch_),
                                 std::cosf(cameraYaw_) * cosPitch};

    DirectX::XMFLOAT3 right = {std::cosf(cameraYaw_), 0.0f,
                               -std::sinf(cameraYaw_)};

    // =========================
    // 繧�E�繝｡繝ｩ蝓ｺ貁E��せ
    // =========================
    DirectX::XMFLOAT3 cameraTargetBase = {
        playerPos.x, playerPos.y + cameraLookHeight_, playerPos.z};

    DirectX::XMFLOAT3 cameraPos{};

    if (isLockOn_) {
        // ---------------------------------
        // 繝ｭ繝�Eけ繧�E�繝ｳ譎�E 謨�E�縺�E�縺�E�繝ｩ繧�E�繝ｳ蝓ｺ貁E��〒蜀・�E��E�霑ｽ蠕�E
        // ---------------------------------
        float toEnemyX = enemyPos.x - playerPos.x;
        float toEnemyZ = enemyPos.z - playerPos.z;
        const float distXZ = DistanceXZ(enemyPos, playerPos);
        const XMFLOAT2 enemyLine = NormalizeXZ(toEnemyX, toEnemyZ);
        const float lineX = enemyLine.x;
        const float lineZ = enemyLine.y;

        // 謨�E�譁E��蜷代Λ繧�E�繝ｳ縺�E�蟁E��縺吶�E�蜿�E�繝吶け繝医΁E
        float orbitRightX = lineZ;
        float orbitRightZ = -lineX;

        // 謨�E�縺�E�縺�E�霍晞屬縺�E�蟁E���E�縺�E�縺大�E�後ｍ縺�E�蠑輔￥
        const float distanceRange = lockOnDistanceMax_ - lockOnDistanceMin_;
        const float pullT =
            distanceRange > kMinVectorLength
                ? Clamp01((distXZ - lockOnDistanceMin_) / distanceRange)
                : 0.0f;

        float usedRadius = lockOnOrbitRadius_ + lockOnOrbitPullBackMax_ * pullT;
        if (isEnemyPressureAction) {
            usedRadius -= 0.45f;
        } else if (isEnemyWideAction) {
            usedRadius += 0.85f;
        } else if (enemyActionKind == ActionKind::BladeClash) {
            usedRadius += 0.35f;
        }
        if (isEnemyPhaseTransition) {
            usedRadius -= phaseTransitionPushIn_ * enemyPhaseTransitionRatio;
        }
        usedRadius -= 1.20f * chargeFocus;
        usedRadius = Clamp(usedRadius, 3.35f, 7.4f);

        // cameraYaw_ 縺�E�謨�E�譁E��蜷代Λ繧�E�繝ｳ縺�E�縺�E�蟾�E�縺�E�縲∝�E蠑ｧ荳翫・蟾�E�蜿�E�菴咲�E��E�繧呈ｱ�E�繧√ａE
        float lineYaw = std::atan2f(lineX, lineZ);
        float yawDiff = WrapRadians(cameraYaw_ - lineYaw);

        // 逵滓ｨ�E�縺�E�縺�E�蝗槭�E�縺吶℁E��九�E隕九▼繧峨�E�縺�E�縺�E�蛻�E�髯・
        const float maxOrbitAngle = 0.65f;
        yawDiff = Clamp(yawDiff, -maxOrbitAngle, maxOrbitAngle);

        float sinA = std::sinf(yawDiff);
        float cosA = std::cosf(yawDiff);

        const float sideBias =
            lockOnOrbitSideBias_ * 0.55f + 0.16f * pullT +
            (isEnemyPressureAction ? 0.10f : 0.0f);
        const float cameraLift =
            (isEnemyWideAction ? 0.16f : 0.0f) +
            (isEnemyWarpEnd ? 0.12f : 0.0f);

        DirectX::XMFLOAT3 desiredCameraPos = {
            playerPos.x - lineX * usedRadius * cosA +
                orbitRightX * usedRadius * sinA +
                orbitRightX * sideBias,
            playerPos.y + 1.42f + 0.16f * pullT + cameraLift,
            playerPos.z - lineZ * usedRadius * cosA +
                orbitRightZ * usedRadius * sinA +
                orbitRightZ * sideBias};

        const float posAlpha =
            SaturatedAlpha(lockOnOrbitLerpSpeed_, ctx_->deltaTime);
        lockOnOrbitCameraPos_ =
            Lerp(lockOnOrbitCameraPos_, desiredCameraPos, posAlpha);

        cameraPos = lockOnOrbitCameraPos_;
    } else {
        // ---------------------------------
        // 騾壼�E��E�譎�E 閧�E�雜翫�E�荳我ｺ�E�遘ｰ
        // ---------------------------------
        float enemyDistanceXZ = DistanceXZ(enemyPos, playerPos);
        float dynamicDistance = cameraDistance_;
        if (enemyDistanceXZ > 5.0f) {
            dynamicDistance +=
                Clamp((enemyDistanceXZ - 5.0f) * 0.18f, 0.0f, 1.1f);
        }
        dynamicDistance -= 1.85f * chargeFocus;
        dynamicDistance = Clamp(dynamicDistance, 3.25f, 7.0f);

        cameraPos = {cameraTargetBase.x - forward.x * dynamicDistance +
                         right.x * cameraSideOffset_,
                     cameraTargetBase.y + cameraHeight_ -
                         forward.y * dynamicDistance,
                     cameraTargetBase.z - forward.z * dynamicDistance +
                         right.z * cameraSideOffset_};

        if (isEnemyPhaseTransition) {
            cameraPos.x += forward.x * phaseTransitionPushIn_ *
                           enemyPhaseTransitionRatio;
            cameraPos.y += 0.12f * enemyPhaseTransitionRatio;
            cameraPos.z += forward.z * phaseTransitionPushIn_ *
                           enemyPhaseTransitionRatio;
        }
        if (chargeFocus > 0.0f) {
            const DirectX::XMFLOAT3 focusCameraPos = {
                enemyPos.x - forward.x * 3.25f + right.x * 0.36f,
                enemyPos.y + 2.05f,
                enemyPos.z - forward.z * 3.25f + right.z * 0.36f};
            cameraPos = Lerp(cameraPos, focusCameraPos, chargeFocus * 0.72f);
        }

        // 髱槭Ο繝�Eけ譎ゅ・蜀・�E��E�逕ｨ迴�E�蝨�E�蛟､繧貞�E譛�E
        lockOnOrbitCameraPos_ = cameraPos;
    }

    // =========================
    // 豕ｨ隕也せ
    // =========================
    DirectX::XMFLOAT3 lookAt{};

    if (isLockOn_) {
        const float enemyLookHeight = isEnemyWideAction ? 2.55f : 2.25f;
        const float playerLookHeight = 1.05f;
        const float enemyLookWeight = isEnemyWideAction ? 0.74f : 0.68f;
        const float playerLookWeight = 1.0f - enemyLookWeight;
        DirectX::XMFLOAT3 desiredLookAt = {
            playerPos.x * lockOnLookPlayerWeight_ +
                enemyPos.x * lockOnLookEnemyWeight_,
            (playerPos.y + playerLookHeight) * playerLookWeight +
                (enemyPos.y + enemyLookHeight) * enemyLookWeight,
            playerPos.z * lockOnLookPlayerWeight_ +
                enemyPos.z * lockOnLookEnemyWeight_};

        const float lookAlpha =
            SaturatedAlpha(lockOnLookAtLerpSpeed_, ctx_->deltaTime);
        lockOnLookAt_ = Lerp(lockOnLookAt_, desiredLookAt, lookAlpha);

        lookAt = lockOnLookAt_;
    } else {
        lookAt = {cameraTargetBase.x + forward.x * cameraLookAhead_,
                  cameraTargetBase.y + forward.y * cameraLookAhead_,
                  cameraTargetBase.z + forward.z * cameraLookAhead_};

        lockOnLookAt_ = lookAt;
    }

    if (isEnemyPhaseTransition) {
        DirectX::XMFLOAT3 transitionLookAt = {
            playerPos.x * (1.0f - phaseTransitionLookAtEnemyWeight_) +
                enemyPos.x * phaseTransitionLookAtEnemyWeight_,
            (playerPos.y + cameraLookHeight_) *
                    (1.0f - phaseTransitionLookAtEnemyWeight_) +
                (enemyPos.y + phaseTransitionLookAtHeight_) *
                    phaseTransitionLookAtEnemyWeight_,
            playerPos.z * (1.0f - phaseTransitionLookAtEnemyWeight_) +
                enemyPos.z * phaseTransitionLookAtEnemyWeight_};

        float blend = enemyPhaseTransitionRatio;
        lookAt = Lerp(lookAt, transitionLookAt, blend);
    }
    if (chargeFocus > 0.0f) {
        const DirectX::XMFLOAT3 focusLookAt = {
            enemyPos.x, enemyPos.y + 1.70f, enemyPos.z};
        lookAt = Lerp(lookAt, focusLookAt, chargeFocus);
    }

    combatFeedback_.ApplyCameraImpulse(cameraPos, lookAt, sceneLightTime_);

    camera_.SetPosition(cameraPos);
    camera_.LookAt(lookAt);
}
