#include "SettingsScene.h"
#include "DirectXCommon.h"
#include "Input.h"
#include "ModeSelectScene.h"
#include "PostEffectRenderer.h"
#include "SceneManager.h"
#include "SoundManager.h"
#include "SpriteManager.h"
#include "TextureManager.h"
#include "WinApp.h"
#include <Xinput.h>
#include <algorithm>
#include <cmath>
#include <memory>

using namespace DirectX;

namespace {
XMFLOAT4 MakeColor(float r, float g, float b, float a = 1.0f) {
    return {r, g, b, a};
}
} // namespace

void SettingsScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    selectedIndex_ = 0;
    volume_ = ctx_->sound ? ctx_->sound->GetMasterVolume() : 1.0f;
    sceneTime_ = 0.0f;

    ctx_->dxCommon->BeginUpload();
    titleImage_ = LoadSettingsImage(L"app/resources/menu/settings_title.png");
    volumeImage_ = LoadSettingsImage(L"app/resources/menu/volume.png");
    backImage_ = LoadSettingsImage(L"app/resources/menu/back.png");
    controlsImage_ =
        LoadSettingsImage(L"app/resources/menu/settings_controls.png");
    ctx_->dxCommon->EndUpload();
    ctx_->texture->ReleaseUploadBuffers();

    ctx_->postEffectRenderer->SetVignettingStrength(0.20f);
    ctx_->postEffectRenderer->SetVignettingEnabled(true);
}

void SettingsScene::Update() {
    sceneTime_ += ctx_->deltaTime;

    const float w = static_cast<float>(ctx_->winApp->GetWidth());
    const float h = static_cast<float>(ctx_->winApp->GetHeight());
    Layout(w, h);
    UpdateInput(ctx_->input);
}

void SettingsScene::Draw() {
    const float w = static_cast<float>(ctx_->winApp->GetWidth());
    const float h = static_cast<float>(ctx_->winApp->GetHeight());

    ctx_->sprite->PreDraw();
    DrawBackground(w, h);
    DrawImage(titleImage_, (w - titleImage_.width) * 0.5f, 84.0f);
    DrawVolumeControl();
    DrawBackButton();
    DrawImage(controlsImage_, (w - controlsImage_.width) * 0.5f, h - 74.0f,
              1.0f, 0.76f);
    ctx_->sprite->PostDraw();
}

void SettingsScene::DrawOverlay() {}

SettingsScene::Image SettingsScene::LoadSettingsImage(const std::wstring &path) {
    Image image{};
    image.textureId = ctx_->texture->Load(path);
    image.width = static_cast<float>(ctx_->texture->GetWidth(image.textureId));
    image.height =
        static_cast<float>(ctx_->texture->GetHeight(image.textureId));
    return image;
}

void SettingsScene::UpdateInput(Input *input) {
    if (IsMouseOver(volumeRect_)) {
        selectedIndex_ = 0;
    } else if (IsMouseOver(backRect_)) {
        selectedIndex_ = 1;
    }

    if (input->IsKeyTrigger(DIK_UP) || input->IsKeyTrigger(DIK_W) ||
        input->IsKeyTrigger(DIK_DOWN) || input->IsKeyTrigger(DIK_S)) {
        selectedIndex_ = 1 - selectedIndex_;
    }

    if (input->IsGamepadConnected()) {
        if (input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_DPAD_UP) ||
            input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_DPAD_DOWN)) {
            selectedIndex_ = 1 - selectedIndex_;
        }
    }

    const bool decrease =
        input->IsKeyTrigger(DIK_LEFT) || input->IsKeyTrigger(DIK_A) ||
        (input->IsGamepadConnected() &&
         input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_DPAD_LEFT));
    const bool increase =
        input->IsKeyTrigger(DIK_RIGHT) || input->IsKeyTrigger(DIK_D) ||
        (input->IsGamepadConnected() &&
         input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_DPAD_RIGHT));

    if (selectedIndex_ == 0) {
        if (decrease) {
            SetVolume(volume_ - 0.05f);
        }
        if (increase) {
            SetVolume(volume_ + 0.05f);
        }
    }

    if ((input->IsMousePress(0) || input->IsMouseTrigger(0)) &&
        IsMouseOver(volumeRect_)) {
        const float t =
            (MouseX() - volumeRect_.x) / (std::max)(volumeRect_.w, 1.0f);
        SetVolume(t);
    }

    if (input->IsMouseTrigger(0) && IsMouseOver(backRect_)) {
        ReturnToModeSelect();
        return;
    }

    if ((selectedIndex_ == 1 &&
         (input->IsKeyTrigger(DIK_RETURN) ||
          input->IsKeyTrigger(DIK_SPACE) ||
          (input->IsGamepadConnected() &&
           input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_A)))) ||
        input->IsKeyTrigger(DIK_ESCAPE) ||
        (input->IsGamepadConnected() &&
         input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_B))) {
        ReturnToModeSelect();
    }
}

void SettingsScene::SetVolume(float volume) {
    volume_ = std::clamp(volume, 0.0f, 1.0f);
    if (ctx_->sound) {
        ctx_->sound->SetMasterVolume(volume_);
    }
}

void SettingsScene::ReturnToModeSelect() {
    sceneManager_->ChangeScene(std::make_unique<ModeSelectScene>());
}

