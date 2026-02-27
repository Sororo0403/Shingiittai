#include "Input.h"
#include <cassert>
#include <cmath>

using namespace DirectX;

static inline float Clamp01(float x) {
    return (x < 0.0f) ? 0.0f : (x > 1.0f ? 1.0f : x);
}

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
    if (count > 0)
        jsHandle_ = handles[0];
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
            biasSampleCount_ = 0;
            biasSumX_ = biasSumY_ = biasSumZ_ = 0.0f;
        }
    }
    if (jsHandle_ == -1)
        return;

    IMU_STATE imu = JslGetIMUState(jsHandle_);

    // ★ 方向が逆だったので符号はあなたの現状に合わせて維持
    float gx = -imu.gyroX * degToRad;
    float gy = -imu.gyroY * degToRad;
    float gz = imu.gyroZ * degToRad;

    // 加速度（JoyShockLibraryで取れる環境なら使う）
    float ax = imu.accelX;
    float ay = imu.accelY;
    float az = imu.accelZ;

    // ===== 起動時の初期バイアス（2秒平均）
    if (!biasInitialized_) {
        biasSumX_ += gx;
        biasSumY_ += gy;
        biasSumZ_ += gz;
        biasSampleCount_++;
        if (biasSampleCount_ > 120) {
            gyroBiasX_ = biasSumX_ / biasSampleCount_;
            gyroBiasY_ = biasSumY_ / biasSampleCount_;
            gyroBiasZ_ = biasSumZ_ / biasSampleCount_;
            biasInitialized_ = true;
        }
        return;
    }

    // 初期バイアス除去
    gx -= gyroBiasX_;
    gy -= gyroBiasY_;
    gz -= gyroBiasZ_;

    // ===== デッドゾーン
    if (fabs(gx) < gyroDeadZone)
        gx = 0.0f;
    if (fabs(gy) < gyroDeadZone)
        gy = 0.0f;
    if (fabs(gz) < gyroDeadZone)
        gz = 0.0f;

    // ===== スムージング（X/Yだけ外に出してる用途向け）
    gyroX_ = gyroX_ * (1.0f - smoothFactor) + gx * smoothFactor;
    gyroY_ = gyroY_ * (1.0f - smoothFactor) + gy * smoothFactor;

    // ===== Mahony風：重力で姿勢を“引き戻す” ＋ バイアスをプレイ中に推定更新
    XMVECTOR q = XMLoadFloat4(&orientation_);

    // 1) 推定重力方向（ワールドの下= (0,-1,0) とするなら、機体座標での重力は
    // q^-1 * down * q）
    //    ここでは「現在姿勢から見た down ベクトル」を求める
    XMVECTOR downW = XMVectorSet(0, 1, 0, 0);
    XMVECTOR gEst =
        XMVector3Normalize(XMVector3Rotate(downW, XMQuaternionConjugate(q)));

    // 2) 観測重力（加速度）を正規化。振ってる最中は信用しない（|a| が 1g
    // 近い時だけ）
    XMVECTOR aMeas = XMVectorSet(-ax, -ay, -az, 0);
    float aLen = XMVectorGetX(XMVector3Length(aMeas));

    float trust = 0.0f;
    if (aLen > 1e-4f) {
        float g = aLen; // JoyShockLibrary の単位が "g"
                        // 近い前提（違っても比で判定するので大丈夫）
        if (g >= accelTrustMin && g <= accelTrustMax) {
            trust = 1.0f;
        }
        aMeas = XMVector3Normalize(aMeas);
    }

    // 3) エラー = 推定重力 と 観測重力 の外積（ズレの軸）
    XMVECTOR err = XMVectorZero();
    if (trust > 0.5f) {
        err = XMVector3Cross(gEst,
                             aMeas); // right-hand の定義なので、符号が逆なら
                                     // Cross の順序を入れ替える
    }

    // 4) バイアスをプレイ中に更新（“振るほど正しくなる” のコア）
    //    gyroBias*
    //    は「初期バイアス」なので、ここでは補正用の追加バイアスとして蓄積する
    //    ※簡易に gyroBiasX/Y/Z に加算していく（温度ドリフト追従）
    if (trust > 0.5f) {
        gyroBiasX_ += (XMVectorGetX(err) * Ki) * deltaTime;
        gyroBiasY_ += (XMVectorGetY(err) * Ki) * deltaTime;
        gyroBiasZ_ += (XMVectorGetZ(err) * Ki) * deltaTime;
    }

    // 5) 補正角速度 = 生ジャイロ + Kp*err
    XMVECTOR omega = XMVectorSet(gx, gy, gz, 0);
    if (trust > 0.5f) {
        omega = XMVectorAdd(omega, XMVectorScale(err, Kp));
    }

    // 6) クォータニオン積分（小角近似の方が安定しやすい）
    //    q_dot = 0.5 * q ⊗ [0, ω]
    XMVECTOR wQuat = XMVectorSet(XMVectorGetX(omega), XMVectorGetY(omega),
                                 XMVectorGetZ(omega), 0.0f);
    XMVECTOR qDot = XMQuaternionMultiply(q, wQuat);
    qDot = XMVectorScale(qDot, 0.5f);

    q = XMVectorAdd(q, XMVectorScale(qDot, deltaTime));
    q = XMQuaternionNormalize(q);

    XMStoreFloat4(&orientation_, q);
}

XMVECTOR Input::GetOrientation() const { return XMLoadFloat4(&orientation_); }

void Input::ResetOrientation() {
    orientation_ = {0, 0, 0, 1};

    biasInitialized_ = false;
    biasSampleCount_ = 0;
    biasSumX_ = biasSumY_ = biasSumZ_ = 0.0f;

    // 温度ドリフト追従をやり直したいので、推定バイアスもリセット
    gyroBiasX_ = gyroBiasY_ = gyroBiasZ_ = 0.0f;
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