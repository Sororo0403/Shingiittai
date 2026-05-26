#pragma once
#include "Camera.h"
#include "EnemyActionData.h"
#include "OBB.h"
#include "Player.h"
#include "Transform.h"
#include <cstddef>
#include <cstdint>

class ModelManager;

enum class ActionStep {
    None,
    Start,
    Charge,
    Active,
    Recovery,
    Move,
    Hold,
    End,
};

enum class WarpApproachSlot { None, Front, Back };

// Enemy combat is driven by a two-part FSM:
// - ActionKind decides which behavior is currently running.
// - ActionStep tracks the local phase within that behavior.
struct ActionState {
    ActionKind kind = ActionKind::None;
    ActionStep step = ActionStep::None;
};

struct WarpContext {
    WarpApproachSlot approachSlot = WarpApproachSlot::None;
    DirectX::XMFLOAT3 targetPos = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 departurePos = {0.0f, 0.0f, 0.0f};
    float targetYaw = 0.0f;
    ActionKind followupKind = ActionKind::None;
    ActionStep followupStep = ActionStep::None;
    bool isCutIn = false;
    bool isFeint = false;
    bool feintFollowup = false;
    bool immediateFollowup = false;
    bool farSlashFollowup = false;
    bool phantomChain = false;
    bool phantomFinal = false;
    int phantomViewWarpsRemaining = 0;
    bool faceLivePlayerOnEnd = false;
    bool collisionDisabled = false;
    bool hasValidTarget = false;
    bool hasDeparturePos = false;
    bool hasTargetYaw = false;
};

struct WarpTrailGhost {
    DirectX::XMFLOAT3 position = {0.0f, 0.0f, 0.0f};
    float life = 0.0f;
    float scale = 1.0f;
    bool isActive = false;
};

// 仕様書に合わせて BodyCenter -> BodyRight に整理

// 上位戦術
enum class TacticState {
    Melee,
    Chase
};

enum class BossPhase { Phase1, Phase2, Phase3 };

struct PlayerCombatObservation {
    DirectX::XMFLOAT3 position = {0.0f, 0.0f, 0.0f};
    bool isAttacking = false;
};

struct AttackTimingParam {
    float totalTime = 1.0f;
    float trackingEndTime = 0.0f;
    float activeStartTime = 0.0f;
    float activeEndTime = 0.0f;
    float recoveryStartTime = 0.0f;
};

struct RangeF {
    float min = 0.0f;
    float max = 0.0f;
};

struct EnemyCoreConfig {
    float maxHp = 1080.0f;
    float phase2HealthRatioThreshold = 0.74f;
    float phase3HealthRatioThreshold = 0.37f;
    float nearAttackDistance = 4.0f;
};

struct EnemyAttackProfile {
    AttackParam attack{};
    AttackTimingParam timing{};
    float chargeTime = 0.0f;
};

struct EnemyMeleeAttackProfile {
    EnemyAttackProfile base{};
    RangeF holdTime{};
    float feintChance = 0.0f;
};

struct EnemySmashConfig {
    EnemyMeleeAttackProfile melee{};
    float attackForwardOffset = 0.0f;
    float attackHeightOffset = 0.0f;
};

struct EnemySweepConfig {
    EnemyMeleeAttackProfile melee{};
    float attackSideOffset = 0.0f;
    float attackHeightOffset = 0.0f;
};

struct EnemyBladeClashConfig {
    EnemyAttackProfile profile = {
        {12.0f, 4.0f, {1.75f, 1.60f, 1.95f}},
        {1.46f, 0.72f, 0.0f, 0.88f, 0.88f},
        0.72f};
    float advanceSpeed = 3.8f;
};

