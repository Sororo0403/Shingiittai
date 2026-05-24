#pragma once
#include <DirectXMath.h>

class MahonyFilter {
  public:
    void Initialize(float kp = 2.0f, float ki = 0.0f);
    void Update(float gx, float gy, float gz, float ax, float ay, float az,
                float deltaTime);
    void Reset();

    DirectX::XMVECTOR GetQuaternion() const;

  private:
    float kp_ = 2.0f;
    float ki_ = 0.0f;

    DirectX::XMFLOAT4 q_ = {0.0f, 0.0f, 0.0f, 1.0f};
    DirectX::XMFLOAT3 integralError_ = {0.0f, 0.0f, 0.0f};
};
