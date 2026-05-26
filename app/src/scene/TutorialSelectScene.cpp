#include "TutorialSelectScene.h"
#include "AppSceneServices.h"
#include "Input.h"
#include "SceneManager.h"
#include "Sprite.h"
#include "SpriteManager.h"
#include "TextureManager.h"
#include "WeaponSelectScene.h"
#include "WinApp.h"
#include <algorithm>
#include <cmath>
#include <memory>

using namespace DirectX;

namespace {
constexpr float kTransitionDuration = 0.16f;
constexpr float kIntroDuration = 0.46f;
constexpr float kIntroButtonDelay = 0.075f;
constexpr float kIntroInputDelay = 0.34f;
constexpr float kControlsPadding = 32.0f;
constexpr float kControlsImageBottomTransparentPixels = 18.0f;
constexpr float kButtonGroupYOffset = 24.0f;
constexpr float kTitleBandVerticalPadding = 32.0f;
constexpr float kNormalButtonNameMaxAspect = 495.0f / 53.0f;
constexpr float kSmallButtonNameMaxAspect = 374.0f / 47.0f;

struct ImageContentBounds {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;

    float Width() const { return right - left + 1.0f; }
    float Height() const { return bottom - top + 1.0f; }
    float CenterX() const { return (left + right + 1.0f) * 0.5f; }
    float CenterY() const { return (top + bottom + 1.0f) * 0.5f; }
};

constexpr std::array<ImageContentBounds, 3> kButtonNameContentBounds = {{
    {132.0f, 13.0f, 626.0f, 65.0f},
    {95.0f, 16.0f, 261.0f, 65.0f},
    {95.0f, 16.0f, 419.0f, 65.0f},
}};

constexpr std::array<ImageContentBounds, 3> kButtonIllustrationBounds = {{
    {-148.0f, 42.0f, 126.0f, 150.0f},
    {-92.0f, 38.0f, 140.0f, 137.0f},
    {-116.0f, 73.0f, 66.0f, 131.0f},
}};

constexpr ImageContentBounds kTitleContentBounds{
    0.0f, 0.0f, 244.0f, 39.0f};

XMFLOAT4 MakeColor(float r, float g, float b, float a = 1.0f) {
    return {r, g, b, a};
}

XMFLOAT4 ScaleAlpha(XMFLOAT4 color, float alpha) {
    color.w *= alpha;
    return color;
}

float SmoothStep(float t) { return t * t * (3.0f - 2.0f * t); }

float Smooth01(float t) { return SmoothStep(std::clamp(t, 0.0f, 1.0f)); }
} // namespace

TutorialSelectScene::~TutorialSelectScene() {
    if (!preserveMenuBgmOnExit_) {
        AppSceneServices::StopMenuBgm(ctx_);
    }
}

void TutorialSelectScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    AppSceneServices::RequestHandTrackingStop();
    selectedIndex_ = 0;
    introTimer_ = 0.0f;
    transitionTimer_ = 0.0f;
    startRequested_ = false;
    waitingForHandTrackingReady_ = false;
    handTrackingStartRequested_ = false;
    handCameraConfirmVisible_ = false;
    preserveMenuBgmOnExit_ = false;
    handCameraConfirmIndex_ = 1;
    pulseTimers_.fill(0.0f);
    cameraAvailable_ = false;

    backgroundScene_ =
        std::make_unique<GameScene>(GameScene::Mode::BackgroundOnly);
    backgroundScene_->Initialize(ctx);

    sceneTitleImage_ = LoadTextureImage(
        L"app/resources/ui/tutorial_select/text/title_tutorial.png");
    controlsImage_ =
        LoadTextureImage(L"app/resources/ui/tutorial_select/text/controls.png");
    buttonNameImages_[0] =
        LoadTextureImage(L"app/resources/ui/tutorial_select/text/input_kbm.png");
    buttonNameImages_[1] =
        LoadTextureImage(L"app/resources/ui/tutorial_select/text/input_hand.png");
    buttonNameImages_[kBackButtonIndex] =
        LoadTextureImage(L"app/resources/ui/tutorial_select/text/back_game.png");
    handCameraConfirmMessageImage_ = LoadTextureImage(
        L"app/resources/ui/hand_camera_confirm/use_camera_message.png");
    handCameraConfirmYesImage_ =
        LoadTextureImage(L"app/resources/ui/title/exit_confirm_yes.png");
    handCameraConfirmNoImage_ =
        LoadTextureImage(L"app/resources/ui/title/exit_confirm_no.png");

    AppSceneServices::StartMenuBgm(ctx);
}

