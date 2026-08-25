#include "TitleScene.h"
#include "AppSceneServices.h"
#include "CameraAccuracyDebugScene.h"
#include "GameOverScene.h"
#include "Input.h"
#include "PostEffectManager.h"
#include "SceneManager.h"
#include "SoundManager.h"
#include "Sprite.h"
#include "SpriteManager.h"
#include "TextureManager.h"
#include "WeaponSelectScene.h"
#include "WinApp.h"
#include <Xinput.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

using namespace DirectX;

namespace {
constexpr float kFadeDuration = 0.48f;
constexpr float kFrameIntroDuration = 1.12f;
constexpr float kControlsPadding = 32.0f;
constexpr float kControlsImageBottomTransparentPixels = 18.0f;
constexpr float kPi = 3.14159265f;

XMFLOAT4 MakeColor(float r, float g, float b, float a = 1.0f) {
    return {r, g, b, a};
}

float SmoothStep(float t) { return t * t * (3.0f - 2.0f * t); }

float Smooth01(float t) { return SmoothStep(std::clamp(t, 0.0f, 1.0f)); }

bool IsTitleStartKey(int key) {
    constexpr std::array kIgnoredKeys = {
        DIK_ESCAPE,   DIK_LWIN,    DIK_RWIN,   DIK_APPS,  DIK_LCONTROL,
        DIK_RCONTROL, DIK_LSHIFT,  DIK_RSHIFT, DIK_LMENU, DIK_RMENU,
        DIK_CAPITAL,  DIK_NUMLOCK, DIK_SCROLL, DIK_SYSRQ, DIK_PAUSE,
    };
    return std::ranges::find(kIgnoredKeys, key) == kIgnoredKeys.end();
}
} // namespace

TitleScene::~TitleScene() { StopTitleBgm(); }

void TitleScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    GameOverScene::ResetDefeatCounts();
    AppSceneServices::RequestHandTrackingStop();
    sceneTime_ = 0.0f;
    frameIntroTimer_ = 0.0f;
    fadeTimer_ = 0.0f;
    startRequested_ = false;
    exitConfirmVisible_ = false;
    exitConfirmIndex_ = 1;

    logoImage_ = LoadTitleImage(L"app/resources/ui/title/gamelogo.png");
    pressAnyButtonImage_ =
        LoadTitleImage(L"app/resources/ui/title/press_any_button.png");
    exitPromptImage_ = LoadTitleImage(L"app/resources/ui/title/esc_exit.png");
    exitConfirmMessageImage_ =
        LoadTitleImage(L"app/resources/ui/title/exit_confirm_message.png");
    exitConfirmYesImage_ =
        LoadTitleImage(L"app/resources/ui/title/exit_confirm_yes.png");
    exitConfirmNoImage_ =
        LoadTitleImage(L"app/resources/ui/title/exit_confirm_no.png");
    backgroundScene_ = std::make_unique<GameScene>(GameScene::Mode::TitleDemo);
    backgroundScene_->Initialize(ctx);
    StartTitleBgm();
}

void TitleScene::Update() {
    sceneTime_ += ctx_->frame.deltaTime;
    frameIntroTimer_ = (std::min)(frameIntroTimer_ + ctx_->frame.deltaTime,
                                  kFrameIntroDuration);
    if (backgroundScene_) {
        backgroundScene_->Update();
    }

    if (startRequested_) {
        fadeTimer_ += ctx_->frame.deltaTime;
        if (fadeTimer_ >= kFadeDuration) {
            if (ctx_->rendering.postEffectManager != nullptr) {
                ctx_->rendering.postEffectManager->SetBaseProfile(
                    PostProcessProfile{});
            }
            sceneManager_->ChangeScene(std::make_unique<WeaponSelectScene>());
        }
        return;
    }

    if (exitConfirmVisible_) {
        UpdateExitConfirm(*ctx_->systems.input);
        return;
    }

    if (ctx_->systems.input->IsKeyTrigger(DIK_ESCAPE)) {
        exitConfirmVisible_ = true;
        exitConfirmIndex_ = 1;
        AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Cancel);
        return;
    }

#if defined(_DEBUG)
    if (ctx_->systems.input->IsKeyTrigger(DIK_F9)) {
        AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Selected);
        sceneManager_->ChangeScene(
            std::make_unique<CameraAccuracyDebugScene>());
        return;
    }
#endif

    if (IsAnyButtonTriggered(*ctx_->systems.input)) {
        startRequested_ = true;
        fadeTimer_ = 0.0f;
        AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Selected);
        return;
    }
}

