#include "DifficultyCauldronScene.h"
#include "AppSceneServices.h"
#include "GameScene.h"
#include "HandLoadingScene.h"
#include "Input.h"
#include "PostEffectManager.h"
#include "SceneManager.h"
#include "SpriteManager.h"
#include "TextureManager.h"
#include "WeaponSelectScene.h"
#include "WinApp.h"
#include <Xinput.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using namespace DirectX;

namespace {
constexpr float kTransitionDuration = 0.22f;
constexpr float kStartTransitionDuration = 0.42f;
constexpr float kIntroBackgroundDuration = 0.86f;
constexpr float kIntroGaugeDelay = 0.12f;
constexpr float kIntroGaugeDuration = 0.62f;
constexpr float kIntroInputDelay = 0.62f;
constexpr float kControlsPadding = 32.0f;
constexpr float kControlsImageBottomTransparentPixels = 18.0f;
constexpr float kHandSwingStartSpeed = 0.78f;
constexpr float kHandSwingResetSpeed = 0.32f;
constexpr int kDifficultyMinTenths = 0;
constexpr int kDifficultyMaxTenths = 90;
constexpr uint16_t kPreviewPort = 5006;
constexpr float kPreviewStaleSeconds = 0.75f;
constexpr std::array<int, 10> kDigitKeys = {
    DIK_0, DIK_1, DIK_2, DIK_3, DIK_4,
    DIK_5, DIK_6, DIK_7, DIK_8, DIK_9};
constexpr std::array<int, 10> kNumpadDigitKeys = {
    DIK_NUMPAD0, DIK_NUMPAD1, DIK_NUMPAD2, DIK_NUMPAD3, DIK_NUMPAD4,
    DIK_NUMPAD5, DIK_NUMPAD6, DIK_NUMPAD7, DIK_NUMPAD8, DIK_NUMPAD9};

XMFLOAT4 Color(float r, float g, float b, float a = 1.0f) {
    return {r, g, b, a};
}

float SmoothStep(float t) {
    const float clamped = std::clamp(t, 0.0f, 1.0f);
    return clamped * clamped * (3.0f - 2.0f * clamped);
}

bool IsHandControl(InputControlType controlType) {
    return controlType == InputControlType::Hand;
}

float DifficultyRatio(float difficulty) {
    return std::clamp(difficulty, 0.0f, 9.0f) / 9.0f;
}

XMFLOAT4 LerpColor(const XMFLOAT4 &a, const XMFLOAT4 &b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
}

XMFLOAT4 GaugeHeatColor(float t, float alpha) {
    t = std::clamp(t, 0.0f, 1.0f);
    const XMFLOAT4 blue = Color(0.05f, 0.36f, 1.0f, alpha);
    const XMFLOAT4 yellow = Color(1.0f, 0.84f, 0.16f, alpha);
    const XMFLOAT4 red = Color(1.0f, 0.09f, 0.035f, alpha);
    if (t < 0.62f) {
        return LerpColor(blue, yellow, t / 0.62f);
    }
    return LerpColor(yellow, red, (t - 0.62f) / 0.38f);
}

int DifficultyDescriptionIndex(float difficulty) {
    if (difficulty < 2.0f) {
        return 0;
    }
    if (difficulty < 5.0f) {
        return 1;
    }
    if (difficulty < 7.0f) {
        return 2;
    }
    if (difficulty < 9.0f) {
        return 3;
    }
    return 4;
}

uint32_t CreateTriangleTexture(TextureManager *texture, bool gradient) {
    constexpr uint32_t kWidth = 512;
    constexpr uint32_t kHeight = 128;
    constexpr float kAaPixels = 1.8f;
    std::vector<uint8_t> pixels(static_cast<size_t>(kWidth) * kHeight * 4u);
    for (uint32_t py = 0; py < kHeight; ++py) {
        for (uint32_t px = 0; px < kWidth; ++px) {
            const float x = (static_cast<float>(px) + 0.5f) /
                            static_cast<float>(kWidth - 1u);
            const float y = (static_cast<float>(py) + 0.5f) /
                            static_cast<float>(kHeight - 1u);
            const float line = 1.0f - x;
            const float distancePixels = (y - line) * static_cast<float>(kHeight);
            const float alpha =
                std::clamp(distancePixels / kAaPixels + 0.5f, 0.0f, 1.0f);
            const XMFLOAT4 color =
                gradient ? GaugeHeatColor(x, alpha)
                         : Color(1.0f, 1.0f, 1.0f, alpha);
            const size_t index = (static_cast<size_t>(py) * kWidth + px) * 4u;
            pixels[index + 0] =
                static_cast<uint8_t>(std::clamp(color.x, 0.0f, 1.0f) * 255.0f);
            pixels[index + 1] =
                static_cast<uint8_t>(std::clamp(color.y, 0.0f, 1.0f) * 255.0f);
            pixels[index + 2] =
                static_cast<uint8_t>(std::clamp(color.z, 0.0f, 1.0f) * 255.0f);
            pixels[index + 3] =
                static_cast<uint8_t>(std::clamp(color.w, 0.0f, 1.0f) * 255.0f);
        }
    }

    return texture->CreateFromRgbaPixels(kWidth, kHeight, pixels.data());
}
} // namespace

