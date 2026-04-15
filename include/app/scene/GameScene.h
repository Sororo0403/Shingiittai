#pragma once
#include "BaseScene.h"
#include "Bullet.h"
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
    bool ProjectWorldToScreen(const DirectX::XMFLOAT3 &worldPos,
                              DirectX::XMFLOAT2 &outScreen) const;
    void DrawWarpSmokePass();
    void DrawWarpDistortionPass();

  private:
    static constexpr DirectX::XMFLOAT3 kCameraStartPos = {0.0f, 1.0f, 0.5f};
    static constexpr float kCameraDistance = 3.5f;
    static constexpr float kCameraHeight = 1.2f;

    // Camera
    Camera camera_;
#ifdef _DEBUG
    DebugCamera debugCamera_;

    // 三脚カメラ
    Camera tripodCamera_;
    bool useTripodCamera_ = false;

    // 三脚カメラ設定
    DirectX::XMFLOAT3 tripodPos_ = {0.0f, 2.0f, -5.0f};
    DirectX::XMFLOAT3 tripodTarget_ = {0.0f, 1.0f, 0.0f};
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
#ifdef _DEBUG
    bool dbgFreezeEnemyMotion_ = false;
#endif
    Bullet bullet_;
    uint32_t warpSmokeSpriteId_ = 0;

    // 完全一人称カメラ用
    /*DirectX::XMFLOAT3 fpCameraOffset_ = {0.0f, 1.55f, 0.0f};
    float cameraYaw_ = 0.0f;
    float cameraPitch_ = 0.0f;
    float cameraPitchMin_ = -1.2f;
    float cameraPitchMax_ = 1.0f;
    float cameraLookSensitivity_ = 0.025f;*/
    float cameraYaw_ = 0.0f;
    float cameraPitch_ = 0.15f;
    float cameraPitchMin_ = -0.35f;
    float cameraPitchMax_ = 0.65f;
    float cameraLookSensitivity_ = 0.025f;
    // プレイヤー基準の肩越しオフセット
    float cameraDistance_ = 4.8f;    // 後方距離
    float cameraHeight_ = 1.8f;      // 高さ
    float cameraSideOffset_ = 0.65f; // 右肩寄せ
    float cameraLookHeight_ = 1.35f; // 注視点の高さ

    // 視線の補間
    float cameraLookAhead_ = 2.0f; // 非ロック時の前方注視距離

    // ロックオン用
    bool isLockOn_ = false;
    float lockOnAssistStrength_ = 2.0f;
    float lockOnAssistMaxStep_ = 3.5f;
    float lockOnInputReduce_ = 0.25f;

    // ロックオン時の戦闘カメラ構図
    float lockOnCameraDistance_ = 6.2f;
    float lockOnCameraHeight_ = 2.1f;
    float lockOnCameraSideOffset_ = 0.35f;
    float lockOnLookPlayerWeight_ = 0.35f;
    float lockOnLookEnemyWeight_ = 0.65f;

     // プレイヤーと敵の距離で少しだけ後ろに引く補正
    float lockOnDistanceMin_ = 3.0f;
    float lockOnDistanceMax_ = 12.0f;
    float lockOnDistancePullBackMin_ = 0.0f;
    float lockOnDistancePullBackMax_ = 1.8f;

    // ロックオン時の円弧追従
    float lockOnOrbitRadius_ = 5.8f;      // 基本半径
    float lockOnOrbitHeight_ = 2.0f;      // 高さ
    float lockOnOrbitSideBias_ = 0.35f;   // 肩寄せの残し量
    float lockOnOrbitLerpSpeed_ = 10.0f;  // 円弧位置の追従速度
    float lockOnOrbitPullBackMax_ = 1.6f; // 敵との距離で後ろに引く最大量

    // 円弧追従で使う現在位置
    DirectX::XMFLOAT3 lockOnOrbitCameraPos_ = {0.0f, 0.0f, 0.0f};

    // ロックオン時の注視点補間
    float lockOnLookAtLerpSpeed_ = 12.0f;
    DirectX::XMFLOAT3 lockOnLookAt_ = {0.0f, 0.0f, 0.0f};

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
    float phaseTransitionFovDeg_ = 72.0f;
    float fovLerpSpeed_ = 8.0f;
    float phaseTransitionFovLerpSpeed_ = 5.5f;
    float phaseTransitionLookAtEnemyWeight_ = 0.82f;
    float phaseTransitionLookAtHeight_ = 1.45f;
    float phaseTransitionPushIn_ = 0.85f;

    // Warp screen-space distortion
    float warpDistortionRadiusPx_ = 116.0f;
    float warpDistortionThicknessPx_ = 4.0f;
    float warpDistortionLineLengthPx_ = 72.0f;
    float warpDistortionAlpha_ = 0.34f;
    float warpDistortionMoveAlphaBonus_ = 0.20f;
    float warpDistortionJitterPx_ = 16.0f;
    float warpDistortionPreviewOffsetPx_ = 46.0f;
    float warpSmokeBaseSizePx_ = 132.0f;
    float warpSmokeMoveStretchPx_ = 92.0f;
    float warpSmokeAlpha_ = 0.34f;
    float warpSourceSmokeBloomScale_ = 1.42f;
    float warpSourceSmokeDriftPx_ = 30.0f;
    float warpSourceSmokeDarkAlpha_ = 0.58f;
    float warpSourceSmokeRedAlpha_ = 0.42f;
    float warpArrivalSmokeScale_ = 0.76f;
    float warpArrivalSmokeAlphaScale_ = 0.42f;
    float warpMoveSmokeAlphaScale_ = 0.78f;
    float warpMoveSmokeStretchScale_ = 0.78f;
};
