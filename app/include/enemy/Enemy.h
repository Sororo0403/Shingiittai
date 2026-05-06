#pragma once
#include "Camera.h"
#include "EnemyActionData.h"
#include "OBB.h"
#include "Player.h"
#include "Transform.h"
#include <cstddef>
#include <cstdint>
#include <vector>

class ModelManager;

enum class ActionStep {
    None,
    Charge,
    Active,
    Recovery,
    Start,
    Move,
    End
};

enum class WarpType { None, Approach, Escape };

enum class ActionVariant {
    None,
    Smash,
    Sweep,
    Shot,
    Wave
};

// Enemy combat is driven by a two-part FSM:
// - ActionKind decides which behavior is currently running.
// - ActionStep tracks the local phase within that behavior.
struct ActionState {
    ActionKind kind = ActionKind::None;
    ActionVariant variant = ActionVariant::None;
    ActionStep step = ActionStep::None;
};

struct WarpContext {
    WarpType type = WarpType::None;
    DirectX::XMFLOAT3 targetPos = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 departurePos = {0.0f, 0.0f, 0.0f};

    ActionKind followupKind = ActionKind::None;
    ActionStep followupStep = ActionStep::None;

    bool collisionDisabled = false;
    bool hasValidTarget = false;
    bool hasDeparturePos = false;
};

struct ChainContext {
    static constexpr int kMaxModules = 4;

    bool active = false;
    int stepCount = 0;
    int maxSteps = 0;
    int moduleCount = 0;
    int moduleIndex = 0;
    ActionKind modules[kMaxModules] = {};
};

struct EnemyBullet {
    DirectX::XMFLOAT3 position = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 velocity = {0.0f, 0.0f, 0.0f};
    float lifeTime = 0.0f;
    bool isAlive = false;
    bool isReflected = false;
};

struct EnemyWave {
    DirectX::XMFLOAT3 position = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 direction = {0.0f, 0.0f, 1.0f};
    float speed = 0.0f;
    float traveledDistance = 0.0f;
    float maxDistance = 0.0f;
    bool isAlive = false;
    bool isReflected = false;
};

struct WarpTrailGhost {
    DirectX::XMFLOAT3 position = {0.0f, 0.0f, 0.0f};
    float life = 0.0f;
    float scale = 1.0f;
    bool isActive = false;
};

// 仕様書に合わせて BodyCenter -> BodyRight に整理

enum class BossPhase { Phase1, Phase2 };

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
    float maxHp = 1000.0f;
    float phase2HealthRatioThreshold = 0.60f;
    float nearAttackDistance = 4.0f;
    float farAttackDistance = 6.5f;
};

struct EnemyAttackProfile {
    AttackParam attack{};
    AttackTimingParam timing{};
    float chargeTime = 0.0f;
};

struct EnemyMeleeAttackProfile {
    EnemyAttackProfile base{};
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

struct EnemyShotConfig {
    AttackParam attack{};
    float chargeTime = 0.0f;
    float recoveryTime = 0.0f;
    float interval = 0.0f;
    int minCount = 0;
    int maxCount = 0;
    float bulletSpeed = 0.0f;
    float bulletLifeTime = 0.0f;
    float spawnHeightOffset = 0.0f;
};

struct EnemyWaveConfig {
    AttackParam attack{};
    float chargeTime = 0.0f;
    float recoveryTime = 0.0f;
    float speed = 0.0f;
    float maxDistance = 0.0f;
    float spawnForwardOffset = 0.0f;
    float spawnHeightOffset = 0.0f;
};

struct EnemyAttackSet {
    EnemySmashConfig smash = {{{{10.0f, 4.0f, {1.5f, 1.8f, 1.5f}},
                                {0.88f, 0.28f, 0.04f, 0.10f, 0.18f}, 0.45f}},
                              1.4f, 0.8f};
    EnemySweepConfig sweep = {{{{10.0f, 4.0f, {3.2f, 1.2f, 1.4f}},
                                {1.27f, 0.36f, 0.12f, 0.24f, 0.32f}, 0.65f}},
                              0.2f, 0.8f};
    EnemyShotConfig shot = {{5.0f, 2.5f, {0.4f, 0.4f, 0.4f}},
                            0.6f, 0.8f, 0.2f, 3, 5, 6.0f, 2.0f, 0.2f};
    EnemyWaveConfig wave = {{8.0f, 3.0f, {1.2f, 0.6f, 1.6f}},
                            0.6f, 0.8f, 4.0f, 8.0f, 1.5f, 0.0f};
};

struct EnemyWarpConfig {
    float startTime = 0.2f;
    float moveTime = 0.10f;
    float endTime = 0.2f;
};

struct EnemyChainConfig {
    int maxStepsPhase1 = 2;
    int maxStepsPhase2 = 3;
    float continueChance = 0.42f;
    float phase2ContinueBonus = 0.16f;
    float warpLinkChance = 0.34f;
    float movementLinkChance = 0.28f;
    float rangedLinkChance = 0.32f;
    float nearRetreatDistance = 3.1f;
    float farApproachDistance = 5.2f;
};

struct EnemyConfig {
    EnemyCoreConfig core{};
    EnemyAttackSet attacks{};
    EnemyWarpConfig warp{};
    EnemyChainConfig chain{};
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
    ActionVariant lastActionVariant = ActionVariant::None;
    bool isAttackActive = false;

