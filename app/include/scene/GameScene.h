#pragma once
#include "BaseScene.h"
#include "Camera.h"
#include "CameraPreviewReceiver.h"
#include "CollisionManager.h"
#include "CombatFeedbackDirector.h"
#include "Enemy.h"
#include "EnemyPhaseMaterial.h"
#include "GPUParticleSystem.h"
#include "GameSceneHud.h"
#include "Player.h"
#include "SwordInputCalibration.h"
#include "SwordSlashArcRenderer.h"
#include "SwordTrailRenderer.h"
#include "Transform.h"
#include <DirectXMath.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/// <summary>
/// プレイヤーと敵の戦闘、カメラ、演出、勝敗遷移を統括する
/// </summary>
class GameScene : public BaseScene {
  public:
    enum class Mode {
        Gameplay,
        TitleDemo,
        BackgroundOnly,
        ReadyPreview,
        TutorialBackgroundOnly,
        Tutorial,
    };

    /// <summary>
    /// GameSceneに対応する公開処理を実行する
    /// </summary>
    explicit GameScene(const SwordInputCalibration &inputCalibration = {},
                       float combatDifficulty = 5.0f)
        : inputCalibration_(inputCalibration),
          combatDifficulty_(combatDifficulty) {}
    /// <summary>
    /// GameSceneに対応する公開処理を実行する
    /// </summary>
    explicit GameScene(bool titleDemoMode) : titleDemoMode_(titleDemoMode) {}
    /// <summary>
    /// GameSceneに対応する公開処理を実行する
    /// </summary>
    explicit GameScene(Mode mode)
        : titleDemoMode_(mode == Mode::TitleDemo),
          backgroundOnlyMode_(mode == Mode::BackgroundOnly ||
                              mode == Mode::TutorialBackgroundOnly),
          readyPreviewMode_(mode == Mode::ReadyPreview),
          tutorialBackgroundMode_(mode == Mode::TutorialBackgroundOnly ||
                                  mode == Mode::Tutorial) {}
    /// <summary>
    /// GameSceneに対応する公開処理を実行する
    /// </summary>
    GameScene(const SwordInputCalibration &inputCalibration, Mode mode)
        : inputCalibration_(inputCalibration),
          titleDemoMode_(mode == Mode::TitleDemo),
          backgroundOnlyMode_(mode == Mode::BackgroundOnly ||
                              mode == Mode::TutorialBackgroundOnly),
          readyPreviewMode_(mode == Mode::ReadyPreview),
          tutorialBackgroundMode_(mode == Mode::TutorialBackgroundOnly ||
                                  mode == Mode::Tutorial),
          tutorialMode_(mode == Mode::Tutorial) {}
    /// <summary>
    /// GameSceneに対応する公開処理を実行する
    /// </summary>
    GameScene(const SwordInputCalibration &inputCalibration,
              float combatDifficulty, Mode mode)
        : inputCalibration_(inputCalibration),
          combatDifficulty_(combatDifficulty),
          titleDemoMode_(mode == Mode::TitleDemo),
          backgroundOnlyMode_(mode == Mode::BackgroundOnly ||
                              mode == Mode::TutorialBackgroundOnly),
          readyPreviewMode_(mode == Mode::ReadyPreview),
          tutorialBackgroundMode_(mode == Mode::TutorialBackgroundOnly ||
                                  mode == Mode::Tutorial),
          tutorialMode_(mode == Mode::Tutorial) {}
    /// <summary>
    /// ~GameSceneに対応する公開処理を実行する
    /// </summary>
    ~GameScene() override;

    /// <summary>
    /// 使用するリソースと初期状態を準備する
    /// </summary>
    void Initialize(const SceneContext &ctx) override;
    /// <summary>
    /// 入力と状態を1フレーム進める
    /// </summary>
    void Update() override;
    /// <summary>
    /// 現在の状態を描画する
    /// </summary>
    void Draw() override;
    /// <summary>
    /// 透明描画パスへ必要な要素を描画する
    /// </summary>
    void DrawTransparent() override;
    /// <summary>
    /// SetReadyPreviewHeatに対応する状態を設定する
    /// </summary>
    void SetReadyPreviewHeat(float heat);

  private:
    struct ArcaneProjectileState;

