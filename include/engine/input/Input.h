#pragma once
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
    /// <param name="hwnd">入力を取得するウィンドウのハンドル</param>
    void Initialize(HINSTANCE hInstance, HWND hwnd);

    /// <summary>
    /// 更新処理
    /// </summary>
    void Update();

    // KeyBoard
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

  private:
    static constexpr float degToRad = 3.1415926535f / 180.0f;

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
};