DifficultyCauldronScene::DifficultyCauldronScene(
    const SwordInputCalibration &inputCalibration)
    : inputCalibration_(inputCalibration) {}

DifficultyCauldronScene::~DifficultyCauldronScene() {
    previewReceiver_.Close();
}

void DifficultyCauldronScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    sceneTime_ = 0.0f;
    transitionTimer_ = 0.0f;
    selectionPulse_ = 0.0f;
    handSwingCooldown_ = 0.0f;
    selectionHoldTimer_ = 0.0f;
    selectionRepeatTimer_ = 0.0f;
    selectionHoldDirection_ = 0;
    selectedDifficultyTenths_ = 20;
    handSwingArmed_ = true;
    handTrackingStartRequested_ = false;
    returnToSelectRequested_ = false;
    startGameRequested_ = false;

    if (IsHandControl(inputCalibration_.controlType)) {
        handController_.SetCalibration(inputCalibration_);
        RequestHandTrackingStartOnce();
        previewReceiver_.Initialize(ctx_->rendering.texture, kPreviewPort);
    }

    backgroundScene_ = std::make_unique<GameScene>(GameScene::Mode::ReadyPreview);
    backgroundScene_->Initialize(ctx);
    backgroundScene_->SetReadyPreviewHeat(DifficultyRatio(SelectedDifficultyValue()));

    for (int i = 0; i < 10; ++i) {
        digitImages_[static_cast<size_t>(i)] =
            LoadTextureImage(L"app/resources/ui/result/glyphs/char_" +
                             std::to_wstring(i) + L".png");
    }
    dotImage_ =
        LoadTextureImage(L"app/resources/ui/result/glyphs/char_dot.png");
    difficultyDescriptionImages_[0] = LoadTextureImage(
        L"app/resources/ui/difficulty/description_easy.png");
    difficultyDescriptionImages_[1] = LoadTextureImage(
        L"app/resources/ui/difficulty/description_normal.png");
    difficultyDescriptionImages_[2] = LoadTextureImage(
        L"app/resources/ui/difficulty/description_experienced.png");
    difficultyDescriptionImages_[3] = LoadTextureImage(
        L"app/resources/ui/difficulty/description_hard.png");
    difficultyDescriptionImages_[4] = LoadTextureImage(
        L"app/resources/ui/difficulty/description_extreme.png");
    controlsImage_ = LoadTextureImage(
        L"app/resources/ui/weapon_select/text/weapon_controls.png");
    triangleMaskImage_.textureId =
        CreateTriangleTexture(ctx_->rendering.texture, false);
    triangleMaskImage_.width = 512.0f;
    triangleMaskImage_.height = 128.0f;
    triangleGradientImage_.textureId =
        CreateTriangleTexture(ctx_->rendering.texture, true);
    triangleGradientImage_.width = 512.0f;
    triangleGradientImage_.height = 128.0f;

    ApplyHeatPostProcess();
}

