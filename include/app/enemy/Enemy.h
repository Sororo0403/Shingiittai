#pragma once
#include "Camera.h"
#include "OBB.h"
#include "Transform.h"
#include <cstdint>

class ModelManager;

class Enemy {
  public:
    /// <summary>
    /// 初期化処理
    /// </summary>
    /// <param name="modelId">敵のモデルID</param>
    void Initialize(uint32_t modelId);

    /// <summary>
    /// 更新処理
    /// </summary>
    void Update();

    /// <summary>
    /// 描画処理
    /// </summary>
    /// <param name="modelManager">ModelManagerインスタンス</param>
    /// <param name="camera">描画に使用するカメラ</param>
    void Draw(ModelManager *modelManager, const Camera &camera);

    /// <summary>
    /// ダメージを与える
    /// </summary>
    /// <param name="damage">与えるダメージ量</param>
    void TakeDamage(float damage);

    // Getter
    const Transform &GetTransform() const { return tf_; }
    bool IsAlive() const { return hp_ > 0.0f; }
    OBB GetOBB() const;

  private:
    Transform tf_;
    uint32_t modelId_ = 0;

    DirectX::XMFLOAT3 size_ = {0.5f, 1.2f, 0.5f};

    float hp_ = 100.0f;
};