struct EnemyAttackSet {
    EnemySmashConfig smash = {{{{15.0f, 4.0f, {2.8f, 2.1f, 3.2f}},
                                {1.20f, 0.86f, 0.05f, 0.22f, 0.38f}, 1.64f},
                               {0.42f, 0.86f}, 0.62f},
                              1.4f, 0.8f};
    EnemySweepConfig sweep = {{{{15.0f, 4.0f, {5.0f, 1.65f, 2.6f}},
                                {1.12f, 0.78f, 0.05f, 0.20f, 0.36f}, 1.52f},
                               {0.38f, 0.78f}, 0.48f},
                              0.2f, 0.8f};
    EnemyBladeClashConfig bladeClash{};
};

struct EnemyWarpConfig {
    float startTime = 0.55f;
    float moveTime = 0.24f;
    float endTime = 0.48f;
};

struct EnemyConfig {
    EnemyCoreConfig core{};
    EnemyAttackSet attacks{};
    EnemyWarpConfig warp{};
};

struct EnemyRuntimeState {
    float hp = 1000.0f;
    bool isDying = false;
    bool deathFinished = false;
    float hitReactionTimer = 0.0f;
    float counterRecoilTimer = 0.0f;
    float deathTimer = 0.0f;
    float deathStartY = 0.0f;

    ActionState action{};
    float stateTimer = 0.0f;
    ActionKind lastActionKind = ActionKind::None;
    bool isAttackActive = false;

    DirectX::XMFLOAT3 playerPos = {0.0f, 0.0f, 0.0f};
    PlayerCombatObservation playerObs{};

    float facingYaw = 0.0f;
    float lockedAttackYaw = 0.0f;

    WarpContext warp{};
    bool isVisible = true;
    float warpTrailEmitTimer = 0.0f;
    static constexpr int kWarpTrailGhostCount = 4;
    WarpTrailGhost warpTrailGhosts[kWarpTrailGhostCount]{};

    TacticState tactic = TacticState::Chase;
    BossPhase phase = BossPhase::Phase1;
    bool phaseTransitionActive = false;
    float phaseTransitionTimer = 0.0f;
    bool holdConfigured = false;
    float currentHoldDuration = 0.0f;
    bool quickSlashActive = false;
    bool farSlashActive = false;
    bool warpFeintFollowupLocked = false;
    bool warpFeintImmediate = false;
    bool warpFeintDecisionMade = false;
    bool directionFeintDecisionMade = false;
    float phantomWarpCooldown = 0.0f;
    float phantomFinalLockTimer = 0.0f;

    bool tellActive = false;
    float tellDuration = 0.0f;

    bool hasTrackingLocked = false;

};

class Enemy {
  public:
    void Initialize(uint32_t modelId);
    void SetDifficulty(float difficulty);
    float GetDifficulty() const { return difficulty_; }

    void Update(const PlayerCombatObservation &playerObs, float deltaTime);
    void UpdateTutorial(const PlayerCombatObservation &playerObs,
                        float deltaTime);
    void BeginTutorialAttack(ActionKind kind);
    void BeginDebugBladeClash(const DirectX::XMFLOAT3 &targetPosition);
    void ResetTutorialState();
    void SetTutorialPosition(const DirectX::XMFLOAT3 &position);

    void Draw(ModelManager *modelManager, const Camera &camera,
              float visualScale = 1.0f);
    float TakeDamage(float damage);
    float TakeDamageDeferTransitions(float damage);
    void ResolveDeferredDamageTransitions();
    void ForcePunishRelease();
    bool NotifyCountered(float vulnerabilityDuration);
    bool IsBladeClashAction() const;
    bool IsBladeClashWindow() const;
    void ResolveBladeClash(bool playerWon);
    void NotifyBladeClashLanded();
    void FinishCounterRecoil();
    void ApplyVictoryDefeatPose(float ratio,
                                const DirectX::XMFLOAT3 &startPosition,
                                const DirectX::XMFLOAT3 &playerPosition);
    void SetCinematicTransform(const DirectX::XMFLOAT3 &position, float yaw);
    void SetCinematicTransform(const DirectX::XMFLOAT3 &position, float yaw,
                               float pitch, float roll);
    void FaceTargetImmediately(const DirectX::XMFLOAT3 &targetPosition);
    const Transform &GetTransform() const { return tf_; }
    float GetHP() const { return runtime_.hp; }
    float GetMaxHP() const { return config_.core.maxHp; }

