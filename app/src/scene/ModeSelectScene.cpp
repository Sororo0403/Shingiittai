#include "ModeSelectScene.h"
#include "DirectXCommon.h"
#include "GameScene.h"
#include "Input.h"
#include "PostEffectRenderer.h"
#include "SceneManager.h"
#include "SettingsScene.h"
#include "SpriteManager.h"
#include "TextureManager.h"
#include "TitleScene.h"
#include "WeaponSelectScene.h"
#include "WinApp.h"
#include <Xinput.h>
#include <algorithm>
#include <cmath>
#include <memory>

using namespace DirectX;

namespace {
constexpr float kTransitionDuration = 0.18f;

XMFLOAT4 MakeColor(float r, float g, float b, float a = 1.0f) {
    return {r, g, b, a};
}

float SmoothStep(float t) { return t * t * (3.0f - 2.0f * t); }
} // namespace

void ModeSelectScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    selectedIndex_ = 0;
    sceneTime_ = 0.0f;
    transitionTimer_ = 0.0f;
    nextScene_ = NextScene::None;

    ctx_->rendering.dxCommon->BeginUpload();
    buttonImages_[0] = LoadMenuImage(L"app/resources/menu/game_start.png");
    buttonImages_[1] = LoadMenuImage(L"app/resources/menu/settings.png");
    helpImages_[0] = LoadMenuImage(L"app/resources/menu/game_start_help.png");
    helpImages_[1] = LoadMenuImage(L"app/resources/menu/settings_help.png");
    ctx_->rendering.dxCommon->EndUpload();
    ctx_->rendering.texture->ReleaseUploadBuffers();

    ctx_->rendering.postEffectRenderer->SetVignettingStrength(0.22f);
    ctx_->rendering.postEffectRenderer->SetVignettingEnabled(true);
}

void ModeSelectScene::Update() {
    sceneTime_ += ctx_->frame.deltaTime;

    const float w = static_cast<float>(ctx_->systems.winApp->GetWidth());
    const float h = static_cast<float>(ctx_->systems.winApp->GetHeight());
    LayoutButtons(w, h);

    if (nextScene_ != NextScene::None) {
        transitionTimer_ += ctx_->frame.deltaTime;
        if (transitionTimer_ >= kTransitionDuration) {
            if (nextScene_ == NextScene::Game) {
                sceneManager_->ChangeScene(
                    std::make_unique<WeaponSelectScene>());
            } else if (nextScene_ == NextScene::Settings) {
                sceneManager_->ChangeScene(std::make_unique<SettingsScene>());
            }
        }
        return;
    }

    UpdateSelection(ctx_->systems.input);
}

void ModeSelectScene::Draw() {
    const float w = static_cast<float>(ctx_->systems.winApp->GetWidth());
    const float h = static_cast<float>(ctx_->systems.winApp->GetHeight());

    ctx_->rendering.sprite->PreDraw();
    DrawBackground(w, h);

    for (int i = 0; i < kButtonCount; ++i) {
        DrawButton(buttons_[i], buttonImages_[i], i == selectedIndex_);
    }

    DrawHelp(w, h);

    if (nextScene_ != NextScene::None) {
        const float fadeT =
            std::clamp(transitionTimer_ / kTransitionDuration, 0.0f, 1.0f);
        DrawRect(0.0f, 0.0f, w, h,
                 MakeColor(0.0f, 0.0f, 0.0f, SmoothStep(fadeT)));
    }

    ctx_->rendering.sprite->PostDraw();
}

void ModeSelectScene::DrawTransparent() {}

ModeSelectScene::Image
ModeSelectScene::LoadMenuImage(const std::wstring &path) {
    Image image{};
    image.textureId = ctx_->rendering.texture->Load(path);
    image.width = static_cast<float>(ctx_->rendering.texture->GetWidth(image.textureId));
    image.height =
        static_cast<float>(ctx_->rendering.texture->GetHeight(image.textureId));
    return image;
}

void ModeSelectScene::UpdateSelection(Input *input) {
    if (input->IsKeyTrigger(DIK_UP) || input->IsKeyTrigger(DIK_LEFT) ||
        input->IsKeyTrigger(DIK_W) || input->IsKeyTrigger(DIK_A)) {
        selectedIndex_ = (selectedIndex_ + kButtonCount - 1) % kButtonCount;
    }
    if (input->IsKeyTrigger(DIK_DOWN) || input->IsKeyTrigger(DIK_RIGHT) ||
        input->IsKeyTrigger(DIK_S) || input->IsKeyTrigger(DIK_D)) {
        selectedIndex_ = (selectedIndex_ + 1) % kButtonCount;
    }

    if (input->IsGamepadConnected()) {
        if (input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_DPAD_UP) ||
            input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_DPAD_LEFT)) {
            selectedIndex_ =
                (selectedIndex_ + kButtonCount - 1) % kButtonCount;
        }
        if (input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_DPAD_DOWN) ||
            input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_DPAD_RIGHT)) {
            selectedIndex_ = (selectedIndex_ + 1) % kButtonCount;
        }
    }

    if (input->IsKeyTrigger(DIK_RETURN) || input->IsKeyTrigger(DIK_SPACE) ||
        (input->IsGamepadConnected() &&
         input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_A))) {
        ActivateSelection();
    }

    if (input->IsKeyTrigger(DIK_TAB) ||
        (input->IsGamepadConnected() &&
         input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_B))) {
        sceneManager_->ChangeScene(std::make_unique<TitleScene>());
    }
}

