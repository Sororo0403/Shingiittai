#pragma once
#include "SwordControllerState.h"
#include "SwordPose.h"
#include "Transform.h"

class Input;
class SwordMouseController {
  public:
    void Update(Input *input, float dt, const Transform &swordPos);

    bool IsActive(Input *input);

    float GetMouseSpeed() const { return mouseSpeed_; }
    bool GetIsSlashMode() const { return state_.isSlashMode; }
    bool GetIsGuard() const { return state_.isGuard; }
    bool GetCounter() const { return state_.isCounter; }
    const DirectX::XMFLOAT2 &GetSlashDir() { return state_.slashDir; }
    const DirectX::XMFLOAT4 &GetOrientation() { return state_.orientation; }
    SwordPose GetPose() const;

    void SetCounter(bool isCounter) { state_.isCounter = isCounter; }

  private:
    void UpdateOrientation(Input *input, float dt);
    void UpdateGuard(Input *input);
    void UpdateCounterFromGuardMotion();
    void UpdateSlash(Input *input, float dt);

    SwordControllerState state_{};

    float mouseSpeed_ = 0.0f;
    float yaw_ = 0.0f;
    float pitch_ = 0.0f;
    bool prevCounterMotionActive_ = false;
    float counterSwingThreshold_ = 820.0f;

    DirectX::XMFLOAT2 mouseDelta_{};
};