    void UpdateCamera(Input *input);
#ifdef _DEBUG
    void UpdateDebugKeys(Input *input);
#endif
    void UpdateBattleCamera();
    void ApplyGameplayCameraPose(const DirectX::XMFLOAT3 &cameraPos,
                                 const DirectX::XMFLOAT3 &lookAt,
                                 float positionLerpSpeed,
                                 float lookAtLerpSpeed);
    void UpdateTutorial(float deltaTime);
    bool IsTutorialOperationStepComplete() const;
    void AdvanceTutorialOperationStep();
    void UpdateBackgroundCamera(float deltaTime);
    void UpdateReadyPreviewCamera(float deltaTime);
    void UpdateSceneLighting();
    void EmitReadyPreviewHeatParticles(float deltaTime);
    void EmitIntenseReadyPreviewParticles(
        float heat, float danger, float side, float depth, float wave,
        const DirectX::XMFLOAT3 &enemyPosition,
        const DirectX::XMFLOAT3 &corePosition,
        const DirectX::XMFLOAT3 &upwardDirection);
    void DrawArena();
    void DrawDistantHazardBackdrop(float buildProgress);
    float BackgroundBuildProgress(float delay, float duration) const;
    float BattleIntroWorldRevealProgress() const;
    void DrawVictoryFlash();
    void DrawDefeatFlash();
    float GetDefeatFadeToBlackRatio() const;
    void DrawBattleIntroFlash();
    void OpenPauseMenu();
    void ClosePauseMenu();
    void UpdatePauseMenu(Input *input);
    void ExecutePauseMenuSelection();
    void DrawPauseMenu();
    void DrawTutorialOverlay();
    int ResolveTutorialMessageIndex(ActionKind actionKind,
                                    ActionStep actionStep,
                                    float releaseRatio) const;
    void DrawTutorialMessagePanel(float width, float height, int messageIndex);
    void DrawTutorialExcellent(float width, float height);
    void DrawTutorialEntryFade();
    void DrawTutorialSlashCounter(float x, float y, float scale, float alpha);
    void DrawTutorialCounterDigit(int digit, float x, float y, float scale,
                                  const DirectX::XMFLOAT4 &color);
    void UpdateHandCameraPreview(float deltaTime);
    void DrawHandCameraPreview();
    void DrawPauseRect(float x, float y, float w, float h,
                       const DirectX::XMFLOAT4 &color);
    void DrawPauseImage(uint32_t textureId, float textureWidth,
                        float textureHeight, float x, float y, float scale,
                        float alpha = 1.0f);
    void LoadPauseMenuImages();
    void LoadTutorialImages();
    void UpdateBattleIntro(float deltaTime);
    void FinishBattleIntro();
    void ApplyEnemyIntroDissolve(float revealRatio);
    void ApplyEnemyPhaseMaterials();
    void UpdatePhaseTransitionCinematic(float deltaTime);
    void EmitPhaseTransitionStartEffects();
    void EmitPhaseTransitionLoopEffects(float deltaTime);
    void EmitPhaseTransitionReleaseEffects();
    void BeginVictorySequence();
    void UpdateVictorySequence(float deltaTime);
    void EmitVictoryEnemyVanishExplosion();
    void DrawVictoryEnemyVanishExplosionBillboards();
    struct VictoryBillboardPatch {
        uint32_t modelId = 0;
        DirectX::XMFLOAT3 offset{};
        DirectX::XMFLOAT2 scale{};
        float roll = 0.0f;
        float delay = 0.0f;
        bool fire = false;
    };
    void DrawVictoryBillboardPatch(const VictoryBillboardPatch &patch,
                                   const DirectX::XMFLOAT3 &center, float yaw,
                                   float age, float alpha);
    void BeginDefeatSequence();
    void UpdateDefeatSequence(float deltaTime);
    void SyncEnemyAnimation();
    void UpdateBladeClashEnemyAnimation(float deltaTime);
    void SelectBladeClashStartupClip(bool hasTeleport, bool hasSweep,
                                     std::string &clip, float &clipRatio) const;
    void SelectBladeClashGuardBreakClip(bool hasTeleport, bool hasSweep,
                                        std::string &clip,
                                        float &clipRatio) const;
    void SelectBladeClashWinClip(bool hasTeleport, bool hasSweep, bool hasSmash,
                                 std::string &clip, float &clipRatio) const;
    void SelectBladeClashLossClip(bool hasTeleport, bool hasSweep,
                                  bool hasSmash, std::string &clip,
                                  float &clipRatio) const;
    void UpdateBattleIntroEnemyAnimation(float deltaTime);
    void UpdateReadyPreviewEnemyAnimation();
    void UpdatePhaseTransitionEnemyAnimation(float deltaTime);
    void ApplyEnemyProceduralAnimation();
    void SetEnemyAnimationFrozen(bool frozen);
    void UpdateCombat(float gameplayDeltaTime);
    bool UpdateBladeClashPlayerSlashes();
    void InitializePostEffects(const SceneContext &ctx);
    void InitializeBattleActors(DirectXCommon *dx, TextureManager *texture);
    void InitializeBattleArenaModels(ModelManager *model,
                                     uint32_t arenaStoneTextureId);
    void BindSharedBattleArenaModels();
    void ResetBattleSessionState(ModelManager *model);
    void LoadBattleSounds(const SceneContext &ctx);
    void InitializeBattleCameraState(ModelManager *model);
    void InitializeHandTracking(const SceneContext &ctx);
    void InitializeSceneMode();
    struct GameplayTiming {
        float gameplayDeltaTime = 0.0f;
        float playerDeltaTime = 0.0f;
        float enemyDeltaTime = 0.0f;
        bool cataclysmProjectilePreviewSlow = false;
    };
    bool UpdatePreviewOrTutorialMode(float baseDeltaTime);
    bool UpdatePauseOrIntro(Input *input, float baseDeltaTime);
    bool UpdateBattleSequenceMode(Input *input, float baseDeltaTime);
    bool UpdateBladeClashFinishMode(Input *input, float baseDeltaTime);
    GameplayTiming ComputeGameplayTiming(float baseDeltaTime) const;
    void UpdateGameplayPlayer(Input *input, const GameplayTiming &timing,
                              float baseDeltaTime);
    void UpdateGameplayEnemy(float enemyDeltaTime);
    void UpdateGameplayEnemyAnimation(float enemyDeltaTime,
                                      float baseDeltaTime);
    void UpdateGameplayCombat(float gameplayDeltaTime);
    bool UpdateGameplayEndState(float baseDeltaTime);
    void UpdateGameplayParticles(float gameplayDeltaTime);
    bool UpdateTutorialExit(float deltaTime);
    bool HandleTutorialNavigationInput(Input *input);
    void UpdateTutorialFrame(Input *input, float deltaTime);
    void UpdateTutorialTimers(float deltaTime);
    bool UpdateTutorialBasicStep(float deltaTime);
    void UpdateTutorialHandPresence(float deltaTime);
    void UpdateTutorialOperationStep(float deltaTime);
    void UpdateTutorialIdlePresentation(float deltaTime);
    ActionKind GetTutorialAttackKind() const;
    bool IsTutorialRedWaitStep() const;
    bool IsTutorialGreenCutStep() const;
    void BeginTutorialAttackIfReady(float deltaTime);
    bool DidTutorialSlashStart(const std::array<bool, 2> &slashStates) const;
    void HandleTutorialRedEarlySlash(
        const std::array<bool, 2> &swordSlashStatesBeforeCombat,
        bool tutorialSlashStarted);
    void UpdateTutorialEnemyAttack(
        float deltaTime,
        const std::array<bool, 2> &swordSlashStatesBeforeCombat);
    float ComputeTutorialEnemyDeltaTime(float deltaTime) const;
    void UpdateTutorialRedWait(
        float deltaTime,
        const std::array<bool, 2> &swordSlashStatesBeforeCombat);
    void HandleTutorialEnemyActionTransition(ActionKind previousKind,
                                             ActionStep previousStep,
                                             ActionKind currentKind,
                                             ActionStep currentStep);
    void UpdateTutorialEnemyAnimation(float deltaTime);
    void UpdateTutorialCombat(
        float deltaTime,
        const std::array<bool, 2> &swordSlashStatesBeforeCombat);
    void FinishTutorialAttackState(
        float deltaTime,
        const std::array<bool, 2> &swordSlashStatesBeforeCombat);
    void UpdateTutorialGreenCutTimeout(
        float deltaTime,
        const std::array<bool, 2> &swordSlashStatesBeforeCombat);
    void ResolveFinishedTutorialAttack();
    void UpdateTutorialCounterCinematic(float deltaTime);
    struct BattleCameraContext {
        DirectX::XMFLOAT3 playerPos{};
        DirectX::XMFLOAT3 enemyPos{};
        DirectX::XMFLOAT3 enemyCameraPos{};
        ActionKind enemyActionKind = ActionKind::None;
        ActionStep enemyActionStep = ActionStep::None;
        bool enemyMeleeAction = false;
        bool enemyPressureAction = false;
        bool enemyPhaseTransition = false;
        bool tripleIaiCameraFocus = false;
        float enemyPhaseTransitionRatio = 0.0f;
    };
    bool UpdateVictoryBattleCamera(const BattleCameraContext &cameraContext);
    bool UpdateIntroBattleCamera(const BattleCameraContext &cameraContext);
    bool UpdateDefeatBattleCamera(const BattleCameraContext &cameraContext);
    bool UpdateBladeClashFinishCamera(const BattleCameraContext &cameraContext);
    bool UpdateBladeClashWinFinishCamera(
        const BattleCameraContext &cameraContext,
        const DirectX::XMFLOAT2 &line, const DirectX::XMFLOAT3 &right,
        float strike, float hold);
    void ApplyBladeClashWinPrepCamera(
        const DirectX::XMFLOAT2 &line, const DirectX::XMFLOAT3 &right,
        float cameraSnap, const DirectX::XMFLOAT3 &guardBreakCamera,
        const DirectX::XMFLOAT3 &guardBreakLookAt,
        DirectX::XMFLOAT3 &cameraPosition, DirectX::XMFLOAT3 &lookAt) const;
    void ApplyBladeClashFinishCameraPose(
        const DirectX::XMFLOAT2 &line, DirectX::XMFLOAT3 cameraPosition,
        DirectX::XMFLOAT3 lookAt, float targetFov);
    bool UpdateBladeClashLossFinishCamera(
        const DirectX::XMFLOAT2 &line, const DirectX::XMFLOAT3 &right,
        const DirectX::XMFLOAT3 &center, float strike, float hold);
    void ConfigureBattleCameraFov(const BattleCameraContext &cameraContext);
    bool UpdateActiveBladeClashCamera(const BattleCameraContext &cameraContext);
    bool UpdatePlayerViewBattleCamera(const BattleCameraContext &cameraContext);
    void
    UpdateThirdPersonBattleCamera(const BattleCameraContext &cameraContext);
    void UpdateThirdPersonLockAssist(const BattleCameraContext &cameraContext);
    DirectX::XMFLOAT3
    ComputeThirdPersonCameraPosition(const BattleCameraContext &cameraContext,
                                     const DirectX::XMFLOAT3 &forward,
                                     const DirectX::XMFLOAT3 &right);
    DirectX::XMFLOAT3
    ComputeLockOnCameraPosition(const BattleCameraContext &cameraContext);
    DirectX::XMFLOAT3 ComputeFreeCameraPosition(
        const BattleCameraContext &cameraContext,
        const DirectX::XMFLOAT3 &cameraTargetBase,
        const DirectX::XMFLOAT3 &forward, const DirectX::XMFLOAT3 &right);
    DirectX::XMFLOAT3
    ComputeThirdPersonLookAt(const BattleCameraContext &cameraContext,
                             const DirectX::XMFLOAT3 &cameraTargetBase,
                             const DirectX::XMFLOAT3 &forward);
    struct CombatFrameContext {
        std::array<CollisionManager::BodyId, 3> enemyHurtBodies{};
        ActionKind enemyActionKind = ActionKind::None;
        ActionStep enemyActionStep = ActionStep::None;
        std::array<const Sword *, 2> swords{};
        std::array<bool, 2> swordSlashStates{};
        std::array<float, 2> swordAttackDamages{};
        OBB enemyAttackBox{};
        CollisionManager::BodyId enemyAttackBody =
            CollisionManager::kInvalidBodyId;
        float enemyAttackDamage = 0.0f;
        float enemyAttackKnockback = 0.0f;
        bool enemyMeleeActive = false;
        bool enemyAttackCommitted = false;
        bool enemyBladeClashCounterWindow = false;
        bool enemyCounterWindow = false;
        bool enemyMeleeDamagePending = false;
        bool startCounterCinematic = false;
        bool stopCounterCinematic = false;
        bool forceSyncEnemyAnimation = false;
        bool counterTriggered = false;
        bool projectileReflected = false;
    };
    void InitializeCombatFrame(float gameplayDeltaTime,
                               CombatFrameContext &combat);
    void ConfigureCombatWindows(CombatFrameContext &combat);
    void ConfigureMeleeCombatWindows(CombatFrameContext &combat);
    bool ConfigureCounterCombatWindows(CombatFrameContext &combat);
    void ConfigureEnemyAttackCollision(CombatFrameContext &combat,
                                       bool attackCommitted);
    void TriggerSuccessfulCounter(CombatFrameContext &combat, size_t swordIndex,
                                  float enemyDamage, float hitCooldown);
    bool IsEnemyHurtBodyHit(const CombatFrameContext &combat,
                            CollisionManager::BodyId attackBody) const;
    bool
    IsProjectileInDeflectRange(const ArcaneProjectileState &projectile) const;
    bool IsProjectileSlashAligned(const ArcaneProjectileState &projectile,
                                  const Sword &sword) const;
    void HandleBadSlashPunish(CombatFrameContext &combat);
    void ProcessSwordAttacks(CombatFrameContext &combat);
    bool ProcessSwordAttack(CombatFrameContext &combat, size_t swordIndex);
    bool TrySwordCounter(CombatFrameContext &combat, size_t swordIndex,
                         const Sword &sword);
    bool TryReflectProjectiles(CombatFrameContext &combat, size_t swordIndex,
                               const Sword &sword);
    bool TryReflectProjectile(CombatFrameContext &combat, size_t swordIndex,
                              const Sword &sword,
                              ArcaneProjectileState &projectile,
                              bool slashStarted);
    bool TryNormalSwordHit(CombatFrameContext &combat, size_t swordIndex,
                           const Sword &sword, bool hitBody);
    bool ProcessReflectedProjectileHit(CombatFrameContext &combat,
                                       ArcaneProjectileState &projectile);
    bool ProcessHostileProjectileHit(CombatFrameContext &combat,
                                     ArcaneProjectileState &projectile);
    void ResolveEnemyMeleeDamage(CombatFrameContext &combat);
    void FinishCombatFrame(CombatFrameContext &combat);
    void BeginBladeClash(size_t swordIndex);
    void UpdateBladeClash(float gameplayDeltaTime);
    void FinishBladeClash(bool playerWon);
    void DrawBladeClashOverlay();
    void UpdateBladeClashFinish(float deltaTime);
    void EmitBladeClashWinGuardBreak();
    void UpdateBladeClashWinFinish(float bladeClashWinActionTimer);
    void UpdateBladeClashWinPose(float bladeClashWinActionTimer);
    void UpdateBladeClashWinActionPose(float bladeClashWinActionTimer,
                                       float finishYaw);
    void UpdateBladeClashLossFinish();
    float AdvanceBladeClashFinishTimer(float deltaTime);
    void ApplyBladeClashFinishPostProcess();
    void CompleteBladeClashFinish();
    void DrawBladeClashFinishFrame();
    void DispatchCombatFeedback(const CombatFeedbackEvent &event);
    void PrepareCombatFeedback(const CombatFeedbackEvent &event,
                               bool suppressSwordVfx);
    void EmitCombatFeedbackVfx(const CombatFeedbackEvent &event,
                               bool suppressSwordVfx);
    void PlayCombatFeedbackSound(const CombatFeedbackEvent &event);
    void EmitCombatParticles(const CombatFeedbackEvent &event);
    void EmitEnemyActionParticles(ActionKind kind, ActionStep step);
    void EmitArcaneLaserParticles(float deltaTime);
    void EmitArcaneLaserChargeParticles(bool cataclysmLaser,
                                        const DirectX::XMFLOAT3 &muzzle,
                                        const DirectX::XMFLOAT3 &direction);
    void EmitArcaneProjectileTrail(const ArcaneProjectileState &projectile,
                                   const DirectX::XMFLOAT3 &fallbackDirection);
    void
    EmitArcaneProjectileTrailBursts(const ArcaneProjectileState &projectile,
                                    const DirectX::XMFLOAT3 &projectileDir);
    void EmitArcaneProjectileTrailSmoke(const ArcaneProjectileState &projectile,
                                        const DirectX::XMFLOAT3 &projectileDir);
    bool HasReflectedArcaneProjectiles() const;
    void EmitEnemyCueParticles(float deltaTime);
    bool EmitArcaneProjectileCue(const ArcaneProjectileState &projectile);
    bool EmitArcaneProjectileCues();
    void EmitTripleIaiCueLines();
    void UpdateFarSlashCueParticles(float deltaTime, ActionStep step,
                                    bool farWarpSlashActive);
    bool AdjustTutorialReleaseCue(ActionKind kind,
                                  bool &releaseCounterCueVisible) const;
    bool SupportsEnemyChargeCue(ActionKind kind, ActionStep step,
                                bool farWarpSlashActive) const;
    bool ShouldSuppressEnemyAttackCue(bool releaseCounterCueVisible,
                                      bool farWarpSlashActive) const;
    void EmitEnemyAttackCueLine(ActionKind kind, bool releaseCounterCueVisible);
    void BeginArcaneProjectileVolley();
    void UpdateArcaneProjectileVolley(float deltaTime);
    bool FireCataclysmVolleyProjectile();
    bool HasActiveArcaneProjectiles() const;
    void SpawnArcaneProjectile();
    ArcaneProjectileState *FindFreeArcaneProjectile(bool cataclysmShot);
    void ConfigureCataclysmProjectileAim(DirectX::XMFLOAT3 &muzzle,
                                         DirectX::XMFLOAT3 &direction) const;
    void ConfigureArcaneArcProjectileAim(DirectX::XMFLOAT3 &muzzle,
                                         DirectX::XMFLOAT3 &direction) const;
    void InitializeArcaneProjectileState(ArcaneProjectileState &projectile,
                                         bool cataclysmShot, bool arcaneArcShot,
                                         const DirectX::XMFLOAT3 &muzzle,
                                         const DirectX::XMFLOAT3 &direction);
    void ResetArcaneProjectile();
    void UpdateArcaneProjectile(float deltaTime);
    void UpdateSingleArcaneProjectile(ArcaneProjectileState &projectile,
                                      float deltaTime);
    void UpdateReflectedArcaneProjectile(ArcaneProjectileState &projectile,
                                         float deltaTime);
    void UpdateArcingArcaneProjectile(ArcaneProjectileState &projectile,
                                      float deltaTime);
    void UpdateHomingArcaneProjectile(ArcaneProjectileState &projectile,
                                      float deltaTime);
    void FinishArcaneProjectileFrame(ArcaneProjectileState &projectile,
                                     float deltaTime);
    void UpdatePlayerChargedProjectile(float deltaTime);
    void ReflectArcaneProjectile(size_t swordIndex);
    void ReflectArcaneProjectile(ArcaneProjectileState &projectile,
                                 size_t swordIndex);
    float GetEnemyProjectileSpeedScale() const;
    float GetEnemyRangedVolleyInterval() const;
    void EmitArcaneProjectileExplosion(const ArcaneProjectileState &projectile,
                                       const DirectX::XMFLOAT3 &position,
                                       const DirectX::XMFLOAT3 &direction,
                                       bool hitEnemy);
    DirectX::XMFLOAT2 GetArcaneProjectileCueDirection() const;
    uint32_t GetCurrentEnemyTextureId() const;
    void ApplyBulletTextureToModel(uint32_t textureId);
    DirectX::XMFLOAT2 ProjectWorldDirectionToCueDirection(
        const DirectX::XMFLOAT3 &worldDir) const;
    void DrawArcaneProjectile();
    void DrawPlayerChargedProjectile();
    void UpdateBattlePostProcessState(float deltaTime);
    PlayerCombatObservation BuildPlayerCombatObservation() const;
    float ComputeGameplayTimeScale() const;
    bool ShouldSuppressSwordVfx() const;
    void ClearSwordVfx();
    void ClearGameplayParticles();
    void UpdateSwordVfx(float deltaTime);
    float GetDifficultyRatio() const;
    float GetHighDifficultyPressure() const;
    float GetCounterCinematicDuration() const;
    float GetCounterVulnerabilityDuration() const;
    float GetCounterPlayerHitCooldown(float baseCooldown) const;
    float GetEnemyNormalHitCooldown() const;
    float ApplyEnemyDamage(float damage, bool deferTransitions = false,
                           bool triggerHitReaction = true);
    float ApplyPlayerDamage(float enemyAttackDamage);
    void StartBattleBgm();
    void StopBattleBgm();
    void UpdateEnemyWarpSound(ActionKind previousKind, ActionStep previousStep,
                              ActionKind currentKind, ActionStep currentStep);
    void PlayEnemyWarpSound();

