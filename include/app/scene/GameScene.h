#pragma once
#include "BaseScene.h"
#include "Camera.h"
#include "Enemy.h"
#include "Player.h"
#include "Bullet.h"
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
    static constexpr float kCameraDistance = 3.5f;
    static constexpr float kCameraHeight = 1.2f;

    // Camera
    Camera camera_;
#ifdef _DEBUG
    DebugCamera debugCamera_;
#endif
    Camera *currentCamera_ = nullptr;

    // Game
    Player player_;
    Enemy enemy_;
    uint32_t playerModelId_ = 0;
    uint32_t enemyModelId_ = 0;

    float playerHitCooldown_ = 0.0f;

    // ヒットクールダウンタイマー
    float enemyHitCooldown_ = 0.0f;

    // デバッグ用ヒット表示
    bool dbgHitLeftHand_ = false;
    bool dbgHitRightHand_ = false;
    bool dbgHitBody_ = false;
    bool dbgBossHitPlayer_ = false;
    bool dbgBulletHitPlayer_ = false;
    bool dbgWaveHitPlayer_ = false;
    bool dbgPlayerGuardedHit_ = false;
    Bullet bullet_;
};