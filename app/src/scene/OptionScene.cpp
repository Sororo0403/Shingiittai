#include "OptionScene.h"
#include "AppSceneServices.h"
#include "Input.h"
#include "SceneManager.h"
#include "Sprite.h"
#include "SpriteManager.h"
#include "TextureManager.h"
#include "TutorialSelectScene.h"
#include "WeaponSelectScene.h"
#include "WinApp.h"
#include <algorithm>

using namespace DirectX;

namespace {
constexpr float kIntroDuration = 1.10f;
constexpr float kBackgroundRevealDuration = 0.84f;
constexpr float kContentFadeDelay = 0.48f;
constexpr float kContentFadeDuration = 0.50f;
constexpr float kTransitionDuration = 0.16f;
constexpr float kControlsPadding = 32.0f;
constexpr float kControlsImageBottomTransparentPixels = 18.0f;
constexpr float kAdjustHoldDelay = 0.24f;
constexpr float kAdjustRepeatInterval = 0.045f;

XMFLOAT4 Color(float r, float g, float b, float a = 1.0f) {
    return {r, g, b, a};
}

float SmoothStep(float t) { return t * t * (3.0f - 2.0f * t); }

float Smooth01(float t) {
    return SmoothStep(std::clamp(t, 0.0f, 1.0f));
}

} // namespace

OptionScene::OptionScene(ReturnTarget returnTarget)
    : returnTarget_(returnTarget) {}

OptionScene::~OptionScene() {
    if (!preserveMenuBgmOnExit_) {
        AppSceneServices::StopMenuBgm(ctx_);
    }
}

void OptionScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    AppSceneServices::RequestHandTrackingStop();
    selectedIndex_ = 0;
    introTimer_ = 0.0f;
    transitionTimer_ = 0.0f;
    adjustHoldDirection_ = 0;
    adjustHoldTimer_ = 0.0f;
    adjustRepeatTimer_ = 0.0f;
    returnRequested_ = false;
    preserveMenuBgmOnExit_ = false;

    const GameScene::Mode backgroundMode =
        returnTarget_ == ReturnTarget::TutorialSelect
            ? GameScene::Mode::TutorialBackgroundOnly
            : GameScene::Mode::BackgroundOnly;
    backgroundScene_ = std::make_unique<GameScene>(backgroundMode);
    backgroundScene_->Initialize(ctx);
    titleImage_ = LoadTextureImage(L"app/resources/ui/option/title.png");
    bgmLabelImage_ = LoadTextureImage(L"app/resources/ui/option/bgm.png");
    seLabelImage_ = LoadTextureImage(L"app/resources/ui/option/se.png");
    mouseSlashLabelImage_ =
        LoadTextureImage(L"app/resources/ui/option/mouse_slash.png");
    keyAImage_ = LoadTextureImage(L"app/resources/ui/sound_test/key_a.png");
    keyDImage_ = LoadTextureImage(L"app/resources/ui/sound_test/key_d.png");
    tabBackPromptImage_ =
        LoadTextureImage(L"app/resources/ui/option/tab_back.png");
    AppSceneServices::StartMenuBgm(ctx);
}