void SettingsScene::Layout(float screenWidth, float screenHeight) {
    const float panelW = (std::min)(680.0f, screenWidth - 140.0f);
    volumeRect_ = {(screenWidth - panelW) * 0.5f, 292.0f, panelW, 82.0f};
    backRect_ = {(screenWidth - 320.0f) * 0.5f,
                 (std::min)(screenHeight - 168.0f, 474.0f), 320.0f, 82.0f};
}

bool SettingsScene::IsMouseOver(const Rect &rect) const {
    POINT cursor{};
    if (!GetCursorPos(&cursor)) {
        return false;
    }
    if (!ScreenToClient(ctx_->winApp->GetHwnd(), &cursor)) {
        return false;
    }

    const float x = static_cast<float>(cursor.x);
    const float y = static_cast<float>(cursor.y);
    return x >= rect.x && x <= rect.x + rect.w && y >= rect.y &&
           y <= rect.y + rect.h;
}

float SettingsScene::MouseX() const {
    POINT cursor{};
    if (!GetCursorPos(&cursor) ||
        !ScreenToClient(ctx_->winApp->GetHwnd(), &cursor)) {
        return volumeRect_.x;
    }
    return static_cast<float>(cursor.x);
}

void SettingsScene::DrawBackground(float screenWidth, float screenHeight) {
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
             MakeColor(0.985f, 0.988f, 0.992f, 1.0f));
    DrawRect(0.0f, 0.0f, screenWidth, 78.0f,
             MakeColor(0.04f, 0.04f, 0.05f, 1.0f));
    DrawRect(0.0f, screenHeight - 42.0f, screenWidth, 42.0f,
             MakeColor(0.04f, 0.04f, 0.05f, 1.0f));
    DrawRect(0.0f, 78.0f, screenWidth, 8.0f,
             MakeColor(1.0f, 0.82f, 0.00f, 1.0f));
    DrawRect(0.0f, screenHeight - 50.0f, screenWidth, 8.0f,
             MakeColor(0.02f, 0.34f, 0.86f, 0.95f));
}

void SettingsScene::DrawVolumeControl() {
    const bool selected = selectedIndex_ == 0;
    const float pulse = 0.5f + 0.5f * std::sinf(sceneTime_ * 7.0f);

    DrawRect(volumeRect_.x, volumeRect_.y, volumeRect_.w, volumeRect_.h,
             selected ? MakeColor(0.06f, 0.06f, 0.07f, 0.96f)
                      : MakeColor(1.0f, 1.0f, 1.0f, 0.82f));
    DrawRect(volumeRect_.x, volumeRect_.y, volumeRect_.w, 6.0f,
             selected ? MakeColor(1.0f, 0.82f, 0.00f, 1.0f)
                      : MakeColor(0.06f, 0.06f, 0.07f, 0.24f));

    if (selected) {
        DrawRect(volumeRect_.x - 10.0f, volumeRect_.y - 10.0f, 10.0f,
                 volumeRect_.h + 20.0f,
                 MakeColor(1.0f, 0.82f, 0.00f, 0.70f + pulse * 0.18f));
    }

    DrawImage(volumeImage_, volumeRect_.x + 34.0f,
              volumeRect_.y + (volumeRect_.h - volumeImage_.height) * 0.5f,
              1.0f, selected ? 1.0f : 0.82f);

    const float sliderX = volumeRect_.x + 228.0f;
    const float sliderY = volumeRect_.y + 37.0f;
    const float sliderW = volumeRect_.w - 278.0f;
    DrawRect(sliderX, sliderY, sliderW, 12.0f,
             MakeColor(0.20f, 0.21f, 0.23f, selected ? 0.90f : 0.60f));
    DrawRect(sliderX, sliderY, sliderW * volume_, 12.0f,
             MakeColor(0.02f, 0.34f, 0.86f, 1.0f));
    DrawRect(sliderX + sliderW * volume_ - 9.0f, sliderY - 13.0f, 18.0f,
             38.0f, MakeColor(1.0f, 0.82f, 0.00f, 1.0f));
}

void SettingsScene::DrawBackButton() {
    const bool selected = selectedIndex_ == 1;
    DrawRect(backRect_.x, backRect_.y, backRect_.w, backRect_.h,
             selected ? MakeColor(0.06f, 0.06f, 0.07f, 0.96f)
                      : MakeColor(1.0f, 1.0f, 1.0f, 0.80f));
    DrawRect(backRect_.x, backRect_.y, backRect_.w, 6.0f,
             selected ? MakeColor(1.0f, 0.82f, 0.00f, 1.0f)
                      : MakeColor(0.06f, 0.06f, 0.07f, 0.24f));
    DrawRect(backRect_.x, backRect_.y + backRect_.h - 6.0f, backRect_.w, 6.0f,
             selected ? MakeColor(0.92f, 0.02f, 0.02f, 1.0f)
                      : MakeColor(0.06f, 0.06f, 0.07f, 0.18f));

    DrawImage(backImage_, backRect_.x + (backRect_.w - backImage_.width) * 0.5f,
              backRect_.y + (backRect_.h - backImage_.height) * 0.5f, 1.0f,
              selected ? 1.0f : 0.82f);
}

void SettingsScene::DrawRect(float x, float y, float w, float h,
                             const XMFLOAT4 &color) {
    Sprite sprite{};
    sprite.position = {x, y};
    sprite.size = {w, h};
    sprite.color = color;
    sprite.textureId = 0;
    ctx_->sprite->DrawSprite(sprite);
}

void SettingsScene::DrawImage(const Image &image, float x, float y,
                              float scale, float alpha) {
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