  private:
    SwordInputCalibration inputCalibration_{};
    float combatDifficulty_ = 5.0f;
    bool titleDemoMode_ = false;
    bool backgroundOnlyMode_ = false;
    bool readyPreviewMode_ = false;
    bool tutorialBackgroundMode_ = false;
    bool tutorialMode_ = false;

    Camera camera_;

    Player player_;
    Enemy enemy_;
    GameSceneHud hud_;
    CollisionManager collisionManager_;
    CombatFeedbackDirector combatFeedback_;
    CameraPreviewReceiver cameraPreviewReceiver_{};
    GPUParticleSystem sparkParticles_;
    GPUParticleSystem explosionParticles_;
    GPUParticleSystem smokeParticles_;
    GPUParticleSystem swordFlashParticles_;
    SwordTrailRenderer swordTrailRenderer_;
    SwordSlashArcRenderer swordSlashArcRenderer_;
    struct ArcaneProjectileState {
        bool active = false;
        bool reflected = false;
        bool cataclysm = false;
        bool fromAbove = false;
        bool waitingToFire = false;
        DirectX::XMFLOAT3 position = {0.0f, 0.0f, 0.0f};
        DirectX::XMFLOAT3 previousPosition = {0.0f, 0.0f, 0.0f};
        DirectX::XMFLOAT3 velocity = {0.0f, 0.0f, 0.0f};
        DirectX::XMFLOAT3 reflectedLaunchDirection = {0.0f, 0.0f, 1.0f};
        float age = 0.0f;
        float life = 0.0f;
        float damage = 0.0f;
        float knockback = 0.0f;
        uint32_t textureId = 0;
        DirectX::XMFLOAT2 cueDirection = {1.0f, 0.0f};
        size_t reflectedBySwordIndex = 0;
    };
    struct PlayerChargedProjectileState {
        bool active = false;
        DirectX::XMFLOAT3 position = {0.0f, 0.0f, 0.0f};
        DirectX::XMFLOAT3 velocity = {0.0f, 0.0f, 0.0f};
        DirectX::XMFLOAT3 direction = {0.0f, 0.0f, 1.0f};
        float age = 0.0f;
        float life = 0.0f;
        float damage = 0.0f;
        bool hitConsumed = false;
    };
    ArcaneProjectileState arcaneProjectile_{};
    PlayerChargedProjectileState playerChargedProjectile_{};
    static constexpr int kCataclysmProjectileCapacity_ = 5;
    std::array<ArcaneProjectileState, kCataclysmProjectileCapacity_>
        cataclysmProjectiles_{};
    bool arcaneProjectileVolleyActive_ = false;
    bool arcaneProjectileVolleyCataclysm_ = false;
    int arcaneProjectileVolleyShotsFired_ = 0;
    int arcaneProjectileVolleyReflectedHits_ = 0;
    float arcaneProjectileVolleyTimer_ = 0.0f;
    uint32_t particleTextureId_ = 0;
    uint32_t playerModelId_ = 0;
    uint32_t swordModelId_ = 0;
    uint32_t enemyModelId_ = 0;
    uint32_t bulletModelId_ = 0;
    uint32_t victoryFireBillboardModelId_ = 0;
    uint32_t victorySmokeBillboardModelId_ = 0;
    uint32_t victoryDarkSmokeBillboardModelId_ = 0;
    EnemyPhaseMaterialSet enemyPhaseMaterials_{};
    uint32_t arenaFloorModelId_ = 0;
    uint32_t arenaLowPolyTerrainModelId_ = 0;
    uint32_t arenaDistantTerrainModelId_ = 0;
    uint32_t arenaHazardSpireModelId_ = 0;
    uint32_t arenaHazardGlowRingModelId_ = 0;
    uint32_t arenaCityTowerModelId_ = 0;
    uint32_t arenaCityWindowModelId_ = 0;
    uint32_t arenaGiantBodyModelId_ = 0;
    uint32_t arenaGiantHeadModelId_ = 0;
    uint32_t arenaCenterDiskModelId_ = 0;
    uint32_t arenaSpokeModelId_ = 0;
    uint32_t arenaTutorialSpokeModelId_ = 0;
    uint32_t arenaInnerRingModelId_ = 0;
    uint32_t arenaOuterRingModelId_ = 0;
    uint32_t arenaColumnModelId_ = 0;
    uint32_t arenaColumnCapModelId_ = 0;
    uint32_t arenaDomeModelId_ = 0;
    uint32_t arenaBarrierRingModelId_ = 0;
    uint32_t chargeWeakPointModelId_ = 0;
    uint32_t slashSoundId_ = 0;
    uint32_t normalHitSlashSoundId_ = 0;
    uint32_t enemyReleaseSoundId_ = 0;
    uint32_t hitSoundId_ = 0;
    uint32_t counterSoundId_ = 0;
    uint32_t damageSoundId_ = 0;
    uint32_t counterSuccessSlashSoundId_ = 0;
    uint32_t mistimedCounterSoundId_ = 0;
    uint32_t explosionSoundId_ = 0;
    uint32_t victoryExplosionSoundId_ = 0;
    uint32_t warpSoundId_ = UINT32_MAX;
    uint32_t battleBgmSoundId_ = UINT32_MAX;
    uint32_t battleBgmVoiceHandle_ = UINT32_MAX;
    bool soundsLoaded_ = false;
    float mistimedCounterSoundStartSeconds_ = 0.0f;
    std::array<bool, Player::kSwordCount> previousSwordSoundStates_{};
    float arcaneLaserParticleTimer_ = 0.0f;
    float farSlashChargeParticleTimer_ = 0.0f;
    uint32_t arenaNoiseTextureId_ = 0;
    std::string enemyAnimationName_;
    bool enemyAnimationLoop_ = true;