void DifficultyCauldronScene::Update() {
    sceneTime_ += ctx_->frame.deltaTime;
    selectionPulse_ =
        (std::max)(0.0f, selectionPulse_ - ctx_->frame.deltaTime * 3.2f);
    handSwingCooldown_ =
        (std::max)(0.0f, handSwingCooldown_ - ctx_->frame.deltaTime);

    if (backgroundScene_) {
        const float introHeat =
            SmoothStep(sceneTime_ / kIntroBackgroundDuration);
        backgroundScene_->SetReadyPreviewHeat(
            DifficultyRatio(SelectedDifficultyValue()) * introHeat);
        backgroundScene_->Update();
    }
    if (sceneTime_ <= kIntroBackgroundDuration + 0.08f) {
        ApplyHeatPostProcess();
    }

    if (returnToSelectRequested_) {
        transitionTimer_ += ctx_->frame.deltaTime;
        if (transitionTimer_ >= kTransitionDuration) {
            sceneManager_->ChangeScene(std::make_unique<WeaponSelectScene>());
        }
        return;
    }

    if (startGameRequested_) {
        transitionTimer_ += ctx_->frame.deltaTime;
        if (transitionTimer_ >= kStartTransitionDuration) {
            sceneManager_->ChangeScene(std::make_unique<GameScene>(
                inputCalibration_, SelectedDifficultyValue()));
        }
        return;
    }

    if (IsHandControl(inputCalibration_.controlType)) {
        RequestHandTrackingStartOnce();
        handController_.Update(ctx_->frame.deltaTime);
        UpdateCameraPreview(ctx_->frame.deltaTime);
    }

    if (sceneTime_ >= kIntroInputDelay) {
        UpdateSelection();
    }
}

void DifficultyCauldronScene::Draw() {
    const float w = static_cast<float>(ctx_->systems.winApp->GetWidth());
    const float h = static_cast<float>(ctx_->systems.winApp->GetHeight());
    const float t = DifficultyRatio(SelectedDifficultyValue());

    if (backgroundScene_) {
        backgroundScene_->Draw();
    }

    const float backgroundIntro = SmoothStep(sceneTime_ / kIntroBackgroundDuration);
    const float overlayIntro = SmoothStep(sceneTime_ / 0.58f);
    ctx_->rendering.sprite->PreDraw();
    DrawRect(0.0f, 0.0f, w, h,
             Color(0.0f, 0.0f, 0.0f,
                   1.0f - backgroundIntro +
                       (0.16f - overlayIntro * 0.08f + t * 0.018f) *
                           backgroundIntro));
    DrawHeatEffects(w, h);
    ctx_->rendering.sprite->PostDraw();
}

void DifficultyCauldronScene::DrawPostProcessOverlay() {
    const float w = static_cast<float>(ctx_->systems.winApp->GetWidth());
    const float h = static_cast<float>(ctx_->systems.winApp->GetHeight());

    ctx_->rendering.sprite->PreDraw(true);
    DrawDifficultyGauge(w, h);
    const float controlsIntro = SmoothStep((sceneTime_ - 0.24f) / 0.28f);
    const float controlsScale =
        (std::min)(0.80f,
                   (w * 0.31f) / (std::max)(controlsImage_.width, 1.0f));
    DrawImage(controlsImage_, kControlsPadding,
              h - (controlsImage_.height -
                   kControlsImageBottomTransparentPixels) *
                      controlsScale -
                  kControlsPadding,
              controlsScale, 0.58f * controlsIntro);

    const float outroBlack =
        returnToSelectRequested_
            ? SmoothStep(transitionTimer_ / kTransitionDuration)
            : 0.0f;
    const float startBlack =
        startGameRequested_
            ? SmoothStep(transitionTimer_ / kStartTransitionDuration)
            : 0.0f;
    const float fadeAlpha = (std::max)(outroBlack, startBlack);
    if (fadeAlpha > 0.0f) {
        DrawRect(0.0f, 0.0f, w, h, Color(0.0f, 0.0f, 0.0f, fadeAlpha));
    }
    ctx_->rendering.sprite->PostDraw();

    if (!startGameRequested_ && !returnToSelectRequested_ &&
        IsHandControl(inputCalibration_.controlType)) {
        previewReceiver_.Draw(ctx_->rendering.sprite, ctx_->rendering.texture,
                              kPreviewStaleSeconds, true);
    }
}

void DifficultyCauldronScene::DrawTransparent() {
}

