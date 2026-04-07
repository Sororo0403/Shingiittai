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

    for (int i = 0; i < count; ++i) {
        const int handle = handles[i];
        size_t index = kRightJoyConIndex;

        const int type = JslGetControllerType(handle);
        if (type == JS_TYPE_JOYCON_LEFT) {
            index = kLeftJoyConIndex;
        } else if (type == JS_TYPE_JOYCON_RIGHT) {
            index = kRightJoyConIndex;
        } else if (joyCons_[kRightJoyConIndex].handle >= 0) {
            index = kLeftJoyConIndex;
        }

        if (joyCons_[index].handle >= 0) {
            continue;
        }

        joyCons_[index].handle = handle;
        JslSetAutomaticCalibration(handle, false);
    }

    for (auto &joyCon : joyCons_) {
        joyCon.mahony.Initialize(0.4f, 0.0f);
    }

    StartCalibration();
}

void Input::Update(float deltaTime) {
    if (IsKeyTrigger(DIK_C)) {
        StartCalibration();
    }

    if (IsKeyTrigger(DIK_R)) {
        SetBaseOrientation();
    }

    UpdateKeyboard();
    UpdateMouse();
    UpdateJoyShock(deltaTime);
}

void Input::StartCalibration() {
    for (auto &joyCon : joyCons_) {
        joyCon.isCalibrating = true;
        joyCon.stillTimer = 0.0f;

        joyCon.gyroAccum = {0, 0, 0};
        joyCon.gyroOffset = {0, 0, 0};
        joyCon.gyroSampleCount = 0;

        joyCon.mahony.Reset();

        joyCon.orientation = {0, 0, 0, 1};
        joyCon.baseOrientation = {0, 0, 0, 1};
        joyCon.hasBaseOrientation = false;
    }
}

void Input::SetBaseOrientation() {
    for (size_t i = 0; i < kJoyConCount; ++i) {
        if (!IsJoyConConnected(i == kLeftJoyConIndex)) {
            continue;
        }

        XMStoreFloat4(&joyCons_[i].baseOrientation, GetRawOrientation(i == kLeftJoyConIndex));
        joyCons_[i].hasBaseOrientation = true;
    }
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
    for (size_t i = 0; i < kJoyConCount; ++i) {
        UpdateJoyShockState(i, deltaTime);
    }
}

bool Input::IsJsButtunPress(int buttunMask) const {
    return IsJsButtunPress(false, buttunMask);
}

bool Input::IsJsButtunTrigger(int buttunMask) const {
    return IsJsButtunTrigger(false, buttunMask);
}

bool Input::IsJsButtunPress(bool useLeftJoyCon, int buttunMask) const {
    const JoyConState &joyCon = joyCons_[GetJoyConIndex(useLeftJoyCon)];
    return (joyCon.buttonsNow & buttunMask) != 0;
}

bool Input::IsJsButtunTrigger(bool useLeftJoyCon, int buttunMask) const {
    const JoyConState &joyCon = joyCons_[GetJoyConIndex(useLeftJoyCon)];
    return (joyCon.buttonsNow & buttunMask) && !(joyCon.buttonsPrev & buttunMask);
}

bool Input::IsJoyConConnected(bool useLeftJoyCon) const {
    const int handle = joyCons_[GetJoyConIndex(useLeftJoyCon)].handle;
    return handle >= 0 && JslStillConnected(handle);
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
    return GetRawOrientation(false);
}

XMVECTOR Input::GetOrientation() const {
    return GetOrientation(false);
}

XMVECTOR Input::GetRawOrientation(bool useLeftJoyCon) const {
    const JoyConState &joyCon = joyCons_[GetJoyConIndex(useLeftJoyCon)];
    return XMQuaternionNormalize(XMLoadFloat4(&joyCon.orientation));
}

XMVECTOR Input::GetOrientation(bool useLeftJoyCon) const {
    const JoyConState &joyCon = joyCons_[GetJoyConIndex(useLeftJoyCon)];
    XMVECTOR raw = GetRawOrientation(useLeftJoyCon);

    if (!joyCon.hasBaseOrientation) {
        return raw;
    }

    XMVECTOR base = XMLoadFloat4(&joyCon.baseOrientation);
    XMVECTOR invBase = XMQuaternionInverse(base);

    return XMQuaternionNormalize(XMQuaternionMultiply(invBase, raw));
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

void Input::UpdateJoyShockState(size_t index, float deltaTime) {
    JoyConState &joyCon = joyCons_[index];
    if (joyCon.handle < 0 || !JslStillConnected(joyCon.handle)) {
        joyCon.buttonsPrev = joyCon.buttonsNow;
        joyCon.buttonsNow = 0;
        return;
    }

    joyCon.buttonsPrev = joyCon.buttonsNow;
    joyCon.buttonsNow = JslGetButtons(joyCon.handle);

    IMU_STATE imu = JslGetIMUState(joyCon.handle);

    float gx = imu.gyroX;
    float gy = imu.gyroY;
    float gz = -imu.gyroZ;

    float ax = imu.accelX;
    float ay = imu.accelY;
    float az = -imu.accelZ;

    if (joyCon.isCalibrating) {
        float gyroMagSq = gx * gx + gy * gy + gz * gz;

        if (gyroMagSq < kStillGyroThresholdSq) {
            joyCon.stillTimer += deltaTime;

            joyCon.gyroAccum.x += gx;
            joyCon.gyroAccum.y += gy;
            joyCon.gyroAccum.z += gz;
            joyCon.gyroSampleCount++;

            if (joyCon.stillTimer >= kStillTime && joyCon.gyroSampleCount > 0) {
                joyCon.gyroOffset.x = joyCon.gyroAccum.x / joyCon.gyroSampleCount;
                joyCon.gyroOffset.y = joyCon.gyroAccum.y / joyCon.gyroSampleCount;
                joyCon.gyroOffset.z = joyCon.gyroAccum.z / joyCon.gyroSampleCount;
                joyCon.isCalibrating = false;
            }
        } else {
            joyCon.stillTimer = 0.0f;
            joyCon.gyroAccum = {0, 0, 0};
            joyCon.gyroSampleCount = 0;
        }

        return;
    }

    gx -= joyCon.gyroOffset.x;
    gy -= joyCon.gyroOffset.y;
    gz -= joyCon.gyroOffset.z;

    float gyroMagSq = gx * gx + gy * gy + gz * gz;
    if (gyroMagSq < 1.0f) {
        joyCon.gyroOffset.x += gx * kDriftLearnRate;
        joyCon.gyroOffset.y += gy * kDriftLearnRate;
        joyCon.gyroOffset.z += gz * kDriftLearnRate;
    }

    float norm = sqrtf(ax * ax + ay * ay + az * az);
    if (norm > 0.0001f) {
        ax /= norm;
        ay /= norm;
        az /= norm;
    }

    joyCon.mahony.Update(gx, gy, gz, ax, ay, az, deltaTime);

    XMVECTOR q = XMQuaternionNormalize(joyCon.mahony.GetQuaternion());
    XMStoreFloat4(&joyCon.orientation, q);
}
