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

    // Getter
    const Sword &GetSword() const { return sword_; }
    // 書き替え可能版
    Sword &GetSword() { return sword_; }

  private:
    // Update
    void UpdateMovement(Input *input, float deltaTime);

  private:
    Transform tf_;
    uint32_t modelId_ = 0;

    Sword sword_;

    float moveSpeed_ = 5.0f;
};