DifficultyCauldronScene::Image
DifficultyCauldronScene::LoadTextureImage(const std::wstring &path) {
    Image image{};
    image.textureId = ctx_->rendering.texture->Load(path);
    image.width =
        static_cast<float>(ctx_->rendering.texture->GetWidth(image.textureId));
    image.height =
        static_cast<float>(ctx_->rendering.texture->GetHeight(image.textureId));
    return image;
}

bool DifficultyCauldronScene::RequestHandTrackingStartOnce() {
    if (handTrackingStartRequested_ || ctx_ == nullptr) {
        return handTrackingStartRequested_;
    }
    if (!AppSceneServices::HasHandTrackingStart()) {
        return false;
    }
    handTrackingStartRequested_ = AppSceneServices::RequestHandTrackingStart();
    return handTrackingStartRequested_;
}

void DifficultyCauldronScene::UpdateCameraPreview(float deltaTime) {
    previewReceiver_.Update(deltaTime);
}

float DifficultyCauldronScene::SelectedDifficultyValue() const {
    return static_cast<float>(selectedDifficultyTenths_) * 0.1f;
}

void DifficultyCauldronScene::UpdateSelection() {
    Input *input = ctx_->systems.input;
    if (input == nullptr) {
        return;
    }

    const bool gamepad = input->IsGamepadConnected();
    int nextDifficultyTenths = selectedDifficultyTenths_;
    const bool decreaseTrigger =
        input->IsKeyTrigger(DIK_A) || input->IsKeyTrigger(DIK_LEFT) ||
        (gamepad && input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_DPAD_LEFT));
    const bool increaseTrigger =
        input->IsKeyTrigger(DIK_D) || input->IsKeyTrigger(DIK_RIGHT) ||
        (gamepad && input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_DPAD_RIGHT));
    const bool decreaseHold =
        input->IsKeyPress(DIK_A) || input->IsKeyPress(DIK_LEFT) ||
        (gamepad && input->IsGamepadButtonPress(XINPUT_GAMEPAD_DPAD_LEFT));
    const bool increaseHold =
        input->IsKeyPress(DIK_D) || input->IsKeyPress(DIK_RIGHT) ||
        (gamepad && input->IsGamepadButtonPress(XINPUT_GAMEPAD_DPAD_RIGHT));

    int holdDirection = 0;
    if (decreaseHold && !increaseHold) {
        holdDirection = -1;
    } else if (increaseHold && !decreaseHold) {
        holdDirection = 1;
    }

    if (holdDirection == 0) {
        selectionHoldTimer_ = 0.0f;
        selectionRepeatTimer_ = 0.0f;
        selectionHoldDirection_ = 0;
    } else {
        if (holdDirection != selectionHoldDirection_) {
            selectionHoldDirection_ = holdDirection;
            selectionHoldTimer_ = 0.0f;
            selectionRepeatTimer_ = 0.0f;
        }

        selectionHoldTimer_ += ctx_->frame.deltaTime;
        selectionRepeatTimer_ -= ctx_->frame.deltaTime;
        const bool freshTrigger =
            (holdDirection < 0 && decreaseTrigger && !increaseTrigger) ||
            (holdDirection > 0 && increaseTrigger && !decreaseTrigger);
        const float repeatInterval =
            selectionHoldTimer_ >= 1.10f
                ? 0.032f
                : (selectionHoldTimer_ >= 0.40f ? 0.050f : 0.076f);
        const float initialRepeatDelay = 0.30f;
        const int step =
            selectionHoldTimer_ >= 1.10f ? 2 : 1;
        if (freshTrigger) {
            nextDifficultyTenths += holdDirection;
            selectionRepeatTimer_ = initialRepeatDelay;
        } else if (selectionHoldTimer_ >= initialRepeatDelay &&
                   selectionRepeatTimer_ <= 0.0f) {
            nextDifficultyTenths += holdDirection * step;
            selectionRepeatTimer_ = repeatInterval;
        }
    }

    for (int i = 0; i < 10; ++i) {
        if (input->IsKeyTrigger(kDigitKeys[static_cast<size_t>(i)]) ||
            input->IsKeyTrigger(kNumpadDigitKeys[static_cast<size_t>(i)])) {
            nextDifficultyTenths = i * 10;
            selectionHoldTimer_ = 0.0f;
            selectionRepeatTimer_ = 0.0f;
            selectionHoldDirection_ = 0;
        }
    }

    if (IsHandControl(inputCalibration_.controlType)) {
        const float speed =
            (std::max)(handController_.GetMotionSpeed(0),
                       handController_.GetMotionSpeed(1));
        if (handSwingArmed_ && speed >= kHandSwingStartSpeed &&
            handSwingCooldown_ <= 0.0f) {
            nextDifficultyTenths = selectedDifficultyTenths_ + 1;
            handSwingArmed_ = false;
            handSwingCooldown_ = 0.22f;
        }
        if (speed <= kHandSwingResetSpeed) {
            handSwingArmed_ = true;
        }
    }

    nextDifficultyTenths = std::clamp(nextDifficultyTenths,
                                      kDifficultyMinTenths,
                                      kDifficultyMaxTenths);
    if (nextDifficultyTenths != selectedDifficultyTenths_) {
        selectedDifficultyTenths_ = nextDifficultyTenths;
        selectionPulse_ = 1.0f;
        ApplyHeatPostProcess();
        AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Select);
    }

    if (input->IsKeyTrigger(DIK_ESCAPE) ||
        (gamepad && input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_B))) {
        AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Cancel);
        BeginReturnToWeaponSelect();
        return;
    }

    const bool confirm =
        input->IsKeyTrigger(DIK_SPACE) || input->IsKeyTrigger(DIK_RETURN) ||
        input->IsMouseTrigger(0) ||
        (gamepad && input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_A));
    if (confirm) {
        AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Selected);
        BeginStartGame();
    }
}