    float playerHitCooldown_ = 0.0f;
    float enemyHitCooldown_ = 0.0f;
    bool enemyMeleeHitConsumed_ = false;
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

    float lockOnLookPlayerWeight_ = 0.32f;
    float lockOnLookEnemyWeight_ = 0.68f;

    float lockOnDistanceMin_ = 2.5f;
    float lockOnDistanceMax_ = 11.0f;

    float lockOnOrbitRadius_ = 5.4f;
    float lockOnOrbitHeight_ = 2.95f;
    float lockOnOrbitSideBias_ = 0.55f;
    float lockOnOrbitLerpSpeed_ = 6.2f;
    float lockOnOrbitPullBackMax_ = 1.8f;
    DirectX::XMFLOAT3 lockOnOrbitCameraPos_ = {0.0f, 0.0f, 0.0f};

    float lockOnLookAtLerpSpeed_ = 8.2f;
    DirectX::XMFLOAT3 lockOnLookAt_ = {0.0f, 0.0f, 0.0f};

    float currentFovDeg_ = 82.0f;
    float targetFovDeg_ = 82.0f;
    float normalFovDeg_ = 82.0f;
    float lockOnFovDeg_ = 82.0f;
    float phaseTransitionFovDeg_ = 68.0f;
    float fovLerpSpeed_ = 6.5f;
    float phaseTransitionFovLerpSpeed_ = 8.0f;
    float phaseTransitionLookAtEnemyWeight_ = 0.96f;
    float phaseTransitionLookAtHeight_ = 1.55f;
    float phaseTransitionCameraPullBack_ = 2.25f;
    float phaseTransitionCameraRise_ = 1.35f;
    bool playerViewCamera_ = false;
    float playerViewEyeHeight_ = 1.42f;
    float playerViewForwardOffset_ = 0.10f;
    float playerViewSideOffset_ = 0.12f;
    float playerViewLookAhead_ = 8.0f;
    float playerViewLockOnLookHeight_ = 1.48f;
    float gameplayCameraPositionLerpSpeed_ = 7.8f;
    float gameplayCameraLookAtLerpSpeed_ = 10.5f;
    float playerViewCameraPositionLerpSpeed_ = 10.0f;
    float playerViewCameraLookAtLerpSpeed_ = 12.0f;
    bool gameplayCameraPoseInitialized_ = false;
    DirectX::XMFLOAT3 gameplayCameraPos_ = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 gameplayCameraLookAt_ = {0.0f, 0.0f, 0.0f};

