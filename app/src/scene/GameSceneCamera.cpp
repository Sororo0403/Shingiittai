#include "BladeClashCinematic.h"
#include "GameScene.h"
#include "Input.h"
#include "WinApp.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;

static constexpr float kPi = 3.14159265f;
static constexpr float kTwoPi = 6.28318530f;
static constexpr float kMinVectorLength = 0.0001f;
namespace Clash = BladeClashCinematic;

static float Clamp(float value, float minValue, float maxValue) {
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return value;
}

static float Clamp01(float value) { return Clamp(value, 0.0f, 1.0f); }

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
    return {from.x + (to.x - from.x) * alpha, from.y + (to.y - from.y) * alpha,
            from.z + (to.z - from.z) * alpha};
}

void GameScene::UpdateCamera(Input *input) {
    // ===== 騾壼�E��E�繧�E�繝｡繝ｩ =====

    // 繝ｭ繝�Eけ繧�E�繝ｳ蛻・�E�譖ｿ縺・
    if (input != nullptr &&
        (input->IsKeyTrigger(DIK_Q) ||
         input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_RIGHT_SHOULDER))) {
        isLockOn_ = !isLockOn_;
    }

    float yawInput = 0.0f;
    float pitchInput = 0.0f;

    if (input != nullptr && input->IsGamepadConnected() &&
        player_.UsesGamepadCameraLook()) {
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

void GameScene::UpdateBackgroundCamera(float deltaTime) {
    (void)deltaTime;
    const int width =
        ctx_->systems.winApp != nullptr ? ctx_->systems.winApp->GetWidth() : 0;
    const int height =
        ctx_->systems.winApp != nullptr ? ctx_->systems.winApp->GetHeight() : 0;
    if (width > 0 && height > 0) {
        camera_.SetAspect(static_cast<float>(width) /
                          static_cast<float>(height));
    }

    camera_.SetPerspectiveFovDeg(64.0f);
    const float orbit = sceneLightTime_ * 0.045f;
    const XMFLOAT3 cameraPos{
        std::sinf(orbit) * 5.2f,
        2.45f + std::sinf(sceneLightTime_ * 0.12f) * 0.08f,
        -8.4f + std::cosf(orbit) * 1.6f,
    };
    const XMFLOAT3 lookAt{
        0.0f,
        0.58f,
        3.2f,
    };
    camera_.SetPosition(cameraPos);
    AppLookAt(camera_, lookAt);
    camera_.UpdateMatrices();
}

void GameScene::UpdateReadyPreviewCamera(float deltaTime) {
    (void)deltaTime;
    const int width =
        ctx_->systems.winApp != nullptr ? ctx_->systems.winApp->GetWidth() : 0;
    const int height =
        ctx_->systems.winApp != nullptr ? ctx_->systems.winApp->GetHeight() : 0;
    if (width > 0 && height > 0) {
        camera_.SetAspect(static_cast<float>(width) /
                          static_cast<float>(height));
    }

    camera_.SetPerspectiveFovDeg(50.0f);
    const XMFLOAT3 &enemyPos = enemy_.GetTransform().position;
    const float breath = std::sinf(sceneLightTime_ * 0.82f);
    const float orbit = std::sinf(sceneLightTime_ * 0.16f) * 0.16f;
    const float radius = 14.8f;
    const XMFLOAT3 cameraPos{
        enemyPos.x + std::sinf(orbit) * radius,
        enemyPos.y + 3.85f + breath * 0.08f,
        enemyPos.z - std::cosf(orbit) * radius,
    };
    const XMFLOAT3 lookAt{
        enemyPos.x,
        enemyPos.y + 1.32f,
        enemyPos.z,
    };
    camera_.SetPosition(cameraPos);
    AppLookAt(camera_, lookAt);
    camera_.UpdateMatrices();
}

void GameScene::ApplyGameplayCameraPose(const XMFLOAT3 &cameraPos,
                                        const XMFLOAT3 &lookAt,
                                        float positionLerpSpeed,
                                        float lookAtLerpSpeed) {
    if (!gameplayCameraPoseInitialized_) {
        gameplayCameraPos_ = cameraPos;
        gameplayCameraLookAt_ = lookAt;
        gameplayCameraPoseInitialized_ = true;
    } else {
        const float positionAlpha =
            SaturatedAlpha(positionLerpSpeed, ctx_->frame.deltaTime);
        const float lookAtAlpha =
            SaturatedAlpha(lookAtLerpSpeed, ctx_->frame.deltaTime);
        gameplayCameraPos_ = Lerp(gameplayCameraPos_, cameraPos, positionAlpha);
        gameplayCameraLookAt_ =
            Lerp(gameplayCameraLookAt_, lookAt, lookAtAlpha);
    }

    XMFLOAT3 finalCameraPos = gameplayCameraPos_;
    XMFLOAT3 finalLookAt = gameplayCameraLookAt_;
    combatFeedback_.ApplyCameraImpulse(finalCameraPos, finalLookAt,
                                       sceneLightTime_);

    camera_.SetPosition(finalCameraPos);
    AppLookAt(camera_, finalLookAt);
}

void GameScene::UpdateBattleCamera() {
    const ActionKind actionKind = enemy_.GetActionKind();
    const ActionStep actionStep = enemy_.GetActionStep();
    const bool meleeAction = actionKind == ActionKind::Smash ||
                             actionKind == ActionKind::Sweep ||
                             actionKind == ActionKind::BladeClash;
    const BattleCameraContext cameraContext{
        player_.GetTransform().position,
        enemy_.GetTransform().position,
        enemy_.IsTripleIaiCenterCameraHold()
            ? enemy_.GetTripleIaiCenterFocusPosition()
            : enemy_.GetTransform().position,
        actionKind,
        actionStep,
        meleeAction,
        meleeAction && (actionStep == ActionStep::Charge ||
                        actionStep == ActionStep::Active ||
                        actionStep == ActionStep::Recovery),
        enemy_.IsPhaseTransitionActive(),
        enemy_.IsTripleIaiCenterCameraHold(),
        enemy_.GetPhaseTransitionRatio()};

    if (UpdateVictoryBattleCamera(cameraContext) ||
        UpdateIntroBattleCamera(cameraContext) ||
        UpdateDefeatBattleCamera(cameraContext) ||
        UpdateBladeClashFinishCamera(cameraContext)) {
        return;
    }
    ConfigureBattleCameraFov(cameraContext);
    if (UpdateActiveBladeClashCamera(cameraContext) ||
        UpdatePlayerViewBattleCamera(cameraContext)) {
        return;
    }
    UpdateThirdPersonBattleCamera(cameraContext);
}

bool GameScene::UpdateVictoryBattleCamera(
    const BattleCameraContext &cameraContext) {
    if (!victorySequenceActive_) {
        return false;
    }
    float toEnemyX = cameraContext.enemyPos.x - cameraContext.playerPos.x;
    float toEnemyZ = cameraContext.enemyPos.z - cameraContext.playerPos.z;
    const XMFLOAT2 line = NormalizeXZ(toEnemyX, toEnemyZ);
    const float sideX = line.y;
    const float sideZ = -line.x;
    const DirectX::XMFLOAT3 frontCameraPos = {
        cameraContext.enemyPos.x - line.x * 8.2f,
        cameraContext.enemyPos.y + 2.25f,
        cameraContext.enemyPos.z - line.y * 8.2f};
    const DirectX::XMFLOAT3 frontLookAt = {cameraContext.enemyPos.x,
                                           cameraContext.enemyPos.y + 1.45f,
                                           cameraContext.enemyPos.z};
    const DirectX::XMFLOAT3 fallCameraPos = {
        cameraContext.enemyPos.x - line.x * 7.6f + sideX * 4.2f,
        cameraContext.enemyPos.y + 0.92f,
        cameraContext.enemyPos.z - line.y * 7.6f + sideZ * 4.2f};
    const DirectX::XMFLOAT3 fallLookAt = {
        cameraContext.enemyPos.x + line.x * 0.55f,
        cameraContext.enemyPos.y + 0.95f,
        cameraContext.enemyPos.z + line.y * 0.55f};
    const float introMoveT = Clamp01(victorySequenceTimer_ / 0.72f);
    const float cutT = introMoveT * introMoveT * (3.0f - 2.0f * introMoveT);
    const DirectX::XMFLOAT3 desiredCameraPos =
        Lerp(frontCameraPos, fallCameraPos, cutT);
    const DirectX::XMFLOAT3 desiredLookAt = Lerp(frontLookAt, fallLookAt, cutT);
    const float alpha = victorySequenceTimer_ < 0.82f
                            ? 1.0f
                            : SaturatedAlpha(9.5f, ctx_->frame.deltaTime);
    lockOnOrbitCameraPos_ =
        Lerp(lockOnOrbitCameraPos_, desiredCameraPos, alpha);
    lockOnLookAt_ = Lerp(lockOnLookAt_, desiredLookAt, alpha);
    targetFovDeg_ = 52.0f + 4.0f * cutT;
    currentFovDeg_ += (targetFovDeg_ - currentFovDeg_) *
                      SaturatedAlpha(5.8f, ctx_->frame.deltaTime);
    camera_.SetPerspectiveFovDeg(currentFovDeg_);
    gameplayCameraPoseInitialized_ = true;
    gameplayCameraPos_ = lockOnOrbitCameraPos_;
    gameplayCameraLookAt_ = lockOnLookAt_;
    camera_.SetPosition(lockOnOrbitCameraPos_);
    AppLookAt(camera_, lockOnLookAt_);
    return true;
}

bool GameScene::UpdateIntroBattleCamera(
    const BattleCameraContext &cameraContext) {
    if (!battleIntroActive_) {
        return false;
    }
    float toEnemyX = cameraContext.enemyPos.x - cameraContext.playerPos.x;
    float toEnemyZ = cameraContext.enemyPos.z - cameraContext.playerPos.z;
    const XMFLOAT2 line = NormalizeXZ(toEnemyX, toEnemyZ);
    const float sideX = line.y;
    const float sideZ = -line.x;
    const float settle =
        Clamp01((battleIntroTimer_ - 2.86f) / (battleIntroDuration_ - 2.86f));
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
        cameraContext.enemyPos.x - line.x * 10.8f + sideX * 1.65f,
        cameraContext.enemyPos.y + 3.05f,
        cameraContext.enemyPos.z - line.y * 10.8f + sideZ * 1.65f};
    const DirectX::XMFLOAT3 revealCameraPos = {
        cameraContext.enemyPos.x - line.x * 8.7f + sideX * 0.70f,
        cameraContext.enemyPos.y + 2.86f + flashKick * 0.10f,
        cameraContext.enemyPos.z - line.y * 8.7f + sideZ * 0.70f};
    const DirectX::XMFLOAT3 playCameraPos = {
        cameraContext.playerPos.x - line.x * 6.0f + sideX * 1.15f,
        cameraContext.playerPos.y + 2.35f,
        cameraContext.playerPos.z - line.y * 6.0f + sideZ * 1.15f};

    const DirectX::XMFLOAT3 summonLookAt = {cameraContext.enemyPos.x,
                                            cameraContext.enemyPos.y + 1.45f +
                                                0.22f * revealEase,
                                            cameraContext.enemyPos.z};
    const DirectX::XMFLOAT3 revealLookAt = {
        cameraContext.enemyPos.x + line.x * 0.12f,
        cameraContext.enemyPos.y + 1.62f + flashKick * 0.08f,
        cameraContext.enemyPos.z + line.y * 0.12f};
    const DirectX::XMFLOAT3 playLookAt = {
        cameraContext.playerPos.x * 0.34f + cameraContext.enemyPos.x * 0.66f,
        cameraContext.playerPos.y * 0.26f + cameraContext.enemyPos.y * 0.74f +
            1.36f,
        cameraContext.playerPos.z * 0.34f + cameraContext.enemyPos.z * 0.66f};

    const DirectX::XMFLOAT3 heroCameraPos =
        Lerp(summonCameraPos, revealCameraPos, revealEase);
    const DirectX::XMFLOAT3 heroLookAt =
        Lerp(summonLookAt, revealLookAt, revealEase);
    DirectX::XMFLOAT3 desiredCameraPos =
        Lerp(heroCameraPos, playCameraPos, settleEase);
    DirectX::XMFLOAT3 desiredLookAt = Lerp(heroLookAt, playLookAt, settleEase);
    desiredCameraPos.x += sideX * (flashKick * 0.14f + threatShake * 0.055f);
    desiredCameraPos.y += flashKick * 0.05f + threatShake * 0.024f;
    desiredCameraPos.z += sideZ * (flashKick * 0.14f + threatShake * 0.055f);
    lockOnOrbitCameraPos_ = desiredCameraPos;
    lockOnLookAt_ = desiredLookAt;
    const float heroFov = 58.0f - 4.0f * revealEase + 2.0f * flashKick;
    currentFovDeg_ = heroFov + (66.0f - heroFov) * settleEase;
    camera_.SetPerspectiveFovDeg(currentFovDeg_);
    gameplayCameraPoseInitialized_ = true;
    gameplayCameraPos_ = desiredCameraPos;
    gameplayCameraLookAt_ = desiredLookAt;
    combatFeedback_.ApplyCameraImpulse(desiredCameraPos, desiredLookAt,
                                       sceneLightTime_);
    camera_.SetPosition(desiredCameraPos);
    AppLookAt(camera_, desiredLookAt);
    return true;
}