void DifficultyCauldronScene::BeginReturnToWeaponSelect() {
    returnToSelectRequested_ = true;
    startGameRequested_ = false;
    transitionTimer_ = 0.0f;
}

void DifficultyCauldronScene::BeginStartGame() {
    AppSceneServices::StopMenuBgm(ctx_);
    if (IsHandControl(inputCalibration_.controlType) &&
        !previewReceiver_.HasFreshFrame(kPreviewStaleSeconds)) {
        RequestHandTrackingStartOnce();
        sceneManager_->ChangeScene(std::make_unique<HandLoadingScene>(
            inputCalibration_, SelectedDifficultyValue()));
        return;
    }
    startGameRequested_ = true;
    returnToSelectRequested_ = false;
    transitionTimer_ = 0.0f;
}

void DifficultyCauldronScene::ApplyHeatPostProcess() {
    if (ctx_ == nullptr || ctx_->rendering.postEffectManager == nullptr) {
        return;
    }

    const float t = DifficultyRatio(SelectedDifficultyValue());
    const float intro = SmoothStep(sceneTime_ / kIntroBackgroundDuration);
    const float danger = t * t * (3.0f - 2.0f * t);
    PostProcessProfile profile{};
    profile.vignette.enabled = true;
    profile.vignette.strength = (0.04f + 0.06f * t + 0.08f * danger) * intro;
    profile.vignette.scale = 8.2f + 1.4f * t + 1.8f * danger;
    profile.vignette.power = 1.02f + 0.08f * danger;
    profile.radialBlur.sampleCount = 1;
    profile.radialBlur.strength = 0.0f;
    profile.sceneDim.strength =
        (0.004f + 0.018f * t + 0.030f * danger) * intro;
    ctx_->rendering.postEffectManager->SetBaseProfile(profile);
}

