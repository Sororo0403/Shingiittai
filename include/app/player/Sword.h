#pragma once
#include "Transform.h"
#include <DirectXMath.h>
#include <cstdint>

class ModelManager;
class Input;
class Camera;

class Sword {
  public:
    /// <summary>
    /// 初期化処理
    /// </summary>
    /// <param name="modelId">剣のモデルID</param>
    void Initialize(uint32_t modelId);

    /// <summary>
    /// 更新処理
    /// </summary>
    /// <param name="input">Inputインスタンス</param>
    /// <param name="playerPos">プレイヤーの座標</param>
    void Update(Input *input, const DirectX::XMFLOAT3 &playerPos);

    /// <summary>
    /// 描画処理
    /// </summary>
    /// <param name="modelManager">ModelManagerインスタンス</param>
    /// <param name="camera">描画に使用するカメラ</param>
    void Draw(ModelManager *modelManager, const Camera &camera);

    // Getter
    const Transform &GetTransform() const { return tf_; }

  private:
    Transform tf_;
    uint32_t modelId_ = 0;
};