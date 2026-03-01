#pragma once
#include "BaseScene.h"
#include "Camera.h"
#include "Transform.h"
#include <cstdint>

class GameScene : public BaseScene {
  public:
    /// <summary>
    /// 初期化処理
    /// </summary>
    /// <param name="ctx">シーンコンテキスト</param>
    void Initialize(const SceneContext &ctx) override;

    /// <summary>
    /// 更新処理
    /// </summary>
    void Update() override;

    /// <summary>
    /// 描画処理
    /// </summary>
    void Draw() override;

  private:
    Camera camera_;

    uint32_t swordModelId_ = 0;
    Transform swordTf_;
};