#pragma once
#include "BaseScene.h"
#include "GameScene.h"
#include "InputControlType.h"
#include <DirectXMath.h>
#include <array>
#include <cstdint>
#include <memory>
#include <string>

class Input;

class WeaponSelectScene : public BaseScene {
  public:
    ~WeaponSelectScene() override;

    void Initialize(const SceneContext &ctx) override;
    void Update() override;
    void Draw() override;
    void DrawTransparent() override {}

  private:
    struct Image {
        uint32_t textureId = 0;
        float width = 0.0f;
        float height = 0.0f;
    };

    struct ButtonRect {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
    };

    static constexpr int kWeaponCount = 2;
    static constexpr int kButtonCount = 3;
    static constexpr int kTutorialButtonIndex = 2;
    static constexpr int kSelectableCount = kButtonCount;

    Image LoadTextureImage(const std::wstring &path);
    void UpdateSelection(Input *input);
    void UpdateDeviceAvailability();
    void BeginStart();
    void BeginHandCameraConfirm();
    void BeginCameraTestConfirm();
    void BeginCameraTest();
    void ContinueHandStart();
    void BeginReturnToTitle();
    void BeginShowUtilityMenu();
    void BeginUtilityMenuAction();
    void UpdateUtilityMenu(Input *input);
    void UpdateHandCameraConfirm(Input *input);
    void Layout(float screenWidth, float screenHeight);
    float ButtonIntroProgress(int index, float offset = 0.0f) const;
    InputControlType SelectedControlType() const;
    InputControlType ControlTypeForIndex(int index) const;
    bool IsHandTrackingReady() const;
    bool RequestHandTrackingStartOnce();
    bool IsModeAvailable(int index) const;
    void ShowUnavailableMessage();
    void StartMenuBgm();
    void StopMenuBgm();
    void DrawOverlay(float screenWidth, float screenHeight);
    void DrawButtons();
    void DrawButtonIllustration(int index, const ButtonRect &rect, float lift,
                                float alpha, bool selected);
    void DrawLabels(float screenWidth, float screenHeight);
    void DrawUtilityMenuWindow(float screenWidth, float screenHeight);
    void DrawUtilityMenuIcon(int index, float centerX, float centerY,
                             float size, float alpha, bool selected);
    void DrawHandCameraConfirmWindow(float screenWidth, float screenHeight);
    void DrawStartTransition(float screenWidth, float screenHeight);
    void DrawRect(float x, float y, float w, float h,
                  const DirectX::XMFLOAT4 &color);
    void DrawFrame(float x, float y, float w, float h, float thickness,
                   const DirectX::XMFLOAT4 &color);
    void DrawImage(const Image &image, float x, float y, float scale = 1.0f,
                   float alpha = 1.0f);

    int selectedIndex_ = 0;
    float sceneTime_ = 0.0f;
    float introTimer_ = 0.0f;
    float transitionTimer_ = 0.0f;
    bool startRequested_ = false;
    bool titleReturnRequested_ = false;
    bool menuActionRequested_ = false;
    bool utilityMenuVisible_ = false;
    bool waitingForHandTrackingReady_ = false;
    bool handTrackingStartRequested_ = false;
    bool handCameraConfirmVisible_ = false;
    bool cameraTestConfirmPending_ = false;
    bool preserveMenuBgmOnExit_ = false;
    int handCameraConfirmIndex_ = 1;
    int utilityMenuIndex_ = 0;

    std::unique_ptr<GameScene> backgroundScene_;
    Image sceneTitleImage_{};
    Image controlsImage_{};
    Image menuPromptImage_{};
    Image utilityMenuTitleImage_{};
    Image optionMenuLabelImage_{};
    std::array<Image, 4> utilityMenuOptionImages_{};
    std::array<Image, kButtonCount> modeNameImages_{};
    Image handCameraConfirmMessageImage_{};
    Image handCameraConfirmYesImage_{};
    Image handCameraConfirmNoImage_{};
    std::array<ButtonRect, kButtonCount> buttonRects_{};
    std::array<float, kButtonCount> pulseTimers_{};
    bool cameraAvailable_ = false;
};
