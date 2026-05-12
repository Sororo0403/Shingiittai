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
#include <array>
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
    void DrawEnemyFocusMarker();
    void DrawEnemyWeaponTrail();
    void DrawChargeWeakPoint();
    void DrawChargeWeakPointTimeGauge();
    void DrawJoyConTutorial();
    void DrawVictoryFlash();
    void BeginVictorySequence();
    void UpdateVictorySequence(float deltaTime);
    void SyncEnemyAnimation();
    void ApplyEnemyProceduralAnimation();
    void SetEnemyAnimationFrozen(bool frozen);
    void UpdateCombat(float gameplayDeltaTime);
    void DispatchCombatFeedback(const CombatFeedbackEvent &event);
    void EmitCombatParticles(const CombatFeedbackEvent &event);
    void EmitEnemyActionParticles(ActionKind kind, ActionStep step);
    void EmitEnemyCueParticles(float deltaTime);
    bool IsChargeWeakPointFocusActive() const;
    void UpdateChargeWeakPointFocus(float deltaTime);
    PlayerCombatObservation BuildPlayerCombatObservation() const;
    float ComputeGameplayTimeScale() const;

  private:
    struct EnemyWeaponTrailSample {
        DirectX::XMFLOAT3 root = {0.0f, 0.0f, 0.0f};
        DirectX::XMFLOAT3 tip = {0.0f, 0.0f, 0.0f};
        DirectX::XMFLOAT4 color = {1.0f, 1.0f, 1.0f, 1.0f};
        ActionKind kind = ActionKind::None;
        float thickness = 0.0f;
        float age = 0.0f;
        bool active = false;
    };

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
    uint32_t enemyFocusRingModelId_ = 0;
    uint32_t enemyWeaponTrailModelId_ = 0;
    uint32_t chargeWeakPointModelId_ = 0;
    uint32_t slashSoundId_ = 0;
    uint32_t enemyReleaseSoundId_ = 0;
    uint32_t hitSoundId_ = 0;
    uint32_t counterSoundId_ = 0;
    uint32_t damageSoundId_ = 0;
    uint32_t explosionSoundId_ = 0;
    bool soundsLoaded_ = false;
    std::array<bool, Player::kSwordCount> previousSwordSoundStates_{};
    std::array<EnemyWeaponTrailSample, 6> enemyWeaponTrailSamples_{};
    uint32_t enemyWeaponTrailSampleCursor_ = 0;
    float enemyWeaponTrailSampleTimer_ = 0.0f;
    float enemyWeaponTrailLastDrawTime_ = 0.0f;
    ActionKind enemyWeaponTrailLastKind_ = ActionKind::None;
    ActionStep enemyWeaponTrailLastStep_ = ActionStep::None;
    float enemyCueParticleTimer_ = 0.0f;
    float enemyWeakPointParticleTimer_ = 0.0f;
    float enemySwordParticleTimer_ = 0.0f;
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
    float battleElapsedTime_ = 0.0f;
    bool battleResultRequested_ = false;
    bool victorySequenceActive_ = false;
    float victorySequenceTimer_ = 0.0f;
    float victorySequenceDuration_ = 5.45f;
    float victoryClearTime_ = 0.0f;
    bool victoryFinalExplosionEmitted_ = false;
    DirectX::XMFLOAT3 victoryEnemyStartPos_ = {0.0f, 0.0f, 0.0f};

    bool counterCinematicActive_ = false;
    bool enemyAnimationFrozen_ = false;
    float counterCinematicTimer_ = 0.0f;
    float counterCinematicDuration_ = 0.85f;
    float counterTimeScale_ = 0.05f;
    float counterCameraShakeX_ = 0.035f;
    float counterCameraShakeY_ = 0.020f;
    float counterCameraShakeFrequency_ = 18.0f;

    float damageMultiplier_ = 2.0f;
    bool showCollisionDebug_ = false;
    bool showJoyConTutorial_ = true;
    ActionKind chargeWeakPointActionKind_ = ActionKind::None;
    ActionKind failedChargeWeakPointActionKind_ = ActionKind::None;
    bool chargeWeakPointBroken_ = false;
    bool chargeWeakPointFailedThisAction_ = false;
    bool enemyRedPunishUncounterable_ = false;
    int chargeWeakPointSlashCount_ = 0;
    float chargeWeakPointFocusRatio_ = 0.0f;
    float chargeWeakPointFocusInSpeed_ = 7.5f;
    float chargeWeakPointFocusOutSpeed_ = 10.0f;
    float chargeWeakPointFocusTimeScale_ = 0.28f;
    std::array<DirectX::XMFLOAT2, 2> chargeWeakPointRequiredDirections_ = {
        DirectX::XMFLOAT2{0.0f, -1.0f}, DirectX::XMFLOAT2{1.0f, 0.0f}};
    std::array<bool, Player::kSwordCount> previousChargeWeakPointSlashStates_{};

};