    OBB GetBodyOBB() const;
    OBB GetLeftHandOBB() const;
    OBB GetRightHandOBB() const;

    ActionKind GetActionKind() const { return runtime_.action.kind; }
    ActionStep GetActionStep() const { return runtime_.action.step; }
    float GetActionTimerForPresentation() const { return runtime_.stateTimer; }
    float GetReleaseAnticipationRatio() const;
    float GetTelegraphYaw() const;
    BossPhase GetBossPhase() const { return runtime_.phase; }
    bool IsPhaseTransitionActive() const { return runtime_.phaseTransitionActive; }
    bool GetIsPhaseChanging() const { return isPhaseChanging_; }
    void SetIsPhaseChanging(bool changing) { isPhaseChanging_ = changing; }
    float GetPhaseTransitionRatio() const {
        if (phaseTransitionDuration_ <= 0.0001f) {
            return 1.0f;
        }
        float t = runtime_.phaseTransitionTimer / phaseTransitionDuration_;
        if (t < 0.0f) {
            t = 0.0f;
        }
        if (t > 1.0f) {
            t = 1.0f;
        }
        return t;
    }

    bool IsAttackActive() const { return runtime_.isAttackActive; }
    OBB GetAttackOBB() const;
    bool IsWarpCollisionDisabled() const { return runtime_.warp.collisionDisabled; }

    float GetCurrentAttackDamage() const;
    float GetCurrentAttackKnockback() const;

  private:
    Transform tf_{};

    Transform visualTf_{};
    Transform bodyTf_{};
    Transform leftHandTf_{};
    Transform rightHandTf_{};

    uint32_t modelId_ = 0;

    DirectX::XMFLOAT3 bodySize_ = {1.45f, 1.8f, 1.25f};
    DirectX::XMFLOAT3 handSize_ = {0.82f, 0.82f, 0.82f};

    EnemyRuntimeState runtime_{};
    float &hp_ = runtime_.hp;
    bool &isDying_ = runtime_.isDying;
    bool &deathFinished_ = runtime_.deathFinished;
    float &hitReactionTimer_ = runtime_.hitReactionTimer;
    float hitReactionDuration_ = 0.16f;
    float &counterRecoilTimer_ = runtime_.counterRecoilTimer;
    float counterRecoilDuration_ = 0.62f;
    float counterRecoilPitchRad_ = 0.14f;
    float cinematicPitch_ = 0.0f;
    float cinematicRoll_ = 0.0f;
    float &deathTimer_ = runtime_.deathTimer;
    float deathDuration_ = 0.75f;
    float deathSinkDistance_ = 2.2f;
    float &deathStartY_ = runtime_.deathStartY;

    ActionState &action_ = runtime_.action;
    float &stateTimer_ = runtime_.stateTimer;
    ActionKind &lastActionKind_ = runtime_.lastActionKind;

    bool &isAttackActive_ = runtime_.isAttackActive;

    DirectX::XMFLOAT3 &playerPos_ = runtime_.playerPos;
    PlayerCombatObservation &playerObs_ = runtime_.playerObs;

    float &facingYaw_ = runtime_.facingYaw;
    float &lockedAttackYaw_ = runtime_.lockedAttackYaw;

    WarpContext &warp_ = runtime_.warp;
    bool &isVisible_ = runtime_.isVisible;

