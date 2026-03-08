#include "Input.h"
#include <algorithm>
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

    // JoyShock
    JslConnectDevices();

    int handles[4];
    int count = JslGetConnectedDeviceHandles(handles, 4);

    if (count > 0) {
        jsHandle_ = handles[0];
        JslSetAutomaticCalibration(jsHandle_, false);
    }

    mahony_.Initialize(0.4f, 0.0f);

    StartCalibration();
}

void Input::Update(float deltaTime) {
    UpdateKeyboard();
    UpdateMouse();
    UpdateJoyShock(deltaTime);
}

void Input::StartCalibration() {
    isCalibrating_ = true;
    stillTimer_ = 0.0f;

    gyroAccum_ = {0, 0, 0};
    gyroOffset_ = {0, 0, 0};
    gyroSampleCount_ = 0;

    mahony_.Reset();

    orientation_ = {0, 0, 0, 1};
    baseOrientation_ = {0, 0, 0, 1};
    hasBaseOrientation_ = false;
}

void Input::SetBaseOrientation() {
    XMStoreFloat4(&baseOrientation_, GetRawOrientation());
    hasBaseOrientation_ = true;
}

void Input::UpdateKeyboard() {
    keyPrev_ = keyNow_;

    HRESULT hr = keyboard_->GetDeviceState(256, keyNow_.data());

    if (FAILED(hr)) {
        hr = keyboard_->Acquire();

        if (SUCCEEDED(hr)) {
            keyboard_->GetDeviceState(256, keyNow_.data());
        }
    }
}

void Input::UpdateMouse() {
    mousePrevState_ = mouseState_;

    HRESULT hr = mouse_->GetDeviceState(sizeof(DIMOUSESTATE), &mouseState_);

    if (FAILED(hr)) {
        hr = mouse_->Acquire();

        if (SUCCEEDED(hr)) {
            mouse_->GetDeviceState(sizeof(DIMOUSESTATE), &mouseState_);
        }
    }
}

void Input::UpdateJoyShock(float dt) {
    if (dt <= 0.0f) {
        return;
    }
    if (jsHandle_ < 0 || !JslStillConnected(jsHandle_)) {
        return;
    }

    IMU_STATE imu = JslGetIMUState(jsHandle_);

    float gx = imu.gyroX;
    float gy = imu.gyroY;
    float gz = -imu.gyroZ;

    float ax = imu.accelX;
    float ay = imu.accelY;
    float az = -imu.accelZ;

    if (isCalibrating_) {
        float gyroMagSq = gx * gx + gy * gy + gz * gz;

        if (gyroMagSq < kStillGyroThresholdSq) {
            stillTimer_ += dt;

            gyroAccum_.x += gx;
            gyroAccum_.y += gy;
            gyroAccum_.z += gz;
            gyroSampleCount_++;

            if (stillTimer_ >= kStillTime && gyroSampleCount_ > 0) {
                gyroOffset_.x = gyroAccum_.x / gyroSampleCount_;
                gyroOffset_.y = gyroAccum_.y / gyroSampleCount_;
                gyroOffset_.z = gyroAccum_.z / gyroSampleCount_;

                isCalibrating_ = false;
            }

        } else {
            stillTimer_ = 0.0f;
            gyroAccum_ = {0, 0, 0};
            gyroSampleCount_ = 0;
        }

        return;
    }

    gx -= gyroOffset_.x;
    gy -= gyroOffset_.y;
    gz -= gyroOffset_.z;

    float gyroMagSq = gx * gx + gy * gy + gz * gz;
    if (gyroMagSq < 1.0f) {
        gyroOffset_.x += gx * kDriftLearnRate;
        gyroOffset_.y += gy * kDriftLearnRate;
        gyroOffset_.z += gz * kDriftLearnRate;
    }

    float norm = sqrtf(ax * ax + ay * ay + az * az);
    if (norm > 0.0001f) {
        ax /= norm;
        ay /= norm;
        az /= norm;
    }

    mahony_.Update(gx, gy, gz, ax, ay, az, dt);

    XMVECTOR q = XMQuaternionNormalize(mahony_.GetQuaternion());
    XMStoreFloat4(&orientation_, q);
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

XMVECTOR Input::GetRawOrientation() const {
    return XMQuaternionNormalize(XMLoadFloat4(&orientation_));
}

XMVECTOR Input::GetOrientation() const {
    XMVECTOR raw = GetRawOrientation();

    if (!hasBaseOrientation_) {
        return raw;
    }

    XMVECTOR base = XMLoadFloat4(&baseOrientation_);
    XMVECTOR invBase = XMQuaternionInverse(base);

    return XMQuaternionNormalize(XMQuaternionMultiply(invBase, raw));
}