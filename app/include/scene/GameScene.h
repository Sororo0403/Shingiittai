#pragma once
#include "BaseScene.h"
#include "Bullet.h"
#include "Camera.h"
#include "CombatFeedbackDirector.h"
#include "CollisionDebugRenderer.h"
#include "CollisionManager.h"
#include "Enemy.h"
#include "GPUParticleSystem.h"
#include "Player.h"
#include "PlayerWeaponType.h"
#include "Transform.h"
#include <DirectXMath.h>
#include <cstdint>
#include <string>
#include "GameSceneHud.h"

class GameScene : public BaseScene {
  public:
    explicit GameScene(PlayerWeaponType weaponType = PlayerWeaponType::Standard)
        : selectedWeaponType_(weaponType) {}

    void Initialize(const SceneContext &ctx) override;
    void Update() override;
    void Draw() override;
    void DrawOverlay() override;

  private:
    void UpdateCamera(Input *input);
    void UpdateBattleCamera();
    void UpdateSceneLighting();
    void DrawArena();
    void SyncEnemyAnimation();
    void ApplyEnemyProceduralAnimation();
    void SetEnemyAnimationFrozen(bool frozen);
    void UpdateCombat(float gameplayDeltaTime);
    void DispatchCombatFeedback(const CombatFeedbackEvent &event);
    void EmitCombatParticles(const CombatFeedbackEvent &event);
    PlayerCombatObservation BuildPlayerCombatObservation() const;
    float ComputeGameplayTimeScale() const;

  private:
    PlayerWeaponType selectedWeaponType_ = PlayerWeaponType::Standard;

    Camera camera_;

    Player player_;
    Enemy enemy_;
    GameSceneHud hud_;
    CollisionManager collisionManager_;
    CollisionDebugRenderer collisionDebugRenderer_;
    CombatFeedbackDirector combatFeedback_;
    GPUParticleSystem sparkParticles_;
    GPUParticleSystem explosionParticles_;
    GPUParticleSystem smokeParticles_;
    uint32_t particleTextureId_ = 0;
    uint32_t playerModelId_ = 0;
    uint32_t enemyModelId_ = 0;
    uint32_t arenaFloorModelId_ = 0;
    uint32_t arenaLowPolyTerrainModelId_ = 0;
    uint32_t arenaCenterDiskModelId_ = 0;
    uint32_t arenaSpokeModelId_ = 0;
    uint32_t arenaInnerRingModelId_ = 0;
    uint32_t arenaOuterRingModelId_ = 0;
    uint32_t arenaColumnModelId_ = 0;
    uint32_t arenaColumnCapModelId_ = 0;
    uint32_t arenaDomeModelId_ = 0;
    uint32_t arenaBarrierRingModelId_ = 0;
    uint32_t arenaNoiseTextureId_ = 0;
    std::string enemyAnimationName_{};
    bool enemyAnimationLoop_ = true;

    float playerHitCooldown_ = 0.0f;
    float enemyHitCooldown_ = 0.0f;
    Bullet bullet_;

    float cameraYaw_ = 0.0f;
    float cameraPitch_ = 0.12f;
    float cameraPitchMin_ = -0.20f;
    float cameraPitchMax_ = 0.55f;
    float cameraLookSensitivity_ = 0.022f;
    float cameraDistance_ = 5.8f;
    float cameraHeight_ = 1.95f;
    float cameraSideOffset_ = 0.18f;
    float cameraLookHeight_ = 1.30f;
    float cameraLookAhead_ = 3.0f;

    bool isLockOn_ = false;
    float lockOnAssistStrength_ = 4.3f;
    float lockOnAssistMaxStep_ = 5.8f;
    float lockOnInputReduce_ = 0.35f;

    float lockOnCameraDistance_ = 8.0f;
    float lockOnCameraHeight_ = 2.9f;
    float lockOnCameraSideOffset_ = 0.20f;
    float lockOnLookPlayerWeight_ = 0.32f;
    float lockOnLookEnemyWeight_ = 0.68f;

    float lockOnDistanceMin_ = 2.5f;
    float lockOnDistanceMax_ = 11.0f;
    float lockOnDistancePullBackMin_ = 0.0f;
    float lockOnDistancePullBackMax_ = 2.5f;

    float lockOnOrbitRadius_ = 5.4f;
    float lockOnOrbitHeight_ = 2.95f;
    float lockOnOrbitSideBias_ = 0.55f;
    float lockOnOrbitLerpSpeed_ = 6.2f;
    float lockOnOrbitPullBackMax_ = 1.8f;
    DirectX::XMFLOAT3 lockOnOrbitCameraPos_ = {0.0f, 0.0f, 0.0f};

    float lockOnLookAtLerpSpeed_ = 8.2f;
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

    float currentFovDeg_ = 82.0f;
    float targetFovDeg_ = 82.0f;
    float normalFovDeg_ = 82.0f;
    float lockOnFovDeg_ = 82.0f;
    float rushFovDeg_ = 84.0f;
    float warpFovDeg_ = 84.0f;
    float phaseTransitionFovDeg_ = 78.0f;
    float fovLerpSpeed_ = 6.5f;
    float phaseTransitionFovLerpSpeed_ = 5.5f;
    float phaseTransitionLookAtEnemyWeight_ = 0.82f;
    float phaseTransitionLookAtHeight_ = 2.35f;
    float phaseTransitionPushIn_ = 0.85f;
    bool playerViewCamera_ = false;
    float playerViewEyeHeight_ = 1.42f;
    float playerViewForwardOffset_ = 0.10f;
    float playerViewSideOffset_ = 0.12f;
    float playerViewLookAhead_ = 8.0f;
    float playerViewLockOnLookHeight_ = 1.48f;

    float sceneLightTime_ = 0.0f;

    bool counterCinematicActive_ = false;
    bool enemyAnimationFrozen_ = false;
    float counterTimeScale_ = 0.05f;
    float counterCameraShakeX_ = 0.035f;
    float counterCameraShakeY_ = 0.020f;
    float counterCameraShakeFrequency_ = 18.0f;

    float damageMultiplier_ = 2.0f;
    bool showCollisionDebug_ = false;

};
