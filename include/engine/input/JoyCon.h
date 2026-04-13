#pragma once
#include "MahonyFilter.h"
#include <DirectXMath.h>
#include <JoyShockLibrary.h>

#ifndef JSL_BUTTON_ZR
#define JSL_BUTTON_ZR 0x00800
#endif

#ifndef JSL_BUTTON_ZL
#define JSL_BUTTON_ZL 0x00400
#endif

class JoyCon {
  public:
    void Initialize(bool useLeftJoyCon);
    void Update(float deltaTime);

    void StartCalibration();
    void SetBaseOrientation();

    bool IsButtonPress(int buttonMask) const;
    bool IsButtonTrigger(int buttonMask) const;
    bool IsConnected() const;
    bool IsCalibrating() const { return isCalibrating_; }

    DirectX::XMVECTOR GetOrientation() const;
    DirectX::XMVECTOR GetRawOrientation() const;
    float GetStillTimer() const { return stillTimer_; }
    float GetCalibrationTimer() const { return calibrationTimer_; }

    bool IsLeft() const { return useLeftJoyCon_; }

  private:
    void TryConnect();

  private:
    static constexpr float kStillGyroThreshold = 35.0f;
    static constexpr float kStillGyroThresholdSq =
        kStillGyroThreshold * kStillGyroThreshold;
    static constexpr float kStillTime = 0.5f;
    static constexpr float kCalibrationTimeout = 2.0f;
    static constexpr float kDriftLearnRate = 0.0005f;

    int handle_ = -1;
    int buttonsNow_ = 0;
    int buttonsPrev_ = 0;
    bool useLeftJoyCon_ = false;

    MahonyFilter mahony_;

    DirectX::XMFLOAT4 orientation_{0, 0, 0, 1};
    DirectX::XMFLOAT4 baseOrientation_{0, 0, 0, 1};
    bool hasBaseOrientation_ = false;

    bool isCalibrating_ = true;
    float stillTimer_ = 0.0f;
    float calibrationTimer_ = 0.0f;

    DirectX::XMFLOAT3 gyroOffset_{0, 0, 0};
    DirectX::XMFLOAT3 gyroAccum_{0, 0, 0};
    int gyroSampleCount_ = 0;
};
