#include "MahonyFilter.h"
#include <cmath>

using namespace DirectX;

void MahonyFilter::Initialize(float kp, float ki) {
    kp_ = kp;
    ki_ = ki;
    Reset();
}

void MahonyFilter::Reset() {
    q_ = {0.0f, 0.0f, 0.0f, 1.0f};
    integralError_ = {0.0f, 0.0f, 0.0f};
}

void MahonyFilter::Update(float gxDeg, float gyDeg, float gzDeg, float ax,
                          float ay, float az, float dt) {
    if (dt <= 0.0f) {
        return;
    }

    XMVECTOR q = XMLoadFloat4(&q_);

    // 加速度の長さチェック
    float accNorm = std::sqrt(ax * ax + ay * ay + az * az);
    bool useAccel = accNorm > 1e-6f;

    // gyro [deg/s] -> [rad/s]
    float gx = XMConvertToRadians(gxDeg);
    float gy = XMConvertToRadians(gyDeg);
    float gz = XMConvertToRadians(gzDeg);

    if (useAccel) {
        ax /= accNorm;
        ay /= accNorm;
        az /= accNorm;

        // 現在のqから「機体ローカルで見た重力方向」を求める
        // world重力 = (0, 0, -1) とする
        XMVECTOR gravityWorld = XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);

        XMVECTOR qConj = XMQuaternionConjugate(q);
        XMVECTOR gravityBodyQ =
            XMQuaternionMultiply(XMQuaternionMultiply(qConj, gravityWorld), q);

        XMFLOAT4 gBody;
        XMStoreFloat4(&gBody, gravityBodyQ);

        float vx = gBody.x;
        float vy = gBody.y;
        float vz = gBody.z;

        // 測定重力(acc) と 推定重力(v) の外積
        // acc は静止時にほぼ重力方向を向く前提
        float ex = (ay * vz - az * vy);
        float ey = (az * vx - ax * vz);
        float ez = (ax * vy - ay * vx);

        // 積分項
        integralError_.x += ex * dt;
        integralError_.y += ey * dt;
        integralError_.z += ez * dt;

        // PI補正
        gx += kp_ * ex + ki_ * integralError_.x;
        gy += kp_ * ey + ki_ * integralError_.y;
        gz += kp_ * ez + ki_ * integralError_.z;
    }

    // q_dot = 0.5 * q * omega
    XMVECTOR omega = XMVectorSet(gx, gy, gz, 0.0f);
    XMVECTOR qDot = XMQuaternionMultiply(q, omega);
    qDot = XMVectorScale(qDot, 0.5f);

    q = XMVectorAdd(q, XMVectorScale(qDot, dt));
    q = XMQuaternionNormalize(q);

    XMStoreFloat4(&q_, q);
}

XMVECTOR MahonyFilter::GetQuaternion() const {
    return XMQuaternionNormalize(XMLoadFloat4(&q_));
}