void TutorialSelectScene::Update() {
    introTimer_ =
        (std::min)(introTimer_ + ctx_->frame.deltaTime, kIntroDuration + 0.2f);
    UpdateDeviceAvailability();
    if (backgroundScene_) {
        backgroundScene_->Update();
    }
    for (float &pulse : pulseTimers_) {
        pulse = (std::max)(0.0f, pulse - ctx_->frame.deltaTime * 3.0f);
    }

    const float screenWidth =
        static_cast<float>(ctx_->systems.winApp->GetWidth());
    const float screenHeight =
        static_cast<float>(ctx_->systems.winApp->GetHeight());
    Layout(screenWidth, screenHeight);

    if (startRequested_) {
        if (selectedIndex_ == kBackButtonIndex) {
            transitionTimer_ += ctx_->frame.deltaTime;
            if (transitionTimer_ >= kTransitionDuration) {
                preserveMenuBgmOnExit_ = true;
                sceneManager_->ChangeScene(std::make_unique<WeaponSelectScene>());
            }
            return;
        }

        const InputControlType selectedType = SelectedControlType();
        if (selectedType == InputControlType::Hand &&
            waitingForHandTrackingReady_) {
            if (!IsHandTrackingReady()) {
                transitionTimer_ =
                    (std::min)(transitionTimer_ + ctx_->frame.deltaTime,
                               kTransitionDuration * 0.72f);
                return;
            }

            RequestHandTrackingStartOnce();
            waitingForHandTrackingReady_ = false;
            transitionTimer_ = 0.0f;
        } else {
            transitionTimer_ += ctx_->frame.deltaTime;
        }

        if (transitionTimer_ >= kTransitionDuration) {
            SwordInputCalibration calibration{};
            calibration.controlType = selectedType;
            sceneManager_->ChangeScene(
                std::make_unique<GameScene>(calibration,
                                            GameScene::Mode::Tutorial));
        }
        return;
    }

    if (introTimer_ >= kIntroInputDelay) {
        UpdateSelection(ctx_->systems.input);
    }
}

void TutorialSelectScene::Draw() {
    const float screenWidth =
        static_cast<float>(ctx_->systems.winApp->GetWidth());
    const float screenHeight =
        static_cast<float>(ctx_->systems.winApp->GetHeight());

    if (backgroundScene_) {
        backgroundScene_->Draw();
    }

    ctx_->rendering.sprite->PreDraw();
    const float backgroundReveal = Smooth01(introTimer_ / 0.16f);
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
             MakeColor(0.0f, 0.0f, 0.0f, 1.0f - backgroundReveal));
    DrawOverlay(screenWidth, screenHeight);
    DrawButtons();
    DrawLabels(screenWidth, screenHeight);
    DrawHandCameraConfirmWindow(screenWidth, screenHeight);
    DrawStartTransition(screenWidth, screenHeight);
    ctx_->rendering.sprite->PostDraw();
}

TutorialSelectScene::Image
TutorialSelectScene::LoadTextureImage(const std::wstring &path) {
    Image image{};
    image.textureId = ctx_->rendering.texture->Load(path);
    image.width =
        static_cast<float>(ctx_->rendering.texture->GetWidth(image.textureId));
    image.height =
        static_cast<float>(ctx_->rendering.texture->GetHeight(image.textureId));
    return image;
}

