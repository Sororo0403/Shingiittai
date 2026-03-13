#pragma once
#include "BaseScene.h"
#include "Camera.h"
#include "Enemy.h"
#include "Player.h"
#include "Transform.h"
#include <DirectXMath.h>
#include <cstdint>

#ifdef _DEBUG
#include "DebugCamera.h"
#endif // _DEBUG

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
    // Update
    void UpdateCamera(Input *input);
    void UpdateBattleCamera();

  private:
    static constexpr DirectX::XMFLOAT3 kCameraStartPos = {0.0f, 1.0f, 0.5f};

    // Camera
    Camera camera_;

#ifdef _DEBUG
    DebugCamera debugCamera_;
#endif

    Camera *currentCamera_ = nullptr;

    // Game objects
    Player player_;
    Enemy enemy_;
};