#include "SettingsScene.h"
#include "DirectXCommon.h"
#include "Input.h"
#include "ModeSelectScene.h"
#include "PostProcessSystem.h"
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
    volume_ = ctx_->systems.sound ? ctx_->systems.sound->GetMasterVolume() : 1.0f;
    sceneTime_ = 0.0f;

    titleImage_ = LoadSettingsImage(L"app/resources/menu/settings_title.png");
    volumeImage_ = LoadSettingsImage(L"app/resources/menu/volume.png");
    backImage_ = LoadSettingsImage(L"app/resources/menu/back.png");
    controlsImage_ =
        LoadSettingsImage(L"app/resources/menu/settings_controls.png");

    PostProcessProfile postProfile{};
    postProfile.vignette.enabled = true;
    postProfile.vignette.strength = 0.20f;
    ctx_->rendering.postProcessSystem->SetProfile(postProfile);
}

void SettingsScene::Update() {
    sceneTime_ += ctx_->frame.deltaTime;

    const float w = static_cast<float>(ctx_->systems.winApp->GetWidth());
    const float h = static_cast<float>(ctx_->systems.winApp->GetHeight());
    Layout(w, h);
    UpdateInput(ctx_->systems.input);
}

void SettingsScene::Draw() {
    const float w = static_cast<float>(ctx_->systems.winApp->GetWidth());
    const float h = static_cast<float>(ctx_->systems.winApp->GetHeight());

    ctx_->rendering.sprite->PreDraw();
    DrawBackground(w, h);
    DrawImage(titleImage_, (w - titleImage_.width) * 0.5f, 84.0f);
    DrawVolumeControl();
    DrawBackButton();
    const float helpH = (std::max)(58.0f, h * 0.085f);
    DrawImage(controlsImage_, (w - controlsImage_.width) * 0.5f,
              h - helpH + (helpH - controlsImage_.height) * 0.5f, 1.0f,
              1.0f);
    ctx_->rendering.sprite->PostDraw();
}

void SettingsScene::DrawTransparent() {}

SettingsScene::Image SettingsScene::LoadSettingsImage(const std::wstring &path) {
    Image image{};
    image.textureId = ctx_->rendering.texture->Load(path);
    image.width = static_cast<float>(ctx_->rendering.texture->GetWidth(image.textureId));
    image.height =
        static_cast<float>(ctx_->rendering.texture->GetHeight(image.textureId));
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
        input->IsKeyTrigger(DIK_TAB) ||
        (input->IsGamepadConnected() &&
         input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_B))) {
        ReturnToModeSelect();
    }
}

void SettingsScene::SetVolume(float volume) {
    volume_ = std::clamp(volume, 0.0f, 1.0f);
    if (ctx_->systems.sound) {
        ctx_->systems.sound->SetMasterVolume(volume_);
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
    if (!ScreenToClient(ctx_->systems.winApp->GetHwnd(), &cursor)) {
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
        !ScreenToClient(ctx_->systems.winApp->GetHwnd(), &cursor)) {
        return volumeRect_.x;
    }
    return static_cast<float>(cursor.x);
}

void SettingsScene::DrawBackground(float screenWidth, float screenHeight) {
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
             MakeColor(0.025f, 0.028f, 0.034f, 1.0f));
    DrawRect(screenWidth * 0.18f, screenHeight * 0.25f, screenWidth * 0.32f,
             2.0f, MakeColor(1.0f, 0.82f, 0.02f, 0.22f));
    DrawRect(screenWidth * 0.56f, screenHeight * 0.68f, screenWidth * 0.20f,
             2.0f, MakeColor(1.0f, 0.82f, 0.02f, 0.16f));
    const float helpH = (std::max)(58.0f, screenHeight * 0.085f);
    DrawRect(0.0f, screenHeight - helpH, screenWidth, helpH,
             MakeColor(0.0f, 0.0f, 0.0f, 1.0f));
}

