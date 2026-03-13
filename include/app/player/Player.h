#pragma once
#include "Camera.h"
#include "Sword.h"
#include "Transform.h"
#include <cstdint>

class ModelManager;
class Input;

class Player {
  public:
    /// <summary>
    /// 初期化処理
    /// </summary>
    /// <param name="playerModelId">プレイヤーのモデルID</param>
    /// <param name="swordModelId">剣のモデルID</param>
    void Initialize(uint32_t playerModelId, uint32_t swordModelId);

    /// <summary>
    /// 更新処理
    /// </summary>
    /// <param name="input">Inputインスタンス</param>
    /// <param name="deltaTime">前フレームからの経過時間(秒)</param>
    void Update(Input *input, float deltaTime);

    /// <summary>
    /// 描画処理
    /// </summary>
    /// <param name="modelManager">ModelManagerインスタンス</param>
    /// <param name="camera">描画使用するカメラ</param>
    void Draw(ModelManager *modelManager, const Camera &camera);

    /// <summary>
    /// 指定した座標を見る
    /// </summary>
    /// <param name="target">見る座標</param>
    void LookAt(const DirectX::XMFLOAT3 &target);

    // Getter
    const Sword &GetSword() const { return sword_; }
    const Transform &GetTransform() const { return tf_; }
    float GetYaw() const { return yaw_; }

  private:
    // Update
    void UpdateMovement(Input *input, float deltaTime);

  private:
    static constexpr float kHandHeight = 1.0f;
    static constexpr float kArmLength = 1.0f;

    Transform tf_;
    uint32_t modelId_ = 0;

    Sword sword_;

    float moveSpeed_ = 5.0f;

    float yaw_ = 0.0f;
};