bool GameScene::UpdateDefeatBattleCamera(
    const BattleCameraContext &cameraContext) {
    if (!defeatSequenceActive_) {
        return false;
    }
    float toEnemyX = cameraContext.enemyPos.x - cameraContext.playerPos.x;
    float toEnemyZ = cameraContext.enemyPos.z - cameraContext.playerPos.z;
    const XMFLOAT2 line = NormalizeXZ(toEnemyX, toEnemyZ);
    const float sideX = line.y;
    const float sideZ = -line.x;
    const float ratio = Clamp01(defeatSequenceTimer_ / defeatSequenceDuration_);
    const float fall = Clamp01((defeatSequenceTimer_ - 0.34f) / 1.22f);
    const DirectX::XMFLOAT3 startCameraPos = {
        cameraContext.playerPos.x - line.x * 5.0f + sideX * 1.85f,
        cameraContext.playerPos.y + 1.85f,
        cameraContext.playerPos.z - line.y * 5.0f + sideZ * 1.85f};
    const DirectX::XMFLOAT3 fallCameraPos = {
        cameraContext.playerPos.x - line.x * 4.3f + sideX * 3.2f,
        cameraContext.playerPos.y + 0.78f,
        cameraContext.playerPos.z - line.y * 4.3f + sideZ * 3.2f};
    const DirectX::XMFLOAT3 startLookAt = {cameraContext.playerPos.x,
                                           cameraContext.playerPos.y + 1.05f,
                                           cameraContext.playerPos.z};
    const DirectX::XMFLOAT3 fallLookAt = {
        cameraContext.playerPos.x + line.x * 0.42f,
        cameraContext.playerPos.y + 0.46f - 0.16f * fall,
        cameraContext.playerPos.z + line.y * 0.42f};
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
    currentFovDeg_ += (targetFovDeg_ - currentFovDeg_) *
                      SaturatedAlpha(5.8f, ctx_->frame.deltaTime);
    camera_.SetPerspectiveFovDeg(currentFovDeg_);
    gameplayCameraPoseInitialized_ = true;
    gameplayCameraPos_ = lockOnOrbitCameraPos_;
    gameplayCameraLookAt_ = lockOnLookAt_;
    camera_.SetPosition(lockOnOrbitCameraPos_);
    AppLookAt(camera_, lockOnLookAt_);
    return true;
}

