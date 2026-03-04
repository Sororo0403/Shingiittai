#pragma once
#include "BaseScene.h"
#include "Camera.h"
#include "Enemy.h"
#include "Player.h"

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

    Player player_;
    Enemy enemy_;

#ifdef _DEBUG
    uint32_t debugBoxModel_ = 0;
    Transform swordBoxTf_;
    Transform enemyBoxTf_;
#endif // _DEBUG
};