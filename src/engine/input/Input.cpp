#include "Input.h"
#include <cassert>

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

    // JoyShock
    JslConnectDevices();
    int handles[4];
    int count = JslGetConnectedDeviceHandles(handles, 4);

    if (count > 0) {
        jsHandle_ = handles[0];
        JslSetAutomaticCalibration(jsHandle_, false);
    }

    StartCalibration();
}

void Input::Update(float deltaTime) {
    UpdateKeyboard();
    UpdateMouse();
    UpdateJoyShock(deltaTime);
}

void Input::StartCalibration() {
    isCalibrating_ = true;
    calibrationTimer_ = 0.0f;
    gyroAccum_ = {0, 0, 0};
    gyroOffset_ = {0, 0, 0};
    gyroSampleCount_ = 0;
}

void Input::UpdateKeyboard() {
    keyPrev_ = keyNow_;
    keyboard_->GetDeviceState(256, keyNow_.data());
}

void Input::UpdateMouse() {
    mousePrevState_ = mouseState_;
    mouse_->GetDeviceState(sizeof(DIMOUSESTATE), &mouseState_);
}

void Input::UpdateJoyShock(float deltaTime) {
    if (jsHandle_ < 0 || !JslStillConnected(jsHandle_))
        return;

    float gx = 0.0f;
    float gy = 0.0f;
    float gz = 0.0f;

    JslGetAndFlushAccumulatedGyro(jsHandle_, gx, gy, gz);

    if (isCalibrating_) {
        calibrationTimer_ += deltaTime;

        gyroAccum_.x += gx;
        gyroAccum_.y += gy;
        gyroAccum_.z += gz;
        gyroSampleCount_++;

        if (calibrationTimer_ >= calibrationTime_) {
            gyroOffset_.x = gyroAccum_.x / gyroSampleCount_;
            gyroOffset_.y = gyroAccum_.y / gyroSampleCount_;
            gyroOffset_.z = gyroAccum_.z / gyroSampleCount_;
            isCalibrating_ = false;
        }

        return;
    }

    gx -= gyroOffset_.x;
    gy -= gyroOffset_.y;
    gz -= gyroOffset_.z;

    float radX = XMConvertToRadians(gx) * deltaTime;
    float radY = XMConvertToRadians(gy) * deltaTime;
    float radZ = XMConvertToRadians(-gz) * deltaTime;

    XMVECTOR delta = XMQuaternionRotationRollPitchYaw(radX, radY, radZ);
    XMVECTOR current = XMLoadFloat4(&orientation_);

    current = XMQuaternionMultiply(current, delta);
    current = XMQuaternionNormalize(current);

    XMStoreFloat4(&orientation_, current);
}

bool Input::IsKeyPress(int dik) const {
    return (keyNow_[dik] & kPressMask) != 0;
}

bool Input::IsKeyTrigger(int dik) const {
    return (keyNow_[dik] & kPressMask) && !(keyPrev_[dik] & kPressMask);
}

bool Input::IsKeyRelease(int dik) const {
    return !(keyNow_[dik] & kPressMask) && (keyPrev_[dik] & kPressMask);
}

XMVECTOR Input::GetOrientation() const { return XMLoadFloat4(&orientation_); }