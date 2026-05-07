#include "TitleScene.h"
#include "DirectXCommon.h"
#include "Input.h"
#include "ModeSelectScene.h"
#include "PostEffectRenderer.h"
#include "SceneManager.h"
#include "SpriteManager.h"
#include "TextureManager.h"
#include "WinApp.h"
#include <Xinput.h>
#include <algorithm>
#include <cmath>
#include <memory>

using namespace DirectX;

namespace {
constexpr float kFadeDuration = 0.2f;

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

    ctx_->dxCommon->BeginUpload();
    logoImage_ = LoadTitleImage(L"app/resources/title/title_logo.png");
    pressAnyButtonImage_ =
        LoadTitleImage(L"app/resources/title/press_any_button.png");
    ctx_->dxCommon->EndUpload();
    ctx_->texture->ReleaseUploadBuffers();

    ctx_->postEffectRenderer->SetVignettingStrength(0.28f);
    ctx_->postEffectRenderer->SetVignettingEnabled(true);
}

void TitleScene::Update() {
    sceneTime_ += ctx_->deltaTime;

    if (startRequested_) {
        fadeTimer_ += ctx_->deltaTime;
        if (fadeTimer_ >= kFadeDuration) {
            sceneManager_->ChangeScene(std::make_unique<ModeSelectScene>());
        }
        return;
    }

    if (IsAnyButtonTriggered(*ctx_->input)) {
        startRequested_ = true;
        fadeTimer_ = 0.0f;
    }
}

void TitleScene::Draw() {
    const float w = static_cast<float>(ctx_->winApp->GetWidth());
    const float h = static_cast<float>(ctx_->winApp->GetHeight());
    const float letterboxH = (std::max)(42.0f, h * 0.085f);

    ctx_->sprite->PreDraw();

    DrawRect(0.0f, 0.0f, w, h, MakeColor(1.0f, 1.0f, 1.0f, 1.0f));
    DrawRect(0.0f, 0.0f, w, letterboxH, MakeColor(0.0f, 0.0f, 0.0f, 1.0f));
    DrawRect(0.0f, h - letterboxH, w, letterboxH,
             MakeColor(0.0f, 0.0f, 0.0f, 1.0f));

    const float logoX = (w - logoImage_.width) * 0.5f;
    const float logoY = (h - logoImage_.height) * 0.5f - 26.0f;
    DrawImage(logoImage_, logoX, logoY);

    const float blink = startRequested_
                            ? 1.0f
                            : 0.74f + 0.26f * std::sinf(sceneTime_ * 4.6f);
    const float promptX = (w - pressAnyButtonImage_.width) * 0.5f;
    const float promptY =
        (std::min)(h - letterboxH - pressAnyButtonImage_.height - 36.0f,
                   logoY + logoImage_.height + 54.0f);
    DrawImage(pressAnyButtonImage_, promptX, promptY, blink);

    if (startRequested_) {
        const float fadeT =
            std::clamp(fadeTimer_ / kFadeDuration, 0.0f, 1.0f);
        DrawRect(0.0f, 0.0f, w, h,
                 MakeColor(0.0f, 0.0f, 0.0f, SmoothStep(fadeT)));
    }

    ctx_->sprite->PostDraw();
}

void TitleScene::DrawOverlay() {}

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
    if (image.width <= 0.0f || image.height <= 0.0f) {
        return;
    }

    Sprite sprite{};
    sprite.position = {x, y};
    sprite.size = {image.width, image.height};
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