void OptionScene::Update() {
    introTimer_ = (std::min)(introTimer_ + ctx_->frame.deltaTime,
                             kIntroDuration + 0.2f);
    if (backgroundScene_) {
        backgroundScene_->Update();
    }

    if (returnRequested_) {
        transitionTimer_ += ctx_->frame.deltaTime;
        if (transitionTimer_ >= kTransitionDuration) {
            preserveMenuBgmOnExit_ = true;
            sceneManager_->ChangeScene(CreateReturnScene());
        }
        return;
    }

    Input *input = ctx_->systems.input;
    if (input == nullptr) {
        return;
    }

    if (input->IsKeyTrigger(DIK_ESCAPE) || input->IsKeyTrigger(DIK_TAB)) {
        AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Cancel);
        BeginReturn();
        return;
    }
    if (input->IsKeyTrigger(DIK_W) || input->IsKeyTrigger(DIK_UP)) {
        selectedIndex_ = (selectedIndex_ + kOptionCount - 1) % kOptionCount;
        AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Select);
    }
    if (input->IsKeyTrigger(DIK_S) || input->IsKeyTrigger(DIK_DOWN)) {
        selectedIndex_ = (selectedIndex_ + 1) % kOptionCount;
        AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Select);
    }

    const bool leftTrigger =
        input->IsKeyTrigger(DIK_A) || input->IsKeyTrigger(DIK_LEFT);
    const bool rightTrigger =
        input->IsKeyTrigger(DIK_D) || input->IsKeyTrigger(DIK_RIGHT);
    const bool leftPress = input->IsKeyPress(DIK_A) || input->IsKeyPress(DIK_LEFT);
    const bool rightPress =
        input->IsKeyPress(DIK_D) || input->IsKeyPress(DIK_RIGHT);

    int holdDirection = 0;
    if (leftPress && !rightPress) {
        holdDirection = -1;
    } else if (rightPress && !leftPress) {
        holdDirection = 1;
    }

    if (leftTrigger && !rightPress) {
        AdjustSelectedOption(-1);
        adjustHoldDirection_ = -1;
        adjustHoldTimer_ = 0.0f;
        adjustRepeatTimer_ = 0.0f;
        return;
    }
    if (rightTrigger && !leftPress) {
        AdjustSelectedOption(1);
        adjustHoldDirection_ = 1;
        adjustHoldTimer_ = 0.0f;
        adjustRepeatTimer_ = 0.0f;
        return;
    }

    if (holdDirection == 0) {
        adjustHoldDirection_ = 0;
        adjustHoldTimer_ = 0.0f;
        adjustRepeatTimer_ = 0.0f;
        return;
    }

    if (holdDirection != adjustHoldDirection_) {
        adjustHoldDirection_ = holdDirection;
        adjustHoldTimer_ = 0.0f;
        adjustRepeatTimer_ = 0.0f;
        return;
    }

    adjustHoldTimer_ += ctx_->frame.deltaTime;
    if (adjustHoldTimer_ < kAdjustHoldDelay) {
        return;
    }

    adjustRepeatTimer_ += ctx_->frame.deltaTime;
    while (adjustRepeatTimer_ >= kAdjustRepeatInterval) {
        adjustRepeatTimer_ -= kAdjustRepeatInterval;
        AdjustSelectedOption(adjustHoldDirection_);
    }
}

void OptionScene::Draw() {
    const float screenWidth =
        static_cast<float>(ctx_->systems.winApp->GetWidth());
    const float screenHeight =
        static_cast<float>(ctx_->systems.winApp->GetHeight());

    if (backgroundScene_) {
        backgroundScene_->Draw();
    }

    ctx_->rendering.sprite->PreDraw();
    DrawOverlay(screenWidth, screenHeight);
    DrawPanel(screenWidth, screenHeight);
    DrawControlsPrompt(screenWidth, screenHeight);
    DrawTransition(screenWidth, screenHeight);
    ctx_->rendering.sprite->PostDraw();
}

void OptionScene::BeginReturn() {
    returnRequested_ = true;
    transitionTimer_ = 0.0f;
}

OptionScene::Image OptionScene::LoadTextureImage(const std::wstring &path) {
    Image image{};
    image.textureId = ctx_->rendering.texture->Load(path);
    image.width =
        static_cast<float>(ctx_->rendering.texture->GetWidth(image.textureId));
    image.height =
        static_cast<float>(ctx_->rendering.texture->GetHeight(image.textureId));
    return image;
}

