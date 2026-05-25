#pragma once
#include "BaseScene.h"
#include "GameScene.h"
#include "InputControlType.h"
#include "JoyCon.h"
#include <memory>

class Input;

class WeaponSelectScene : public BaseScene {
  public:
    void Initialize(const SceneContext &ctx) override;
    void Update() override;
    void Draw() override;
    void DrawTransparent() override {}

  private:
    static constexpr int kWeaponCount = 3;
    static constexpr int kSelectableCount = kWeaponCount;

    void UpdateSelection(Input *input);
    void UpdateDeviceAvailability();
    void BeginStart();
    InputControlType SelectedControlType() const;
    InputControlType ControlTypeForIndex(int index) const;
    bool IsHandTrackingReady() const;
    void RequestHandTrackingStartOnce();
    bool IsModeAvailable(int index) const;
    void ShowUnavailableMessage(int index);

    int selectedIndex_ = 0;
    float transitionTimer_ = 0.0f;
    bool startRequested_ = false;
    bool waitingForHandTrackingReady_ = false;
    bool handTrackingStartRequested_ = false;

    JoyCon leftJoyCon_;
    JoyCon rightJoyCon_;
    std::unique_ptr<GameScene> backgroundScene_;
    bool joyConAvailable_ = false;
    bool cameraAvailable_ = false;
};
