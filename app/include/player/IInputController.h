#pragma once
#include "SwordJoyConController.h"
#include "SwordMouseController.h"

// 使用コントローラーのタイプ
enum ControllerType {
    JoyCon,
    Mouse
};

// 状態データ
struct StatusInfo {
    ControllerType ctrlType_ = Mouse;
    bool isSlashMode_ = false;
    bool isGuard_ = false;
    bool isCounter_ = false;
    DirectX::XMFLOAT2 slashDir_{};
    DirectX::XMFLOAT4 orientation_{0, 0, 0, 1};
};

class IInputController {
  public:
    void Update(Input *input, float dt, const Transform &swordPos);

    // Getter関数
    const StatusInfo GetStatusInfo() const { return data_; }

    // Setter関数
    void SetCounter(bool isCounter);

    void ImGuiDraw();

  private:
    // メンバ変数
    SwordJoyConController swordJoyConController_;
    SwordMouseController swordMouseController_;

    StatusInfo data_{};

    // メンバ関数
    const char* CtrlTypeToString(ControllerType type);
};