void TitleScene::Draw() {
    if (backgroundScene_) {
        backgroundScene_->Draw();
    }
}

void TitleScene::DrawTransparent() {
    const float w = static_cast<float>(ctx_->systems.winApp->GetWidth());
    const float h = static_cast<float>(ctx_->systems.winApp->GetHeight());

    ctx_->rendering.sprite->PreDraw();
    DrawTitleOverlay(w, h);
    if (exitConfirmVisible_) {
        DrawExitConfirmWindow(w, h);
    }
    ctx_->rendering.sprite->PostDraw();
}

TitleScene::Image TitleScene::LoadTitleImage(const std::wstring &path) {
    Image image{};
    image.textureId = ctx_->rendering.texture->Load(path);
    image.width =
        static_cast<float>(ctx_->rendering.texture->GetWidth(image.textureId));
    image.height =
        static_cast<float>(ctx_->rendering.texture->GetHeight(image.textureId));
    return image;
}

void TitleScene::UpdateExitConfirm(Input &input) {
    if (input.IsKeyTrigger(DIK_A) || input.IsKeyTrigger(DIK_LEFT)) {
        if (exitConfirmIndex_ != 0) {
            exitConfirmIndex_ = 0;
            AppSceneServices::PlayMenuSe(*ctx_,
                                         AppSceneServices::MenuSe::Select);
        }
    }
    if (input.IsKeyTrigger(DIK_D) || input.IsKeyTrigger(DIK_RIGHT)) {
        if (exitConfirmIndex_ != 1) {
            exitConfirmIndex_ = 1;
            AppSceneServices::PlayMenuSe(*ctx_,
                                         AppSceneServices::MenuSe::Select);
        }
    }

    if (input.IsKeyTrigger(DIK_ESCAPE)) {
        exitConfirmVisible_ = false;
        exitConfirmIndex_ = 1;
        AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Cancel);
        return;
    }

    const bool confirm =
        input.IsKeyTrigger(DIK_RETURN) || input.IsKeyTrigger(DIK_SPACE);
    if (!confirm) {
        return;
    }

    if (exitConfirmIndex_ == 0) {
        AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Selected);
        ctx_->systems.winApp->RequestClose();
    } else {
        exitConfirmVisible_ = false;
        AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Cancel);
    }
}

void TitleScene::DrawTitleOverlay(float screenWidth, float screenHeight) {
    const float frameT =
        std::clamp(frameIntroTimer_ / kFrameIntroDuration, 0.0f, 1.0f);
    const float backgroundReveal = Smooth01((frameT - 0.42f) / 0.38f);
    const float idle = Smooth01((frameT - 0.82f) / 0.18f);
    const float breath = idle * (0.5f + 0.5f * std::sinf(sceneTime_ * 0.9f));
    const float settledDim = 0.42f + 0.03f * breath;
    const float backgroundDim = 1.0f - (1.0f - settledDim) * backgroundReveal;

    DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
             MakeColor(0.0f, 0.0f, 0.0f, backgroundDim));
    DrawStartupFrame(screenWidth, screenHeight);

    constexpr float kLogoScale = 1.0f;
    const float logoX = (screenWidth - logoImage_.width * kLogoScale) * 0.5f;
    const float logoY =
        (screenHeight - logoImage_.height * kLogoScale) * 0.5f - 10.0f;
    DrawImage(logoImage_, logoX, logoY, backgroundReveal, kLogoScale);

    const float pressScale = std::clamp(
        screenWidth * 0.28f / pressAnyButtonImage_.width, 0.48f, 0.82f);
    const float pressX =
        (screenWidth - pressAnyButtonImage_.width * pressScale) * 0.5f;
    const float pressY = logoY + logoImage_.height * kLogoScale + 26.0f +
                         idle * std::sinf(sceneTime_ * 1.15f) * 1.4f;
    const float pressAlpha =
        idle * (0.48f + 0.18f * (0.5f + 0.5f * std::sinf(sceneTime_ * 3.0f)));
    DrawImage(pressAnyButtonImage_, pressX, pressY, pressAlpha, pressScale);

    const float exitPromptScale =
        (std::min)(0.80f, (screenWidth * 0.31f) /
                              (std::max)(exitPromptImage_.width, 1.0f));
    const float exitPromptY =
        screenHeight -
        (exitPromptImage_.height - kControlsImageBottomTransparentPixels) *
            exitPromptScale -
        kControlsPadding;
    DrawImage(exitPromptImage_, kControlsPadding, exitPromptY, idle * 0.58f,
              exitPromptScale);

    if (startRequested_) {
        const float fadeT = std::clamp(fadeTimer_ / kFadeDuration, 0.0f, 1.0f);
        DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
                 MakeColor(0.0f, 0.0f, 0.0f, SmoothStep(fadeT)));
    }
}

