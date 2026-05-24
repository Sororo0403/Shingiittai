#pragma once
#include "SwordControllerState.h"
#include "SwordPose.h"
#include "Transform.h"

class Input;
class SwordMouseController {
  public:
    void Update(Input *input, float dt, const Transform &swordPos);

    bool IsActive(Input *input);

    SwordPose GetPose() const;

  private:
    void UpdateOrientation(Input *input, float dt);
    void UpdateSlash(Input *input, float dt);

    SwordControllerState state_{};

    float mouseSpeed_ = 0.0f;
    float yaw_ = 0.0f;
    float pitch_ = 0.0f;

    DirectX::XMFLOAT2 mouseDelta_{};
};
