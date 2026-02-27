#include "Input.h"
#include <cassert>
#include <cmath>

using namespace DirectX;

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

    hr = directInput_->CreateDevice(GUID_SysMouse, mouse_.GetAddressOf(),
                                    nullptr);
    assert(SUCCEEDED(hr));
    hr = mouse_->SetDataFormat(&c_dfDIMouse);
    assert(SUCCEEDED(hr));
    hr = mouse_->SetCooperativeLevel(hwnd,
                                     DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
    assert(SUCCEEDED(hr));
    mouse_->Acquire();

    JslConnectDevices();

    int handles[4];
    int count = JslGetConnectedDeviceHandles(handles, 4);
    if (count > 0) {
        jsHandle_ = handles[0];
    }
}

void Input::Update(float deltaTime) {

    keyPrev_ = keyNow_;
    mousePrevState_ = mouseState_;

    keyboard_->GetDeviceState(static_cast<DWORD>(keyNow_.size()),
                              keyNow_.data());
    mouse_->GetDeviceState(sizeof(DIMOUSESTATE), &mouseState_);

    if (jsHandle_ == -1) {
        JslConnectDevices();
        int handles[4];
        int count = JslGetConnectedDeviceHandles(handles, 4);
        if (count > 0) {
            jsHandle_ = handles[0];
            biasInitialized_ = false;
        }
    }

    if (jsHandle_ == -1)
        return;

    IMU_STATE imu = JslGetIMUState(jsHandle_);

    float rawX = -imu.gyroX * degToRad; // ← マイナス
    float rawY = -imu.gyroY * degToRad; // ← マイナス
    float rawZ = imu.gyroZ * degToRad;  // Zは様子見

    // ===== バイアス初期化（2秒間平均）
    if (!biasInitialized_) {

        biasSumX_ += rawX;
        biasSumY_ += rawY;
        biasSumZ_ += rawZ;
        biasSampleCount_++;

        if (biasSampleCount_ > 120) {
            gyroBiasX_ = biasSumX_ / biasSampleCount_;
            gyroBiasY_ = biasSumY_ / biasSampleCount_;
            gyroBiasZ_ = biasSumZ_ / biasSampleCount_;
            biasInitialized_ = true;
        }

        return;
    }

    rawX -= gyroBiasX_;
    rawY -= gyroBiasY_;
    rawZ -= gyroBiasZ_;

    // ===== デッドゾーン
    if (fabs(rawX) < gyroDeadZone)
        rawX = 0.0f;
    if (fabs(rawY) < gyroDeadZone)
        rawY = 0.0f;
    if (fabs(rawZ) < gyroDeadZone)
        rawZ = 0.0f;

    // ===== スムージング
    gyroX_ = gyroX_ * (1.0f - smoothFactor) + rawX * smoothFactor;
    gyroY_ = gyroY_ * (1.0f - smoothFactor) + rawY * smoothFactor;

    // ===== 姿勢更新
    XMVECTOR q = XMLoadFloat4(&orientation_);

    XMVECTOR omega = XMVectorSet(gyroX_, gyroY_, rawZ, 0.0f);
    float angle = XMVectorGetX(XMVector3Length(omega));

    if (angle > 0.0f) {
        float theta = angle * deltaTime;
        XMVECTOR axis = XMVector3Normalize(omega);
        XMVECTOR dq = XMQuaternionRotationAxis(axis, theta);

        q = XMQuaternionMultiply(dq, q);
        q = XMQuaternionNormalize(q);
    }

    // ===== 微ドリフト減衰
    orientation_.x *= 0.9995f;
    orientation_.y *= 0.9995f;
    orientation_.z *= 0.9995f;

    XMStoreFloat4(&orientation_, q);
}

XMVECTOR Input::GetOrientation() const { return XMLoadFloat4(&orientation_); }

void Input::ResetOrientation() {
    orientation_ = {0, 0, 0, 1};
    biasInitialized_ = false;
    biasSampleCount_ = 0;
    biasSumX_ = biasSumY_ = biasSumZ_ = 0;
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