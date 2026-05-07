#pragma once
#include "BaseScene.h"
#include "Bullet.h"
#include "Camera.h"
#include "CollisionManager.h"
#include "Enemy.h"
#include "Player.h"
#include "PlayerWeaponType.h"
#include "Transform.h"
#include <DirectXMath.h>
#include <cstdint>
#include <string>

class GameScene : public BaseScene {
  public:
    explicit GameScene(PlayerWeaponType weaponType = PlayerWeaponType::Standard)
        : selectedWeaponType_(weaponType) {}

    void Initialize(const SceneContext &ctx) override;
    void Update() override;
    void Draw() override;

  private:
    void UpdateCamera(Input *input);
    void UpdateBattleCamera();
    void UpdateSceneLighting();
    void DrawArena();
    void SyncEnemyAnimation();
    void SetEnemyAnimationFrozen(bool frozen);
    void UpdateCombat(float gameplayDeltaTime);
    PlayerCombatObservation BuildPlayerCombatObservation() const;
    float ComputeGameplayTimeScale() const;

  private:
    PlayerWeaponType selectedWeaponType_ = PlayerWeaponType::Standard;

    Camera camera_;

    Player player_;
    Enemy enemy_;
    CollisionManager collisionManager_;
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
    float fovLerpSpeed_ = 6.5f;
    float phaseTransitionFovLerpSpeed_ = 5.5f;
    float phaseTransitionLookAtEnemyWeight_ = 0.82f;
    float phaseTransitionLookAtHeight_ = 1.45f;
    float phaseTransitionPushIn_ = 0.85f;

    float sceneLightTime_ = 0.0f;

    bool counterCinematicActive_ = false;
    bool enemyAnimationFrozen_ = false;
    float counterTimeScale_ = 0.05f;
    float counterCameraShakeX_ = 0.035f;
    float counterCameraShakeY_ = 0.020f;
    float counterCameraShakeFrequency_ = 18.0f;

    float damageMultiplier_ = 2.0f;

};
