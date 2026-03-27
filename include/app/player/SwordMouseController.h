#pragma once
#include "Transform.h"

class Input;
class SwordMouseController {
  public:
    void Update(Input *input, float dt, const Transform &swordPos);

    bool IsActive(Input *input);

    // Getter関数
    float GetMouseSpeed() const { return mouseSpeed_; }
    bool GetIsSlashMode() const { return isSlashMode_; }
    bool GetIsGuard() const { return isGuard_; }
    bool GetCounter() const { return isCounter_; }
    const DirectX::XMFLOAT2 &GetSlashDir() { return slashDir_; }
    const DirectX::XMFLOAT4 &GetOrientation() { return orientation_; }

    // Setter関数
    void SetCounter(bool isCounter) { this->isCounter_ = isCounter; }

  private:
    // メンバ関数
    void UpdateOrientation(Input *input, float dt);
    void UpdateGuard(Input *input);
    void UpdateCounter();
    void UpdateSlash(Input *input, float dt);
    void UpdateSlashDir(const Transform &swordPos);

    // メンバ変数
    DirectX::XMFLOAT4 orientation_{0, 0, 0, 1};
    DirectX::XMFLOAT4 prevOrientation_{0, 0, 0, 1};

    float mouseSpeed_ = 0.0f;
    bool isSlashMode_ = false;
    bool isGuard_ = false;
    bool isCounter_ = false;
    int counterTimer_ = 300;
    float slashTimer_ = 0.0f;
    const float kSlashHold = 720.0f;
    const float kTimeLimit = 1.0f;
    float yaw_ = 0.0f;
    float pitch_ = 0.0f;

    DirectX::XMFLOAT2 prevPos_{};
    DirectX::XMFLOAT2 slashDir_{};
    DirectX::XMFLOAT2 mouseDelta_{};
};
