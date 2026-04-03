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

        // 完全一人称カメラ用
    DirectX::XMFLOAT3 fpCameraOffset_ = {0.0f, 1.55f, 0.0f};
    float cameraYaw_ = 0.0f;
    float cameraPitch_ = 0.0f;
    float cameraPitchMin_ = -1.2f;
    float cameraPitchMax_ = 1.0f;
    float cameraLookSensitivity_ = 0.025f;

        // ロックオン用
    bool isLockOn_ = false;
    float lockOnAssistStrength_ = 2.0f;
    float lockOnAssistMaxStep_ = 3.5f;
    float lockOnInputReduce_ = 0.25f;

        // Rush時のカメラ補助
    float rushChargeAssistStrength_ = 4.0f;
    float rushChargeAssistMaxStep_ = 6.0f;

    float rushActiveAssistStrength_ = 5.0f;
    float rushActiveAssistMaxStep_ = 8.0f;
    float rushLeadDistance_ = 2.5f;

        // Warp時の再捕捉補助
    float warpStartAssistStrength_ = 4.5f;
    float warpStartAssistMaxStep_ = 7.0f;
    float warpEndAssistStrength_ = 6.0f;
    float warpEndAssistMaxStep_ = 10.0f;

    // FOV制御
    float currentFovDeg_ = 80.0f;
    float targetFovDeg_ = 80.0f;
    float normalFovDeg_ = 80.0f;
    float lockOnFovDeg_ = 86.0f;
    float rushFovDeg_ = 88.0f;
    float warpFovDeg_ = 88.0f;
    float fovLerpSpeed_ = 8.0f;

};