void OptionScene::AdjustSelectedOption(int direction) {
    constexpr float kStep = 0.05f;
    if (selectedIndex_ == 0) {
        AppSceneServices::SetBgmVolume(
            *ctx_, std::clamp(AppSceneServices::GetBgmVolume() +
                                  static_cast<float>(direction) * kStep,
                              0.0f, 1.0f));
    } else if (selectedIndex_ == 1) {
        AppSceneServices::SetSeVolume(
            std::clamp(AppSceneServices::GetSeVolume() +
                           static_cast<float>(direction) * kStep,
                       0.0f, 1.0f));
    } else {
        AppSceneServices::SetMouseSlashSensitivity(
            std::clamp(AppSceneServices::GetMouseSlashSensitivity() +
                           static_cast<float>(direction) * kStep,
                       0.0f, 1.0f));
    }
    AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Select);
}

void OptionScene::DrawOverlay(float screenWidth, float screenHeight) {
    const float backgroundReveal =
        Smooth01(introTimer_ / kBackgroundRevealDuration);
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
             Color(0.0f, 0.0f, 0.0f,
                   0.62f + (1.0f - backgroundReveal) * 0.20f));
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight * 0.20f,
             Color(0.0f, 0.0f, 0.0f, 0.32f * backgroundReveal));
    DrawRect(0.0f, screenHeight * 0.80f, screenWidth, screenHeight * 0.20f,
             Color(0.0f, 0.0f, 0.0f, 0.36f * backgroundReveal));
}

void OptionScene::DrawPanel(float screenWidth, float screenHeight) {
    const float intro =
        Smooth01((introTimer_ - kContentFadeDelay) / kContentFadeDuration);
    if (intro <= 0.0f) {
        return;
    }
    const float panelW = std::clamp(screenWidth * 0.58f, 660.0f, 920.0f);
    const float panelH = std::clamp(screenHeight * 0.56f, 440.0f, 560.0f);
    const float panelX = (screenWidth - panelW) * 0.5f;
    const float panelY = (screenHeight - panelH) * 0.5f +
                         (1.0f - intro) * 30.0f;

    DrawRect(panelX + 10.0f, panelY + 12.0f, panelW, panelH,
             Color(0.0f, 0.0f, 0.0f, 0.34f * intro));
    DrawRect(panelX, panelY, panelW, panelH,
             Color(0.018f, 0.020f, 0.026f, 0.96f * intro));
    DrawFrame(panelX, panelY, panelW, panelH, 3.0f,
              Color(0.92f, 0.68f, 0.28f, 0.74f * intro));

    const float titleScale =
        (std::min)(1.0f,
                   (panelW * 0.42f) / (std::max)(titleImage_.width, 1.0f));
    DrawImage(titleImage_,
              panelX + (panelW - titleImage_.width * titleScale) * 0.5f,
              panelY + panelH * 0.16f, titleScale, 0.96f * intro);

    const Image *labels[kOptionCount] = {&bgmLabelImage_, &seLabelImage_,
                                         &mouseSlashLabelImage_};
    const float values[kOptionCount] = {AppSceneServices::GetBgmVolume(),
                                        AppSceneServices::GetSeVolume(),
                                        AppSceneServices::GetMouseSlashSensitivity()};
    const float rowX = panelX + panelW * 0.20f;
    const float barX = panelX + panelW * 0.40f;
    const float barW = panelW * 0.36f;
    const float barH = 22.0f;
    const float firstRowY = panelY + panelH * 0.48f;
    const float rowGap = panelH * 0.18f;

    for (int i = 0; i < kOptionCount; ++i) {
        const bool selected = i == selectedIndex_;
        const float rowCenterY = firstRowY + static_cast<float>(i) * rowGap;
        const float barY = rowCenterY - barH * 0.5f;
        const XMFLOAT4 textColor =
            selected ? Color(1.0f, 0.78f, 0.34f, 0.98f * intro)
                     : Color(0.78f, 0.82f, 0.88f, 0.74f * intro);
        const Image &label = *labels[i];
        const float labelScale =
            (std::min)(1.0f,
                       (panelW * 0.16f) / (std::max)(label.width, 1.0f));
        DrawImage(label, rowX, rowCenterY - label.height * labelScale * 0.5f,
                  labelScale, textColor.w);
        DrawRect(barX, barY, barW, barH,
                 Color(0.055f, 0.062f, 0.074f, 0.96f));
        DrawFrame(barX, barY, barW, barH, 2.0f,
                  selected ? Color(1.0f, 0.78f, 0.34f, 0.88f * intro)
                           : Color(0.54f, 0.58f, 0.64f, 0.42f * intro));
        DrawRect(barX, barY, barW * std::clamp(values[i], 0.0f, 1.0f), barH,
                 selected ? Color(1.0f, 0.68f, 0.20f, 0.90f * intro)
                          : Color(0.72f, 0.76f, 0.82f, 0.64f * intro));
        if (selected) {
            const float keyScale = std::clamp(
                barH * 1.65f / (std::max)(keyAImage_.height, 1.0f), 0.38f,
                0.58f);
            const float keyW = keyAImage_.width * keyScale;
            const float keyH = keyAImage_.height * keyScale;
            const float keyY = rowCenterY - keyH * 0.5f;
            DrawImage(keyAImage_, barX - keyW - 18.0f, keyY, keyScale,
                      0.78f * intro);
            DrawImage(keyDImage_, barX + barW + 18.0f, keyY, keyScale,
                      0.78f * intro);
        }
    }

}

