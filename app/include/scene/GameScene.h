#pragma once
#include "BaseScene.h"
#include "Camera.h"
#include "CameraPreviewReceiver.h"
#include "CollisionManager.h"
#include "CombatFeedbackDirector.h"
#include "Enemy.h"
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

    explicit GameScene(const SwordInputCalibration &inputCalibration = {})
        : inputCalibration_(inputCalibration) {}
    explicit GameScene(bool titleDemoMode) : titleDemoMode_(titleDemoMode) {}
    explicit GameScene(Mode mode)
        : titleDemoMode_(mode == Mode::TitleDemo),
          backgroundOnlyMode_(mode == Mode::BackgroundOnly ||
                              mode == Mode::TutorialBackgroundOnly),
          readyPreviewMode_(mode == Mode::ReadyPreview),
          tutorialBackgroundMode_(mode == Mode::TutorialBackgroundOnly) {}
    GameScene(const SwordInputCalibration &inputCalibration, Mode mode)
        : inputCalibration_(inputCalibration),
          titleDemoMode_(mode == Mode::TitleDemo),
          backgroundOnlyMode_(mode == Mode::BackgroundOnly ||
                              mode == Mode::TutorialBackgroundOnly),
          readyPreviewMode_(mode == Mode::ReadyPreview),
          tutorialBackgroundMode_(mode == Mode::TutorialBackgroundOnly),
          tutorialMode_(mode == Mode::Tutorial) {}
    ~GameScene() override;

    void Initialize(const SceneContext &ctx) override;
    void Update() override;
    void Draw() override;
    void DrawTransparent() override;

  private:
    void UpdateCamera(Input *input);
    void UpdateBattleCamera();
    void UpdateTutorial(float deltaTime);
    void UpdateBackgroundCamera(float deltaTime);
    void UpdateReadyPreviewCamera(float deltaTime);
    void UpdateSceneLighting();
    void DrawArena();
    void DrawDistantHazardBackdrop(float buildProgress);
    void DrawBladeClashFinishBackdrop();
    float BackgroundBuildProgress(float delay, float duration) const;
    float BattleIntroWorldRevealProgress() const;
    void DrawVictoryFlash();
    void DrawDefeatFlash();
    void DrawBattleIntroFlash();
    void OpenPauseMenu();
    void ClosePauseMenu();
    void UpdatePauseMenu(Input *input);
    void ExecutePauseMenuSelection();
    void DrawPauseMenu();
    void DrawTutorialOverlay();
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
    void UpdatePhaseTransitionCinematic(float deltaTime);
    void EmitPhaseTransitionStartEffects();
    void EmitPhaseTransitionLoopEffects(float deltaTime);
    void EmitPhaseTransitionReleaseEffects();
    void BeginVictorySequence();
    void UpdateVictorySequence(float deltaTime);
    void BeginDefeatSequence();
    void UpdateDefeatSequence(float deltaTime);
    void SyncEnemyAnimation();
    void UpdateBladeClashEnemyAnimation(float deltaTime);
    void UpdateBattleIntroEnemyAnimation(float deltaTime);
    void UpdatePhaseTransitionEnemyAnimation(float deltaTime);
    void ApplyEnemyProceduralAnimation();
    void SetEnemyAnimationFrozen(bool frozen);
    void UpdateCombat(float gameplayDeltaTime);
    void TriggerDebugBladeClash();
    void BeginBladeClash(size_t swordIndex);
    void UpdateBladeClash(float gameplayDeltaTime);
    void FinishBladeClash(bool playerWon);
    void DrawBladeClashOverlay();
    void UpdateBladeClashFinish(float deltaTime);
    float AdvanceBladeClashFinishTimer(float deltaTime);
    void ApplyBladeClashFinishPostProcess();
    void CompleteBladeClashFinish();
    void DrawBladeClashFinishFrame();
    void DispatchCombatFeedback(const CombatFeedbackEvent &event);
    void EmitCombatParticles(const CombatFeedbackEvent &event);
    void EmitEnemyActionParticles(ActionKind kind, ActionStep step);
    void EmitEnemyCueParticles(float deltaTime);
    void UpdateBattlePostProcessState(float deltaTime);
    PlayerCombatObservation BuildPlayerCombatObservation() const;
    float ComputeGameplayTimeScale() const;
    void UpdateSwordVfx(float deltaTime);
    float ApplyEnemyDamage(float damage, bool deferTransitions = false);

  private:
    SwordInputCalibration inputCalibration_{};
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
    uint32_t particleTextureId_ = 0;
    uint32_t playerModelId_ = 0;
    uint32_t swordModelId_ = 0;
    uint32_t enemyModelId_ = 0;
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
    uint32_t arenaInnerRingModelId_ = 0;
    uint32_t arenaOuterRingModelId_ = 0;
    uint32_t arenaColumnModelId_ = 0;
    uint32_t arenaColumnCapModelId_ = 0;
    uint32_t arenaDomeModelId_ = 0;
    uint32_t arenaBarrierRingModelId_ = 0;
    uint32_t chargeWeakPointModelId_ = 0;
    uint32_t slashSoundId_ = 0;
    uint32_t enemyReleaseSoundId_ = 0;
    uint32_t hitSoundId_ = 0;
    uint32_t counterSoundId_ = 0;
    uint32_t damageSoundId_ = 0;
    uint32_t explosionSoundId_ = 0;
    bool soundsLoaded_ = false;
    std::array<bool, Player::kSwordCount> previousSwordSoundStates_{};
    float enemyCueParticleTimer_ = 0.0f;
    uint32_t arenaNoiseTextureId_ = 0;
    std::string enemyAnimationName_{};
    bool enemyAnimationLoop_ = true;

    float playerHitCooldown_ = 0.0f;
    float enemyHitCooldown_ = 0.0f;
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
    float phaseTransitionFovDeg_ = 58.0f;
    float fovLerpSpeed_ = 6.5f;
    float phaseTransitionFovLerpSpeed_ = 8.0f;
    float phaseTransitionLookAtEnemyWeight_ = 0.92f;
    float phaseTransitionLookAtHeight_ = 1.90f;
    float phaseTransitionPushIn_ = 2.05f;
    bool playerViewCamera_ = false;
    float playerViewEyeHeight_ = 1.42f;
    float playerViewForwardOffset_ = 0.10f;
    float playerViewSideOffset_ = 0.12f;
    float playerViewLookAhead_ = 8.0f;
    float playerViewLockOnLookHeight_ = 1.48f;

    float sceneLightTime_ = 0.0f;
    float backgroundBuildTimer_ = 0.0f;
    float battleElapsedTime_ = 0.0f;
    bool battleIntroActive_ = true;
    float battleIntroTimer_ = 0.0f;
    float battleIntroDuration_ = 4.35f;
    bool battleIntroRevealEmitted_ = false;
    bool phaseTransitionWasActive_ = false;
    bool phaseTransitionReleaseEmitted_ = false;
    float phaseTransitionLoopTimer_ = 0.0f;
    bool battleResultRequested_ = false;
    bool paused_ = false;
    bool pausePostProcessSaved_ = false;
    PostProcessProfile pauseSavedPostProcess_{};
    int pauseMenuIndex_ = 0;
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
    std::array<bool, Player::kSwordCount> previousCombatSlashStates_{};
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
    float tutorialAttackDelay_ = 1.2f;
    float tutorialSuccessTimer_ = 0.0f;
    float tutorialMissTimer_ = 0.0f;
    int tutorialAttackIndex_ = 0;
    bool tutorialAttackInProgress_ = false;
    bool tutorialCounterSuccess_ = false;
    bool tutorialImagesLoaded_ = false;
    std::array<uint32_t, 7> tutorialTextureIds_{};
    std::array<float, 7> tutorialTextureWidths_{};
    std::array<float, 7> tutorialTextureHeights_{};
};
