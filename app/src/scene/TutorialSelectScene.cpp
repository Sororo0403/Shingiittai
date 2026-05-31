#include "TutorialSelectScene.h"
#include "AppSceneServices.h"
#include "CreditScene.h"
#include "HandLoadingScene.h"
#include "Input.h"
#include "OptionScene.h"
#include "RankingScene.h"
#include "SceneManager.h"
#include "SoundTestScene.h"
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
constexpr int kUtilityMenuItemCount = 5;

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
    {-96.0f, 38.0f, 120.0f, 137.0f},
    {-116.0f, 73.0f, 66.0f, 131.0f},
}};

constexpr ImageContentBounds kTitleContentBounds{
    0.0f, 0.0f, 244.0f, 39.0f};

constexpr ImageContentBounds kUtilityMenuTitleContentBounds{
    1.0f, 25.0f, 118.0f, 52.0f};

constexpr std::array<ImageContentBounds, kUtilityMenuItemCount>
    kUtilityMenuLabelContentBounds = {{
        {1.0f, 25.0f, 150.0f, 54.0f},
        {1.0f, 25.0f, 209.0f, 53.0f},
        {1.0f, 24.0f, 148.0f, 53.0f},
        {0.0f, 0.0f, 222.0f, 39.0f},
        {9.0f, 8.0f, 173.0f, 38.0f},
    }};

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
    menuActionRequested_ = false;
    utilityMenuVisible_ = false;
    waitingForHandTrackingReady_ = false;
    handTrackingStartRequested_ = false;
    handCameraConfirmVisible_ = false;
    cameraTestConfirmPending_ = false;
    preserveMenuBgmOnExit_ = false;
    handCameraConfirmIndex_ = 1;
    utilityMenuIndex_ = 0;
    pulseTimers_.fill(0.0f);
    cameraAvailable_ = false;

    backgroundScene_ =
        std::make_unique<GameScene>(GameScene::Mode::TutorialBackgroundOnly);
    backgroundScene_->Initialize(ctx);

    sceneTitleImage_ = LoadTextureImage(
        L"app/resources/ui/tutorial_select/text/title_tutorial.png");
    controlsImage_ =
        LoadTextureImage(L"app/resources/ui/tutorial_select/text/controls.png");
    menuPromptImage_ =
        LoadTextureImage(L"app/resources/ui/common/tab_menu.png");
    utilityMenuTitleImage_ =
        LoadTextureImage(L"app/resources/ui/menu/menu_title.png");
    optionMenuLabelImage_ =
        LoadTextureImage(L"app/resources/ui/option/menu_label.png");
    utilityMenuOptionImages_[0] =
        LoadTextureImage(L"app/resources/ui/menu/option_ranking.png");
    utilityMenuOptionImages_[1] =
        LoadTextureImage(L"app/resources/ui/menu/option_sound_test.png");
    utilityMenuOptionImages_[2] =
        LoadTextureImage(L"app/resources/ui/menu/option_credits.png");
    utilityMenuOptionImages_[3] =
        LoadTextureImage(L"app/resources/ui/menu/option_camera_test.png");
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
            if (selectedType == InputControlType::Hand) {
                sceneManager_->ChangeScene(std::make_unique<HandLoadingScene>(
                    calibration, GameScene::Mode::Tutorial));
            } else {
                sceneManager_->ChangeScene(
                    std::make_unique<GameScene>(calibration,
                                                GameScene::Mode::Tutorial));
            }
        }
        return;
    }

    if (menuActionRequested_) {
        transitionTimer_ += ctx_->frame.deltaTime;
        if (transitionTimer_ >= kTransitionDuration) {
            switch (utilityMenuIndex_) {
            case 0:
                preserveMenuBgmOnExit_ = true;
                sceneManager_->ChangeScene(std::make_unique<RankingScene>(
                    RankingScene::ReturnTarget::TutorialSelect));
                break;
            case 1:
                sceneManager_->ChangeScene(std::make_unique<SoundTestScene>(
                    SoundTestScene::ReturnTarget::TutorialSelect));
                break;
            case 2:
                sceneManager_->ChangeScene(std::make_unique<CreditScene>(
                    CreditScene::ReturnTarget::TutorialSelect));
                break;
            case 3:
                sceneManager_->ChangeScene(std::make_unique<HandLoadingScene>(
                    CameraAccuracyDebugScene::ReturnTarget::TutorialSelect));
                break;
            case 4:
                preserveMenuBgmOnExit_ = true;
                sceneManager_->ChangeScene(std::make_unique<OptionScene>(
                    OptionScene::ReturnTarget::TutorialSelect));
                break;
            }
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
    DrawUtilityMenuWindow(screenWidth, screenHeight);
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

    if (utilityMenuVisible_) {
        UpdateUtilityMenu(input);
        return;
    }

    if (input->IsKeyTrigger(DIK_ESCAPE)) {
        selectedIndex_ = kBackButtonIndex;
        AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Cancel);
        BeginStart();
        return;
    }

    if (input->IsKeyTrigger(DIK_TAB)) {
        AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Selected);
        BeginShowUtilityMenu();
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
        AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Select);
    }

    if (input->IsKeyTrigger(DIK_SPACE)) {
        AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Selected);
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

