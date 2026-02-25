#pragma once
#include "BaseScene.h"
#include "Camera.h"
#include "Transform.h"
#include <DirectXMath.h>
#include <cstdint>
#include <vector>

class GameScene : public BaseScene {
  public:
    /// <summary>
    /// 初期化処理
    /// </summary>
    /// <param name="ctx"></param>
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
    // モデルID
    uint32_t enemyModelId_ = 0;

    // 敵群
    std::vector<Transform> enemies_;

    // プレイヤー
    Transform playerTf_;

    // カメラ
    Camera camera_;
    DirectX::XMFLOAT3 cameraRot_{0, 0, 0};

    float moveSpeed_ = 8.0f;
};