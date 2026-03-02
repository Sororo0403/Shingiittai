#pragma once
#include "Camera.h"
#include "Transform.h"
#include <DirectXMath.h>
#include <cstdint>
#include <vector>

class ModelManager;

class ParticleSystem {

  public:
    /// <summary>
    /// 初期化処理
    /// </summary>
    /// <param name="modelManager">ModelManagerインスタンス</param>
    /// <param name="modelId">パーティクルのモデルid</param>
    void Initialize(ModelManager *modelManager, uint32_t modelId);

    /// <summary>
    /// 更新処理
    /// </summary>
    /// <param name="deltaTime">前フレームからの経過時間(秒)</param>
    void Update(float deltaTime);

    /// <summary>
    /// 描画処理
    /// </summary>
    /// <param name="camera">描画に使用するカメラ</param>
    void Draw(const Camera &camera);

    /// <summary>
    /// パーティクルを生成
    /// </summary>
    /// <param name="position">発生位置</param>
    /// <param name="direction">初期速度の基準方向</param>
    void Emit(const DirectX::XMFLOAT3 &position,
              const DirectX::XMFLOAT3 &direction);

  private:
    struct Particle {
        Transform tf;
        DirectX::XMFLOAT3 velocity;
        float life;
        float maxLife;
        bool isAlive;
    };

  private:
    static constexpr int kMaxParticles_ = 256;

    std::vector<Particle> particles_;
    ModelManager *modelManager_ = nullptr;
    uint32_t modelId_ = 0;
};