    float sceneLightTime_ = 0.0f;
    float backgroundBuildTimer_ = 0.0f;
    float readyPreviewHeat_ = 0.0f;
    float readyPreviewParticleTimer_ = 0.0f;
    float battleElapsedTime_ = 0.0f;
    bool battleIntroActive_ = true;
    float battleIntroTimer_ = 0.0f;
    float battleIntroDuration_ = 4.35f;
    bool battleIntroRevealEmitted_ = false;
    bool phaseTransitionWasActive_ = false;
    bool phaseTransitionReleaseEmitted_ = false;
    float phaseTransitionLoopTimer_ = 0.0f;
    PostEffectLayerId postEffectCinematicLayer_ = 0;
    bool battleResultRequested_ = false;
    bool paused_ = false;
    bool pausePostProcessSaved_ = false;
    PostProcessProfile pauseSavedPostProcess_{};
    int pauseMenuIndex_ = 0;
    bool pauseExitFadeActive_ = false;
    float pauseExitFadeTimer_ = 0.0f;
    int pauseExitTarget_ = 0;
    bool pauseMenuImagesLoaded_ = false;
    std::array<uint32_t, 4> pauseMenuTextureIds_{};
    std::array<float, 4> pauseMenuTextureWidths_{};
    std::array<float, 4> pauseMenuTextureHeights_{};
    bool victorySequenceActive_ = false;
    float victorySequenceTimer_ = 0.0f;
    float victorySequenceDuration_ = 5.45f;
    float victoryClearTime_ = 0.0f;
    bool victoryFinalExplosionEmitted_ = false;
    DirectX::XMFLOAT3 victoryEnemyStartPos_ = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 victoryFinalExplosionCenter_ = {0.0f, 0.0f, 0.0f};
    bool defeatSequenceActive_ = false;
    float defeatSequenceTimer_ = 0.0f;
    float defeatSequenceDuration_ = 2.75f;
    bool defeatImpactEmitted_ = false;