void TutorialSelectScene::BeginCameraTestConfirm() {
    if (!cameraAvailable_) {
        ShowUnavailableMessage();
        return;
    }
    cameraTestConfirmPending_ = true;
    handCameraConfirmVisible_ = true;
    handCameraConfirmIndex_ = 1;
}

void TutorialSelectScene::BeginCameraTest() {
    handCameraConfirmVisible_ = false;
    cameraTestConfirmPending_ = false;
    menuActionRequested_ = false;
    utilityMenuVisible_ = false;
    sceneManager_->ChangeScene(std::make_unique<HandLoadingScene>(
        CameraAccuracyDebugScene::ReturnTarget::TutorialSelect));
}

void TutorialSelectScene::ContinueHandStart() {
    if (!RequestHandTrackingStartOnce()) {
        return;
    }
    waitingForHandTrackingReady_ = false;
    startRequested_ = true;
    transitionTimer_ = 0.0f;
}

void TutorialSelectScene::BeginShowUtilityMenu() {
    utilityMenuVisible_ = true;
    utilityMenuIndex_ = 0;
}

void TutorialSelectScene::BeginUtilityMenuAction() {
    if (utilityMenuIndex_ == 3) {
        BeginCameraTestConfirm();
        return;
    }
    menuActionRequested_ = true;
    startRequested_ = false;
    utilityMenuVisible_ = true;
    waitingForHandTrackingReady_ = false;
    transitionTimer_ = 0.0f;
}

