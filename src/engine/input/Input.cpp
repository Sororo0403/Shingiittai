#include "Input.h"
#include <cassert>

void Input::Initialize(HINSTANCE hInstance, HWND hwnd) {
    HRESULT hr;

    hr = DirectInput8Create(
        hInstance, DIRECTINPUT_VERSION, IID_IDirectInput8,
        reinterpret_cast<void **>(directInput_.GetAddressOf()), nullptr);
    assert(SUCCEEDED(hr));

    hr = directInput_->CreateDevice(GUID_SysKeyboard, keyboard_.GetAddressOf(),
                                    nullptr);
    assert(SUCCEEDED(hr));

    hr = keyboard_->SetDataFormat(&c_dfDIKeyboard);
    assert(SUCCEEDED(hr));

    hr = keyboard_->SetCooperativeLevel(hwnd,
                                        DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
    assert(SUCCEEDED(hr));

    keyboard_->Acquire();

    now_.fill(0);
    prev_.fill(0);
}

void Input::Update() {
    prev_ = now_;

    HRESULT hr =
        keyboard_->GetDeviceState(static_cast<DWORD>(now_.size()), now_.data());

    if (FAILED(hr)) {
        keyboard_->Acquire();
        keyboard_->GetDeviceState(static_cast<DWORD>(now_.size()), now_.data());
    }
}

bool Input::IsPress(int dik) { return (now_[dik] & 0x80) != 0; }

bool Input::IsTrigger(int dik) {
    return (now_[dik] & 0x80) && !(prev_[dik] & 0x80);
}

bool Input::IsRelease(int dik) {
    return !(now_[dik] & 0x80) && (prev_[dik] & 0x80);
}