void TutorialSelectScene::UpdateSelection(Input *input) {
    if (handCameraConfirmVisible_) {
        UpdateHandCameraConfirm(input);
        return;
    }

    if (input->IsKeyTrigger(DIK_ESCAPE)) {
        selectedIndex_ = kBackButtonIndex;
        BeginStart();
        return;
    }

    int nextIndex = selectedIndex_;
    if (input->IsKeyTrigger(DIK_A) && selectedIndex_ > 0) {
        nextIndex = selectedIndex_ - 1;
    }
    if (input->IsKeyTrigger(DIK_D) && selectedIndex_ < kButtonCount - 1) {
        nextIndex = selectedIndex_ + 1;
    }

    if (nextIndex != selectedIndex_) {
        selectedIndex_ = nextIndex;
        pulseTimers_[selectedIndex_] = 1.0f;
    }

    if (input->IsKeyTrigger(DIK_SPACE)) {
        BeginStart();
    }
}

void TutorialSelectScene::UpdateDeviceAvailability() {
    cameraAvailable_ = !AppSceneServices::HasCameraDeviceAvailable() ||
                       AppSceneServices::IsCameraDeviceAvailable();
}

void TutorialSelectScene::BeginStart() {
    if (selectedIndex_ != kBackButtonIndex &&
        !IsModeAvailable(selectedIndex_)) {
        ShowUnavailableMessage();
        return;
    }

    if (selectedIndex_ != kBackButtonIndex &&
        SelectedControlType() == InputControlType::Hand) {
        BeginHandCameraConfirm();
        return;
    }

    startRequested_ = true;
    transitionTimer_ = 0.0f;
}

void TutorialSelectScene::BeginHandCameraConfirm() {
    if (handTrackingStartRequested_) {
        ContinueHandStart();
        return;
    }

    handCameraConfirmVisible_ = true;
    handCameraConfirmIndex_ = 1;
}

void TutorialSelectScene::ContinueHandStart() {
    if (!RequestHandTrackingStartOnce()) {
        return;
    }
    waitingForHandTrackingReady_ = !IsHandTrackingReady();
    startRequested_ = true;
    transitionTimer_ = 0.0f;
}

void TutorialSelectScene::Layout(float screenWidth, float screenHeight) {
    const float availableWidth =
        (std::max)(screenWidth - kControlsPadding * 2.0f, 1.0f);
    const float availableHeight =
        (std::max)(screenHeight - kControlsPadding * 2.0f, 1.0f);
    const float gap = (std::max)(36.0f, screenWidth * 0.035f);
    const float buttonSize =
        (std::min)({520.0f, availableHeight,
                    (availableWidth - gap * 2.0f) /
                        (static_cast<float>(kModeCount) + 0.5f)});
    const float backSize = buttonSize * 0.5f;
    const float totalW =
        buttonSize * static_cast<float>(kModeCount) + backSize + gap * 2.0f;
    const float startX = (screenWidth - totalW) * 0.5f;
    const float y = (screenHeight - buttonSize) * 0.5f + kButtonGroupYOffset;

    for (int i = 0; i < kModeCount; ++i) {
        buttonRects_[i] = {startX + static_cast<float>(i) * (buttonSize + gap),
                           y, buttonSize, buttonSize};
    }
    buttonRects_[kBackButtonIndex] = {
        startX + buttonSize * static_cast<float>(kModeCount) + gap * 2.0f,
        y + (buttonSize - backSize) * 0.5f, backSize, backSize};
}

float TutorialSelectScene::ButtonIntroProgress(int index, float offset) const {
    const float delay = static_cast<float>(index) * kIntroButtonDelay + offset;
    return Smooth01((introTimer_ - delay) / kIntroDuration);
}