    TacticState &tactic_ = runtime_.tactic;
    BossPhase &phase_ = runtime_.phase;
    EnemyConfig config_{};
    float difficulty_ = 5.0f;
    bool &phaseTransitionActive_ = runtime_.phaseTransitionActive;
    float &phaseTransitionTimer_ = runtime_.phaseTransitionTimer;
    float phaseTransitionDuration_ = 3.40f;
    bool &holdConfigured_ = runtime_.holdConfigured;
    float &currentHoldDuration_ = runtime_.currentHoldDuration;
    bool &quickSlashActive_ = runtime_.quickSlashActive;
    bool &farSlashActive_ = runtime_.farSlashActive;
    bool &warpFeintFollowupLocked_ = runtime_.warpFeintFollowupLocked;
    bool &warpFeintImmediate_ = runtime_.warpFeintImmediate;
    bool &warpFeintDecisionMade_ = runtime_.warpFeintDecisionMade;
    bool &directionFeintDecisionMade_ = runtime_.directionFeintDecisionMade;
    float &phantomWarpCooldown_ = runtime_.phantomWarpCooldown;
    float &phantomFinalLockTimer_ = runtime_.phantomFinalLockTimer;

    bool isPhaseChanging_ = false;

    // ============================================================
    // Step2: Tell
    // ============================================================
    bool &tellActive_ = runtime_.tellActive;

    float &tellDuration_ = runtime_.tellDuration;

    float smashTellTime_ = 0.18f;
    float sweepTellTime_ = 0.16f;

    int nearSmashWeight_ = 30;
    int nearSweepWeight_ = 25;
    int phase2NearSmashBonus_ = 8;
    int phase2NearSweepBonus_ = 14;
    int phase3NearSmashBonus_ = 4;
    int phase3NearSweepBonus_ = 6;
    float chargeTurnSpeed_ = 6.0f;
    float recoveryTurnSpeed_ = 2.0f;
    float idleTurnSpeed_ = 8.0f;
    bool &hasTrackingLocked_ = runtime_.hasTrackingLocked;

    float quickSlashChance_ = 0.34f;
    float quickSmashChargeTime_ = 0.72f;
    float quickSweepChargeTime_ = 0.66f;
    float directionFeintChance_ = 0.32f;
    float chargeWarpFeintChance_ = 0.34f;
    float farWarpSlashChance_ = 0.74f;
    float farWarpSlashDistance_ = 10.8f;
    float farSlashLungeSpeed_ = 44.0f;
    float phantomWarpChance_ = 0.24f;
    float phantomWarpCooldownDuration_ = 5.8f;
    float phantomFinalLockDuration_ = 0.26f;
    float bladeClashChance_ = 0.26f;

    float stalkDurationMin_ = 0.45f;
    float stalkDurationMax_ = 1.10f;
    float stalkMoveSpeed_ = 1.45f;
    float stalkPounceDistanceBonus_ = 0.65f;
    float stalkPounceMinTime_ = 0.22f;
    float stalkPounceChance_ = 0.58f;
    float warpApproachFrontDistance_ = 2.55f;
    float warpApproachBackDistance_ = 2.35f;
    float warpNearChance_ = 0.22f;
    float warpFarChance_ = 0.58f;
    float warpCutInDistance_ = 5.8f;
    float warpCutInChance_ = 0.86f;
    float warpFeintChance_ = 0.26f;
    float warpFeintEndTimeScale_ = 0.55f;
    float warpArrivalPreviewHeight_ = 0.10f;
    float warpTrailLife_ = 0.06f;
    float warpTrailScaleMax_ = 0.88f;
    float &warpTrailEmitTimer_ = runtime_.warpTrailEmitTimer;
    static constexpr int kWarpTrailGhostCount_ =
        EnemyRuntimeState::kWarpTrailGhostCount;
    WarpTrailGhost (&warpTrailGhosts_)[kWarpTrailGhostCount_] =
        runtime_.warpTrailGhosts;

  private:
    void UpdateParts();
    OBB MakeOBB(const Transform &tf, const DirectX::XMFLOAT3 &size) const;

    void UpdateByAction(float deltaTime);

    void UpdateSmashByStep(float deltaTime);
    void UpdateSweepByStep(float deltaTime);
    void UpdateBladeClashByStep(float deltaTime);
    void UpdateWarpByStep(float deltaTime);
    void UpdateIdle(float deltaTime);