void DifficultyCauldronScene::DrawHeatEffects(float screenWidth,
                                              float screenHeight) {
    const float t = DifficultyRatio(SelectedDifficultyValue());
    const float intro = SmoothStep(sceneTime_ / kIntroBackgroundDuration);
    const float danger = t * t * (3.0f - 2.0f * t);
    const float pulse =
        0.5f + 0.5f * std::sinf(sceneTime_ * (4.2f + 7.5f * danger));
    const float panic =
        danger * (0.72f + 0.28f * std::sinf(sceneTime_ * 17.0f));
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
             Color(0.08f + 0.18f * t + 0.22f * danger,
                   0.055f + 0.060f * t,
                   0.15f * (1.0f - t) + 0.04f * t,
                   (0.040f + 0.050f * t + 0.055f * danger) * intro));

    if (t > 0.18f) {
        const float bandAlpha = (0.055f + 0.20f * danger) *
                                (0.55f + 0.45f * pulse) * intro;
        const float bandH = screenHeight * (0.050f + 0.085f * danger);
        DrawRect(0.0f, 0.0f, screenWidth, bandH,
                 Color(1.0f, 0.42f + 0.18f * t, 0.08f, bandAlpha));
        DrawRect(0.0f, screenHeight - bandH, screenWidth, bandH,
                 Color(1.0f, 0.20f + 0.20f * t, 0.04f, bandAlpha * 1.25f));
    }

    if (t > 0.48f) {
        const float sideW = screenWidth * (0.012f + 0.070f * danger);
        const float sideAlpha = (0.035f + 0.26f * panic) * intro;
        DrawRect(0.0f, 0.0f, sideW, screenHeight,
                 Color(1.0f, 0.10f + 0.16f * t, 0.02f, sideAlpha));
        DrawRect(screenWidth - sideW, 0.0f, sideW, screenHeight,
                 Color(1.0f, 0.08f + 0.12f * t, 0.02f, sideAlpha));
    }

    if (t > 0.74f) {
        const float flashAlpha = (t - 0.74f) / 0.26f;
        const float streakY =
            screenHeight * (0.22f + 0.52f * (0.5f + 0.5f * std::sinf(sceneTime_ * 9.3f)));
        DrawRect(0.0f, streakY, screenWidth, 5.0f + 10.0f * danger,
                 Color(1.0f, 0.52f, 0.10f,
                       0.090f * flashAlpha * pulse * intro));
        DrawRect(0.0f, streakY + 16.0f, screenWidth, 2.0f + 5.0f * danger,
                 Color(1.0f, 0.10f, 0.04f, 0.060f * flashAlpha * intro));
    }

    if (t > 0.34f) {
        const float heatAlpha = (t - 0.34f) / 0.66f;
        for (int i = 0; i < 5; ++i) {
            const float lane = static_cast<float>(i) / 4.0f;
            const float wave =
                0.5f + 0.5f * std::sinf(sceneTime_ * (5.8f + lane * 2.7f) +
                                         lane * 6.28318f);
            const float y =
                screenHeight * (0.15f + 0.70f * lane + 0.035f * (wave - 0.5f));
            const float h = 2.0f + 8.0f * danger + 7.0f * wave * heatAlpha;
            const float alpha =
                (0.018f + 0.065f * danger) * heatAlpha *
                (0.45f + 0.55f * wave) * intro;
            DrawRect(0.0f, y, screenWidth, h,
                     Color(1.0f, 0.36f + 0.28f * t, 0.06f, alpha));
        }
    }

    if (t > 0.82f) {
        const float maxAlpha = (t - 0.82f) / 0.18f;
        const float flash =
            0.5f + 0.5f * std::sinf(sceneTime_ * 23.0f);
        DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
                 Color(1.0f, 0.30f, 0.08f,
                       0.022f * maxAlpha * flash * intro));
    }
}

