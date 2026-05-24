#include "GameScene.h"
#include "Input.h"
#include <cmath>

using namespace DirectX;

static constexpr float kPi = 3.14159265f;
static constexpr float kTwoPi = 6.28318530f;
static constexpr float kMinVectorLength = 0.0001f;

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
                                           : SaturatedAlpha(9.5f, ctx_->frame.deltaTime);
        lockOnOrbitCameraPos_ =
            Lerp(lockOnOrbitCameraPos_, desiredCameraPos, alpha);
        lockOnLookAt_ = Lerp(lockOnLookAt_, desiredLookAt, alpha);
        targetFovDeg_ = 52.0f + 4.0f * cutT;
        currentFovDeg_ +=
            (targetFovDeg_ - currentFovDeg_) *
            SaturatedAlpha(5.8f, ctx_->frame.deltaTime);
        camera_.SetPerspectiveFovDeg(currentFovDeg_);
        camera_.SetPosition(lockOnOrbitCameraPos_);
        AppLookAt(camera_, lockOnLookAt_);
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
        AppLookAt(camera_, lockOnLookAt_);
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
                                : SaturatedAlpha(8.2f, ctx_->frame.deltaTime);
        lockOnOrbitCameraPos_ =
            Lerp(lockOnOrbitCameraPos_, desiredCameraPos, alpha);
        lockOnLookAt_ = Lerp(lockOnLookAt_, desiredLookAt, alpha);
        targetFovDeg_ = 58.0f + 8.0f * ratio;
        currentFovDeg_ +=
            (targetFovDeg_ - currentFovDeg_) *
            SaturatedAlpha(5.8f, ctx_->frame.deltaTime);
        camera_.SetPerspectiveFovDeg(currentFovDeg_);
        camera_.SetPosition(lockOnOrbitCameraPos_);
        AppLookAt(camera_, lockOnLookAt_);
        return;
    }

    // 謨�E�陦悟虚迥�E�諷九ｒ蜿門�E�・
    const ActionKind enemyActionKind = enemy_.GetActionKind();
    const ActionStep enemyActionStep = enemy_.GetActionStep();
    const bool isEnemyMeleeAction =
        enemyActionKind == ActionKind::Smash ||
        enemyActionKind == ActionKind::Sweep;
    const bool isEnemyPressureAction =
        isEnemyMeleeAction &&
        (enemyActionStep == ActionStep::Charge ||
         enemyActionStep == ActionStep::Active ||
         enemyActionStep == ActionStep::Recovery);
    const bool isEnemyPhaseTransition = enemy_.IsPhaseTransitionActive();
    const float enemyPhaseTransitionRatio = enemy_.GetPhaseTransitionRatio();
    // =========================
    // FOV繧�E�繝ｼ繧�E�繝�Eヨ豎ｺ螳・
    // =========================
    targetFovDeg_ = normalFovDeg_;

    if (isLockOn_) {
        targetFovDeg_ = lockOnFovDeg_;
    }

    if (isEnemyPressureAction) {
        targetFovDeg_ = 71.5f;
    }

    if (isEnemyPhaseTransition) {
        targetFovDeg_ = phaseTransitionFovDeg_;
    }
    if (playerViewCamera_) {
        targetFovDeg_ = isLockOn_ ? 82.0f : normalFovDeg_;
        if (isEnemyPressureAction) {
            targetFovDeg_ = 80.0f;
        }
        if (isEnemyPhaseTransition) {
            targetFovDeg_ = phaseTransitionFovDeg_;
        }
    }

    float usedFovLerpSpeed = fovLerpSpeed_;
    if (isEnemyPhaseTransition) {
        usedFovLerpSpeed = phaseTransitionFovLerpSpeed_;
    }
    const float fovAlpha = SaturatedAlpha(usedFovLerpSpeed, ctx_->frame.deltaTime);
    currentFovDeg_ += (targetFovDeg_ - currentFovDeg_) * fovAlpha;
    camera_.SetPerspectiveFovDeg(currentFovDeg_ + combatFeedback_.GetFovKickDeg());

    if (playerViewCamera_) {
        const DirectX::XMFLOAT3 playerEye = {
            playerPos.x, playerPos.y + playerViewEyeHeight_, playerPos.z};

        if (isLockOn_) {
            float dx = enemyPos.x - playerPos.x;
            float dz = enemyPos.z - playerPos.z;
            float targetYaw = std::atan2f(dx, dz);
            float diff = WrapRadians(targetYaw - cameraYaw_);

            float inputMagnitude = 0.0f;
            if (ctx_->systems.input != nullptr && ctx_->systems.input->IsGamepadConnected() &&
                player_.UsesGamepadCameraLook()) {
                inputMagnitude =
                    (std::max)(inputMagnitude,
                               std::abs(ctx_->systems.input->GetGamepadRightStickX()));
            }

            const float assistScale = inputMagnitude > 0.0f ? 0.42f : 1.0f;
            const float applied =
                Clamp(diff * 7.2f * assistScale * ctx_->frame.deltaTime,
                      -8.0f * ctx_->frame.deltaTime, 8.0f * ctx_->frame.deltaTime);
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
            DirectX::XMFLOAT3 desiredLookAt = {
                enemyPos.x, enemyPos.y + playerViewLockOnLookHeight_, enemyPos.z};

            const float lookAlpha = SaturatedAlpha(16.0f, ctx_->frame.deltaTime);
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
        combatFeedback_.ApplyCameraImpulse(cameraPos, lookAt, sceneLightTime_);
        camera_.SetPosition(cameraPos);
        AppLookAt(camera_, lookAt);
        return;
    }

    // =========================
    // 繝ｭ繝�Eけ繧�E�繝ｳ荳�E�縺�E�縺・yaw 陬懷勧
    // =========================
    if (isLockOn_) {
        DirectX::XMFLOAT3 assistTarget = enemyPos;

        float assistStrength = lockOnAssistStrength_;
        float assistMaxStep = lockOnAssistMaxStep_;

        if (isEnemyPhaseTransition) {
            assistStrength = lockOnAssistStrength_ * 1.35f;
            assistMaxStep = lockOnAssistMaxStep_ * 1.35f;
        }

        float dx = assistTarget.x - playerPos.x;
        float dz = assistTarget.z - playerPos.z;

        float targetYaw = std::atan2f(dx, dz);
        float diff = WrapRadians(targetYaw - cameraYaw_);

        float inputMagnitude = 0.0f;
        if (ctx_->systems.input != nullptr && ctx_->systems.input->IsGamepadConnected() &&
            player_.UsesGamepadCameraLook()) {
            const float stick = std::abs(ctx_->systems.input->GetGamepadRightStickX());
            if (stick > inputMagnitude) {
                inputMagnitude = stick;
            }
        }

        float assistScale = 1.0f;
        if (inputMagnitude > 0.0f) {
            assistScale = lockOnInputReduce_;
        }

        float maxStep = assistMaxStep * assistScale * ctx_->frame.deltaTime;
        float applied = diff * assistStrength * assistScale * ctx_->frame.deltaTime;

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
        }
        if (isEnemyPhaseTransition) {
            usedRadius -= phaseTransitionPushIn_ * enemyPhaseTransitionRatio;
        }
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

        DirectX::XMFLOAT3 desiredCameraPos = {
            playerPos.x - lineX * usedRadius * cosA +
                orbitRightX * usedRadius * sinA +
                orbitRightX * sideBias,
            playerPos.y + 1.42f + 0.16f * pullT,
            playerPos.z - lineZ * usedRadius * cosA +
                orbitRightZ * usedRadius * sinA +
                orbitRightZ * sideBias};

        const float posAlpha =
            SaturatedAlpha(lockOnOrbitLerpSpeed_, ctx_->frame.deltaTime);
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
        // 髱槭Ο繝�Eけ譎ゅ・蜀・�E��E�逕ｨ迴�E�蝨�E�蛟､繧貞�E譛�E
        lockOnOrbitCameraPos_ = cameraPos;
    }

    // =========================
    // 豕ｨ隕也せ
    // =========================
    DirectX::XMFLOAT3 lookAt{};

    if (isLockOn_) {
        const float enemyLookHeight = 2.25f;
        const float playerLookHeight = 1.05f;
        const float enemyLookWeight = 0.68f;
        const float playerLookWeight = 1.0f - enemyLookWeight;
        DirectX::XMFLOAT3 desiredLookAt = {
            playerPos.x * lockOnLookPlayerWeight_ +
                enemyPos.x * lockOnLookEnemyWeight_,
            (playerPos.y + playerLookHeight) * playerLookWeight +
                (enemyPos.y + enemyLookHeight) * enemyLookWeight,
            playerPos.z * lockOnLookPlayerWeight_ +
                enemyPos.z * lockOnLookEnemyWeight_};

        const float lookAlpha =
            SaturatedAlpha(lockOnLookAtLerpSpeed_, ctx_->frame.deltaTime);
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
    combatFeedback_.ApplyCameraImpulse(cameraPos, lookAt, sceneLightTime_);

    camera_.SetPosition(cameraPos);
    AppLookAt(camera_, lookAt);
}