    TacticState DecideTactic() const;
    void BeginActionFromTactic(TacticState tactic);
    ActionKind SelectNearPressureAction() const;
    bool TryBeginWarpAction(float chance);
    bool TryBeginQuickSlash(float chance);
    bool TryBeginFarWarpSlash(float chance);
    bool TryBeginPhantomWarpSkill(float chance);
    bool TryBeginBladeClash(float chance);
    void BeginPhantomWarpStep(int viewWarpsRemaining, bool finalBehind,
                              ActionKind followupKind);
    void BeginPressureAction();
    void BeginChaseAction();

    void UpdateSmashCharge(float deltaTime);
    void UpdateSmashHold(float deltaTime);
    void UpdateSmashAttack(float deltaTime);
    void UpdateSmashRecovery(float deltaTime);

    void UpdateSweepCharge(float deltaTime);
    void UpdateSweepHold(float deltaTime);
    void UpdateSweepAttack(float deltaTime);
    void UpdateSweepRecovery(float deltaTime);

    void UpdateBladeClashCharge(float deltaTime);
    void UpdateBladeClashActive(float deltaTime);
    void UpdateBladeClashRecovery(float deltaTime);

    void UpdateFacingToPlayer();
    void LockCurrentFacing();
    void SyncBaseRotationToFacing();
    void UpdateFacingToPlayerWithSpeed(float deltaTime, float turnSpeed);
    float NormalizeAngle(float angle) const;

    void UpdateWarpStart(float deltaTime);
    void UpdateWarpMove(float deltaTime);
    void UpdateWarpEnd(float deltaTime);
    void UpdateWarpTrails(float deltaTime);
    void EmitWarpTrailGhost(const DirectX::XMFLOAT3 &position, float scale);
    void ResetWarpTrails();
    bool PrepareWarpContext();
    bool DecideWarpTargetNearPlayer(DirectX::XMFLOAT3 &outTarget);
    bool DecideWarpTargetFarSlash(DirectX::XMFLOAT3 &outTarget);
    bool DecideWarpTargetInPlayerView(DirectX::XMFLOAT3 &outTarget);
    bool DecideWarpTargetBehindPlayer(DirectX::XMFLOAT3 &outTarget);
    bool RefreshLiveBehindWarpTarget();
    void FinalizeWarpTargetFacing(DirectX::XMFLOAT3 &target);
    void ResetWarpContext();

    void UpdateStalkByStep(float deltaTime);
    void UpdateStalkMove(float deltaTime);
    void BeginStalkAction();

    float GetCurrentSmashChargeTime() const;
    float GetCurrentSweepChargeTime() const;

    OBB GetSmashAttackOBB() const;
    OBB GetSweepAttackOBB() const;
    float GetVisualYaw() const;
    bool IsPunishableRecovery() const;
    float GetDistanceToPlayer() const;
    bool IsPlayerInMeleeFront() const;

    const AttackTimingParam *GetCurrentAttackTiming() const;
    AttackParam *GetCurrentAttackParam();
    const AttackParam *GetCurrentAttackParam() const;
    DirectX::XMFLOAT3 GetCurrentAttackHitBoxSize() const;

    bool ShouldUseLockedAttackYaw() const;

    void BeginAction(ActionKind kind, ActionStep step);
    void ChangeActionStep(ActionStep step);
    void EndAttack();

    void ValidateTiming(AttackTimingParam &timing, float chargeTime);
    void ValidateAllTimings();
    void UpdateBossPhase();

    bool ShouldEnterSmashHold() const;
    bool ShouldEnterSweepHold() const;
    void EnterHold(float duration);
    bool TryBeginChargeWarpFeint(ActionKind kind);
    bool TryApplyDirectionFeint(ActionKind kind);
    float TechniqueUnlock(BossPhase requiredPhase) const;

    void EnterTell(ActionKind kind);
    bool IsTellFinished() const;

    void ResetPreAttackPresentationState();

    float RandomRange(float minValue, float maxValue) const;

    bool ApplyCounterBreakReaction(float vulnerabilityDuration = 0.0f);
    bool ShouldSnapReleaseFromRead() const;
};