InputControlType TutorialSelectScene::SelectedControlType() const {
    return ControlTypeForIndex(selectedIndex_);
}

InputControlType TutorialSelectScene::ControlTypeForIndex(int index) const {
    switch (index) {
    case 1:
        return InputControlType::Hand;
    default:
        return InputControlType::KeyboardMouse;
    }
}

bool TutorialSelectScene::IsHandTrackingReady() const {
    return !AppSceneServices::HasHandTrackingReady() ||
           AppSceneServices::IsHandTrackingReady();
}

bool TutorialSelectScene::RequestHandTrackingStartOnce() {
    if (handTrackingStartRequested_ ||
        !AppSceneServices::HasHandTrackingStart()) {
        return handTrackingStartRequested_;
    }
    handTrackingStartRequested_ = AppSceneServices::RequestHandTrackingStart();
    return handTrackingStartRequested_;
}

void TutorialSelectScene::UpdateHandCameraConfirm(Input *input) {
    if (input == nullptr) {
        return;
    }

    if (input->IsKeyTrigger(DIK_A) || input->IsKeyTrigger(DIK_LEFT)) {
        handCameraConfirmIndex_ = 0;
    }
    if (input->IsKeyTrigger(DIK_D) || input->IsKeyTrigger(DIK_RIGHT)) {
        handCameraConfirmIndex_ = 1;
    }
    if (input->IsKeyTrigger(DIK_ESCAPE)) {
        handCameraConfirmVisible_ = false;
        handCameraConfirmIndex_ = 1;
        return;
    }

    const bool confirm =
        input->IsKeyTrigger(DIK_RETURN) || input->IsKeyTrigger(DIK_SPACE);
    if (!confirm) {
        return;
    }
    if (handCameraConfirmIndex_ != 0) {
        handCameraConfirmVisible_ = false;
        return;
    }

    handCameraConfirmVisible_ = false;
    ContinueHandStart();
}

bool TutorialSelectScene::IsModeAvailable(int index) const {
    return index != 1 || cameraAvailable_;
}

void TutorialSelectScene::ShowUnavailableMessage() {
    MessageBoxW(ctx_->systems.winApp->GetHwnd(), L"ウェブカメラがありません",
                L"チュートリアル",
                MB_OK | MB_ICONWARNING | MB_SETFOREGROUND | MB_TOPMOST);
    SetForegroundWindow(ctx_->systems.winApp->GetHwnd());
}

void TutorialSelectScene::DrawOverlay(float screenWidth, float screenHeight) {
    const float intro = Smooth01(introTimer_ / kIntroDuration);
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
             MakeColor(0.0f, 0.0f, 0.0f, 0.68f + (1.0f - intro) * 0.18f));
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight * 0.22f,
             MakeColor(0.0f, 0.0f, 0.0f, 0.30f));
    DrawRect(0.0f, screenHeight * 0.78f, screenWidth, screenHeight * 0.22f,
             MakeColor(0.0f, 0.0f, 0.0f, 0.34f));
}