void DifficultyCauldronScene::DrawDifficultyGauge(float screenWidth,
                                                  float screenHeight) {
    const float difficulty = SelectedDifficultyValue();
    const float t = DifficultyRatio(difficulty);
    const float gaugeW = std::clamp(screenWidth * 0.68f, 640.0f, 980.0f);
    const float gaugeH = std::clamp(screenHeight * 0.18f, 116.0f, 164.0f);
    const float x = (screenWidth - gaugeW) * 0.5f;
    const float y = screenHeight * 0.68f;
    const float form =
        SmoothStep((sceneTime_ - kIntroGaugeDelay) / kIntroGaugeDuration);
    const float labelIntro =
        SmoothStep((sceneTime_ - kIntroGaugeDelay - 0.20f) /
                   (kIntroGaugeDuration - 0.08f));
    if (form <= 0.001f) {
        return;
    }
    const float formedW = gaugeW * form;
    const float leftTrim = 3.0f;
    const XMFLOAT4 gold = Color(0.86f, 0.61f - 0.22f * t, 0.21f, 0.94f);
    const XMFLOAT4 brightGold =
        Color(1.0f, 0.86f - 0.32f * t, 0.38f, 0.92f);

    const float shadowW = formedW + 36.0f * form;
    const float shadowTrim = (std::min)(leftTrim, shadowW);
    const float shadowUvLeft = shadowW > 0.0f ? (shadowTrim / shadowW) * form : 0.0f;
    DrawTextureRect(triangleMaskImage_.textureId, x - 18.0f + shadowTrim,
                    y - 16.0f, shadowW - shadowTrim, gaugeH + 32.0f,
                    Color(0.0f, 0.0f, 0.0f, 0.46f * form),
                    form - shadowUvLeft, SpriteBlendMode::PremultipliedMask,
                    shadowUvLeft);

    const float outerW = formedW + 16.0f * form;
    const float outerTrim = (std::min)(leftTrim, outerW);
    const float outerUvLeft = outerW > 0.0f ? (outerTrim / outerW) * form : 0.0f;
    DrawTextureRect(triangleMaskImage_.textureId, x - 8.0f + outerTrim,
                    y - 8.0f, outerW - outerTrim, gaugeH + 16.0f,
                    Color(gold.x, gold.y, gold.z, gold.w * form),
                    form - outerUvLeft, SpriteBlendMode::PremultipliedMask,
                    outerUvLeft);

    const float backW = (gaugeW - 32.0f) * form;
    const float backTrim = (std::min)(leftTrim, backW);
    const float backUvLeft = backW > 0.0f ? (backTrim / backW) * form : 0.0f;
    DrawTextureRect(triangleMaskImage_.textureId, x + 12.0f + backTrim,
                    y + 14.0f, backW - backTrim, gaugeH - 30.0f,
                    Color(0.010f, 0.012f, 0.020f, 0.94f * form),
                    form - backUvLeft, SpriteBlendMode::PremultipliedMask,
                    backUvLeft);

    const float innerW = gaugeW - 44.0f;
    const float innerH = gaugeH - 40.0f;
    const float innerX = x + 18.0f;
    const float innerY = y + 20.0f;

    const float fillT = (std::min)(t, form);
    if (fillT > 0.001f) {
        const float fillW = innerW * fillT;
        const float fillTrim = (std::min)(leftTrim, fillW);
        const float fillUvLeft =
            fillW > 0.0f ? (fillTrim / fillW) * fillT : 0.0f;
        DrawTextureRect(triangleGradientImage_.textureId, innerX + fillTrim,
                        innerY, fillW - fillTrim, innerH,
                        Color(1.18f, 1.12f, 1.06f, 1.0f * form),
                        fillT - fillUvLeft, SpriteBlendMode::Alpha,
                        fillUvLeft);
    }

    for (int i = 0; i < 10; ++i) {
        const float markerT = DifficultyRatio(static_cast<float>(i));
        if (markerT > form + 0.015f) {
            continue;
        }
        const float markerX = innerX + innerW * markerT;
        const float markerH = (std::max)(6.0f, innerH * markerT);
        const float markerY = innerY + innerH - markerH;
        const bool selected = selectedDifficultyTenths_ == i * 10;
        const float markerAlpha =
            labelIntro * std::clamp((form - markerT) * 8.0f, 0.0f, 1.0f);
        const XMFLOAT4 markerColor =
            selected ? Color(brightGold.x, brightGold.y, brightGold.z,
                             brightGold.w * markerAlpha)
                     : Color(0.70f, 0.76f, 0.88f, 0.20f * markerAlpha);
        if (i != 0) {
            DrawRect(markerX - 2.0f, markerY, 4.0f, markerH, markerColor);
            DrawRect(markerX - 5.0f, markerY - 5.0f, 10.0f, 4.0f,
                     selected
                         ? markerColor
                         : Color(0.52f, 0.45f, 0.32f, 0.42f * markerAlpha));
        }
        DrawDigit(i, markerX, y + gaugeH + 18.0f, selected ? 0.62f : 0.46f,
                  (selected ? 0.95f : 0.54f) * markerAlpha);
    }

    const float selectedX = innerX + innerW * t;
    const float digitScale = 1.56f + 0.20f * selectionPulse_;
    DrawDifficultyDescription(difficulty, innerX + 26.0f, innerY + 12.0f,
                              innerW - 68.0f, 0.96f * labelIntro);
    DrawDifficultyValue(difficulty, selectedX,
                        y - 86.0f - selectionPulse_ * 8.0f, digitScale,
                        0.98f * labelIntro);
}

