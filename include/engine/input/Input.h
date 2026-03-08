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
    /// <summary>
    /// 初期化処理
    /// </summary>
    /// <param name="hInstance">アプリケーションのインスタンスハンドル</param>
    /// <param name="hwnd">入力を受け取るウィンドウハンドル</param>
    void Initialize(HINSTANCE hInstance, HWND hwnd);

    /// <summary>
    /// 更新処理
    /// </summary>
    /// <param name="deltaTime">前フレームからの経過時間(秒)</param>
    void Update(float deltaTime);

    /// <summary>
    /// ジャイロのキャリブレーションを開始
    /// </summary>
    void StartCalibration();

    // Setter
    void SetBaseOrientation();

    // Getter
    bool IsKeyPress(int dik) const;
    bool IsKeyTrigger(int dik) const;
    bool IsKeyRelease(int dik) const;

    DirectX::XMVECTOR GetOrientation() const;
    DirectX::XMVECTOR GetRawOrientation() const;

  private:
    // Update
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