void TutorialSelectScene::DrawButtons() {
    for (int i = 0; i < kButtonCount; ++i) {
        const ButtonRect &rect = buttonRects_[i];
        const float intro = ButtonIntroProgress(i);
        if (intro <= 0.0f) {
            continue;
        }
        const bool selected = i == selectedIndex_;
        const bool available = IsModeAvailable(i);
        const float build = ButtonIntroProgress(i, 0.06f);
        const float detail = ButtonIntroProgress(i, 0.13f);
        const float lift = (selected ? 6.0f : 0.0f) - (1.0f - intro) * 18.0f;
        const float alpha = (available ? 1.0f : 0.50f) * detail;
        const float centerX = rect.x + rect.w * 0.5f;
        const float centerY = rect.y + rect.h * 0.5f - lift;
        const float panelW = rect.w * (0.22f + 0.78f * intro);
        const float panelH = rect.h * (0.08f + 0.92f * build);
        const float panelX = centerX - panelW * 0.5f;
        const float panelY = centerY - panelH * 0.5f;
        const bool backButton = i == kBackButtonIndex;
        const XMFLOAT4 body =
            selected ? MakeColor(0.030f, 0.040f, 0.040f, 0.92f * alpha)
                     : MakeColor(0.014f, 0.023f, 0.024f, 0.76f * alpha);
        const XMFLOAT4 edge =
            selected ? (backButton
                            ? MakeColor(0.95f, 0.70f, 0.32f, 0.88f * intro)
                            : MakeColor(0.00f, 0.86f, 0.78f, 0.90f * intro))
                     : MakeColor(0.60f, 0.70f, 0.70f, 0.22f * alpha);

        DrawRect(panelX + 8.0f, panelY + 10.0f, panelW, panelH,
                 MakeColor(0.0f, 0.0f, 0.0f,
                           (selected ? 0.38f : 0.24f) * intro));
        DrawRect(panelX, panelY, panelW, panelH, body);
        DrawRect(panelX, panelY, panelW, 2.0f, edge);
        DrawRect(panelX, panelY + panelH - 2.0f, panelW, 2.0f, edge);
        DrawRect(panelX, panelY, 2.0f, panelH, edge);
        DrawRect(panelX + panelW - 2.0f, panelY, 2.0f, panelH, edge);
        DrawButtonIllustration(i, rect, lift, alpha, selected);
    }
}