void DifficultyCauldronScene::DrawDigit(int digit, float centerX, float y,
                                        float scale, float alpha) {
    digit = std::clamp(digit, 0, 9);
    const Image &image = digitImages_[static_cast<size_t>(digit)];
    DrawImage(image, centerX - image.width * scale * 0.5f, y, scale, alpha);
}

void DifficultyCauldronScene::DrawDifficultyValue(float value, float centerX,
                                                  float y, float scale,
                                                  float alpha) {
    const int tenths =
        std::clamp(static_cast<int>(std::lround(value * 10.0f)), 0, 90);
    const int ones = tenths / 10;
    const int decimal = tenths % 10;
    const Image &onesImage = digitImages_[static_cast<size_t>(ones)];
    const Image &decimalImage = digitImages_[static_cast<size_t>(decimal)];
    const float spacing = 4.0f * scale;
    const float totalWidth = onesImage.width * scale + spacing +
                             dotImage_.width * scale + spacing +
                             decimalImage.width * scale;
    float x = centerX - totalWidth * 0.5f;
    DrawImage(onesImage, x, y, scale, alpha);
    x += onesImage.width * scale + spacing;
    DrawImage(dotImage_, x, y, scale, alpha);
    x += dotImage_.width * scale + spacing;
    DrawImage(decimalImage, x, y, scale, alpha);
}

void DifficultyCauldronScene::DrawDifficultyDescription(float difficulty,
                                                        float x, float y,
                                                        float maxWidth,
                                                        float alpha) {
    const Image &image = difficultyDescriptionImages_[static_cast<size_t>(
        DifficultyDescriptionIndex(difficulty))];
    if (image.width <= 0.0f || image.height <= 0.0f || maxWidth <= 0.0f) {
        return;
    }

    const float scale = (std::min)(maxWidth / image.width, 1.0f);
    DrawImage(image, x, y, scale, alpha);
}

void DifficultyCauldronScene::DrawCameraPreview() {
    if (ctx_ == nullptr || ctx_->rendering.sprite == nullptr ||
        ctx_->rendering.texture == nullptr || ctx_->systems.winApp == nullptr) {
        return;
    }
    previewReceiver_.Draw(ctx_->rendering.sprite, ctx_->rendering.texture,
                          kPreviewStaleSeconds);
}

void DifficultyCauldronScene::DrawRect(float x, float y, float w, float h,
                                       const XMFLOAT4 &color) {
    Sprite sprite{};
    sprite.position = {x, y};
    sprite.size = {w, h};
    sprite.color = color;
    sprite.textureId = 0;
    ctx_->rendering.sprite->DrawSprite(sprite);
}

void DifficultyCauldronScene::DrawTextureRect(uint32_t textureId, float x,
                                              float y, float w, float h,
                                              const XMFLOAT4 &color,
                                              float uvWidth,
                                              SpriteBlendMode blendMode,
                                              float uvLeft) {
    if (textureId == 0 || w <= 0.0f || h <= 0.0f) {
        return;
    }

    Sprite sprite{};
    sprite.position = {x, y};
    sprite.size = {w, h};
    sprite.uvLeftTop = {std::clamp(uvLeft, 0.0f, 1.0f), 0.0f};
    sprite.uvSize = {std::clamp(uvWidth, 0.0f, 1.0f), 1.0f};
    sprite.color = color;
    sprite.textureId = textureId;
    sprite.blendMode = blendMode;
    ctx_->rendering.sprite->DrawSprite(sprite);
}

void DifficultyCauldronScene::DrawFrame(float x, float y, float w, float h,
                                        float thickness,
                                        const XMFLOAT4 &color) {
    DrawRect(x, y, w, thickness, color);
    DrawRect(x, y + h - thickness, w, thickness, color);
    DrawRect(x, y, thickness, h, color);
    DrawRect(x + w - thickness, y, thickness, h, color);
}

void DifficultyCauldronScene::DrawImage(const Image &image, float x, float y,
                                        float scale, float alpha) {
    if (image.width <= 0.0f || image.height <= 0.0f) {
        return;
    }

    Sprite sprite{};
    sprite.position = {x, y};
    sprite.size = {image.width * scale, image.height * scale};
    sprite.color = {1.0f, 1.0f, 1.0f, alpha};
    sprite.textureId = image.textureId;
    ctx_->rendering.sprite->DrawSprite(sprite);
}