    DirectX::XMFLOAT3 playerPos = {0.0f, 0.0f, 0.0f};

    float facingYaw = 0.0f;
    float lockedAttackYaw = 0.0f;

    std::vector<EnemyBullet> bullets{};
    int shotsRemaining = 0;
    float shotIntervalTimer = 0.0f;

    WarpContext warp{};
    ChainContext chain{};
    bool isVisible = true;

    std::vector<EnemyWave> waves{};

    float lastDistanceToPlayer = 0.0f;
    float stagnantTimer = 0.0f;
    bool isDistanceStagnant = false;

    ActionKind tactic = ActionKind::Movement;
    BossPhase phase = BossPhase::Phase1;
    bool phaseTransitionActive = false;
    float phaseTransitionTimer = 0.0f;

    bool tellActive = false;
    float tellDuration = 0.0f;

    bool hasTrackingLocked = false;

    float warpTrailEmitTimer = 0.0f;
    static constexpr int kWarpTrailGhostCount = 4;
    WarpTrailGhost warpTrailGhosts[kWarpTrailGhostCount]{};

    int stalkRepeatCount = 0;
    float stalkMoveDir = 1.0f;
    float stalkForwardBias = 0.0f;
};

class Enemy {
  public:
    void Initialize(uint32_t modelId, uint32_t projectileModelId = 0);

    void Update(const DirectX::XMFLOAT3 &playerPos, float deltaTime);

    void Draw(ModelManager *modelManager, const Camera &camera);
    void TakeDamage(float damage);
    void ConsumeBullet(size_t index);
    void ConsumeWave(size_t index);
    bool NotifyCountered();

    const Transform &GetTransform() const { return tf_; }

    OBB GetBodyOBB() const;
    OBB GetLeftHandOBB() const;
    OBB GetRightHandOBB() const;

    ActionKind GetActionKind() const { return runtime_.action.kind; }
    ActionStep GetActionStep() const { return runtime_.action.step; }
    bool IsPhaseTransitionActive() const { return runtime_.phaseTransitionActive; }
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

    OBB GetAttackOBB() const;

    float GetDistanceToPlayer() const;

    const std::vector<EnemyBullet> &GetBullets() const { return runtime_.bullets; }
    void DestroyBullet(size_t index);
    void ReflectBullet(size_t index, const DirectX::XMFLOAT3 &targetPos);

    bool IsVisible() const { return runtime_.isVisible; }
    const DirectX::XMFLOAT3 &GetWarpTargetPos() const {
        return runtime_.warp.targetPos;
    }
    const DirectX::XMFLOAT3 &GetWarpDeparturePos() const {
        return runtime_.warp.departurePos;
    }
    const std::vector<EnemyWave> &GetWaves() const { return runtime_.waves; }
    void DestroyWave(size_t index);
    void ReflectWave(size_t index, const DirectX::XMFLOAT3 &targetPos);

    float GetBulletDamage() const { return config_.attacks.shot.attack.damage; }
    float GetWaveDamage() const { return config_.attacks.wave.attack.damage; }

    float GetBulletKnockback() const { return config_.attacks.shot.attack.knockback; }
    float GetWaveKnockback() const { return config_.attacks.wave.attack.knockback; }

    DirectX::XMFLOAT3 GetBulletHitBoxSize() const {
        return config_.attacks.shot.attack.hitBoxSize;
    }
    DirectX::XMFLOAT3 GetWaveHitBoxSize() const {
        return config_.attacks.wave.attack.hitBoxSize;
    }
    float GetCurrentAttackDamage() const;
    float GetCurrentAttackKnockback() const;
    DirectX::XMFLOAT3 GetCurrentAttackHitBoxSize() const;

  private:
    Transform tf_{};

    Transform visualTf_{};
    Transform bodyTf_{};
    Transform leftHandTf_{};
    Transform rightHandTf_{};

    uint32_t modelId_ = 0;
    uint32_t projectileModelId_ = 0;

    DirectX::XMFLOAT3 bodySize_ = {0.8f, 1.4f, 0.6f};
    DirectX::XMFLOAT3 handSize_ = {0.45f, 0.45f, 0.45f};