void ModeSelectScene::ActivateSelection() {
    BeginTransition(selectedIndex_ == 0 ? NextScene::Game
                                        : NextScene::Settings);
}

void ModeSelectScene::BeginTransition(NextScene nextScene) {
    nextScene_ = nextScene;
    transitionTimer_ = 0.0f;
}

void ModeSelectScene::LayoutButtons(float screenWidth, float screenHeight) {
    const float helpH = (std::max)(58.0f, screenHeight * 0.085f);
    const float usableH = (std::max)(1.0f, screenHeight - helpH);
    const float startBaseW = (std::min)(520.0f, screenWidth * 0.50f);
    const float settingsBaseW = (std::min)(330.0f, screenWidth * 0.32f);
    const float startBaseH = 260.0f;
    const float settingsBaseH = 172.0f;
    const float leftCenterX = screenWidth * 0.34f;
    const float rightCenterX = screenWidth * 0.78f;
    const float startCenterY = usableH * 0.52f;
    const float settingsCenterY = usableH * 0.58f;

    buttons_[0] = {leftCenterX - startBaseW * 0.5f,
                   startCenterY - startBaseH * 0.5f, startBaseW, startBaseH};
    buttons_[1] = {rightCenterX - settingsBaseW * 0.5f,
                   settingsCenterY - settingsBaseH * 0.5f, settingsBaseW,
                   settingsBaseH};
}

bool ModeSelectScene::IsMouseOver(const ButtonRect &rect) const {
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

void ModeSelectScene::DrawBackground(float screenWidth, float screenHeight) {
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
             MakeColor(1.0f, 1.0f, 1.0f, 1.0f));
    const float helpH = (std::max)(58.0f, screenHeight * 0.085f);
    DrawRect(screenWidth * 0.11f, screenHeight * 0.22f, screenWidth * 0.52f,
             2.0f, MakeColor(0.0f, 0.0f, 0.0f, 0.18f));
    DrawRect(screenWidth * 0.53f, screenHeight * 0.72f, screenWidth * 0.24f,
             2.0f, MakeColor(0.0f, 0.0f, 0.0f, 0.14f));
    DrawRect(0.0f, screenHeight - helpH, screenWidth, helpH,
             MakeColor(0.0f, 0.0f, 0.0f, 1.0f));
}

void ModeSelectScene::DrawHelp(float screenWidth, float screenHeight) {
    const float helpH = (std::max)(58.0f, screenHeight * 0.085f);
    const Image &help = helpImages_[selectedIndex_];
    const float scale =
        (std::min)(1.0f, (screenWidth - 120.0f) / (std::max)(help.width, 1.0f));
    DrawImage(help, (screenWidth - help.width * scale) * 0.5f,
              screenHeight - helpH + (helpH - help.height * scale) * 0.5f,
              scale, 1.0f);
}

void ModeSelectScene::DrawButton(const ButtonRect &rect, const Image &label,
                                 bool selected) {
    DrawRect(rect.x, rect.y, rect.w, rect.h,
             MakeColor(1.0f, 1.0f, 1.0f, 0.82f));
    DrawRect(rect.x, rect.y + rect.h - 2.0f, rect.w, 2.0f,
             MakeColor(0.0f, 0.0f, 0.0f, selected ? 0.78f : 0.34f));
    DrawRect(rect.x + rect.w - 2.0f, rect.y, 2.0f, rect.h,
             MakeColor(0.0f, 0.0f, 0.0f, selected ? 0.68f : 0.24f));

    const float labelScale =
        (std::min)(1.0f, (rect.w * 0.72f) / (std::max)(label.width, 1.0f));
    DrawImage(label, rect.x + (rect.w - label.width * labelScale) * 0.5f,
              rect.y + (rect.h - label.height * labelScale) * 0.5f,
              labelScale, selected ? 1.0f : 0.46f);

    if (!selected) {
        return;
    }

    const float frame = 5.0f;
    const XMFLOAT4 outer = MakeColor(0.0f, 0.0f, 0.0f, 1.0f);
    DrawRect(rect.x - frame, rect.y - frame, rect.w + frame * 2.0f, frame,
             outer);
    DrawRect(rect.x - frame, rect.y + rect.h, rect.w + frame * 2.0f, frame,
             outer);
    DrawRect(rect.x - frame, rect.y - frame, frame, rect.h + frame * 2.0f,
             outer);
    DrawRect(rect.x + rect.w, rect.y - frame, frame, rect.h + frame * 2.0f,
             outer);
}

void ModeSelectScene::DrawRect(float x, float y, float w, float h,
                               const XMFLOAT4 &color) {
    Sprite sprite{};
    sprite.position = {x, y};
    sprite.size = {w, h};
    sprite.color = color;
    sprite.textureId = 0;
    ctx_->rendering.sprite->DrawSprite(sprite);
}

void ModeSelectScene::DrawImage(const Image &image, float x, float y,
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
