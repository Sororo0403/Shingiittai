#include "JoyCon.h"
#include <cmath>

using namespace DirectX;

static bool gJoyShockInitialized = false;

void JoyCon::Initialize(bool useLeftJoyCon) {
    useLeftJoyCon_ = useLeftJoyCon;

    if (!gJoyShockInitialized) {
        JslConnectDevices();
        gJoyShockInitialized = true;
    }

    mahony_.Initialize(0.4f, 0.0f);
    TryConnect();
    StartCalibration();
}

void JoyCon::Update(float deltaTime) {
    TryConnect();

    if (handle_ < 0 || !JslStillConnected(handle_)) {
        buttonsPrev_ = buttonsNow_;
        buttonsNow_ = 0;
        return;
    }

    buttonsPrev_ = buttonsNow_;
    buttonsNow_ = JslGetButtons(handle_);

    IMU_STATE imu = JslGetIMUState(handle_);

    float gx = imu.gyroX;
    float gy = imu.gyroY;
    float gz = -imu.gyroZ;

    float ax = imu.accelX;
    float ay = imu.accelY;
    float az = -imu.accelZ;

    if (isCalibrating_) {
        calibrationTimer_ += deltaTime;
        float gyroMagSq = gx * gx + gy * gy + gz * gz;

        if (gyroMagSq < kStillGyroThresholdSq) {
            stillTimer_ += deltaTime;

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

        if (isCalibrating_ && calibrationTimer_ >= kCalibrationTimeout) {
            if (gyroSampleCount_ > 0) {
                gyroOffset_.x = gyroAccum_.x / gyroSampleCount_;
                gyroOffset_.y = gyroAccum_.y / gyroSampleCount_;
                gyroOffset_.z = gyroAccum_.z / gyroSampleCount_;
            }
            isCalibrating_ = false;
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

    float norm = std::sqrt(ax * ax + ay * ay + az * az);
    if (norm > 0.0001f) {
        ax /= norm;
        ay /= norm;
        az /= norm;
    }

    mahony_.Update(gx, gy, gz, ax, ay, az, deltaTime);

    XMVECTOR q = XMQuaternionNormalize(mahony_.GetQuaternion());
    XMStoreFloat4(&orientation_, q);
}

void JoyCon::StartCalibration() {
    isCalibrating_ = true;
    stillTimer_ = 0.0f;
    calibrationTimer_ = 0.0f;

    gyroAccum_ = {0, 0, 0};
    gyroOffset_ = {0, 0, 0};
    gyroSampleCount_ = 0;

    mahony_.Reset();

    orientation_ = {0, 0, 0, 1};
    baseOrientation_ = {0, 0, 0, 1};
    hasBaseOrientation_ = false;
}

void JoyCon::SetBaseOrientation() {
    if (!IsConnected()) {
        return;
    }

    XMStoreFloat4(&baseOrientation_, GetRawOrientation());
    hasBaseOrientation_ = true;
}

bool JoyCon::IsButtonPress(int buttonMask) const {
    return (buttonsNow_ & buttonMask) != 0;
}

bool JoyCon::IsButtonTrigger(int buttonMask) const {
    return (buttonsNow_ & buttonMask) && !(buttonsPrev_ & buttonMask);
}

bool JoyCon::IsConnected() const {
    return handle_ >= 0 && JslStillConnected(handle_);
}

XMVECTOR JoyCon::GetRawOrientation() const {
    return XMQuaternionNormalize(XMLoadFloat4(&orientation_));
}

XMVECTOR JoyCon::GetOrientation() const {
    XMVECTOR raw = GetRawOrientation();

    if (!hasBaseOrientation_) {
        return raw;
    }

    XMVECTOR base = XMLoadFloat4(&baseOrientation_);
    XMVECTOR invBase = XMQuaternionInverse(base);

    return XMQuaternionNormalize(XMQuaternionMultiply(invBase, raw));
}

void JoyCon::TryConnect() {
    if (handle_ >= 0 && JslStillConnected(handle_)) {
        return;
    }

    handle_ = -1;

    int handles[4];
    int count = JslGetConnectedDeviceHandles(handles, 4);

    for (int i = 0; i < count; ++i) {
        const int handle = handles[i];
        const int type = JslGetControllerType(handle);

        if (useLeftJoyCon_ && type != JS_TYPE_JOYCON_LEFT) {
            continue;
        }

        if (!useLeftJoyCon_ && type != JS_TYPE_JOYCON_RIGHT) {
            continue;
        }

        handle_ = handle;
        JslSetAutomaticCalibration(handle_, false);
        return;
    }
}
