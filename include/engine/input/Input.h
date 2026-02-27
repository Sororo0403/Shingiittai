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

    bool IsKeyPress(int dik) const;
    bool IsKeyTrigger(int dik) const;
    bool IsKeyRelease(int dik) const;

    LONG GetMouseMoveX() const { return mouseState_.lX; }
    LONG GetMouseMoveY() const { return mouseState_.lY; }
    LONG GetMouseMoveZ() const { return mouseState_.lZ; }

    bool IsMousePress(int button) const;
    bool IsMouseTrigger(int button) const;
    bool IsMouseRelease(int button) const;

    float GetGyroX() const { return gyroX_; }
    float GetGyroY() const { return gyroY_; }

    DirectX::XMVECTOR GetOrientation() const;
    void ResetOrientation();

  private:
    static constexpr float degToRad = 3.1415926535f / 180.0f;

    // 小さい角速度は切る（ゲーム向け強め）
    static constexpr float gyroDeadZone = 0.05f;

    // 角速度の平滑化
    static constexpr float smoothFactor = 0.2f;

    // 姿勢補正ゲイン（強すぎるとビリビリする）
    static constexpr float Kp = 2.5f; // 重力方向への戻し
    static constexpr float Ki =
        0.05f; // バイアス推定（任天堂の“魔法”に近い要素）

    // 加速度が「ほぼ重力だけ」と判断する閾値（振ってる最中は補正を弱める）
    static constexpr float accelTrustMin =
        0.7f; // |a| が 0.7g〜1.3g のときだけ信頼
    static constexpr float accelTrustMax = 1.3f;

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

    // 起動時平均での初期バイアス
    float gyroBiasX_ = 0.0f;
    float gyroBiasY_ = 0.0f;
    float gyroBiasZ_ = 0.0f;
    bool biasInitialized_ = false;

    int biasSampleCount_ = 0;
    float biasSumX_ = 0;
    float biasSumY_ = 0;
    float biasSumZ_ = 0;

    // 姿勢
    DirectX::XMFLOAT4 orientation_{0, 0, 0, 1};
};