void TutorialSelectScene::DrawButtonIllustration(int index,
                                                 const ButtonRect &rect,
                                                 float lift, float alpha,
                                                 bool selected) {
    const bool backButton = index == kBackButtonIndex;
    const float centerX = rect.x + rect.w * 0.5f;
    const ImageContentBounds &bounds = kButtonIllustrationBounds[index];
    const float iconAreaW = rect.w * (backButton ? 0.70f : 0.76f);
    const float iconAreaH = rect.h * (backButton ? 0.42f : 0.48f);
    const float iconCenterY =
        rect.y + rect.h * (backButton ? 0.40f : 0.42f) - lift;
    const float scale =
        (std::min)(iconAreaW / (std::max)(bounds.Width(), 1.0f),
                   iconAreaH / (std::max)(bounds.Height(), 1.0f));
    const XMFLOAT4 selectedLine =
        backButton ? MakeColor(1.0f, 0.80f, 0.36f, 1.0f * alpha)
                   : MakeColor(0.06f, 0.95f, 0.86f, 1.0f * alpha);
    const XMFLOAT4 line = selected
                              ? selectedLine
                              : MakeColor(0.82f, 0.90f, 0.92f, 0.66f * alpha);
    const XMFLOAT4 fill = selected
                              ? MakeColor(0.06f, 0.16f, 0.15f, 0.54f * alpha)
                              : MakeColor(0.10f, 0.12f, 0.13f, 0.44f * alpha);
    auto sx = [&](float value) {
        return centerX + (value - bounds.CenterX()) * scale;
    };
    auto sy = [&](float value) {
        return iconCenterY + (value - bounds.CenterY()) * scale;
    };
    auto sw = [&](float value) { return value * scale; };
    auto rectAt = [&](float x, float y, float w, float h,
                      const XMFLOAT4 &color) {
        DrawRect(sx(x), sy(y), sw(w), sw(h), color);
    };
    auto frameAt = [&](float x, float y, float w, float h, float thickness,
                       const XMFLOAT4 &color) {
        DrawFrame(sx(x), sy(y), sw(w), sw(h), sw(thickness), color);
    };
    auto part = [&](float offset) { return ButtonIntroProgress(index, offset); };

    if (backButton) {
        const float arrowBuild = part(0.18f);
        rectAt(-78.0f, 91.0f, 144.0f * arrowBuild, 22.0f,
               ScaleAlpha(line, arrowBuild));
        rectAt(-100.0f, 73.0f, 34.0f * arrowBuild, 18.0f,
               ScaleAlpha(line, arrowBuild));
        rectAt(-116.0f, 91.0f, 50.0f * arrowBuild, 22.0f,
               ScaleAlpha(line, arrowBuild));
        rectAt(-100.0f, 113.0f, 34.0f * arrowBuild, 18.0f,
               ScaleAlpha(line, arrowBuild));
        return;
    }

    if (index == 0) {
        const float kbdX = -148.0f;
        const float kbdY = 62.0f;
        const float keyboardBuild = part(0.17f);
        if (keyboardBuild > 0.0f) {
            rectAt(kbdX, kbdY, 156.0f * keyboardBuild, 72.0f,
                   ScaleAlpha(fill, keyboardBuild));
            frameAt(kbdX, kbdY, 156.0f * keyboardBuild, 72.0f, 4.0f,
                    ScaleAlpha(line, keyboardBuild));
        }
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 6; ++col) {
                const float keyBuild =
                    part(0.23f + static_cast<float>(row) * 0.025f +
                         static_cast<float>(col) * 0.008f);
                if (keyBuild <= 0.0f) {
                    continue;
                }
                rectAt(kbdX + 16.0f + static_cast<float>(col) * 21.5f,
                       kbdY + 15.0f + static_cast<float>(row) * 17.0f, 13.0f,
                       8.0f * keyBuild, ScaleAlpha(line, keyBuild));
            }
        }
        const float spaceBuild = part(0.31f);
        rectAt(kbdX + 42.0f, kbdY + 56.0f, 76.0f * spaceBuild, 6.0f,
               ScaleAlpha(line, spaceBuild));

        const float mouseX = 62.0f;
        const float mouseY = 42.0f;
        const float mouseBuild = part(0.20f);
        if (mouseBuild > 0.0f) {
            rectAt(mouseX, mouseY + (1.0f - mouseBuild) * 18.0f, 64.0f,
                   108.0f * mouseBuild, ScaleAlpha(fill, mouseBuild));
            frameAt(mouseX, mouseY + (1.0f - mouseBuild) * 18.0f, 64.0f,
                    108.0f * mouseBuild, 4.0f, ScaleAlpha(line, mouseBuild));
        }
        const float mouseDetail = part(0.30f);
        rectAt(mouseX + 30.0f, mouseY + 12.0f, 4.0f, 34.0f * mouseDetail,
               ScaleAlpha(line, mouseDetail));
        rectAt(mouseX + 18.0f, mouseY + 50.0f, 30.0f * mouseDetail, 6.0f,
               ScaleAlpha(line, mouseDetail));
        return;
    }

    const float handX = -112.0f;
    const float handY = 38.0f;
    const float palmBuild = part(0.17f);
    rectAt(handX + 48.0f, handY + 35.0f, 72.0f, 64.0f * palmBuild,
           ScaleAlpha(fill, palmBuild));
    frameAt(handX + 48.0f, handY + 35.0f, 72.0f, 64.0f * palmBuild, 4.0f,
            ScaleAlpha(line, palmBuild));
    for (int i = 0; i < 4; ++i) {
        const float fingerBuild = part(0.22f + static_cast<float>(i) * 0.025f);
        if (fingerBuild <= 0.0f) {
            continue;
        }
        rectAt(handX + 44.0f + static_cast<float>(i) * 20.0f,
               handY + static_cast<float>(i % 2) * 7.0f, 14.0f,
               52.0f * fingerBuild, ScaleAlpha(line, fingerBuild));
    }
    const float thumbBuild = part(0.30f);
    rectAt(handX + 20.0f, handY + 58.0f, 36.0f * thumbBuild, 18.0f,
           ScaleAlpha(line, thumbBuild));

    const float camX = 58.0f;
    const float camY = 46.0f;
    const float cameraBuild = part(0.20f);
    rectAt(camX, camY, 82.0f * cameraBuild, 64.0f,
           ScaleAlpha(fill, cameraBuild));
    frameAt(camX, camY, 82.0f * cameraBuild, 64.0f, 4.0f,
            ScaleAlpha(line, cameraBuild));
    const float lensBuild = part(0.29f);
    rectAt(camX + 29.0f, camY + 20.0f, 24.0f * lensBuild, 24.0f,
           ScaleAlpha(line, lensBuild));
    rectAt(camX + 37.0f, camY + 28.0f, 8.0f * lensBuild, 8.0f,
           ScaleAlpha(MakeColor(0.0f, 0.0f, 0.0f, 0.22f * alpha),
                      lensBuild));
    const float standBuild = part(0.33f);
    rectAt(camX + 24.0f, camY + 76.0f, 34.0f * standBuild, 6.0f,
           ScaleAlpha(line, standBuild));
}

