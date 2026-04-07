#pragma once
#define DIRECTINPUT_VERSION 0x0800
#include "MahonyFilter.h"
#include <DirectXMath.h>
#include <JoyShockLibrary.h>
#include <Windows.h>
#include <array>
#include <dinput.h>
#include <wrl.h>

#ifndef JSL_BUTTON_ZR
#define JSL_BUTTON_ZR 0x00800
#endif

#ifndef JSL_BUTTON_ZL
#define JSL_BUTTON_ZL 0x00400
#endif

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
    bool IsJsButtunPress(int buttunMask) const;
    bool IsJsButtunTrigger(int buttunMask) const;
    bool IsJsButtunPress(bool useLeftJoyCon, int buttunMask) const;
    bool IsJsButtunTrigger(bool useLeftJoyCon, int buttunMask) const;

    bool IsJoyConConnected(bool useLeftJoyCon) const;

    DirectX::XMVECTOR GetOrientation() const;
    DirectX::XMVECTOR GetRawOrientation() const;
    DirectX::XMVECTOR GetOrientation(bool useLeftJoyCon) const;
    DirectX::XMVECTOR GetRawOrientation(bool useLeftJoyCon) const;

    long GetMouseDX() const { return mouseState_.lX; }
    long GetMouseDY() const { return mouseState_.lY; }
    long GetMouseWheel() const { return mouseState_.lZ; }

    bool IsMousePress(int button) const;
    bool IsMouseTrigger(int button) const;
    bool IsMouseRelease(int button) const;

  private:
    // Update
    void UpdateKeyboard();
    void UpdateMouse();
    void UpdateJoyShock(float deltaTime);
    void UpdateJoyShockState(size_t index, float deltaTime);
    static constexpr size_t GetJoyConIndex(bool useLeftJoyCon) {
        return useLeftJoyCon ? 0u : 1u;
    }

    struct JoyConState {
        int handle = -1;
        int buttonsNow = 0;
        int buttonsPrev = 0;

        MahonyFilter mahony;

        DirectX::XMFLOAT4 orientation{0, 0, 0, 1};
        DirectX::XMFLOAT4 baseOrientation{0, 0, 0, 1};
        bool hasBaseOrientation = false;

        bool isCalibrating = true;
        float stillTimer = 0.0f;

        DirectX::XMFLOAT3 gyroOffset{0, 0, 0};
        DirectX::XMFLOAT3 gyroAccum{0, 0, 0};
        int gyroSampleCount = 0;
    };

  private:
    static constexpr BYTE kPressMask = 0x80;
    static constexpr size_t kJoyConCount = 2;
    static constexpr size_t kLeftJoyConIndex = 0;
    static constexpr size_t kRightJoyConIndex = 1;

    static constexpr float kStillGyroThreshold = 20.0f;
    static constexpr float kStillGyroThresholdSq =
        kStillGyroThreshold * kStillGyroThreshold;
    static constexpr float kStillTime = 0.5f;
    static constexpr float kDriftLearnRate = 0.0005f;

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
    std::array<JoyConState, kJoyConCount> joyCons_{};
};
