#include "Input.h"
#include <cassert>
#include <cmath>

void Input::Initialize(HINSTANCE hInstance, HWND hwnd) {
    HRESULT hr;

    // DirectInput 初期化
    hr = DirectInput8Create(
        hInstance, DIRECTINPUT_VERSION, IID_IDirectInput8,
        reinterpret_cast<void **>(directInput_.GetAddressOf()), nullptr);
    assert(SUCCEEDED(hr));

    // Keyboard
    hr = directInput_->CreateDevice(GUID_SysKeyboard, keyboard_.GetAddressOf(),
                                    nullptr);
    assert(SUCCEEDED(hr));

    hr = keyboard_->SetDataFormat(&c_dfDIKeyboard);
    assert(SUCCEEDED(hr));

    hr = keyboard_->SetCooperativeLevel(hwnd,
                                        DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
    assert(SUCCEEDED(hr));

    keyboard_->Acquire();

    // Mouse
    hr = directInput_->CreateDevice(GUID_SysMouse, mouse_.GetAddressOf(),
                                    nullptr);
    assert(SUCCEEDED(hr));

    hr = mouse_->SetDataFormat(&c_dfDIMouse);
    assert(SUCCEEDED(hr));

    hr = mouse_->SetCooperativeLevel(hwnd,
                                     DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
    assert(SUCCEEDED(hr));

    mouse_->Acquire();

    keyNow_.fill(0);
    keyPrev_.fill(0);

    // JoyShock 初期化
    JslConnectDevices();

    int handles[4];
    int count = JslGetConnectedDeviceHandles(handles, 4);

    if (count > 0) {
        jsHandle_ = handles[0];
    }
}

void Input::Update() {
    keyPrev_ = keyNow_;
    mousePrevState_ = mouseState_;

    // Keyboard
    HRESULT hr = keyboard_->GetDeviceState(static_cast<DWORD>(keyNow_.size()),
                                           keyNow_.data());

    if (FAILED(hr)) {
        keyboard_->Acquire();
        keyboard_->GetDeviceState(static_cast<DWORD>(keyNow_.size()),
                                  keyNow_.data());
    }

    // Mouse
    hr = mouse_->GetDeviceState(sizeof(DIMOUSESTATE), &mouseState_);

    if (FAILED(hr)) {
        mouse_->Acquire();
        mouse_->GetDeviceState(sizeof(DIMOUSESTATE), &mouseState_);
    }

    // JoyShock 再接続チェック
    if (jsHandle_ == -1) {
        JslConnectDevices();
        int handles[4];
        int count = JslGetConnectedDeviceHandles(handles, 4);
        if (count > 0) {
            jsHandle_ = handles[0];
        }
    }

    // Gyro 更新
    if (jsHandle_ != -1) {
        IMU_STATE imu = JslGetIMUState(jsHandle_);

        gyroX_ = imu.gyroX * degToRad;
        gyroY_ = imu.gyroY * degToRad;
    } else {
        gyroX_ = 0.0f;
        gyroY_ = 0.0f;
    }
}

bool Input::IsKeyPress(int dik) const { return (keyNow_[dik] & 0x80) != 0; }

bool Input::IsKeyTrigger(int dik) const {
    return (keyNow_[dik] & 0x80) && !(keyPrev_[dik] & 0x80);
}

bool Input::IsKeyRelease(int dik) const {
    return !(keyNow_[dik] & 0x80) && (keyPrev_[dik] & 0x80);
}

bool Input::IsMousePress(int button) const {
    return (mouseState_.rgbButtons[button] & 0x80) != 0;
}

bool Input::IsMouseTrigger(int button) const {
    return (mouseState_.rgbButtons[button] & 0x80) &&
           !(mousePrevState_.rgbButtons[button] & 0x80);
}

bool Input::IsMouseRelease(int button) const {
    return !(mouseState_.rgbButtons[button] & 0x80) &&
           (mousePrevState_.rgbButtons[button] & 0x80);
}