void TutorialSelectScene::UpdateUtilityMenu(Input *input) {
    if (input == nullptr) {
        return;
    }
    if (input->IsKeyTrigger(DIK_A) || input->IsKeyTrigger(DIK_LEFT)) {
        utilityMenuIndex_ =
            (utilityMenuIndex_ + kUtilityMenuItemCount - 1) %
            kUtilityMenuItemCount;
        AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Select);
    }
    if (input->IsKeyTrigger(DIK_D) || input->IsKeyTrigger(DIK_RIGHT)) {
        utilityMenuIndex_ = (utilityMenuIndex_ + 1) % kUtilityMenuItemCount;
        AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Select);
    }
    if (input->IsKeyTrigger(DIK_ESCAPE) || input->IsKeyTrigger(DIK_TAB)) {
        utilityMenuVisible_ = false;
        AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Cancel);
        return;
    }
    if (input->IsKeyTrigger(DIK_SPACE) || input->IsKeyTrigger(DIK_RETURN)) {
        AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Selected);
        BeginUtilityMenuAction();
    }
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
        if (handCameraConfirmIndex_ != 0) {
            handCameraConfirmIndex_ = 0;
            AppSceneServices::PlayMenuSe(*ctx_,
                                         AppSceneServices::MenuSe::Select);
        }
    }
    if (input->IsKeyTrigger(DIK_D) || input->IsKeyTrigger(DIK_RIGHT)) {
        if (handCameraConfirmIndex_ != 1) {
            handCameraConfirmIndex_ = 1;
            AppSceneServices::PlayMenuSe(*ctx_,
                                         AppSceneServices::MenuSe::Select);
        }
    }
    if (input->IsKeyTrigger(DIK_ESCAPE)) {
        handCameraConfirmVisible_ = false;
        cameraTestConfirmPending_ = false;
        handCameraConfirmIndex_ = 1;
        AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Cancel);
        return;
    }

    const bool confirm =
        input->IsKeyTrigger(DIK_RETURN) || input->IsKeyTrigger(DIK_SPACE);
    if (!confirm) {
        return;
    }
    if (handCameraConfirmIndex_ != 0) {
        handCameraConfirmVisible_ = false;
        cameraTestConfirmPending_ = false;
        AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Cancel);
        return;
    }

    handCameraConfirmVisible_ = false;
    AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Selected);
    if (cameraTestConfirmPending_) {
        BeginCameraTest();
        return;
    }
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

    auto drawHand = [&](float handX, float delay, bool thumbRight) {
        const float handY = 38.0f;
        const float palmBuild = part(delay);
        if (palmBuild > 0.0f) {
            const float palmX = thumbRight ? handX + 20.0f : handX + 48.0f;
            rectAt(palmX, handY + 35.0f, 72.0f, 64.0f * palmBuild,
                   ScaleAlpha(fill, palmBuild));
            frameAt(palmX, handY + 35.0f, 72.0f, 64.0f * palmBuild, 4.0f,
                    ScaleAlpha(line, palmBuild));
        }

        for (int i = 0; i < 4; ++i) {
            const float fingerBuild =
                part(delay + 0.05f + static_cast<float>(i) * 0.025f);
            if (fingerBuild <= 0.0f) {
                continue;
            }
            const float fingerX = thumbRight
                                      ? handX + 22.0f +
                                            static_cast<float>(i) * 20.0f
                                      : handX + 44.0f +
                                            static_cast<float>(i) * 20.0f;
            rectAt(fingerX, handY + static_cast<float>(i % 2) * 7.0f, 14.0f,
                   52.0f * fingerBuild, ScaleAlpha(line, fingerBuild));
        }

        const float thumbBuild = part(delay + 0.13f);
        const float thumbX = thumbRight ? handX + 84.0f : handX + 20.0f;
        rectAt(thumbX, handY + 58.0f, 36.0f * thumbBuild, 18.0f,
               ScaleAlpha(line, thumbBuild));
    };

    drawHand(-116.0f, 0.17f, false);
    drawHand(-8.0f, 0.20f, true);
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
    const float controlsY =
        screenHeight -
        (controlsImage_.height - kControlsImageBottomTransparentPixels) *
            controlsScale -
        kControlsPadding;
    DrawImage(controlsImage_, kControlsPadding, controlsY, controlsScale,
              0.58f * controlsIntro);
    DrawImage(menuPromptImage_, kControlsPadding,
              controlsY - 36.0f, controlsScale, 0.58f * controlsIntro);
}

