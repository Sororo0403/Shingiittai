#pragma once
#define DIRECTINPUT_VERSION 0x0800

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

    // Keyboard
    bool IsKeyPress(int dik) const;
    bool IsKeyTrigger(int dik) const;
    bool IsKeyRelease(int dik) const;

    // Mouse
    LONG GetMouseMoveX() const { return mouseState_.lX; }
    LONG GetMouseMoveY() const { return mouseState_.lY; }
    LONG GetMouseMoveZ() const { return mouseState_.lZ; }

    bool IsMousePress(int button) const;
    bool IsMouseTrigger(int button) const;
    bool IsMouseRelease(int button) const;

    // Gyro
    float GetGyroX() const { return gyroX_; }
    float GetGyroY() const { return gyroY_; }

    // Orientation
    DirectX::XMVECTOR GetOrientation() const;
    void ResetOrientation();

  private:
    static constexpr float degToRad = 3.1415926535f / 180.0f;

    // ★ 強めデッドゾーン
    static constexpr float gyroDeadZone = 0.05f;

    // ★ スムージング
    static constexpr float smoothFactor = 0.2f;

    Microsoft::WRL::ComPtr<IDirectInput8> directInput_;
    Microsoft::WRL::ComPtr<IDirectInputDevice8> keyboard_;
    Microsoft::WRL::ComPtr<IDirectInputDevice8> mouse_;

    std::array<BYTE, 256> keyNow_{};
    std::array<BYTE, 256> keyPrev_{};

    DIMOUSESTATE mouseState_{};
    DIMOUSESTATE mousePrevState_{};

    int jsHandle_ = -1;

    float gyroX_ = 0.0f;
    float gyroY_ = 0.0f;

    float gyroBiasX_ = 0.0f;
    float gyroBiasY_ = 0.0f;
    float gyroBiasZ_ = 0.0f;

    bool biasInitialized_ = false;

    int biasSampleCount_ = 0;
    float biasSumX_ = 0;
    float biasSumY_ = 0;
    float biasSumZ_ = 0;

    DirectX::XMFLOAT4 orientation_{0, 0, 0, 1};
};