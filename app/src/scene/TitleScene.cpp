#include "TitleScene.h"
#include "DirectXCommon.h"
#include "GameScene.h"
#include "HandRegistrationScene.h"
#include "Input.h"
#include "PostEffectRenderer.h"
#include "SceneManager.h"
#include "SpriteManager.h"
#include "TextureManager.h"
#include "WinApp.h"
#include "WeaponSelectScene.h"
#include <Xinput.h>
#include <algorithm>
#include <cmath>
#include <memory>

using namespace DirectX;

namespace {
constexpr float kFadeDuration = 0.35f;

XMFLOAT4 MakeColor(float r, float g, float b, float a = 1.0f) {
    return {r, g, b, a};
}

float SmoothStep(float t) { return t * t * (3.0f - 2.0f * t); }
} // namespace

TitleScene::~TitleScene() = default;

void TitleScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    sceneTime_ = 0.0f;
    fadeTimer_ = 0.0f;
    startRequested_ = false;
    cameraStartRequested_ = false;
    waitingForCameraReady_ = false;

    ctx_->dxCommon->BeginUpload();
    logoImage_ = LoadTitleImage(L"app/resources/title/gamelogo.png");
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

    handWarmupController_.Update(ctx_->deltaTime);

    if (waitingForCameraReady_ && IsCameraReady()) {
        startRequested_ = true;
        waitingForCameraReady_ = false;
        fadeTimer_ = 0.0f;
    }

    if (startRequested_) {
        fadeTimer_ += ctx_->deltaTime;
        if (fadeTimer_ >= kFadeDuration) {
            if (cameraStartRequested_) {
                sceneManager_->ChangeScene(
                    std::make_unique<HandRegistrationScene>());
            } else {
                sceneManager_->ChangeScene(
                    std::make_unique<WeaponSelectScene>());
            }
        }
        return;
    }

    if (IsCameraShortcutTriggered(*ctx_->input)) {
        cameraStartRequested_ = true;
        if (ctx_->requestHandTrackingStart) {
            ctx_->requestHandTrackingStart();
        }
        return;
    }

    if (IsAnyButtonTriggered(*ctx_->input)) {
        if (cameraStartRequested_ && !IsCameraReady()) {
            waitingForCameraReady_ = true;
            return;
        }
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

    const float logoScale = std::clamp(w * 0.50f / logoImage_.width,
                                       0.58f, 1.0f);
    const float logoX = (w - logoImage_.width * logoScale) * 0.5f;
    const float logoY = (h - logoImage_.height * logoScale) * 0.5f;
    DrawImage(logoImage_, logoX, logoY, 1.0f, logoScale);

    DrawCameraModeBadge(w, h);

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

void TitleScene::DrawCameraModeBadge(float, float) {
    if (!cameraStartRequested_) {
        return;
    }

    const bool ready = IsCameraReady();
    const float x = 18.0f;
    const float y = 18.0f;
    const float pulse = 0.55f + 0.45f * std::sinf(sceneTime_ * 5.4f);
    DrawRect(x, y, 94.0f, 42.0f, MakeColor(0.02f, 0.025f, 0.035f, 0.78f));
    DrawRect(x, y + 39.0f, 78.0f, 3.0f,
             MakeColor(0.10f, 0.58f, 1.0f, 0.92f));
    DrawRect(x + 15.0f, y + 15.0f, 31.0f, 19.0f,
             MakeColor(0.86f, 0.92f, 1.0f, 0.92f));
    DrawRect(x + 21.0f, y + 9.0f, 14.0f, 7.0f,
             MakeColor(0.86f, 0.92f, 1.0f, 0.92f));
    DrawRect(x + 24.0f, y + 19.0f, 13.0f, 10.0f,
             MakeColor(0.08f, 0.12f, 0.18f, 0.92f));
    DrawRect(x + 49.0f, y + 19.0f, 13.0f, 10.0f,
             MakeColor(0.86f, 0.92f, 1.0f, 0.92f));
    DrawRect(x + 66.0f, y + 10.0f, 8.0f, 8.0f,
             ready ? MakeColor(0.20f, 1.0f, 0.42f, 0.92f)
                   : MakeColor(1.0f, 0.12f, 0.08f, 0.55f + pulse * 0.45f));
    DrawRect(x + 66.0f, y + 26.0f, ready ? 18.0f : 8.0f + pulse * 10.0f,
             4.0f,
             ready ? MakeColor(0.20f, 1.0f, 0.42f, 0.88f)
                   : MakeColor(0.10f, 0.58f, 1.0f, 0.60f));
    if (waitingForCameraReady_) {
        DrawRect(x, y + 46.0f, 94.0f, 4.0f,
                 MakeColor(0.08f, 0.10f, 0.13f, 0.84f));
        DrawRect(x, y + 46.0f, 28.0f + pulse * 46.0f, 4.0f,
                 MakeColor(1.0f, 0.82f, 0.18f, 0.92f));
    }
}

bool TitleScene::IsAnyButtonTriggered(const Input &input) const {
    for (int dik = 0; dik < 256; ++dik) {
        if (dik == DIK_F1 || dik == DIK_C || dik == DIK_R) {
            continue;
        }
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

bool TitleScene::IsCameraShortcutTriggered(const Input &input) const {
    return input.IsKeyTrigger(DIK_F1);
}

bool TitleScene::IsCameraReady() const {
    return handWarmupController_.HasRecentPacket();
}