void TutorialSelectScene::DrawLabels(float screenWidth, float screenHeight) {
    for (int i = 0; i < kButtonCount; ++i) {
        const ButtonRect &rect = buttonRects_[i];
        const float intro = ButtonIntroProgress(i, 0.18f);
        const bool selected = i == selectedIndex_;
        const bool available = IsModeAvailable(i);
        const float lift = selected ? 6.0f : 0.0f;
        const float alpha =
            (available ? (selected ? 1.0f : 0.76f) : 0.38f) * intro;
        const Image &name = buttonNameImages_[i];
        const ImageContentBounds &bounds = kButtonNameContentBounds[i];
        const bool backButton = i == kBackButtonIndex;
        const float maxLabelAspect =
            backButton ? kSmallButtonNameMaxAspect : kNormalButtonNameMaxAspect;
        const float targetLabelHeight = (std::min)(
            rect.h * (backButton ? 0.11f : 0.10f),
            (rect.w * 0.82f) / maxLabelAspect);
        const float nameScale = (std::min)(
            {backButton ? 0.62f : 1.10f,
             targetLabelHeight / (std::max)(bounds.Height(), 1.0f),
             (rect.w * 0.82f) / (std::max)(bounds.Width(), 1.0f)});
        const float centerX = rect.x + rect.w * 0.5f;
        const float centerY =
            rect.y + rect.h * (backButton ? 0.80f : 0.78f) - lift +
            (1.0f - intro) * 8.0f;
        DrawImage(name, centerX - bounds.CenterX() * nameScale,
                  centerY - bounds.CenterY() * nameScale,
                  nameScale, alpha);
    }

    const float titleIntro = Smooth01((introTimer_ - 0.10f) / 0.24f);
    const float titleBandY = kControlsPadding - kTitleBandVerticalPadding;
    const float titleBandH =
        kTitleContentBounds.Height() + kTitleBandVerticalPadding * 2.0f;
    const float titleCenterX = screenWidth * 0.5f;
    const float titleCenterY = titleBandY + titleBandH * 0.5f;
    DrawRect(0.0f, titleBandY, screenWidth, titleBandH,
             MakeColor(0.0f, 0.0f, 0.0f, 0.42f * titleIntro));
    DrawRect(0.0f, titleBandY + titleBandH - 2.0f, screenWidth, 2.0f,
             MakeColor(0.00f, 0.86f, 0.78f, 0.30f * titleIntro));
    DrawImage(sceneTitleImage_,
              titleCenterX - kTitleContentBounds.CenterX(),
              titleCenterY - kTitleContentBounds.CenterY(), 1.0f,
              0.92f * titleIntro);

    const float controlsIntro = Smooth01((introTimer_ - 0.24f) / 0.28f);
    const float controlsScale =
        (std::min)(0.80f, (screenWidth * 0.31f) /
                              (std::max)(controlsImage_.width, 1.0f));
    DrawImage(controlsImage_, kControlsPadding,
              screenHeight -
                  (controlsImage_.height -
                   kControlsImageBottomTransparentPixels) *
                      controlsScale -
                  kControlsPadding,
              controlsScale, 0.58f * controlsIntro);
}

