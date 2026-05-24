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

    float GetAngularVelocity() const { return angularVelocity_; }
    SwordPose GetPose() const;
    void ResetTracking(JoyCon *joyCon);

  private:
    void UpdateOrientation(JoyCon *joyCon, float dt);
    void UpdateSlash(float dt);
    void UpdateSlashDirFromOrientation();

  private:
    DirectX::XMFLOAT4 prevOrientation_{0, 0, 0, 1};
    DirectX::XMFLOAT3 prevTipDirection_{0.0f, 0.0f, 1.0f};
    SwordControllerState state_{};

    float angularVelocity_ = 0.0f;
};