void SettingsScene::DrawVolumeControl() {
    const bool selected = selectedIndex_ == 0;
    DrawRect(volumeRect_.x, volumeRect_.y, volumeRect_.w, volumeRect_.h,
             MakeColor(0.12f, 0.13f, 0.15f, selected ? 0.94f : 0.72f));
    DrawRect(volumeRect_.x, volumeRect_.y + volumeRect_.h - 2.0f,
             volumeRect_.w, 2.0f,
             MakeColor(1.0f, 0.82f, 0.02f, selected ? 0.86f : 0.28f));
    DrawRect(volumeRect_.x + volumeRect_.w - 2.0f, volumeRect_.y, 2.0f,
             volumeRect_.h,
             MakeColor(1.0f, 0.82f, 0.02f, selected ? 0.66f : 0.18f));

    if (selected) {
        const float frame = 4.0f;
        DrawRect(volumeRect_.x - frame, volumeRect_.y - frame,
                 volumeRect_.w + frame * 2.0f, frame,
                 MakeColor(0.0f, 0.0f, 0.0f, 1.0f));
        DrawRect(volumeRect_.x - frame, volumeRect_.y + volumeRect_.h,
                 volumeRect_.w + frame * 2.0f, frame,
                 MakeColor(0.0f, 0.0f, 0.0f, 1.0f));
        DrawRect(volumeRect_.x - frame, volumeRect_.y - frame, frame,
                 volumeRect_.h + frame * 2.0f,
                 MakeColor(0.0f, 0.0f, 0.0f, 1.0f));
        DrawRect(volumeRect_.x + volumeRect_.w, volumeRect_.y - frame, frame,
                 volumeRect_.h + frame * 2.0f,
                 MakeColor(0.0f, 0.0f, 0.0f, 1.0f));
    }

    DrawImage(volumeImage_, volumeRect_.x + 34.0f,
              volumeRect_.y + (volumeRect_.h - volumeImage_.height) * 0.5f,
              1.0f, selected ? 1.0f : 0.48f);

    const float sliderX = volumeRect_.x + 228.0f;
    const float sliderY = volumeRect_.y + 37.0f;
    const float sliderW = volumeRect_.w - 278.0f;
    DrawRect(sliderX, sliderY, sliderW, 12.0f,
             MakeColor(0.0f, 0.0f, 0.0f, selected ? 0.22f : 0.12f));
    DrawRect(sliderX, sliderY, sliderW * volume_, 12.0f,
             MakeColor(0.0f, 0.0f, 0.0f, selected ? 0.94f : 0.40f));
    DrawRect(sliderX + sliderW * volume_ - 9.0f, sliderY - 13.0f, 18.0f,
             38.0f, MakeColor(0.0f, 0.0f, 0.0f, selected ? 1.0f : 0.45f));
}

void SettingsScene::DrawBackButton() {
    const bool selected = selectedIndex_ == 1;
    DrawRect(backRect_.x, backRect_.y, backRect_.w, backRect_.h,
             MakeColor(0.12f, 0.13f, 0.15f, selected ? 0.94f : 0.72f));
    DrawRect(backRect_.x, backRect_.y + backRect_.h - 2.0f, backRect_.w, 2.0f,
             MakeColor(1.0f, 0.82f, 0.02f, selected ? 0.86f : 0.28f));
    DrawRect(backRect_.x + backRect_.w - 2.0f, backRect_.y, 2.0f,
             backRect_.h,
             MakeColor(1.0f, 0.82f, 0.02f, selected ? 0.66f : 0.18f));

    if (selected) {
        const float frame = 4.0f;
        DrawRect(backRect_.x - frame, backRect_.y - frame,
                 backRect_.w + frame * 2.0f, frame,
                 MakeColor(0.0f, 0.0f, 0.0f, 1.0f));
        DrawRect(backRect_.x - frame, backRect_.y + backRect_.h,
                 backRect_.w + frame * 2.0f, frame,
                 MakeColor(0.0f, 0.0f, 0.0f, 1.0f));
        DrawRect(backRect_.x - frame, backRect_.y - frame, frame,
                 backRect_.h + frame * 2.0f,
                 MakeColor(0.0f, 0.0f, 0.0f, 1.0f));
        DrawRect(backRect_.x + backRect_.w, backRect_.y - frame, frame,
                 backRect_.h + frame * 2.0f,
                 MakeColor(0.0f, 0.0f, 0.0f, 1.0f));
    }

    DrawImage(backImage_, backRect_.x + (backRect_.w - backImage_.width) * 0.5f,
              backRect_.y + (backRect_.h - backImage_.height) * 0.5f, 1.0f,
              selected ? 1.0f : 0.48f);
}

void SettingsScene::DrawRect(float x, float y, float w, float h,
                             const XMFLOAT4 &color) {
    Sprite sprite{};
    sprite.position = {x, y};
    sprite.size = {w, h};
    sprite.color = color;
    sprite.textureId = 0;
    ctx_->rendering.sprite->DrawSprite(sprite);
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
    ctx_->rendering.sprite->DrawSprite(sprite);
}