void TutorialSelectScene::DrawHandCameraConfirmWindow(float screenWidth,
                                                      float screenHeight) {
    if (!handCameraConfirmVisible_) {
        return;
    }

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

    const Image &message = handCameraConfirmMessageImage_;
    const float messageScale =
        (std::min)(1.0f, (panelW * 0.86f) /
                             ((std::max)(message.width, 1.0f)));
    const float messageW = message.width * messageScale;
    const float messageH = message.height * messageScale;
    DrawImage(message, panelX + (panelW - messageW) * 0.5f,
              panelY + panelH * 0.26f - messageH * 0.5f, messageScale);

    const float buttonW = std::clamp(panelW * 0.24f, 130.0f, 176.0f);
    const float buttonH = std::clamp(panelH * 0.23f, 58.0f, 76.0f);
    const float buttonGap = panelW * 0.08f;
    const float totalButtonW = buttonW * 2.0f + buttonGap;
    const float buttonY = panelY + panelH * 0.61f;
    const float firstButtonX = panelX + (panelW - totalButtonW) * 0.5f;
    const Image *labels[2] = {&handCameraConfirmYesImage_,
                              &handCameraConfirmNoImage_};

    for (int i = 0; i < 2; ++i) {
        const float x =
            firstButtonX + static_cast<float>(i) * (buttonW + buttonGap);
        const bool selected = i == handCameraConfirmIndex_;
        const XMFLOAT4 body =
            selected ? MakeColor(0.18f, 0.13f, 0.055f, 0.98f)
                     : MakeColor(0.040f, 0.046f, 0.058f, 0.92f);
        const XMFLOAT4 line =
            selected ? MakeColor(1.0f, 0.78f, 0.34f, 0.96f)
                     : MakeColor(0.62f, 0.66f, 0.72f, 0.38f);

        DrawRect(x, buttonY, buttonW, buttonH, body);
        DrawFrame(x, buttonY, buttonW, buttonH, 2.0f, line);

        const Image &label = *labels[i];
        const float labelScale =
            (std::min)({1.0f,
                        (buttonH * 0.68f) /
                            ((std::max)(label.height, 1.0f)),
                        (buttonW * 0.86f) /
                            ((std::max)(label.width, 1.0f))});
        const float labelW = label.width * labelScale;
        const float labelH = label.height * labelScale;
        DrawImage(label, x + (buttonW - labelW) * 0.5f,
                  buttonY + (buttonH - labelH) * 0.5f, labelScale,
                  selected ? 1.0f : 0.82f);
    }
}

void TutorialSelectScene::DrawStartTransition(float screenWidth,
                                              float screenHeight) {
    if (!startRequested_) {
        return;
    }
    const float t =
        std::clamp(transitionTimer_ / kTransitionDuration, 0.0f, 1.0f);
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
             MakeColor(0.0f, 0.0f, 0.0f, SmoothStep(t)));
}

void TutorialSelectScene::DrawRect(float x, float y, float w, float h,
                                   const XMFLOAT4 &color) {
    Sprite sprite{};
    sprite.position = {x, y};
    sprite.size = {w, h};
    sprite.color = color;
    sprite.textureId = 0;
    ctx_->rendering.sprite->DrawSprite(sprite);
}

void TutorialSelectScene::DrawFrame(float x, float y, float w, float h,
                                    float thickness,
                                    const XMFLOAT4 &color) {
    DrawRect(x, y, w, thickness, color);
    DrawRect(x, y + h - thickness, w, thickness, color);
    DrawRect(x, y, thickness, h, color);
    DrawRect(x + w - thickness, y, thickness, h, color);
}

void TutorialSelectScene::DrawImage(const Image &image, float x, float y,
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
