#pragma once
#include "BaseScene.h"
#include "Bullet.h"
#include "Camera.h"
#include "Enemy.h"
#include "Player.h"
#include "Transform.h"
#include "VignetteRenderer.h"
#include <DirectXMath.h>
#include <cstdint>
#include <string>

#ifdef _DEBUG
#include "DebugCamera.h"
#endif // _DEBUG

class GameScene : public BaseScene {
  public:
    void Initialize(const SceneContext &ctx) override;
    void Update() override;
    void Draw() override;

  private:
    void UpdateCamera(Input *input);
    void UpdateBattleCamera();
    void UpdateSceneLighting();
    void SyncEnemyAnimation();
    void SetEnemyAnimationFrozen(bool frozen);
    float ComputeGameplayTimeScale() const;
    void UpdateCounterVignette(float deltaTime);
    void DrawCounterVignette();
    bool ProjectWorldToScreen(const DirectX::XMFLOAT3 &worldPos,
                              DirectX::XMFLOAT2 &outScreen) const;
    void DrawWarpSmokePass();
    void DrawWarpDistortionPass();

  private:
    static constexpr DirectX::XMFLOAT3 kCameraStartPos = {0.0f, 1.0f, 0.5f};
    static constexpr float kCameraDistance = 3.5f;
    static constexpr float kCameraHeight = 1.2f;

    Camera camera_;
#ifdef _DEBUG
    DebugCamera debugCamera_;
    Camera tripodCamera_;
    bool useTripodCamera_ = false;
    DirectX::XMFLOAT3 tripodPos_ = {0.0f, 2.0f, -5.0f};
    DirectX::XMFLOAT3 tripodTarget_ = {0.0f, 1.0f, 0.0f};
#endif
    Camera *currentCamera_ = nullptr;

    Player player_;
    Enemy enemy_;
    uint32_t playerModelId_ = 0;
    uint32_t enemyModelId_ = 0;
    std::string enemyAnimationName_{};
    bool enemyAnimationLoop_ = true;
    bool enemyIntroAnimationStarted_ = false;
    IntroPhase enemyIntroPhase_ = IntroPhase::SecondSlash;

    float playerHitCooldown_ = 0.0f;
    float enemyHitCooldown_ = 0.0f;

    bool dbgHitLeftHand_ = false;
    bool dbgHitRightHand_ = false;
    bool dbgHitBody_ = false;
    bool dbgBossHitPlayer_ = false;
    bool dbgBulletHitPlayer_ = false;
    bool dbgWaveHitPlayer_ = false;
    bool dbgPlayerGuardedHit_ = false;
    bool dbgTriggerCounterRequested_ = false;
#ifdef _DEBUG
    bool dbgFreezeEnemyMotion_ = false;
#endif
    Bullet bullet_;
    uint32_t warpSmokeSpriteId_ = 0;

    float cameraYaw_ = 0.0f;
    float cameraPitch_ = 0.22f;
    float cameraPitchMin_ = -0.20f;
    float cameraPitchMax_ = 0.55f;
    float cameraLookSensitivity_ = 0.022f;
    float cameraDistance_ = 6.4f;
    float cameraHeight_ = 2.3f;
    float cameraSideOffset_ = 0.10f;
    float cameraLookHeight_ = 1.55f;
    float cameraLookAhead_ = 2.7f;

    bool isLockOn_ = false;
    float lockOnAssistStrength_ = 3.6f;
    float lockOnAssistMaxStep_ = 4.8f;
    float lockOnInputReduce_ = 0.45f;

    float lockOnCameraDistance_ = 7.4f;
    float lockOnCameraHeight_ = 2.5f;
    float lockOnCameraSideOffset_ = 0.08f;
    float lockOnLookPlayerWeight_ = 0.48f;
    float lockOnLookEnemyWeight_ = 0.52f;

    float lockOnDistanceMin_ = 2.5f;
    float lockOnDistanceMax_ = 11.0f;
    float lockOnDistancePullBackMin_ = 0.0f;
    float lockOnDistancePullBackMax_ = 1.8f;

    float lockOnOrbitRadius_ = 6.8f;
    float lockOnOrbitHeight_ = 2.4f;
    float lockOnOrbitSideBias_ = 0.05f;
    float lockOnOrbitLerpSpeed_ = 7.5f;
    float lockOnOrbitPullBackMax_ = 2.3f;
    DirectX::XMFLOAT3 lockOnOrbitCameraPos_ = {0.0f, 0.0f, 0.0f};

    float lockOnLookAtLerpSpeed_ = 9.0f;
    DirectX::XMFLOAT3 lockOnLookAt_ = {0.0f, 0.0f, 0.0f};

    float rushChargeAssistStrength_ = 4.8f;
    float rushChargeAssistMaxStep_ = 7.0f;
    float rushActiveAssistStrength_ = 5.8f;
    float rushActiveAssistMaxStep_ = 8.5f;
    float rushLeadDistance_ = 1.6f;

    float warpStartAssistStrength_ = 4.5f;
    float warpStartAssistMaxStep_ = 7.0f;
    float warpEndAssistStrength_ = 6.0f;
    float warpEndAssistMaxStep_ = 10.0f;

    float currentFovDeg_ = 74.0f;
    float targetFovDeg_ = 74.0f;
    float normalFovDeg_ = 74.0f;
    float lockOnFovDeg_ = 78.0f;
    float rushFovDeg_ = 80.0f;
    float warpFovDeg_ = 79.0f;
    float phaseTransitionFovDeg_ = 68.0f;
    float enemyIntroFovDeg_ = 64.0f;
    float fovLerpSpeed_ = 6.5f;
    float phaseTransitionFovLerpSpeed_ = 5.5f;
    float enemyIntroFovLerpSpeed_ = 3.4f;
    float phaseTransitionLookAtEnemyWeight_ = 0.82f;
    float phaseTransitionLookAtHeight_ = 1.45f;
    float phaseTransitionPushIn_ = 0.85f;
    float enemyIntroPushIn_ = 1.55f;
    float enemyIntroLookAtEnemyWeight_ = 0.90f;
    float enemyIntroLookAtHeight_ = 1.55f;

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
    float sceneLightTime_ = 0.0f;

    bool counterCinematicActive_ = false;
    bool enemyAnimationFrozen_ = false;
    float counterTimeScale_ = 0.05f;
    float counterVignetteAlpha_ = 0.0f;
    float counterVignetteFadeSpeed_ = 4.5f;
    VignetteRenderer counterVignetteRenderer_;
};