void TitleScene::DrawExitConfirmWindow(float screenWidth, float screenHeight) {
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
             MakeColor(0.0f, 0.0f, 0.0f, 0.54f));

    const float panelW = std::clamp(screenWidth * 0.50f, 520.0f, 760.0f);
    const float panelH = std::clamp(screenHeight * 0.30f, 240.0f, 320.0f);
    const float panelX = (screenWidth - panelW) * 0.5f;
    const float panelY = (screenHeight - panelH) * 0.5f;
    const float edge = 3.0f;

    DrawRect(panelX + 10.0f, panelY + 12.0f, panelW, panelH,
             MakeColor(0.0f, 0.0f, 0.0f, 0.36f));
    DrawRect(panelX, panelY, panelW, panelH,
             MakeColor(0.018f, 0.020f, 0.026f, 0.96f));
    DrawRect(panelX, panelY, panelW, edge,
             MakeColor(0.92f, 0.68f, 0.28f, 0.88f));
    DrawRect(panelX, panelY + panelH - edge, panelW, edge,
             MakeColor(0.92f, 0.68f, 0.28f, 0.74f));
    DrawRect(panelX, panelY, edge, panelH,
             MakeColor(0.92f, 0.68f, 0.28f, 0.62f));
    DrawRect(panelX + panelW - edge, panelY, edge, panelH,
             MakeColor(0.92f, 0.68f, 0.28f, 0.62f));

    const float messageScale =
        (std::min)(1.0f,
                   (panelW * 0.78f) /
                       ((std::max)(exitConfirmMessageImage_.width, 1.0f)));
    const float messageW = exitConfirmMessageImage_.width * messageScale;
    const float messageH = exitConfirmMessageImage_.height * messageScale;
    DrawImage(exitConfirmMessageImage_, panelX + (panelW - messageW) * 0.5f,
              panelY + panelH * 0.26f - messageH * 0.5f, 1.0f, messageScale);

    const float buttonW = std::clamp(panelW * 0.24f, 130.0f, 176.0f);
    const float buttonH = std::clamp(panelH * 0.23f, 58.0f, 76.0f);
    const float buttonGap = panelW * 0.08f;
    const float totalButtonW = buttonW * 2.0f + buttonGap;
    const float buttonY = panelY + panelH * 0.61f;
    const float firstButtonX = panelX + (panelW - totalButtonW) * 0.5f;
    const Image *labels[2] = {&exitConfirmYesImage_, &exitConfirmNoImage_};

    for (int i = 0; i < 2; ++i) {
        const float x =
            firstButtonX + static_cast<float>(i) * (buttonW + buttonGap);
        const bool selected = i == exitConfirmIndex_;
        const XMFLOAT4 body = selected
                                  ? MakeColor(0.18f, 0.13f, 0.055f, 0.98f)
                                  : MakeColor(0.040f, 0.046f, 0.058f, 0.92f);
        const XMFLOAT4 line = selected ? MakeColor(1.0f, 0.78f, 0.34f, 0.96f)
                                       : MakeColor(0.62f, 0.66f, 0.72f, 0.38f);

        DrawRect(x, buttonY, buttonW, buttonH, body);
        DrawRect(x, buttonY, buttonW, 2.0f, line);
        DrawRect(x, buttonY + buttonH - 2.0f, buttonW, 2.0f, line);
        DrawRect(x, buttonY, 2.0f, buttonH, line);
        DrawRect(x + buttonW - 2.0f, buttonY, 2.0f, buttonH, line);

        const Image &label = *labels[i];
        const float labelScale =
            (std::min)({1.0f,
                        (buttonH * 0.68f) / ((std::max)(label.height, 1.0f)),
                        (buttonW * 0.86f) / ((std::max)(label.width, 1.0f))});
        const float labelW = label.width * labelScale;
        const float labelH = label.height * labelScale;
        DrawImage(label, x + (buttonW - labelW) * 0.5f,
                  buttonY + (buttonH - labelH) * 0.5f, selected ? 1.0f : 0.82f,
                  labelScale);
    }
}

