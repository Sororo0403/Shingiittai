#include "IInputController.h"
#include "imgui.h"

void IInputController::Update(
    Input* input, 
    float dt,
    const Transform& swordPos) {
    // ジョイコンがアクティブか
    if (swordJoyConController_.IsActive(input)) {
        data_.ctrlType_ = JoyCon;
    }

    // マウスがアクティブか
    if (swordMouseController_.IsActive(input)) {
        data_.ctrlType_ = Mouse;
    }

    // コントローラーの更新処理
    if (data_.ctrlType_ == JoyCon) {
        swordJoyConController_.Update(input, dt, swordPos);
        data_.isCounter_ = swordJoyConController_.GetCounter();
        data_.isGuard_ = swordJoyConController_.GetIsGuard();
        data_.isSlashMode_ = swordJoyConController_.GetIsSlashMode();
        data_.slashDir_ = swordJoyConController_.GetSlashDir();
        data_.orientation_ = swordJoyConController_.GetOrientation();
    } else if (data_.ctrlType_ == Mouse) {
        swordMouseController_.Update(input, dt, swordPos);
        data_.isCounter_ = swordMouseController_.GetCounter();
        data_.isGuard_ = swordMouseController_.GetIsGuard();
        data_.isSlashMode_ = swordMouseController_.GetIsSlashMode();
        data_.slashDir_ = swordMouseController_.GetSlashDir();
        data_.orientation_ = swordMouseController_.GetOrientation();
    }
}

void IInputController::SetCounter(bool isCounter) { 
    if (data_.ctrlType_ == JoyCon) {
        swordJoyConController_.SetCounter(isCounter);
    } else if (data_.ctrlType_ == Mouse) {
        swordMouseController_.SetCounter(isCounter);
    }
}

void IInputController::ImGuiDraw() {
    ImGui::Text("isSlashMode_: %s", data_.isSlashMode_ ? "true" : "false");
    ImGui::Text("isGuard_: %s", data_.isGuard_ ? "true" : "false");
    ImGui::Text("counter_: %s", data_.isCounter_ ? "true" : "false");
    ImGui::Text("ctrlType_: %s", CtrlTypeToString(data_.ctrlType_));
    ImGui::Text("slashDir_: %.1f, %.1f", data_.slashDir_.x, data_.slashDir_.y);
}

const char* IInputController::CtrlTypeToString(ControllerType type) {
    switch (type) {
    case ControllerType::JoyCon:
        return "JoyCon";
    case ControllerType::Mouse:
        return "Mouse";
    default:
        return "Unknown";
    }
}
