#include "TitleScene.h"
#include "AppSceneServices.h"
#include "Input.h"
#include "PostProcessSystem.h"
#include "SceneManager.h"
#include "SoundManager.h"
#include "Sprite.h"
#include "SpriteManager.h"
#include "TextureManager.h"
#include "WeaponSelectScene.h"
#include "WinApp.h"
#include <Xinput.h>
#include <algorithm>
#include <cmath>
#include <memory>

using namespace DirectX;

namespace {
constexpr float kFadeDuration = 0.35f;
constexpr float kFrameIntroDuration = 1.12f;
constexpr float kPi = 3.14159265f;

XMFLOAT4 MakeColor(float r, float g, float b, float a = 1.0f) {
    return {r, g, b, a};
}

float SmoothStep(float t) { return t * t * (3.0f - 2.0f * t); }

float Smooth01(float t) {
    return SmoothStep(std::clamp(t, 0.0f, 1.0f));
}
} // namespace

TitleScene::~TitleScene() { StopTitleBgm(); }

void TitleScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    sceneTime_ = 0.0f;
    frameIntroTimer_ = 0.0f;
    fadeTimer_ = 0.0f;
    startRequested_ = false;
    titleBgmSoundId_ = 0;
    titleBgmVoice_ = SoundManager::kInvalidVoiceHandle;

    if (AppSceneServices::HasHandTrackingStart()) {
        AppSceneServices::RequestHandTrackingStart();
    }

    logoImage_ = LoadTitleImage(L"app/resources/ui/title/gamelogo.png");
    pressAnyButtonImage_ =
        LoadTitleImage(L"app/resources/ui/title/press_any_button.png");
    backgroundScene_ =
        std::make_unique<GameScene>(GameScene::Mode::TitleDemo);
    backgroundScene_->Initialize(ctx);

    if (ctx_->systems.sound != nullptr) {
        titleBgmSoundId_ = ctx_->systems.sound->LoadOrCreateSilent(
            L"app/resources/audio/bgm/maou_game_battle20.mp3");
        titleBgmVoice_ =
            ctx_->systems.sound->Play(titleBgmSoundId_, 0.54f, true);
    }
}