void TitleScene::DrawStartupFrame(float screenWidth, float screenHeight) {
    const float t = Smooth01(frameIntroTimer_ / kFrameIntroDuration);
    const float rawT =
        std::clamp(frameIntroTimer_ / kFrameIntroDuration, 0.0f, 1.0f);
    const float overshoot = std::sinf(std::clamp(t, 0.0f, 1.0f) * kPi) * 0.045f;
    const float barHeight = screenHeight * (0.124f + overshoot);
    const float topY = -barHeight * (1.0f - t);
    const float bottomY = screenHeight - barHeight * t;

    DrawRect(0.0f, topY, screenWidth, barHeight,
             MakeColor(0.0f, 0.0f, 0.0f, 0.96f));
    DrawRect(0.0f, bottomY, screenWidth, barHeight,
             MakeColor(0.0f, 0.0f, 0.0f, 0.96f));
    DrawRect(0.0f, topY + barHeight * 0.62f, screenWidth, barHeight * 0.38f,
             MakeColor(0.020f, 0.016f, 0.012f, 0.44f));
    DrawRect(0.0f, bottomY, screenWidth, barHeight * 0.38f,
             MakeColor(0.020f, 0.016f, 0.012f, 0.44f));

    const float idle = Smooth01((rawT - 0.86f) / 0.14f);
    if (idle > 0.0f) {
        const float sheenWidth = screenWidth * 0.30f;
        const float sheenTravel = std::fmod(sceneTime_ * 0.105f, 1.0f);
        const float sheenX =
            -sheenWidth + (screenWidth + sheenWidth * 2.0f) * sheenTravel;
        const float sheenAlpha =
            idle * (0.038f + 0.012f * std::sinf(sceneTime_ * 1.1f));
        DrawRect(sheenX, topY + barHeight * 0.18f, sheenWidth,
                 barHeight * 0.22f,
                 MakeColor(0.095f, 0.080f, 0.055f, sheenAlpha));
        DrawRect(screenWidth - sheenX - sheenWidth, bottomY + barHeight * 0.60f,
                 sheenWidth, barHeight * 0.22f,
                 MakeColor(0.095f, 0.080f, 0.055f, sheenAlpha));

        const float undertoneWidth = screenWidth * 0.42f;
        const float undertoneTravel =
            std::fmod(sceneTime_ * 0.062f + 0.31f, 1.0f);
        const float undertoneX =
            -undertoneWidth +
            (screenWidth + undertoneWidth * 2.0f) * undertoneTravel;
        DrawRect(undertoneX, topY + barHeight * 0.69f, undertoneWidth,
                 barHeight * 0.12f, MakeColor(0.0f, 0.0f, 0.0f, idle * 0.12f));
        DrawRect(screenWidth - undertoneX - undertoneWidth,
                 bottomY + barHeight * 0.19f, undertoneWidth, barHeight * 0.12f,
                 MakeColor(0.0f, 0.0f, 0.0f, idle * 0.12f));
    }

    const float edgeAlpha =
        std::clamp((1.0f - std::fabs(t - 0.62f) / 0.38f), 0.22f, 1.0f);
    const float lineBreath = 0.88f + 0.12f * std::sinf(sceneTime_ * 1.35f);
    const float lineYTop = topY + barHeight - 3.0f;
    const float lineYBottom = bottomY;
    DrawRect(0.0f, lineYTop, screenWidth, 2.0f,
             MakeColor(0.92f, 0.68f, 0.28f, 0.56f * edgeAlpha * lineBreath));
    DrawRect(0.0f, lineYBottom, screenWidth, 2.0f,
             MakeColor(0.92f, 0.68f, 0.28f, 0.56f * edgeAlpha * lineBreath));
    DrawRect(0.0f, lineYTop + 4.0f, screenWidth, 1.0f,
             MakeColor(1.0f, 0.92f, 0.60f, 0.24f * edgeAlpha * lineBreath));
    DrawRect(0.0f, lineYBottom - 4.0f, screenWidth, 1.0f,
             MakeColor(1.0f, 0.92f, 0.60f, 0.24f * edgeAlpha * lineBreath));
    if (idle > 0.0f) {
        const float innerLineAlpha =
            idle * edgeAlpha * (0.060f + 0.014f * std::sinf(sceneTime_ * 1.6f));
        DrawRect(0.0f, lineYTop - 7.0f, screenWidth, 1.0f,
                 MakeColor(0.88f, 0.62f, 0.24f, innerLineAlpha));
        DrawRect(0.0f, lineYBottom + 8.0f, screenWidth, 1.0f,
                 MakeColor(0.88f, 0.62f, 0.24f, innerLineAlpha));
    }

    if (t < 1.0f) {
        const float sweepWidth = screenWidth * 0.34f;
        const float sweepX =
            -sweepWidth + (screenWidth + sweepWidth * 2.0f) * t;
        const float sweepAlpha = std::sinf(t * kPi) * 0.84f;
        DrawRect(sweepX, lineYTop - 1.0f, sweepWidth, 4.0f,
                 MakeColor(1.0f, 0.86f, 0.42f, sweepAlpha));
        DrawRect(screenWidth - sweepX - sweepWidth, lineYBottom - 1.0f,
                 sweepWidth, 4.0f, MakeColor(1.0f, 0.86f, 0.42f, sweepAlpha));
        DrawRect(sweepX - sweepWidth * 0.38f, lineYTop + 5.0f,
                 sweepWidth * 0.62f, 1.0f,
                 MakeColor(1.0f, 0.96f, 0.72f, sweepAlpha * 0.54f));
        DrawRect(screenWidth - sweepX - sweepWidth * 0.24f, lineYBottom - 6.0f,
                 sweepWidth * 0.62f, 1.0f,
                 MakeColor(1.0f, 0.96f, 0.72f, sweepAlpha * 0.54f));
    }

    if (idle > 0.0f) {
        const float glintWidth = screenWidth * 0.18f;
        const float glintTravel = std::fmod(sceneTime_ * 0.18f, 1.0f);
        const float glintX =
            -glintWidth + (screenWidth + glintWidth * 2.0f) * glintTravel;
        const float glintAlpha =
            idle * edgeAlpha * (0.10f + 0.025f * std::sinf(sceneTime_ * 1.9f));
        DrawRect(glintX, lineYTop - 1.0f, glintWidth, 3.0f,
                 MakeColor(1.0f, 0.92f, 0.62f, glintAlpha));
        DrawRect(screenWidth - glintX - glintWidth, lineYBottom, glintWidth,
                 3.0f, MakeColor(1.0f, 0.92f, 0.62f, glintAlpha));
    }
}