    EnemyRuntimeState runtime_{};
    float &hp_ = runtime_.hp;
    bool &isDying_ = runtime_.isDying;
    bool &deathFinished_ = runtime_.deathFinished;
    float &hitReactionTimer_ = runtime_.hitReactionTimer;
    float hitReactionDuration_ = 0.12f;
    float hitReactionMoveSpeed_ = 3.5f;
    float &counterRecoilTimer_ = runtime_.counterRecoilTimer;
    float counterRecoilDuration_ = 0.85f;
    float counterRecoilPitchRad_ = 0.12f;
    float &deathTimer_ = runtime_.deathTimer;
    float deathDuration_ = 0.75f;
    float deathSinkDistance_ = 2.2f;
    float &deathStartY_ = runtime_.deathStartY;

    ActionState &action_ = runtime_.action;
    float &stateTimer_ = runtime_.stateTimer;
    ActionKind &lastActionKind_ = runtime_.lastActionKind;
    ActionVariant &lastActionVariant_ = runtime_.lastActionVariant;

    bool &isAttackActive_ = runtime_.isAttackActive;

    DirectX::XMFLOAT3 &playerPos_ = runtime_.playerPos;

    float &facingYaw_ = runtime_.facingYaw;
    float &lockedAttackYaw_ = runtime_.lockedAttackYaw;

    std::vector<EnemyBullet> &bullets_ = runtime_.bullets;

    int &shotsRemaining_ = runtime_.shotsRemaining;
    float &shotIntervalTimer_ = runtime_.shotIntervalTimer;

    WarpContext &warp_ = runtime_.warp;
    ChainContext &chain_ = runtime_.chain;
    bool &isVisible_ = runtime_.isVisible;

    std::vector<EnemyWave> &waves_ = runtime_.waves;

    float &lastDistanceToPlayer_ = runtime_.lastDistanceToPlayer;
    float &stagnantTimer_ = runtime_.stagnantTimer;
    bool &isDistanceStagnant_ = runtime_.isDistanceStagnant;

    ActionKind &tactic_ = runtime_.tactic;
    BossPhase &phase_ = runtime_.phase;
    EnemyConfig config_{};
    bool &phaseTransitionActive_ = runtime_.phaseTransitionActive;
    float &phaseTransitionTimer_ = runtime_.phaseTransitionTimer;
    float phaseTransitionDuration_ = 0.90f;

    bool &tellActive_ = runtime_.tellActive;

    float &tellDuration_ = runtime_.tellDuration;

    float smashTellTime_ = 0.12f;
    float sweepTellTime_ = 0.10f;

    bool suspendWarpForPresentation_ = true;
    float warpDepartureEchoOffset_ = 0.28f;
    float warpArrivalEchoOffset_ = 0.22f;
    float warpArrivalPreviewHeight_ = 0.10f;
    float warpMoveGhostScaleX_ = 0.78f;
    float warpMoveGhostScaleY_ = 1.28f;
    float warpMoveGhostScaleZ_ = 0.36f;
    float warpArrivalPreviewScale_ = 1.18f;
    float warpParticleRadius_ = 0.46f;
    float warpParticleHeight_ = 0.72f;
    float warpParticleScale_ = 0.10f;
    int warpParticleCount_ = 5;
    float warpTrailLife_ = 0.06f;
    float warpTrailInterval_ = 0.032f;
    float warpTrailScaleMin_ = 0.52f;
    float warpTrailScaleMax_ = 0.88f;
    float &warpTrailEmitTimer_ = runtime_.warpTrailEmitTimer;
    static constexpr int kWarpTrailGhostCount_ =
        EnemyRuntimeState::kWarpTrailGhostCount;
    WarpTrailGhost (&warpTrailGhosts_)[kWarpTrailGhostCount_] =
        runtime_.warpTrailGhosts;

    float warpNearRadiusMin_ = 1.8f;
    float warpNearRadiusMax_ = 3.0f;
    float warpApproachForwardDistance_ = 2.3f;
    float warpApproachSideDistance_ = 2.1f;
    float warpApproachLongFrontDistance_ = 4.0f;
    float farDistanceTimer_ = 0.0f;
    float farDistanceWarpTimeThreshold_ = 2.0f;

    int nearSmashWeight_ = 30;
    int nearSweepWeight_ = 25;
    int phase2NearSmashBonus_ = 8;
    int phase2NearSweepBonus_ = 14;

    int midShotWeight_ = 25;
    int midWaveWeight_ = 30;
    int phase2MidShotBonus_ = 12;

    int farShotWeight_ = 35;
    int farWaveWeight_ = 40;
    int neutralMidShotBonus_ = 12;
    int neutralMidWaveBonus_ = 10;

