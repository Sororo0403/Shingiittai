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
    void Update(Input *input);

    /// <summary>
    /// 描画処理
    /// </summary>
    /// <param name="modelManager">ModelManagerインスタンス</param>
    /// <param name="camera">描画使用するカメラ</param>
    void Draw(ModelManager *modelManager, const Camera &camera);

  private:
    Transform playerTf_;
    uint32_t playerModelId_ = 0;

    Sword sword_;
};