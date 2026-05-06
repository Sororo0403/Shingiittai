#pragma once
#include "JoyCon.h"
#include "SwordControllerState.h"
#include "SwordPose.h"
#include <DirectXMath.h>
#include <Transform.h>
#include <cstdint>

class SwordJoyConController {
  public:
    void Update(JoyCon *joyCon, float dt, const Transform &swordPos);
    bool IsActive(const JoyCon *joyCon) const;

    float GetAngularVelocity() const { return angularVelocity_; }
    bool GetIsSlashMode() const { return state_.isSlashMode; }
    bool GetIsGuard() const { return state_.isGuard; }
    bool GetCounter() const { return state_.isCounter; }
    const DirectX::XMFLOAT2 &GetSlashDir() { return state_.slashDir; }
    const DirectX::XMFLOAT4 &GetOrientation() { return state_.orientation; }
    SwordPose GetPose() const;

    void SetCounter(bool isCounter) { state_.isCounter = isCounter; }

  private:
    void UpdateOrientation(JoyCon *joyCon, float dt);
    void UpdateGuard(JoyCon *joyCon);
    void UpdateCounterFromGuardMotion();
    void UpdateSlash(float dt);

  private:
    DirectX::XMFLOAT4 prevOrientation_{0, 0, 0, 1};
    SwordControllerState state_{};

    float angularVelocity_ = 0.0f;
    bool prevCounterMotionActive_ = false;
    float counterSwingThreshold_ = 820.0f;
};