    bool counterCinematicActive_ = false;
    bool enemyAnimationFrozen_ = false;
    float counterCinematicTimer_ = 0.0f;
    float counterCinematicDuration_ = 0.66f;
    float counterTimeScale_ = 0.05f;

    bool enemyRedPunishUncounterable_ = false;
    bool mistimedCounterSlashThisFrame_ = false;
    bool counterSuccessSlashThisFrame_ = false;
    bool enemyLaserHitConsumed_ = false;
    std::array<bool, Player::kSwordCount> previousCombatSlashStates_{};
    std::array<bool, Player::kSwordCount> normalSlashHitConsumed_{};
    std::array<float, Player::kSwordCount> normalSlashRearmTimers_{};
    bool bladeClashActive_ = false;
    float bladeClashTimer_ = 0.0f;
    float bladeClashDuration_ = 4.8f;
    float bladeClashGauge_ = 0.0f;
    float bladeClashEnemyPushSpeed_ = 0.29f;
    float bladeClashSlashPush_ = 0.22f;
    std::array<bool, Player::kSwordCount> bladeClashPreviousSlashStates_{};
    DirectX::XMFLOAT3 bladeClashPlayerLosePos_ = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 bladeClashPlayerWinPos_ = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 bladeClashPlayerFixedPos_ = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 bladeClashCenter_ = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 bladeClashDirection_ = {0.0f, 0.0f, 1.0f};
    float bladeClashCameraPush_ = 0.0f;
    float bladeClashImpactPulse_ = 0.0f;
    float bladeClashEnemySurgeTimer_ = 0.0f;
    float bladeClashChainTimer_ = 0.0f;
    int bladeClashSlashChain_ = 0;
    bool bladeClashFinishActive_ = false;
    bool bladeClashFinishPlayerWon_ = false;
    bool bladeClashFinishImpactEmitted_ = false;
    bool bladeClashFinishSkidEmitted_ = false;
    bool bladeClashFinishGuardBreakEmitted_ = false;
    bool bladeClashFinishWallImpactEmitted_ = false;
    bool bladeClashFinishPendingEnemyTransition_ = false;
    float bladeClashFinishTimer_ = 0.0f;
    float bladeClashFinishDuration_ = 2.05f;
    DirectX::XMFLOAT3 bladeClashFinishCenter_ = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 bladeClashFinishPlayerStart_ = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 bladeClashFinishPlayerEnd_ = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 bladeClashFinishEnemyStart_ = {0.0f, 0.0f, 0.0f};
    bool handTrackingStartRequested_ = false;
    float tutorialTimer_ = 0.0f;
    float tutorialEntryFadeTimer_ = 0.0f;
    float tutorialExitFadeTimer_ = 0.0f;
    float tutorialAttackDelay_ = 1.8f;
    float tutorialSuccessTimer_ = 0.0f;
    float tutorialMissTimer_ = 0.0f;
    float tutorialExcellentTimer_ = 0.0f;
    float tutorialHandHoldTimer_ = 0.0f;
    float tutorialRedWaitTimer_ = 0.0f;
    float tutorialGreenCutTimer_ = 0.0f;
    int tutorialStep_ = 0;
    int tutorialPendingStep_ = -1;
    int tutorialPendingAttackIndexIncrement_ = 0;
    int tutorialOperationSlashCount_ = 0;
    int tutorialAttackIndex_ = 0;
    bool tutorialAttackInProgress_ = false;
    bool tutorialCounterSuccess_ = false;
    bool tutorialExitRequested_ = false;
    bool tutorialExitToSelect_ = false;
    bool tutorialImagesLoaded_ = false;
    std::array<uint32_t, 12> tutorialTextureIds_{};
    std::array<float, 12> tutorialTextureWidths_{};
    std::array<float, 12> tutorialTextureHeights_{};
    std::array<uint32_t, 10> tutorialDigitTextureIds_{};
    std::array<float, 10> tutorialDigitTextureWidths_{};
    std::array<float, 10> tutorialDigitTextureHeights_{};
    uint32_t tutorialSlashTextureId_ = 0;
    float tutorialSlashTextureWidth_ = 0.0f;
    float tutorialSlashTextureHeight_ = 0.0f;
#ifdef _DEBUG
    bool debugPlayerInvincible_ = false;
#endif
};
