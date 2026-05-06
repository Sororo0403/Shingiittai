#pragma once
#include <DirectXMath.h>

class MahonyFilter {
  public:
    /// <summary>
    /// 初期化処理
    /// </summary>
    /// <param name="kp">比例補正ゲイン</param>
    /// <param name="ki">積分補正ゲイン</param>
    void Initialize(float kp = 2.0f, float ki = 0.0f);

    /// <summary>
    /// 更新処理
    /// </summary>
    /// </summary>
    /// <param name="gx">X軸の角速度</param>
    /// <param name="gy">Y軸の角速度</param>
    /// <param name="gz">Z軸の角速度</param>
    /// <param name="ax">X軸の加速度</param>
    /// <param name="ay">Y軸の加速度</param>
    /// <param name="az">Z軸の加速度</param>
    /// <param name="deltaTime">前回更新からの経過時間(秒)</param>
    void Update(float gx, float gy, float gz, float ax, float ay, float az,
                float deltaTime);

    /// <summary>
    /// フィルタの内部状態を初期状態に戻す
    /// </summary>
    void Reset();

    // Getter
    DirectX::XMVECTOR GetQuaternion() const;

  private:
    float kp_ = 2.0f;
    float ki_ = 0.0f;

    DirectX::XMFLOAT4 q_ = {0.0f, 0.0f, 0.0f, 1.0f};
    DirectX::XMFLOAT3 integralError_ = {0.0f, 0.0f, 0.0f};
};