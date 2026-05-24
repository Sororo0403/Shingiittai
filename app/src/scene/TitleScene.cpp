#include "TitleScene.h"
#include "DirectXCommon.h"
#include "GameScene.h"
#include "Input.h"
#include "PostEffectRenderer.h"
#include "SceneManager.h"
#include "SpriteManager.h"
#include "TextureManager.h"
#include "WeaponSelectScene.h"
#include "WinApp.h"
#include <Xinput.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

using namespace DirectX;

namespace {
constexpr float kFadeDuration = 0.35f;

XMFLOAT4 MakeColor(float r, float g, float b, float a = 1.0f) {
    return {r, g, b, a};
}

float SmoothStep(float t) { return t * t * (3.0f - 2.0f * t); }
} // namespace

void TitleScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    sceneTime_ = 0.0f;
    fadeTimer_ = 0.0f;
    startRequested_ = false;

    if (ctx_->requestHandTrackingStart) {
        ctx_->requestHandTrackingStart();
    }

    ctx_->dxCommon->BeginUpload();
    logoImage_ = LoadTitleImage(L"app/resources/title/gamelogo.png");
    pressAnyButtonImage_ =
        LoadTitleImage(L"app/resources/title/press_any_button.png");
    ctx_->dxCommon->EndUpload();
    ctx_->texture->ReleaseUploadBuffers();

    demoScene_ = std::make_unique<GameScene>(GameScene::RunMode::TitleDemo);
    demoScene_->SetSceneManager(sceneManager_);
    demoScene_->Initialize(ctx);
}

void TitleScene::Update() {
    sceneTime_ += ctx_->deltaTime;

    if (demoScene_) {
        demoScene_->Update();
    }

    if (startRequested_) {
        fadeTimer_ += ctx_->deltaTime;
        if (fadeTimer_ >= kFadeDuration) {
            if (ctx_->postEffectRenderer != nullptr) {
                ctx_->postEffectRenderer->SetColorMode(
                    PostEffectRenderer::ColorMode::None);
                ctx_->postEffectRenderer->SetRadialBlurStrength(0.0f);
                ctx_->postEffectRenderer->SetSceneDimStrength(0.0f);
                ctx_->postEffectRenderer->SetVignettingShape(11.0f, 1.15f);
                ctx_->postEffectRenderer->SetVignettingStrength(0.20f);
            }
            sceneManager_->ChangeScene(std::make_unique<WeaponSelectScene>());
        }
        return;
    }

    if (IsAnyButtonTriggered(*ctx_->input)) {
        startRequested_ = true;
        fadeTimer_ = 0.0f;
    }
}

void TitleScene::Draw() {
    if (demoScene_) {
        demoScene_->Draw();
    }
}

void TitleScene::DrawOverlay() {
    const float w = static_cast<float>(ctx_->winApp->GetWidth());
    const float h = static_cast<float>(ctx_->winApp->GetHeight());

    ctx_->sprite->PreDraw();

    DrawRect(0.0f, 0.0f, w, h, MakeColor(0.0f, 0.0f, 0.0f, 0.42f));

    const float logoScale =
        std::clamp(w * 0.50f / logoImage_.width, 0.58f, 1.0f);
    const float logoX = (w - logoImage_.width * logoScale) * 0.5f;
    const float logoY = (h - logoImage_.height * logoScale) * 0.5f;
    DrawImage(logoImage_, logoX, logoY, 1.0f, logoScale);

    const float pressScale =
        std::clamp(w * 0.28f / pressAnyButtonImage_.width, 0.48f, 0.82f);
    const float pressX =
        (w - pressAnyButtonImage_.width * pressScale) * 0.5f;
    const float pressY = logoY + logoImage_.height * logoScale + 18.0f;
    const float pressAlpha =
        0.58f + 0.32f * (0.5f + 0.5f * std::sinf(sceneTime_ * 4.2f));
    DrawImage(pressAnyButtonImage_, pressX, pressY, pressAlpha, pressScale);

    if (startRequested_) {
        const float fadeT =
            std::clamp(fadeTimer_ / kFadeDuration, 0.0f, 1.0f);
        DrawRect(0.0f, 0.0f, w, h,
                 MakeColor(0.0f, 0.0f, 0.0f, SmoothStep(fadeT)));
    }

    ctx_->sprite->PostDraw();
}

TitleScene::Image TitleScene::LoadTitleImage(const std::wstring &path) {
    Image image{};
    image.textureId = ctx_->texture->Load(path);
    image.width = static_cast<float>(ctx_->texture->GetWidth(image.textureId));
    image.height =
        static_cast<float>(ctx_->texture->GetHeight(image.textureId));
    return image;
}

void TitleScene::DrawRect(float x, float y, float w, float h,
                          const XMFLOAT4 &color) {
    Sprite sprite{};
    sprite.position = {x, y};
    sprite.size = {w, h};
    sprite.color = color;
    sprite.textureId = 0;
    ctx_->sprite->DrawSprite(sprite);
}

void TitleScene::DrawImage(const Image &image, float x, float y, float alpha) {
    DrawImage(image, x, y, alpha, 1.0f);
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
    ctx_->sprite->DrawSprite(sprite);
}

bool TitleScene::IsAnyButtonTriggered(const Input &input) const {
    for (int dik = 0; dik < 256; ++dik) {
        if (input.IsKeyTrigger(dik)) {
            return true;
        }
    }

    if (!input.IsGamepadConnected()) {
        return false;
    }

    constexpr WORD kButtons[] = {
        XINPUT_GAMEPAD_DPAD_UP,        XINPUT_GAMEPAD_DPAD_DOWN,
        XINPUT_GAMEPAD_DPAD_LEFT,      XINPUT_GAMEPAD_DPAD_RIGHT,
        XINPUT_GAMEPAD_START,          XINPUT_GAMEPAD_BACK,
        XINPUT_GAMEPAD_LEFT_THUMB,     XINPUT_GAMEPAD_RIGHT_THUMB,
        XINPUT_GAMEPAD_LEFT_SHOULDER,  XINPUT_GAMEPAD_RIGHT_SHOULDER,
        XINPUT_GAMEPAD_A,              XINPUT_GAMEPAD_B,
        XINPUT_GAMEPAD_X,              XINPUT_GAMEPAD_Y,
    };

    for (WORD button : kButtons) {
        if (input.IsGamepadButtonTrigger(button)) {
            return true;
        }
    }

    return input.IsGamepadLeftTriggerTrigger() ||
           input.IsGamepadRightTriggerTrigger();
}
