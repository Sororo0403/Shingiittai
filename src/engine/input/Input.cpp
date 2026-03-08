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

    mahony_.Initialize(2.0f, 0.05f);
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

void Input::UpdateJoyShock(float deltaTime) {
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
        calibrationTimer_ += deltaTime;

        gyroAccum_.x += gx;
        gyroAccum_.y += gy;
        gyroAccum_.z += gz;
        gyroSampleCount_++;

        if (calibrationTimer_ >= kCalibrationTime_ && gyroSampleCount_ > 0) {
            gyroOffset_.x = gyroAccum_.x / static_cast<float>(gyroSampleCount_);
            gyroOffset_.y = gyroAccum_.y / static_cast<float>(gyroSampleCount_);
            gyroOffset_.z = gyroAccum_.z / static_cast<float>(gyroSampleCount_);
            isCalibrating_ = false;

            // キャリブ完了時点の縦持ちを基準姿勢にしたいならここで保存
            SetBaseOrientation();
        }
        return;
    }

    // バイアス除去
    gx -= gyroOffset_.x;
    gy -= gyroOffset_.y;
    gz -= gyroOffset_.z;

    // JoyShockLibrary v2以降は gyro/accel 軸系が整理されている。
    // まずはそのまま Mahony に渡して基準姿勢で吸収する。
    // もしゲーム空間で前後左右が合わなければ、
    // ここではなく「モデル補正用クォータニオン」で合わせるのが安全。
    mahony_.Update(gx, gy, gz, ax, ay, az, deltaTime);

    XMVECTOR q = mahony_.GetQuaternion();
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

    // 基準姿勢からの相対回転
    return XMQuaternionNormalize(XMQuaternionMultiply(invBase, raw));
}