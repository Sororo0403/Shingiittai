#pragma once
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
    /// <param name="hwnd">メインウィンドウのハンドル</param>
    void Initialize(HINSTANCE hInstance, HWND hwnd);

    /// <summary>
    /// 更新処理
    /// </summary>
    void Update();

    // キー入力判定
    bool IsPress(int dik);
    bool IsTrigger(int dik);
    bool IsRelease(int dik);

  private:
    Microsoft::WRL::ComPtr<IDirectInput8> directInput_;
    Microsoft::WRL::ComPtr<IDirectInputDevice8> keyboard_;

    std::array<BYTE, 256> now_;
    std::array<BYTE, 256> prev_;
};
