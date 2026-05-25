#include "WeaponSelectScene.h"
#include "AppSceneServices.h"
#include "CalibrationScene.h"
#include "Input.h"
#include "SceneManager.h"
#include "TipScene.h"
#include "WinApp.h"
#include <Xinput.h>
#include <memory>

namespace {
constexpr float kTransitionDuration = 0.16f;
} // namespace

void WeaponSelectScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    selectedIndex_ = 0;
    transitionTimer_ = 0.0f;
    startRequested_ = false;
    waitingForHandTrackingReady_ = false;
    handTrackingStartRequested_ = false;
    joyConAvailable_ = false;
    cameraAvailable_ = false;

    leftJoyCon_.Initialize(true);
    rightJoyCon_.Initialize(false);

    backgroundScene_ = std::make_unique<GameScene>(GameScene::Mode::BackgroundOnly);
    backgroundScene_->Initialize(ctx);
}

void WeaponSelectScene::Update() {
    UpdateDeviceAvailability();
    if (backgroundScene_) {
        backgroundScene_->Update();
    }

    if (startRequested_) {
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
                sceneManager_->ChangeScene(
                    std::make_unique<TipScene>(calibration));
            } else if (selectedType == InputControlType::JoyCon) {
                sceneManager_->ChangeScene(
                    std::make_unique<CalibrationScene>(selectedType));
            } else {
                sceneManager_->ChangeScene(
                    std::make_unique<TipScene>(calibration));
            }
        }
        return;
    }

    UpdateSelection(ctx_->systems.input);
}

void WeaponSelectScene::Draw() {
    if (backgroundScene_) {
        backgroundScene_->Draw();
    }
}

void WeaponSelectScene::UpdateSelection(Input *input) {
    int nextIndex = selectedIndex_;

    if (input->IsKeyTrigger(DIK_LEFT) || input->IsKeyTrigger(DIK_A)) {
        nextIndex = (selectedIndex_ + kSelectableCount - 1) % kSelectableCount;
    }
    if (input->IsKeyTrigger(DIK_RIGHT) || input->IsKeyTrigger(DIK_D)) {
        nextIndex = (selectedIndex_ + 1) % kSelectableCount;
    }
    if (input->IsKeyTrigger(DIK_1)) {
        nextIndex = 0;
    }
    if (input->IsKeyTrigger(DIK_2)) {
        nextIndex = 1;
    }
    if (input->IsKeyTrigger(DIK_3)) {
        nextIndex = 2;
    }
    if (input->IsGamepadConnected()) {
        if (input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_DPAD_LEFT)) {
            nextIndex =
                (selectedIndex_ + kSelectableCount - 1) % kSelectableCount;
        }
        if (input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_DPAD_RIGHT)) {
            nextIndex = (selectedIndex_ + 1) % kSelectableCount;
        }
    }

    if (nextIndex != selectedIndex_) {
        selectedIndex_ = nextIndex;
    }

    const bool confirm =
        input->IsKeyTrigger(DIK_RETURN) || input->IsKeyTrigger(DIK_SPACE) ||
        (input->IsGamepadConnected() &&
         input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_A));
    if (confirm) {
        BeginStart();
    }
}

void WeaponSelectScene::UpdateDeviceAvailability() {
    leftJoyCon_.Update(ctx_->frame.deltaTime);
    rightJoyCon_.Update(ctx_->frame.deltaTime);

    joyConAvailable_ =
        leftJoyCon_.IsConnected() || rightJoyCon_.IsConnected();
    cameraAvailable_ = !AppSceneServices::HasCameraDeviceAvailable() ||
                       AppSceneServices::IsCameraDeviceAvailable();
}

void WeaponSelectScene::BeginStart() {
    if (!IsModeAvailable(selectedIndex_)) {
        ShowUnavailableMessage(selectedIndex_);
        return;
    }

    const InputControlType selectedType = SelectedControlType();
    if (selectedType == InputControlType::Hand) {
        RequestHandTrackingStartOnce();
        waitingForHandTrackingReady_ = !IsHandTrackingReady();
    }

    startRequested_ = true;
    transitionTimer_ = 0.0f;
}

InputControlType WeaponSelectScene::SelectedControlType() const {
    return ControlTypeForIndex(selectedIndex_);
}

InputControlType WeaponSelectScene::ControlTypeForIndex(int index) const {
    switch (index) {
    case 2:
        return InputControlType::Hand;
    case 1:
        return InputControlType::JoyCon;
    default:
        return InputControlType::KeyboardMouse;
    }
}

bool WeaponSelectScene::IsHandTrackingReady() const {
    return !AppSceneServices::HasHandTrackingReady() ||
           AppSceneServices::IsHandTrackingReady();
}

void WeaponSelectScene::RequestHandTrackingStartOnce() {
    if (handTrackingStartRequested_ ||
        !AppSceneServices::HasHandTrackingStart()) {
        return;
    }
    AppSceneServices::RequestHandTrackingStart();
    handTrackingStartRequested_ = true;
}

bool WeaponSelectScene::IsModeAvailable(int index) const {
    switch (index) {
    case 1:
        return joyConAvailable_;
    case 2:
        return cameraAvailable_;
    case 0:
    default:
        return true;
    }
}

void WeaponSelectScene::ShowUnavailableMessage(int index) {
    const wchar_t *message =
        index == 1 ? L"Joy-Conがありません" : L"ウェブカメラがありません";
    MessageBoxW(ctx_->systems.winApp->GetHwnd(), message, L"入力モード",
                MB_OK | MB_ICONWARNING | MB_SETFOREGROUND | MB_TOPMOST);
    SetForegroundWindow(ctx_->systems.winApp->GetHwnd());
}