void TutorialSelectScene::DrawUtilityMenuWindow(float screenWidth,
                                                float screenHeight) {
    if (!utilityMenuVisible_) {
        return;
    }

    DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
             MakeColor(0.0f, 0.0f, 0.0f, 0.54f));

    const float screenMargin = 48.0f;
    const float availablePanelW =
        (std::max)(320.0f, screenWidth - screenMargin * 2.0f);
    const float availablePanelH =
        (std::max)(280.0f, screenHeight - screenMargin * 2.0f);
    const float panelMaxW = (std::min)(1220.0f, availablePanelW);
    const float panelMaxH = (std::min)(480.0f, availablePanelH);
    const float panelMinW = (std::min)(860.0f, panelMaxW);
    const float panelMinH = (std::min)(380.0f, panelMaxH);
    const float panelW = std::clamp(screenWidth * 0.72f, panelMinW, panelMaxW);
    const float panelH =
        std::clamp(screenHeight * 0.44f, panelMinH, panelMaxH);
    const float panelX = (screenWidth - panelW) * 0.5f;
    const float panelY = (screenHeight - panelH) * 0.5f;
    const float edge = 3.0f;

    DrawRect(panelX + 10.0f, panelY + 12.0f, panelW, panelH,
             MakeColor(0.0f, 0.0f, 0.0f, 0.36f));
    DrawRect(panelX, panelY, panelW, panelH,
             MakeColor(0.018f, 0.020f, 0.026f, 0.96f));
    DrawFrame(panelX, panelY, panelW, panelH, edge,
              MakeColor(0.92f, 0.68f, 0.28f, 0.72f));

    const float titleScale = (std::min)(
        {1.18f, (panelW * 0.30f) /
                    (std::max)(kUtilityMenuTitleContentBounds.Width(), 1.0f),
         (panelH * 0.13f) /
             (std::max)(kUtilityMenuTitleContentBounds.Height(), 1.0f)});
    const float titleCenterX = panelX + panelW * 0.5f;
    const float titleCenterY = panelY + panelH * 0.23f;
    DrawImage(utilityMenuTitleImage_,
              titleCenterX -
                  kUtilityMenuTitleContentBounds.CenterX() * titleScale,
              titleCenterY -
                  kUtilityMenuTitleContentBounds.CenterY() * titleScale,
              titleScale, 0.94f);

    const float buttonGap = panelW * 0.028f;
    const float buttonAreaPadding = panelW * 0.075f;
    const float buttonAreaW =
        (std::max)(0.0f, panelW - buttonAreaPadding * 2.0f);
    const float buttonSizeByWidth =
        (buttonAreaW -
         buttonGap * static_cast<float>(kUtilityMenuItemCount - 1)) /
        static_cast<float>(kUtilityMenuItemCount);
    const float buttonSize = std::clamp((std::min)(panelH * 0.46f,
                                                   buttonSizeByWidth),
                                        32.0f, 180.0f);
    const float buttonW = buttonSize;
    const float buttonH = buttonSize;
    const float totalButtonW =
        buttonSize * static_cast<float>(kUtilityMenuItemCount) +
        buttonGap * static_cast<float>(kUtilityMenuItemCount - 1);
    const float buttonY = panelY + panelH * 0.38f;
    const float firstButtonX = panelX + (panelW - totalButtonW) * 0.5f;

    for (int i = 0; i < kUtilityMenuItemCount; ++i) {
        const float x =
            firstButtonX + static_cast<float>(i) * (buttonW + buttonGap);
        const bool selected = i == utilityMenuIndex_;
        const XMFLOAT4 body =
            selected ? MakeColor(0.18f, 0.13f, 0.055f, 0.98f)
                     : MakeColor(0.040f, 0.046f, 0.058f, 0.92f);
        const XMFLOAT4 line =
            selected ? MakeColor(1.0f, 0.78f, 0.34f, 0.96f)
                     : MakeColor(0.62f, 0.66f, 0.72f, 0.38f);

        DrawRect(x, buttonY, buttonW, buttonH, body);
        DrawFrame(x, buttonY, buttonW, buttonH, 2.0f, line);

        const float iconBoxSize = buttonSize * 0.58f;
        const float iconBoxX = x + (buttonSize - iconBoxSize) * 0.5f;
        const float iconBoxY = buttonY + buttonSize * 0.15f;
        DrawRect(iconBoxX, iconBoxY, iconBoxSize, iconBoxSize,
                 selected ? MakeColor(0.10f, 0.075f, 0.036f, 0.72f)
                          : MakeColor(0.020f, 0.024f, 0.030f, 0.58f));
        DrawFrame(iconBoxX, iconBoxY, iconBoxSize, iconBoxSize, 2.0f, line);
        DrawUtilityMenuIcon(i, iconBoxX + iconBoxSize * 0.5f,
                            iconBoxY + iconBoxSize * 0.5f,
                            iconBoxSize * 0.78f, selected ? 1.0f : 0.78f,
                            selected);

        const Image &label =
            i < 4 ? utilityMenuOptionImages_[i] : optionMenuLabelImage_;
        const ImageContentBounds &labelBounds =
            kUtilityMenuLabelContentBounds[i];
        const float labelScale =
            (std::min)({0.82f,
                        (buttonH * 0.15f) /
                            (std::max)(labelBounds.Height(), 1.0f),
                        (buttonW * 0.86f) /
                            (std::max)(labelBounds.Width(), 1.0f)});
        const float labelCenterX = x + buttonW * 0.5f;
        const float labelCenterY = buttonY + buttonH * 0.78f;
        DrawImage(label, labelCenterX - labelBounds.CenterX() * labelScale,
                  labelCenterY - labelBounds.CenterY() * labelScale,
                  labelScale, selected ? 1.0f : 0.82f);
    }
}