    float chargeTurnSpeed_ = 6.0f;
    float recoveryTurnSpeed_ = 2.0f;
    float idleTurnSpeed_ = 8.0f;
    bool &hasTrackingLocked_ = runtime_.hasTrackingLocked;

    float stagnantDistanceThreshold_ = 0.15f;
    float stagnantTimeThreshold_ = 1.2f;

    float stalkDurationMin_ = 0.45f;
    float stalkDurationMax_ = 1.10f;
    float stalkMoveDuration_ = 0.0f;
    float stalkMoveSpeed_ = 2.2f;
    float stalkStrafeRadiusWeight_ = 0.75f;
    float stalkForwardAdjustWeight_ = 0.35f;
    int stalkRepeatLimit_ = 2;

    int &stalkRepeatCount_ = runtime_.stalkRepeatCount;
    float &stalkMoveDir_ = runtime_.stalkMoveDir;         // -1:left / +1:right
    float &stalkForwardBias_ = runtime_.stalkForwardBias; // -1:後退 / +1:前進

  private:
    void UpdateParts();
    OBB MakeOBB(const Transform &tf, const DirectX::XMFLOAT3 &size) const;

    void UpdateByAction(float deltaTime);

    void UpdateMeleeByStep(float deltaTime);
    void UpdateRangedByStep(float deltaTime);
    void UpdateWarpByStep(float deltaTime);
    void UpdateIdle(float deltaTime);

    ActionKind DecideTactic() const;
    void BeginActionFromTactic(ActionKind tactic);
    ActionVariant SelectRangedVariant(float distance) const;
    ActionVariant SelectMeleeVariant() const;
    bool TryBeginTacticActionOrFallback(ActionKind preferred, ActionKind fallback);
    void BeginNeutralAction();
    void BeginPressureAction();
    void BeginChaseAction();
    void BeginResetAction();

    void UpdateMeleeCharge(float deltaTime);
    void UpdateMeleeAttack(float deltaTime);
    void UpdateMeleeRecovery(float deltaTime);

    void UpdateFacingToPlayer();
    void LockCurrentFacing();
    void UpdateFacingToPlayerWithSpeed(float deltaTime, float turnSpeed);
    float NormalizeAngle(float angle) const;

    void UpdateRangedCharge(float deltaTime);
    void UpdateRangedActive(float deltaTime);
    void UpdateRangedRecovery(float deltaTime);

    void SpawnBullet();
    void UpdateBullets(float deltaTime);

    void UpdateWarpStart(float deltaTime);
    void UpdateWarpMove(float deltaTime);
    void UpdateWarpEnd(float deltaTime);
    void UpdateWarpTrails(float deltaTime);
    void EmitWarpTrailGhost(const DirectX::XMFLOAT3 &position, float scale);
    void ResetWarpTrails();

    bool PrepareWarpContext();
    bool IsWarpSuspendedForPresentation() const;
    bool DecideWarpTargetNearPlayer(DirectX::XMFLOAT3 &outTarget);
    bool DecideWarpTargetFarFromPlayer(DirectX::XMFLOAT3 &outTarget) const;
    void ResetWarpContext();

    bool TryContinueChain();
    bool DecideNextChainAction(ActionKind finishedKind, ActionKind &outKind,
                               ActionStep &outStep) const;
    bool TryStartModularActionChain(ActionKind finishedKind);
    bool BeginChainFollowup(ActionKind finishedKind, ActionKind nextKind,
                            ActionStep nextStep);
    void ResetChainContext();
    void FinishCurrentAction();

    void SpawnWave();
    void UpdateWaves(float deltaTime);

    void UpdateStalkByStep(float deltaTime);
    void UpdateStalkMove(float deltaTime);
    void BeginStalkAction();

    float GetCurrentActionTime() const;
    float GetCurrentMeleeChargeTime() const;
    float GetVisualYaw() const;

    const AttackTimingParam *GetCurrentAttackTiming() const;
    AttackParam *GetCurrentAttackParam();
    const AttackParam *GetCurrentAttackParam() const;

    bool ShouldUseLockedAttackYaw() const;

    bool TryBeginTacticAction(ActionKind kind);
    void BeginAction(ActionKind kind, ActionStep step,
                     ActionVariant variant = ActionVariant::None);
    void ChangeActionStep(ActionStep step);
    void EndAttack();

    void ValidateTiming(AttackTimingParam &timing, float chargeTime);
    void ValidateAllTimings();
    void UpdateBossPhase();

    void EnterTell(ActionVariant variant);
    bool IsTellFinished() const;

    void ResetPreAttackPresentationState();

    float RandomRange(float minValue, float maxValue) const;

    bool ApplyCounterBreakReaction();
};