bool GameScene::UpdateBladeClashFinishCamera(
    const BattleCameraContext &cameraContext) {
    if (!bladeClashFinishActive_) {
        return false;
    }
    const XMFLOAT2 line =
        bladeClashFinishPlayerWon_
            ? NormalizeXZ(bladeClashDirection_.x, bladeClashDirection_.z)
            : NormalizeXZ(cameraContext.enemyPos.x - cameraContext.playerPos.x,
                          cameraContext.enemyPos.z - cameraContext.playerPos.z);
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
            : XMFLOAT3{(cameraContext.playerPos.x + cameraContext.enemyPos.x) *
                           0.5f,
                       cameraContext.playerPos.y + 1.18f,
                       (cameraContext.playerPos.z + cameraContext.enemyPos.z) *
                           0.5f};

    if (bladeClashFinishPlayerWon_) {
        const float winActionTimer =
            Clash::WinActionTimer(bladeClashFinishTimer_);
        const float winDuration =
            (std::max)(0.001f,
                       bladeClashFinishDuration_ - Clash::kWinGuardBreakLead) /
            Clash::kWinActionSlow;
        const float winRatio = Clamp01(winActionTimer / winDuration);
        const float cameraBlend = Clamp01(
            (bladeClashFinishTimer_ - Clash::kWinGuardBreakCameraMoveStart) /
            (Clash::kWinGuardBreakCameraMoveEnd -
             Clash::kWinGuardBreakCameraMoveStart));
        const float cameraSnap = 1.0f - std::pow(1.0f - cameraBlend, 4.0f);
        const float guardBreakSnap = std::sinf(
            Clamp01((bladeClashFinishTimer_ - Clash::kWinGuardBreakImpactTime) /
                    0.08f) *
            kPi);
        const XMFLOAT3 guardBreakCamera = {
            bladeClashFinishEnemyStart_.x - line.x * 7.35f +
                right.x * (3.05f + 0.16f * guardBreakSnap),
            bladeClashFinishEnemyStart_.y + 1.74f + 0.05f * guardBreakSnap,
            bladeClashFinishEnemyStart_.z - line.y * 7.35f +
                right.z * (3.05f + 0.16f * guardBreakSnap)};
        const XMFLOAT3 guardBreakLookAt = {
            bladeClashFinishEnemyStart_.x + line.x * 0.10f +
                right.x * 0.08f * guardBreakSnap,
            bladeClashFinishEnemyStart_.y + 1.18f - 0.05f * guardBreakSnap,
            bladeClashFinishEnemyStart_.z + line.y * 0.10f +
                right.z * 0.08f * guardBreakSnap};
        const float cutT = Clamp01(winActionTimer / 0.18f);
        const float slideT = Clamp01((winActionTimer - 0.11f) / 0.44f);
        const float cutEase = 1.0f - std::pow(1.0f - cutT, 4.0f);
        const float slideEase = 1.0f - std::pow(1.0f - slideT, 2.0f);
        const float dashEase = Clamp01(0.86f * cutEase + 0.24f * slideEase);
        const float slide =
            1.0f - std::pow(1.0f - Clamp01(winRatio / 0.42f), 2.0f);
        const XMFLOAT3 cinematicPlayer = Lerp(
            bladeClashFinishPlayerStart_, bladeClashFinishPlayerEnd_, dashEase);
        const XMFLOAT3 enemyChest = {cameraContext.enemyPos.x,
                                     cameraContext.enemyPos.y + 1.16f,
                                     cameraContext.enemyPos.z};
        const XMFLOAT3 lookAtBase = {
            cinematicPlayer.x + line.x * (2.70f + 4.20f * slide),
            cinematicPlayer.y + 1.20f + 0.16f * slide,
            cinematicPlayer.z + line.y * (2.70f + 4.20f * slide)};
        const float shake = (0.064f * strike + 0.014f * hold) * 0.74f;
        const float phase = sceneLightTime_ * 44.0f;
        XMFLOAT3 cameraPos = {
            cinematicPlayer.x - line.x * (3.05f - 0.44f * slide) +
                right.x * (2.18f + 0.42f * slide) +
                right.x * std::sinf(phase) * shake,
            cinematicPlayer.y + 1.30f + 0.10f * slide +
                std::cosf(phase * 1.27f) * shake * 0.5f,
            cinematicPlayer.z - line.y * (3.05f - 0.44f * slide) +
                right.z * (2.18f + 0.42f * slide) +
                right.z * std::sinf(phase) * shake};
        XMFLOAT3 lookAt = {lookAtBase.x * (1.0f - 0.38f * slide) +
                               enemyChest.x * (0.38f * slide) + right.x * 0.10f,
                           lookAtBase.y * (1.0f - 0.38f * slide) +
                               (enemyChest.y + 0.20f) * (0.38f * slide),
                           lookAtBase.z * (1.0f - 0.38f * slide) +
                               enemyChest.z * (0.38f * slide) +
                               right.z * 0.10f};
        const float pierceViewT = Clamp01((winActionTimer - 0.10f) / 0.24f);
        const float pierceViewEase =
            pierceViewT * pierceViewT * (3.0f - 2.0f * pierceViewT);
        const XMFLOAT3 pierceCamera = {
            cinematicPlayer.x + line.x * (3.25f + 0.58f * slide) +
                right.x * (2.42f + 0.36f * slide) +
                right.x * std::sinf(phase) * shake,
            cinematicPlayer.y + 1.16f + 0.12f * slide +
                std::cosf(phase * 1.27f) * shake * 0.5f,
            cinematicPlayer.z + line.y * (3.25f + 0.58f * slide) +
                right.z * (2.42f + 0.36f * slide) +
                right.z * std::sinf(phase) * shake};
        const XMFLOAT3 pierceLookAt = {
            cinematicPlayer.x - line.x * (2.18f + 2.25f * slide) +
                right.x * 0.08f,
            cinematicPlayer.y + 1.02f,
            cinematicPlayer.z - line.y * (2.18f + 2.25f * slide) +
                right.z * 0.08f};
        cameraPos = Lerp(cameraPos, pierceCamera, pierceViewEase);
        lookAt =
            Lerp(lookAt, Lerp(pierceLookAt, enemyChest, 0.42f), pierceViewEase);
        if (bladeClashFinishTimer_ < Clash::kWinGuardBreakLead) {
            const XMFLOAT3 playerPrepCamera = {
                bladeClashFinishPlayerStart_.x - line.x * 3.20f +
                    right.x * 1.90f,
                bladeClashFinishPlayerStart_.y + 1.46f,
                bladeClashFinishPlayerStart_.z - line.y * 3.20f +
                    right.z * 1.90f};
            const XMFLOAT3 playerPrepLookAt = {
                bladeClashFinishEnemyStart_.x + line.x * 0.78f +
                    right.x * 0.10f,
                bladeClashFinishEnemyStart_.y + 1.10f,
                bladeClashFinishEnemyStart_.z + line.y * 0.78f +
                    right.z * 0.10f};
            cameraPos = Lerp(guardBreakCamera, playerPrepCamera, cameraSnap);
            lookAt = Lerp(guardBreakLookAt, playerPrepLookAt, cameraSnap);
        }
        cameraYaw_ = std::atan2f(line.x, line.y);
        const float fovPunch =
            std::sinf(Clamp01((winActionTimer - 0.10f) / 0.28f) * kPi);
        targetFovDeg_ = 64.0f + 7.0f * fovPunch + 2.0f * slide;
        currentFovDeg_ += (targetFovDeg_ - currentFovDeg_) *
                          SaturatedAlpha(7.0f, ctx_->frame.deltaTime);
        camera_.SetPerspectiveFovDeg(currentFovDeg_ +
                                     combatFeedback_.GetFovKickDeg());
        gameplayCameraPoseInitialized_ = true;
        gameplayCameraPos_ = cameraPos;
        gameplayCameraLookAt_ = lookAt;
        combatFeedback_.ApplyCameraImpulse(cameraPos, lookAt, sceneLightTime_);
        camera_.SetPosition(cameraPos);
        AppLookAt(camera_, lookAt);
        return true;
    }

    const Clash::LossPose lossPose = Clash::EvaluateLossPose(
        bladeClashFinishTimer_, bladeClashFinishPlayerStart_,
        bladeClashDirection_);
    XMFLOAT3 cinematicPlayer = lossPose.position;
    const float catchPrep = Clamp01((bladeClashFinishTimer_ - 0.92f) / 0.28f);
    const float catchLaunch = Clamp01((bladeClashFinishTimer_ - 1.12f) / 0.34f);
    const float catchPrepEase =
        catchPrep * catchPrep * (3.0f - 2.0f * catchPrep);
    const float catchLaunchEase = 1.0f - std::pow(1.0f - catchLaunch, 4.0f);
    const float catchT =
        Clamp01(0.18f * catchPrepEase + 0.82f * catchLaunchEase);
    const float crashT = Clamp01((bladeClashFinishTimer_ - 1.52f) / 0.18f);
    const float crash = std::sinf(crashT * kPi);
    const float shake =
        (0.070f * strike + 0.018f * hold + 0.120f * crash) * 0.92f;
    const float phase = sceneLightTime_ * 44.0f;
    const XMFLOAT3 sideCamera = {center.x + right.x * 5.35f - line.x * 1.15f,
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
    const XMFLOAT3 sideLookAt = {center.x + line.x * 0.12f, center.y + 0.04f,
                                 center.z + line.y * 0.12f};
    const XMFLOAT3 frontLookAt = {
        cinematicPlayer.x + line.x * (0.74f - 0.30f * catchT),
        cinematicPlayer.y + 1.02f + 0.12f * catchT,
        cinematicPlayer.z + line.y * (0.74f - 0.30f * catchT)};
    XMFLOAT3 lookAt = Lerp(sideLookAt, frontLookAt, catchT);
    cameraYaw_ = std::atan2f(line.x, line.y);
    targetFovDeg_ = 72.0f + 6.0f * catchT;
    currentFovDeg_ += (targetFovDeg_ - currentFovDeg_) *
                      SaturatedAlpha(7.0f, ctx_->frame.deltaTime);
    camera_.SetPerspectiveFovDeg(currentFovDeg_ +
                                 combatFeedback_.GetFovKickDeg());
    gameplayCameraPoseInitialized_ = true;
    gameplayCameraPos_ = cameraPos;
    gameplayCameraLookAt_ = lookAt;
    combatFeedback_.ApplyCameraImpulse(cameraPos, lookAt, sceneLightTime_);
    camera_.SetPosition(cameraPos);
    AppLookAt(camera_, lookAt);
    return true;
}

void GameScene::ConfigureBattleCameraFov(
    const BattleCameraContext &cameraContext) {
    targetFovDeg_ = normalFovDeg_;

    if (isLockOn_) {
        targetFovDeg_ = lockOnFovDeg_;
    }

    if (cameraContext.enemyPressureAction) {
        targetFovDeg_ = 71.5f;
    }

    if (cameraContext.enemyPhaseTransition) {
        targetFovDeg_ = phaseTransitionFovDeg_;
    }
    if (playerViewCamera_) {
        targetFovDeg_ = isLockOn_ ? 82.0f : normalFovDeg_;
        if (cameraContext.enemyPressureAction) {
            targetFovDeg_ = 80.0f;
        }
        if (cameraContext.enemyPhaseTransition) {
            targetFovDeg_ = phaseTransitionFovDeg_;
        }
    }

    float usedFovLerpSpeed = fovLerpSpeed_;
    if (cameraContext.enemyPhaseTransition) {
        usedFovLerpSpeed = phaseTransitionFovLerpSpeed_;
    }
    const float fovAlpha =
        SaturatedAlpha(usedFovLerpSpeed, ctx_->frame.deltaTime);
    currentFovDeg_ += (targetFovDeg_ - currentFovDeg_) * fovAlpha;
    camera_.SetPerspectiveFovDeg(currentFovDeg_ +
                                 combatFeedback_.GetFovKickDeg());
}

bool GameScene::UpdateActiveBladeClashCamera(
    const BattleCameraContext &cameraContext) {
    if (!bladeClashActive_) {
        return false;
    }
    const XMFLOAT2 line =
        NormalizeXZ(cameraContext.enemyPos.x - cameraContext.playerPos.x,
                    cameraContext.enemyPos.z - cameraContext.playerPos.z);
    const XMFLOAT3 right = {line.y, 0.0f, -line.x};
    const float gaugeProgress = Clamp01((bladeClashGauge_ + 1.0f) * 0.5f);
    const XMFLOAT3 mid = {
        bladeClashCenter_.x + line.x * (gaugeProgress - 0.5f) * 0.18f,
        bladeClashCenter_.y + 0.06f,
        bladeClashCenter_.z + line.y * (gaugeProgress - 0.5f) * 0.18f};
    const float push = Clamp01(bladeClashCameraPush_);
    const float pulse = push * push * (3.0f - 2.0f * push);
    const float impact = Clamp01(bladeClashImpactPulse_);
    const float tension = 0.35f + std::fabs(bladeClashGauge_) * 0.32f;
    const float shakePhase = sceneLightTime_ * (34.0f + 18.0f * impact);
    const float shake = (0.018f + 0.034f * impact) * tension;
    const float enemyPressure = Clamp01(1.0f - gaugeProgress);
    const XMFLOAT3 playerShoulder = {
        cameraContext.playerPos.x + line.x * 0.48f + right.x * 0.22f,
        cameraContext.playerPos.y + 0.92f,
        cameraContext.playerPos.z + line.y * 0.48f + right.z * 0.22f};
    XMFLOAT3 cameraPos = {
        cameraContext.playerPos.x - line.x * (4.65f + 0.42f * enemyPressure) +
            right.x * (2.15f + 0.16f * pulse) +
            right.x * std::sinf(shakePhase) * shake,
        cameraContext.playerPos.y + 0.72f + 0.10f * pulse +
            std::cosf(shakePhase * 1.31f) * shake * 0.42f,
        cameraContext.playerPos.z - line.y * (4.65f + 0.42f * enemyPressure) +
            right.z * (2.15f + 0.16f * pulse) +
            right.z * std::sinf(shakePhase) * shake};
    XMFLOAT3 lookAt = {playerShoulder.x * 0.34f + mid.x * 0.66f +
                           line.x * (0.20f + 0.34f * enemyPressure),
                       playerShoulder.y * 0.28f + mid.y * 0.72f + 0.14f +
                           0.05f * impact,
                       playerShoulder.z * 0.34f + mid.z * 0.66f +
                           line.y * (0.20f + 0.34f * enemyPressure)};
    cameraYaw_ = std::atan2f(line.x, line.y);
    gameplayCameraPoseInitialized_ = true;
    gameplayCameraPos_ = cameraPos;
    gameplayCameraLookAt_ = lookAt;
    combatFeedback_.ApplyCameraImpulse(cameraPos, lookAt, sceneLightTime_);
    camera_.SetPosition(cameraPos);
    AppLookAt(camera_, lookAt);
    return true;
}

bool GameScene::UpdatePlayerViewBattleCamera(
    const BattleCameraContext &cameraContext) {
    if (!playerViewCamera_) {
        return false;
    }
    const DirectX::XMFLOAT3 playerEye = {cameraContext.playerPos.x,
                                         cameraContext.playerPos.y +
                                             playerViewEyeHeight_,
                                         cameraContext.playerPos.z};

    if (isLockOn_) {
        float dx = cameraContext.enemyCameraPos.x - cameraContext.playerPos.x;
        float dz = cameraContext.enemyCameraPos.z - cameraContext.playerPos.z;
        float targetYaw = std::atan2f(dx, dz);
        float diff = WrapRadians(targetYaw - cameraYaw_);

        float inputMagnitude = 0.0f;
        if (ctx_->systems.input != nullptr &&
            ctx_->systems.input->IsGamepadConnected() &&
            player_.UsesGamepadCameraLook()) {
            inputMagnitude =
                (std::max)(inputMagnitude,
                           std::abs(
                               ctx_->systems.input->GetGamepadRightStickX()));
        }

        const float assistScale = inputMagnitude > 0.0f ? 0.42f : 1.0f;
        const float tripleIaiTurnBoost =
            cameraContext.tripleIaiCameraFocus ? 2.45f : 1.0f;
        const float applied =
            Clamp(diff * 7.2f * tripleIaiTurnBoost * assistScale *
                      ctx_->frame.deltaTime,
                  -8.0f * tripleIaiTurnBoost * ctx_->frame.deltaTime,
                  8.0f * tripleIaiTurnBoost * ctx_->frame.deltaTime);
        cameraYaw_ += applied;
    }

    const float viewCosPitch = std::cosf(cameraPitch_);
    const DirectX::XMFLOAT3 viewForward = {
        std::sinf(cameraYaw_) * viewCosPitch, std::sinf(cameraPitch_),
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
        DirectX::XMFLOAT3 desiredLookAt = {cameraContext.enemyCameraPos.x,
                                           cameraContext.enemyCameraPos.y +
                                               playerViewLockOnLookHeight_,
                                           cameraContext.enemyCameraPos.z};

        const float lookAlpha = SaturatedAlpha(16.0f, ctx_->frame.deltaTime);
        lockOnLookAt_ = Lerp(lockOnLookAt_, desiredLookAt, lookAlpha);
        lookAt = lockOnLookAt_;
    } else {
        lookAt = {cameraPos.x + viewForward.x * playerViewLookAhead_,
                  cameraPos.y + viewForward.y * playerViewLookAhead_,
                  cameraPos.z + viewForward.z * playerViewLookAhead_};
        lockOnLookAt_ = lookAt;
    }

    if (cameraContext.enemyPhaseTransition) {
        DirectX::XMFLOAT3 transitionLookAt = {cameraContext.enemyPos.x,
                                              cameraContext.enemyPos.y +
                                                  phaseTransitionLookAtHeight_,
                                              cameraContext.enemyPos.z};
        cameraPos.x -= viewForward.x * phaseTransitionCameraPullBack_ *
                       cameraContext.enemyPhaseTransitionRatio;
        cameraPos.y += phaseTransitionCameraRise_ *
                       cameraContext.enemyPhaseTransitionRatio;
        cameraPos.z -= viewForward.z * phaseTransitionCameraPullBack_ *
                       cameraContext.enemyPhaseTransitionRatio;
        lookAt = Lerp(lookAt, transitionLookAt,
                      cameraContext.enemyPhaseTransitionRatio);
    }
    ApplyGameplayCameraPose(cameraPos, lookAt,
                            playerViewCameraPositionLerpSpeed_,
                            playerViewCameraLookAtLerpSpeed_);
    return true;
}

void GameScene::UpdateThirdPersonLockAssist(
    const BattleCameraContext &cameraContext) {
    if (isLockOn_) {
        DirectX::XMFLOAT3 assistTarget = cameraContext.enemyCameraPos;

        float assistStrength = lockOnAssistStrength_;
        float assistMaxStep = lockOnAssistMaxStep_;

        if (cameraContext.enemyPhaseTransition) {
            assistStrength = lockOnAssistStrength_ * 1.35f;
            assistMaxStep = lockOnAssistMaxStep_ * 1.35f;
        }
        if (cameraContext.tripleIaiCameraFocus) {
            assistStrength *= 2.35f;
            assistMaxStep *= 2.35f;
        }

        float dx = assistTarget.x - cameraContext.playerPos.x;
        float dz = assistTarget.z - cameraContext.playerPos.z;

        float targetYaw = std::atan2f(dx, dz);
        float diff = WrapRadians(targetYaw - cameraYaw_);

        float inputMagnitude = 0.0f;
        if (ctx_->systems.input != nullptr &&
            ctx_->systems.input->IsGamepadConnected() &&
            player_.UsesGamepadCameraLook()) {
            const float stick =
                std::abs(ctx_->systems.input->GetGamepadRightStickX());
            if (stick > inputMagnitude) {
                inputMagnitude = stick;
            }
        }

        float assistScale = 1.0f;
        if (inputMagnitude > 0.0f) {
            assistScale = lockOnInputReduce_;
        }

        float maxStep = assistMaxStep * assistScale * ctx_->frame.deltaTime;
        float applied =
            diff * assistStrength * assistScale * ctx_->frame.deltaTime;

        applied = Clamp(applied, -maxStep, maxStep);

        cameraYaw_ += applied;
    }

    // =========================
    // yaw / pitch 縺九ｉ蝓�E�貁E�E�E��E�繧剁E��懊ａE
    // =========================
}

DirectX::XMFLOAT3 GameScene::ComputeThirdPersonCameraPosition(
    const BattleCameraContext &cameraContext, const DirectX::XMFLOAT3 &forward,
    const DirectX::XMFLOAT3 &right) {
    const DirectX::XMFLOAT3 cameraTargetBase{cameraContext.playerPos.x,
                                             cameraContext.playerPos.y +
                                                 cameraLookHeight_,
                                             cameraContext.playerPos.z};
    DirectX::XMFLOAT3 cameraPos{};

    if (isLockOn_) {
        // ---------------------------------
        // 繝ｭ繝�Eけ繧�E�繝ｳ譎�E
        // 謨�E�縺�E�縺�E�繝ｩ繧�E�繝ｳ蝓ｺ貁E��〒蜀・�E��E�霑ｽ蠕�E
        // ---------------------------------
        float toEnemyX =
            cameraContext.enemyCameraPos.x - cameraContext.playerPos.x;
        float toEnemyZ =
            cameraContext.enemyCameraPos.z - cameraContext.playerPos.z;
        const float distXZ =
            DistanceXZ(cameraContext.enemyCameraPos, cameraContext.playerPos);
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
        if (cameraContext.enemyPressureAction) {
            usedRadius -= 0.45f;
        }
        if (cameraContext.enemyPhaseTransition) {
            usedRadius += phaseTransitionCameraPullBack_ *
                          cameraContext.enemyPhaseTransitionRatio;
        }
        usedRadius = Clamp(usedRadius, 3.35f,
                           cameraContext.enemyPhaseTransition ? 9.2f : 7.4f);

        // cameraYaw_
        // 縺�E�謨�E�譁E��蜷代Λ繧�E�繝ｳ縺�E�縺�E�蟾�E�縺�E�縲∝�E蠑ｧ荳翫・蟾�E�蜿�E�菴咲�E��E�繧呈ｱ�E�繧√ａE
        float lineYaw = std::atan2f(lineX, lineZ);
        float yawDiff = WrapRadians(cameraYaw_ - lineYaw);

        // 逵滓ｨ�E�縺�E�縺�E�蝗槭�E�縺吶℁E��九�E隕九▼繧峨�E�縺�E�縺�E�蛻�E�髯・
        const float maxOrbitAngle = 0.65f;
        yawDiff = Clamp(yawDiff, -maxOrbitAngle, maxOrbitAngle);

        float sinA = std::sinf(yawDiff);
        float cosA = std::cosf(yawDiff);

        const float sideBias =
            lockOnOrbitSideBias_ * 0.55f + 0.16f * pullT +
            (cameraContext.enemyPressureAction ? 0.10f : 0.0f);

        DirectX::XMFLOAT3 desiredCameraPos = {
            cameraContext.playerPos.x - lineX * usedRadius * cosA +
                orbitRightX * usedRadius * sinA + orbitRightX * sideBias,
            cameraContext.playerPos.y + 1.42f + 0.16f * pullT +
                phaseTransitionCameraRise_ *
                    cameraContext.enemyPhaseTransitionRatio,
            cameraContext.playerPos.z - lineZ * usedRadius * cosA +
                orbitRightZ * usedRadius * sinA + orbitRightZ * sideBias};

        const float posAlpha =
            SaturatedAlpha(lockOnOrbitLerpSpeed_, ctx_->frame.deltaTime);
        lockOnOrbitCameraPos_ =
            Lerp(lockOnOrbitCameraPos_, desiredCameraPos, posAlpha);

        cameraPos = lockOnOrbitCameraPos_;
    } else {
        // ---------------------------------
        // 騾壼�E��E�譎�E 閧�E�雜翫�E�荳我ｺ�E�遘ｰ
        // ---------------------------------
        float enemyDistanceXZ =
            DistanceXZ(cameraContext.enemyCameraPos, cameraContext.playerPos);
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

        if (cameraContext.enemyPhaseTransition) {
            cameraPos.x -= forward.x * phaseTransitionCameraPullBack_ *
                           cameraContext.enemyPhaseTransitionRatio;
            cameraPos.y += phaseTransitionCameraRise_ *
                           cameraContext.enemyPhaseTransitionRatio;
            cameraPos.z -= forward.z * phaseTransitionCameraPullBack_ *
                           cameraContext.enemyPhaseTransitionRatio;
        }
        // 髱槭Ο繝�Eけ譎ゅ・蜀・�E��E�逕ｨ迴�E�蝨�E�蛟､繧貞�E譛�E
        lockOnOrbitCameraPos_ = cameraPos;
    }

    // =========================
    // 豕ｨ隕也せ
    // =========================
    return cameraPos;
}

DirectX::XMFLOAT3
GameScene::ComputeThirdPersonLookAt(const BattleCameraContext &cameraContext,
                                    const DirectX::XMFLOAT3 &cameraTargetBase,
                                    const DirectX::XMFLOAT3 &forward) {
    DirectX::XMFLOAT3 lookAt{};

    if (isLockOn_) {
        const float enemyLookHeight = 2.25f;
        const float playerLookHeight = 1.05f;
        const float enemyLookWeight = 0.68f;
        const float playerLookWeight = 1.0f - enemyLookWeight;
        DirectX::XMFLOAT3 desiredLookAt = {
            cameraContext.playerPos.x * lockOnLookPlayerWeight_ +
                cameraContext.enemyCameraPos.x * lockOnLookEnemyWeight_,
            (cameraContext.playerPos.y + playerLookHeight) * playerLookWeight +
                (cameraContext.enemyCameraPos.y + enemyLookHeight) *
                    enemyLookWeight,
            cameraContext.playerPos.z * lockOnLookPlayerWeight_ +
                cameraContext.enemyCameraPos.z * lockOnLookEnemyWeight_};

        const float lookAlpha = SaturatedAlpha(
            lockOnLookAtLerpSpeed_ *
                (cameraContext.tripleIaiCameraFocus ? 2.0f : 1.0f),
            ctx_->frame.deltaTime);
        lockOnLookAt_ = Lerp(lockOnLookAt_, desiredLookAt, lookAlpha);

        lookAt = lockOnLookAt_;
    } else {
        lookAt = {cameraTargetBase.x + forward.x * cameraLookAhead_,
                  cameraTargetBase.y + forward.y * cameraLookAhead_,
                  cameraTargetBase.z + forward.z * cameraLookAhead_};

        lockOnLookAt_ = lookAt;
    }

    if (cameraContext.enemyPhaseTransition) {
        DirectX::XMFLOAT3 transitionLookAt = {
            cameraContext.playerPos.x *
                    (1.0f - phaseTransitionLookAtEnemyWeight_) +
                cameraContext.enemyPos.x * phaseTransitionLookAtEnemyWeight_,
            (cameraContext.playerPos.y + cameraLookHeight_) *
                    (1.0f - phaseTransitionLookAtEnemyWeight_) +
                (cameraContext.enemyPos.y + phaseTransitionLookAtHeight_) *
                    phaseTransitionLookAtEnemyWeight_,
            cameraContext.playerPos.z *
                    (1.0f - phaseTransitionLookAtEnemyWeight_) +
                cameraContext.enemyPos.z * phaseTransitionLookAtEnemyWeight_};

        float blend = cameraContext.enemyPhaseTransitionRatio;
        lookAt = Lerp(lookAt, transitionLookAt, blend);
    }
    return lookAt;
}

void GameScene::UpdateThirdPersonBattleCamera(
    const BattleCameraContext &cameraContext) {
    UpdateThirdPersonLockAssist(cameraContext);
    const float cosPitch = std::cosf(cameraPitch_);
    const DirectX::XMFLOAT3 forward{std::sinf(cameraYaw_) * cosPitch,
                                    std::sinf(cameraPitch_),
                                    std::cosf(cameraYaw_) * cosPitch};
    const DirectX::XMFLOAT3 right{std::cosf(cameraYaw_), 0.0f,
                                  -std::sinf(cameraYaw_)};
    const DirectX::XMFLOAT3 cameraTargetBase{cameraContext.playerPos.x,
                                             cameraContext.playerPos.y +
                                                 cameraLookHeight_,
                                             cameraContext.playerPos.z};
    const DirectX::XMFLOAT3 cameraPos =
        ComputeThirdPersonCameraPosition(cameraContext, forward, right);
    const DirectX::XMFLOAT3 lookAt =
        ComputeThirdPersonLookAt(cameraContext, cameraTargetBase, forward);
    ApplyGameplayCameraPose(cameraPos, lookAt, gameplayCameraPositionLerpSpeed_,
                            gameplayCameraLookAtLerpSpeed_);
}
