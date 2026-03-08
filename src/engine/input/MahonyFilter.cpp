#include "MahonyFilter.h"
#include <cmath>

using namespace DirectX;

void MahonyFilter::Initialize(float kp, float ki) {
    kp_ = kp;
    ki_ = ki;

    Reset();
}

void MahonyFilter::Update(float gx, float gy, float gz, float ax, float ay,
                          float az, float deltaTime) {
    if (deltaTime <= 0.0f) {
        return;
    }

    XMVECTOR q = XMLoadFloat4(&q_);

    // 加速度の長さチェック
    float accNorm = std::sqrt(ax * ax + ay * ay + az * az);
    bool useAccel = accNorm > 1e-6f;

    // gyro
    float gxRad = XMConvertToRadians(gx);
    float gyRad = XMConvertToRadians(gy);
    float gzRad = XMConvertToRadians(gz);

    if (useAccel) {
        ax /= accNorm;
        ay /= accNorm;
        az /= accNorm;

        XMVECTOR gravityWorld = XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);

        XMVECTOR qConj = XMQuaternionConjugate(q);
        XMVECTOR gravityBodyQ =
            XMQuaternionMultiply(XMQuaternionMultiply(qConj, gravityWorld), q);

        XMFLOAT4 gBody;
        XMStoreFloat4(&gBody, gravityBodyQ);

        float vx = gBody.x;
        float vy = gBody.y;
        float vz = gBody.z;

        float ex = (ay * vz - az * vy);
        float ey = (az * vx - ax * vz);
        float ez = (ax * vy - ay * vx);

        // 積分項
        integralError_.x += ex * deltaTime;
        integralError_.y += ey * deltaTime;
        integralError_.z += ez * deltaTime;

        // PI補正
        gxRad += kp_ * ex + ki_ * integralError_.x;
        gyRad += kp_ * ey + ki_ * integralError_.y;
        gzRad += kp_ * ez + ki_ * integralError_.z;
    }

    XMVECTOR omega = XMVectorSet(gxRad, gyRad, gzRad, 0.0f);
    XMVECTOR qDot = XMQuaternionMultiply(q, omega);
    qDot = XMVectorScale(qDot, 0.5f);

    q = XMVectorAdd(q, XMVectorScale(qDot, deltaTime));
    q = XMQuaternionNormalize(q);

    XMStoreFloat4(&q_, q);
}

void MahonyFilter::Reset() {
    q_ = {0.0f, 0.0f, 0.0f, 1.0f};
    integralError_ = {0.0f, 0.0f, 0.0f};
}

XMVECTOR MahonyFilter::GetQuaternion() const {
    return XMQuaternionNormalize(XMLoadFloat4(&q_));
}