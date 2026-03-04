#pragma once
#include "Camera.h"
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

  private:
    Transform tf_;
    uint32_t modelId_ = 0;

    float hp_ = 100.0f;
};