void TutorialSelectScene::DrawUtilityMenuIcon(int index, float centerX,
                                              float centerY, float size,
                                              float alpha, bool selected) {
    const XMFLOAT4 line =
        selected ? MakeColor(1.0f, 0.78f, 0.34f, 0.92f * alpha)
                 : MakeColor(0.82f, 0.86f, 0.92f, 0.66f * alpha);
    const XMFLOAT4 fill =
        selected ? MakeColor(0.16f, 0.12f, 0.070f, 0.48f * alpha)
                 : MakeColor(0.10f, 0.11f, 0.13f, 0.38f * alpha);
    const float s = size / 100.0f;
    auto x = [&](float v) { return centerX + v * s; };
    auto y = [&](float v) { return centerY + v * s; };
    auto r = [&](float px, float py, float w, float h,
                 const XMFLOAT4 &color) {
        DrawRect(x(px), y(py), w * s, h * s, color);
    };
    auto f = [&](float px, float py, float w, float h) {
        DrawFrame(x(px), y(py), w * s, h * s, 4.0f * s, line);
    };

    if (index == 0) {
        r(-10.0f, -36.0f, 18.0f, 72.0f, line);
        r(-28.0f, -22.0f, 34.0f, 14.0f, line);
        r(-24.0f, 24.0f, 58.0f, 14.0f, line);
        r(-4.0f, -30.0f, 12.0f, 60.0f, fill);
        return;
    }

    if (index == 1) {
        r(-12.0f, -38.0f, 12.0f, 64.0f, line);
        r(0.0f, -38.0f, 42.0f, 10.0f, line);
        r(34.0f, -28.0f, 10.0f, 44.0f, line);
        r(-38.0f, 18.0f, 32.0f, 24.0f, fill);
        f(-38.0f, 18.0f, 32.0f, 24.0f);
        r(14.0f, 10.0f, 32.0f, 24.0f, fill);
        f(14.0f, 10.0f, 32.0f, 24.0f);
        return;
    }

    if (index == 3) {
        r(-40.0f, -24.0f, 80.0f, 50.0f, fill);
        f(-40.0f, -24.0f, 80.0f, 50.0f);
        r(-26.0f, -36.0f, 52.0f, 14.0f, line);
        r(-12.0f, -4.0f, 24.0f, 18.0f, line);
        r(-4.0f, -38.0f, 8.0f, 12.0f, fill);
        return;
    }

    if (index == 4) {
        r(-36.0f, -22.0f, 72.0f, 10.0f, line);
        r(-36.0f, 14.0f, 72.0f, 10.0f, line);
        r(-18.0f, -30.0f, 12.0f, 26.0f, fill);
        f(-18.0f, -30.0f, 12.0f, 26.0f);
        r(10.0f, 6.0f, 12.0f, 26.0f, fill);
        f(10.0f, 6.0f, 12.0f, 26.0f);
        return;
    }

    r(-34.0f, -38.0f, 56.0f, 72.0f, fill);
    f(-34.0f, -38.0f, 56.0f, 72.0f);
    r(10.0f, -38.0f, 18.0f, 18.0f, line);
    r(-22.0f, -18.0f, 34.0f, 5.0f, line);
    r(-22.0f, -3.0f, 30.0f, 5.0f, line);
    r(-22.0f, 12.0f, 24.0f, 5.0f, line);
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
    if (!startRequested_ && !menuActionRequested_) {
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