void TitleScene::DrawRect(float x, float y, float w, float h,
                          const XMFLOAT4 &color) {
    Sprite sprite{};
    sprite.position = {x, y};
    sprite.size = {w, h};
    sprite.color = color;
    sprite.textureId = 0;
    ctx_->rendering.sprite->DrawSprite(sprite);
}

void TitleScene::DrawImage(const Image &image, float x, float y, float alpha,
                           float scale) {
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

bool TitleScene::IsAnyButtonTriggered(const Input &input) const {
    for (int key = 0; key < 256; ++key) {
        if (!IsTitleStartKey(key)) {
            continue;
        }
        if (input.IsKeyTrigger(key)) {
            return true;
        }
    }

    if (!input.IsGamepadConnected()) {
        return false;
    }
    constexpr std::array<WORD, 14> kGamepadButtons = {
        XINPUT_GAMEPAD_DPAD_UP,
        XINPUT_GAMEPAD_DPAD_DOWN,
        XINPUT_GAMEPAD_DPAD_LEFT,
        XINPUT_GAMEPAD_DPAD_RIGHT,
        XINPUT_GAMEPAD_START,
        XINPUT_GAMEPAD_BACK,
        XINPUT_GAMEPAD_LEFT_THUMB,
        XINPUT_GAMEPAD_RIGHT_THUMB,
        XINPUT_GAMEPAD_LEFT_SHOULDER,
        XINPUT_GAMEPAD_RIGHT_SHOULDER,
        XINPUT_GAMEPAD_A,
        XINPUT_GAMEPAD_B,
        XINPUT_GAMEPAD_X,
        XINPUT_GAMEPAD_Y,
    };
    return std::ranges::any_of(kGamepadButtons,
                               [&](WORD button) {
                                   return input.IsGamepadButtonTrigger(button);
                               }) ||
           input.IsGamepadLeftTriggerTrigger() ||
           input.IsGamepadRightTriggerTrigger();
}

void TitleScene::StartTitleBgm() {
    if (ctx_ == nullptr || ctx_->systems.sound == nullptr ||
        titleBgmVoiceHandle_ != SoundManager::kInvalidVoiceHandle) {
        return;
    }

    titleBgmSoundId_ = ctx_->systems.sound->LoadOrCreateSilent(
        L"app/resources/audio/bgm/bgm_TitleTheme.wav");
    titleBgmVoiceHandle_ = ctx_->systems.sound->Play(
        titleBgmSoundId_, 0.24f * AppSceneServices::GetBgmVolume(), true);
}

void TitleScene::StopTitleBgm() {
    if (ctx_ == nullptr || ctx_->systems.sound == nullptr ||
        titleBgmVoiceHandle_ == SoundManager::kInvalidVoiceHandle) {
        return;
    }

    ctx_->systems.sound->Stop(titleBgmVoiceHandle_);
    titleBgmVoiceHandle_ = SoundManager::kInvalidVoiceHandle;
}