void OptionScene::DrawControlsPrompt(float screenWidth, float screenHeight) {
    const float intro = Smooth01((introTimer_ - kContentFadeDelay - 0.10f) /
                                 (kContentFadeDuration * 0.72f));
    const float promptScale =
        (std::min)(0.88f, (screenWidth * 0.22f) /
                              (std::max)(tabBackPromptImage_.width, 1.0f));
    const float promptY =
        screenHeight -
        (tabBackPromptImage_.height - kControlsImageBottomTransparentPixels) *
            promptScale -
        kControlsPadding;
    DrawImage(tabBackPromptImage_, kControlsPadding, promptY, promptScale,
              0.62f * intro);
}

void OptionScene::DrawTransition(float screenWidth, float screenHeight) {
    const float introFade = 1.0f - Smooth01(introTimer_ / kIntroDuration);
    if (introFade > 0.0f) {
        DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
                 Color(0.0f, 0.0f, 0.0f, introFade));
    }

    if (!returnRequested_) {
        return;
    }
    const float fade = Smooth01(transitionTimer_ / kTransitionDuration);
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
             Color(0.0f, 0.0f, 0.0f, fade));
}

void OptionScene::DrawRect(float x, float y, float w, float h,
                           const DirectX::XMFLOAT4 &color) {
    if (ctx_ == nullptr || ctx_->rendering.sprite == nullptr) {
        return;
    }
    Sprite sprite{};
    sprite.position = {x, y};
    sprite.size = {w, h};
    sprite.color = color;
    ctx_->rendering.sprite->DrawSprite(sprite);
}

void OptionScene::DrawFrame(float x, float y, float w, float h,
                            float thickness,
                            const DirectX::XMFLOAT4 &color) {
    DrawRect(x, y, w, thickness, color);
    DrawRect(x, y + h - thickness, w, thickness, color);
    DrawRect(x, y, thickness, h, color);
    DrawRect(x + w - thickness, y, thickness, h, color);
}

void OptionScene::DrawImage(const Image &image, float x, float y, float scale,
                            float alpha) {
    if (ctx_ == nullptr || ctx_->rendering.sprite == nullptr ||
        image.textureId == 0) {
        return;
    }
    Sprite sprite{};
    sprite.position = {x, y};
    sprite.size = {image.width * scale, image.height * scale};
    sprite.color = {1.0f, 1.0f, 1.0f, alpha};
    sprite.textureId = image.textureId;
    ctx_->rendering.sprite->DrawSprite(sprite);
}

std::unique_ptr<BaseScene> OptionScene::CreateReturnScene() const {
    switch (returnTarget_) {
    case ReturnTarget::TutorialSelect:
        return std::make_unique<TutorialSelectScene>();
    case ReturnTarget::WeaponSelect:
    default:
        return std::make_unique<WeaponSelectScene>();
    }
}