void TitleScene::Update() {
    sceneTime_ += ctx_->frame.deltaTime;
    frameIntroTimer_ =
        (std::min)(frameIntroTimer_ + ctx_->frame.deltaTime,
                   kFrameIntroDuration);
    if (backgroundScene_) {
        backgroundScene_->Update();
    }

    if (startRequested_) {
        fadeTimer_ += ctx_->frame.deltaTime;
        if (fadeTimer_ >= kFadeDuration) {
            if (ctx_->rendering.postProcessSystem != nullptr) {
                ctx_->rendering.postProcessSystem->SetProfile(
                    PostProcessProfile{});
            }
            StopTitleBgm();
            sceneManager_->ChangeScene(std::make_unique<WeaponSelectScene>());
        }
        UpdateTitleBgmVolume();
        return;
    }

    if (IsAnyButtonTriggered(*ctx_->systems.input)) {
        startRequested_ = true;
        fadeTimer_ = 0.0f;
    }
    UpdateTitleBgmVolume();
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

void TitleScene::StopTitleBgm() {
    if (ctx_ == nullptr || ctx_->systems.sound == nullptr ||
        titleBgmVoice_ == SoundManager::kInvalidVoiceHandle) {
        return;
    }
    ctx_->systems.sound->Stop(titleBgmVoice_);
    titleBgmVoice_ = SoundManager::kInvalidVoiceHandle;
}

void TitleScene::UpdateTitleBgmVolume() {
    if (ctx_ == nullptr || ctx_->systems.sound == nullptr ||
        titleBgmVoice_ == SoundManager::kInvalidVoiceHandle) {
        return;
    }
    const float fadeOut =
        startRequested_ ? 1.0f - Smooth01(fadeTimer_ / kFadeDuration) : 1.0f;
    ctx_->systems.sound->SetVoiceVolume(titleBgmVoice_, 0.54f * fadeOut);
}

void TitleScene::DrawTitleOverlay(float screenWidth, float screenHeight) {
    const float frameT =
        std::clamp(frameIntroTimer_ / kFrameIntroDuration, 0.0f, 1.0f);
    const float idle = Smooth01((frameT - 0.82f) / 0.18f);
    const float breath = idle * (0.5f + 0.5f * std::sinf(sceneTime_ * 0.9f));

    DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
             MakeColor(0.0f, 0.0f, 0.0f, 0.42f + 0.03f * breath));
    DrawStartupFrame(screenWidth, screenHeight);

    const float logoScale =
        std::clamp(screenWidth * 0.50f / logoImage_.width, 0.58f, 1.0f);
    const float logoX =
        (screenWidth - logoImage_.width * logoScale) * 0.5f;
    const float logoY =
        (screenHeight - logoImage_.height * logoScale) * 0.5f - 10.0f;
    DrawImage(logoImage_, logoX, logoY, Smooth01(frameT),
              logoScale *
                  (1.0f + idle * std::sinf(sceneTime_ * 0.82f) * 0.004f));

    const float pressScale =
        std::clamp(screenWidth * 0.28f / pressAnyButtonImage_.width, 0.48f,
                   0.82f);
    const float pressX =
        (screenWidth - pressAnyButtonImage_.width * pressScale) * 0.5f;
    const float pressY =
        logoY + logoImage_.height * logoScale + 26.0f +
        idle * std::sinf(sceneTime_ * 1.15f) * 1.4f;
    const float pressAlpha =
        idle * (0.48f + 0.18f * (0.5f + 0.5f * std::sinf(sceneTime_ * 3.0f)));
    DrawImage(pressAnyButtonImage_, pressX, pressY, pressAlpha, pressScale);

    if (startRequested_) {
        const float fadeT =
            std::clamp(fadeTimer_ / kFadeDuration, 0.0f, 1.0f);
        DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
                 MakeColor(0.0f, 0.0f, 0.0f, SmoothStep(fadeT)));
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
        DrawRect(screenWidth - sheenX - sheenWidth,
                 bottomY + barHeight * 0.60f, sheenWidth, barHeight * 0.22f,
                 MakeColor(0.095f, 0.080f, 0.055f, sheenAlpha));

        const float undertoneWidth = screenWidth * 0.42f;
        const float undertoneTravel =
            std::fmod(sceneTime_ * 0.062f + 0.31f, 1.0f);
        const float undertoneX =
            -undertoneWidth +
            (screenWidth + undertoneWidth * 2.0f) * undertoneTravel;
        DrawRect(undertoneX, topY + barHeight * 0.69f, undertoneWidth,
                 barHeight * 0.12f,
                 MakeColor(0.0f, 0.0f, 0.0f, idle * 0.12f));
        DrawRect(screenWidth - undertoneX - undertoneWidth,
                 bottomY + barHeight * 0.19f, undertoneWidth,
                 barHeight * 0.12f,
                 MakeColor(0.0f, 0.0f, 0.0f, idle * 0.12f));
    }

    const float edgeAlpha =
        std::clamp((1.0f - std::fabs(t - 0.62f) / 0.38f), 0.22f, 1.0f);
    const float lineBreath = 0.88f + 0.12f * std::sinf(sceneTime_ * 1.35f);
    const float lineYTop = topY + barHeight - 3.0f;
    const float lineYBottom = bottomY;
    DrawRect(0.0f, lineYTop, screenWidth, 2.0f,
             MakeColor(0.92f, 0.68f, 0.28f,
                       0.56f * edgeAlpha * lineBreath));
    DrawRect(0.0f, lineYBottom, screenWidth, 2.0f,
             MakeColor(0.92f, 0.68f, 0.28f,
                       0.56f * edgeAlpha * lineBreath));
    DrawRect(0.0f, lineYTop + 4.0f, screenWidth, 1.0f,
             MakeColor(1.0f, 0.92f, 0.60f,
                       0.24f * edgeAlpha * lineBreath));
    DrawRect(0.0f, lineYBottom - 4.0f, screenWidth, 1.0f,
             MakeColor(1.0f, 0.92f, 0.60f,
                       0.24f * edgeAlpha * lineBreath));
    if (idle > 0.0f) {
        const float innerLineAlpha =
            idle * edgeAlpha *
            (0.060f + 0.014f * std::sinf(sceneTime_ * 1.6f));
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
                 sweepWidth, 4.0f,
                 MakeColor(1.0f, 0.86f, 0.42f, sweepAlpha));
        DrawRect(sweepX - sweepWidth * 0.38f, lineYTop + 5.0f,
                 sweepWidth * 0.62f, 1.0f,
                 MakeColor(1.0f, 0.96f, 0.72f, sweepAlpha * 0.54f));
        DrawRect(screenWidth - sweepX - sweepWidth * 0.24f,
                 lineYBottom - 6.0f, sweepWidth * 0.62f, 1.0f,
                 MakeColor(1.0f, 0.96f, 0.72f, sweepAlpha * 0.54f));
    }

    if (idle > 0.0f) {
        const float glintWidth = screenWidth * 0.18f;
        const float glintTravel = std::fmod(sceneTime_ * 0.18f, 1.0f);
        const float glintX =
            -glintWidth + (screenWidth + glintWidth * 2.0f) * glintTravel;
        const float glintAlpha =
            idle * edgeAlpha *
            (0.10f + 0.025f * std::sinf(sceneTime_ * 1.9f));
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
    const bool gamepadTriggered =
        input.IsGamepadConnected() &&
        (input.IsGamepadButtonTrigger(XINPUT_GAMEPAD_A) ||
         input.IsGamepadButtonTrigger(XINPUT_GAMEPAD_START));
    return input.IsKeyTrigger(DIK_RETURN) || input.IsKeyTrigger(DIK_SPACE) ||
           input.IsKeyTrigger(DIK_ESCAPE) || input.IsMouseTrigger(0) ||
           gamepadTriggered;
}
