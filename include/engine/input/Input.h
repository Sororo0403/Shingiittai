#pragma once
#define DIRECTINPUT_VERSION 0x0800

#include "MahonyFilter.h"
#include <DirectXMath.h>
#include <JoyShockLibrary.h>
#include <Windows.h>
#include <array>
#include <dinput.h>
#include <wrl.h>

class Input {
  public:
    void Initialize(HINSTANCE hInstance, HWND hwnd);
    void Update(float deltaTime);

    void StartCalibration();

    /// 現在の姿勢を基準姿勢として保存
    void SetBaseOrientation();

    bool IsKeyPress(int dik) const;
    bool IsKeyTrigger(int dik) const;
    bool IsKeyRelease(int dik) const;

    /// 基準姿勢込みの相対姿勢
    DirectX::XMVECTOR GetOrientation() const;

    /// Mahonyが内部で持っている生の姿勢
    DirectX::XMVECTOR GetRawOrientation() const;

  private:
    void UpdateKeyboard();
    void UpdateMouse();
    void UpdateJoyShock(float deltaTime);

  private:
    static constexpr BYTE kPressMask = 0x80;
    static constexpr float kCalibrationTime_ = 2.0f;

    // Keyboard
    Microsoft::WRL::ComPtr<IDirectInput8> directInput_;
    Microsoft::WRL::ComPtr<IDirectInputDevice8> keyboard_;
    Microsoft::WRL::ComPtr<IDirectInputDevice8> mouse_;

    std::array<BYTE, 256> keyNow_{};
    std::array<BYTE, 256> keyPrev_{};

    // Mouse
    DIMOUSESTATE mouseState_{};
    DIMOUSESTATE mousePrevState_{};

    // JoyShock
    int jsHandle_ = -1;

    MahonyFilter mahony_;

    DirectX::XMFLOAT4 orientation_{0, 0, 0, 1};
    DirectX::XMFLOAT4 baseOrientation_{0, 0, 0, 1};
    bool hasBaseOrientation_ = false;

    bool isCalibrating_ = true;
    float calibrationTimer_ = 0.0f;

    DirectX::XMFLOAT3 gyroOffset_{0, 0, 0};
    DirectX::XMFLOAT3 gyroAccum_{0, 0, 0};
    int gyroSampleCount_ = 0;
};