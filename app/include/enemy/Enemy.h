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
    Hold,
    End
};

enum class WarpType { None, Approach, Escape };
enum class WarpApproachSlot { None, BackLeft, BackRight, DirectBack };

// Enemy combat is driven by a two-part FSM:
// - ActionKind decides which behavior is currently running.
// - ActionStep tracks the local phase within that behavior.
struct ActionState {
    ActionKind kind = ActionKind::None;
    ActionId id = ActionId::None;
    ActionStep step = ActionStep::None;
};

struct WarpContext {
    WarpType type = WarpType::None;
    WarpApproachSlot approachSlot = WarpApproachSlot::None;
    DirectX::XMFLOAT3 targetPos = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 departurePos = {0.0f, 0.0f, 0.0f};

    ActionKind followupKind = ActionKind::None;
    ActionStep followupStep = ActionStep::None;

    bool collisionDisabled = false;
    bool hasValidTarget = false;
    bool hasDeparturePos = false;
};

enum class ChainStarter {
    None,
    WarpApproach,
    WarpEscape,
    SweepWarpSmash,
    WaveWarpSmash
};

struct ChainContext {
    bool active = false;
    ChainStarter starter = ChainStarter::None;
    int stepCount = 0;
    int maxSteps = 0;
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
    DirectX::XMFLOAT3 hitBoxSize = {2.25f, 0.95f, 2.6f};
    float speed = 0.0f;
    float traveledDistance = 0.0f;
    float maxDistance = 0.0f;
    float damage = 0.0f;
    float knockback = 0.0f;
    bool isAlive = false;
    bool isReflected = false;
};

struct EnemyCage {
    DirectX::XMFLOAT3 center = {0.0f, 0.0f, 0.0f};
    float radius = 0.0f;
    float height = 0.0f;
    float lifeTime = 0.0f;
    float maxLifeTime = 0.0f;
    float breakValue = 0.0f;
    float maxBreakValue = 0.0f;
    float hitCooldown = 0.0f;
    int barCount = 0;
    bool isActive = false;
};

struct WarpTrailGhost {
    DirectX::XMFLOAT3 position = {0.0f, 0.0f, 0.0f};
    float life = 0.0f;
    float scale = 1.0f;
    bool isActive = false;
};

// 仕様書に合わせて BodyCenter -> BodyRight に整理

// プレイヤーのカウンター軸
enum class CounterAxis { None, Vertical, Horizontal };

// 攻撃をどう読ませるかの軸
enum class CounterReadAxis {
    None,
    Vertical,
    Horizontal,
    ThrustLike,
    Radial,
    Projectile
};

// 上位戦術
enum class TacticState {
    Warp,
    Melee,
    Ranged,
    DistanceAdjust
};

enum class BossPhase { Phase1, Phase2 };

// GameScene 側から渡す観測情報
struct PlayerCombatObservation {
    DirectX::XMFLOAT3 position = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 velocity = {0.0f, 0.0f, 0.0f};

    bool isGuarding = false;
    bool isCounterStance = false;
    bool justCountered = false;
    bool justCounterFailed = false;
    bool justCounterEarly = false;
    bool justCounterLate = false;
    bool isAttacking = false;

    CounterAxis counterAxis = CounterAxis::None;
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
    RangeF holdTime{};
    float feintChance = 0.0f;
};

struct EnemySmashConfig {
    EnemyMeleeAttackProfile melee{};
    float attackForwardOffset = 0.0f;
    float attackHeightOffset = 0.0f;
    float delayChance = 0.0f;
    float delayExtraChargeTime = 0.0f;
};

struct EnemySweepConfig {
    EnemyMeleeAttackProfile melee{};
    float attackSideOffset = 0.0f;
    float attackHeightOffset = 0.0f;
    float doubleChance = 0.0f;
    float secondDelay = 0.0f;
    float secondChargeScale = 1.0f;
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

struct EnemyBladeClashConfig {
    AttackParam attack{};
    float chargeTime = 0.0f;
    float activeTime = 0.0f;
    float recoveryTime = 0.0f;
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

struct EnemyCageConfig {
    AttackParam attack{};
    float chargeTime = 0.0f;
    float activeTime = 0.0f;
    float recoveryTime = 0.0f;
    float radius = 0.0f;
    float duration = 0.0f;
    float height = 0.0f;
    float breakValue = 0.0f;
    int barCount = 0;
};

struct EnemyNovaConfig {
    float chargeTime = 2.30f;
    float activeTime = 0.44f;
    float recoveryTime = 1.00f;
    float impactTime = 0.04f;
    float impactWindow = 0.12f;
    float impactRadius = 6.70f;
    float impactDamage = 14.0f;
    float impactKnockback = 6.5f;
    float ringInterval = 0.035f;
    int ringCount = 3;
    int wavesPerRing = 16;
    float waveSpeed = 7.2f;
    float waveMaxDistance = 12.2f;
    float firstRingRadius = 0.8f;
    float ringRadiusStep = 0.50f;
    float bulletSpeed = 7.8f;
    float bulletLifeTime = 2.2f;
    int skyBulletCount = 12;
    float skyBulletHeightOffset = 2.10f;
};

struct EnemyAttackSet {
    EnemySmashConfig smash = {{{{10.0f, 4.0f, {2.8f, 2.1f, 3.2f}},
                                {1.20f, 0.86f, 0.05f, 0.22f, 0.38f}, 1.64f},
                               {0.42f, 0.86f}, 0.62f},
                              1.4f, 0.8f, 0.32f, 0.46f};
    EnemySweepConfig sweep = {{{{10.0f, 4.0f, {5.0f, 1.65f, 2.6f}},
                                {1.12f, 0.78f, 0.05f, 0.20f, 0.36f}, 1.52f},
                               {0.38f, 0.78f}, 0.48f},
                              0.2f, 0.8f, 0.30f, 0.28f, 0.72f};
    EnemyShotConfig shot = {{7.0f, 3.0f, {1.55f, 1.55f, 1.55f}},
                            0.72f, 0.82f, 0.58f, 3, 3, 6.6f, 3.0f, 0.08f};
    EnemyBladeClashConfig bladeClash = {
        {8.0f, 4.0f, {1.75f, 1.60f, 1.95f}}, 0.72f, 0.88f, 0.78f};
    EnemyWaveConfig wave = {{8.0f, 3.0f, {2.25f, 0.95f, 2.6f}},
                            1.76f, 0.92f, 5.2f, 8.0f, 1.5f, 0.0f};
    EnemyCageConfig cage = {{6.0f, 2.5f, {0.95f, 2.2f, 0.95f}},
                            1.05f, 0.18f, 0.78f, 1.08f, 3.8f, 1.55f, 3.0f,
                            8};
    EnemyNovaConfig nova{};
};

struct EnemyWarpConfig {
    float startTime = 0.55f;
    float moveTime = 0.24f;
    float endTime = 0.48f;
};

struct EnemyChainConfig {
    int warpApproachMaxSteps = 3;
    int warpEscapeMaxSteps = 2;
    float approachContinueDistance = 5.6f;
    float escapeContinueDistance = 4.5f;
    float sweepWarpSmashMaxDistance = 5.6f;
    float sweepWarpSmashChance = 0.62f;
    float waveWarpSmashMinDistance = 4.0f;
    float waveWarpSmashChance = 0.64f;
};

struct EnemyConfig {
    EnemyCoreConfig core{};
    EnemyAttackSet attacks{};
    EnemyWarpConfig warp{};
    EnemyChainConfig chain{};
};

enum class SmashStyle { Normal, Delay };
enum class SweepStyle { Normal, Double, Advance };
enum class PostActionOption { None, BackWarp };
enum class BackWarpFollowup { None, Shot, Wave };

enum class HoldBranchType { None, Active, Warp };

enum class RecoveryBranchType { None, Recommit, DelayedSecond, EscapeFakeout };

// マルギット風 学習・適応メモリ
struct CounterAdaptMemory {
    float counterStancePressure = 0.0f;
    float earlyCount = 0.0f;
    float lateCount = 0.0f;
    float successCount = 0.0f;
    float verticalBias = 0.0f;
    float horizontalBias = 0.0f;
    int consecutiveSuccess = 0;
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
    bool playerGuarding = false;
    PlayerCombatObservation playerObs{};

    float facingYaw = 0.0f;
    float lockedAttackYaw = 0.0f;

    std::vector<EnemyBullet> bullets{};
    int shotsRemaining = 0;
    float shotIntervalTimer = 0.0f;
    bool shotWarpedToArenaEdge = false;

    WarpContext warp{};
    ChainContext chain{};
    bool isVisible = true;

    std::vector<EnemyWave> waves{};
    EnemyCage cage{};

    int farActionIndex = 0;
    int nearActionIndex = 0;

    float lastDistanceToPlayer = 0.0f;
    float stagnantTimer = 0.0f;
    bool isDistanceStagnant = false;

    SmashStyle smashStyle = SmashStyle::Normal;
    SweepStyle sweepStyle = SweepStyle::Normal;

    PostActionOption postActionOption = PostActionOption::None;
    TacticState tactic = TacticState::DistanceAdjust;
    BossPhase phase = BossPhase::Phase1;
    bool phaseTransitionActive = false;
    float phaseTransitionTimer = 0.0f;

    bool holdConfigured = false;
    float currentHoldDuration = 0.0f;
    HoldBranchType holdBranchType = HoldBranchType::None;
    float holdBranchDecisionTime = 0.0f;
    bool holdBranchDecided = false;

    bool tellActive = false;
    bool fakeCommitActive = false;
    bool freezeHoldActive = false;
    float tellDuration = 0.0f;
    float fakeCommitDuration = 0.0f;
    float freezeHoldDuration = 0.0f;

    RecoveryBranchType recoveryBranchType = RecoveryBranchType::None;
    ActionKind recoveryFollowupKind = ActionKind::None;
    ActionStep recoveryFollowupStep = ActionStep::None;
    float recoveryFollowupDelayTimer = 0.0f;
    bool isMargitComboATransition = false;
    bool isDoubleSweepSecondStage = false;
    bool currentActionConnected = false;
    bool currentActionGuarded = false;
    int dualCounterStage = 0;
    bool dualCounterFirstHand = true;
    bool dualCounterStageResolved = false;
    bool hasTrackingLocked = false;
    bool novaSkyBulletsSpawned = false;
    int novaRingsSpawned = 0;
    float novaRingTimer = 0.0f;
    bool cageTrapSpawned = false;

    CounterAdaptMemory counterMemory{};
    float postCounterRhythmTimer = 0.0f;
    bool forceEscapeWarpNext = false;
    bool forceCounterBaitNext = false;

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

    // 既存互換
    void Update(const DirectX::XMFLOAT3 &playerPos, float deltaTime,
                bool playerGuarding);

    // 新版：観測情報ごと渡す
    void Update(const PlayerCombatObservation &playerObs, float deltaTime);

    void Draw(ModelManager *modelManager, const Camera &camera,
              float visualScale = 1.0f);
    void TakeDamage(float damage);
    void TakeDamageDeferTransitions(float damage);
    void ResolveDeferredDamageTransitions();
    void SetPhase2DebugHealth();
    void ForceDebugBladeClash();
    void ConsumeBullet(size_t index);
    void ConsumeWave(size_t index);
    void NotifyAttackConnected();
    void NotifyAttackGuarded();
    void ForcePunishRelease();
    bool NotifyDualCountered();
    void NotifyDualStrikeLanded();
    void ResolveBladeClash(bool playerWon);
    bool NotifyCountered();
    bool NotifyCountered(float vulnerabilityDuration);
    void FinishCounterRecoil();
    void ApplyVictoryDefeatPose(float ratio,
                                const DirectX::XMFLOAT3 &startPosition,
                                const DirectX::XMFLOAT3 &playerPosition);
    void SetCinematicTransform(const DirectX::XMFLOAT3 &position, float yaw);
    void SetCinematicTransform(const DirectX::XMFLOAT3 &position, float yaw,
                               float pitch, float roll);
    void FaceTargetImmediately(const DirectX::XMFLOAT3 &targetPosition);
    void SetBladeClashPresentationTime(float time) {
        bladeClashPresentationTime_ = time;
    }

    const Transform &GetTransform() const { return tf_; }
    bool IsAlive() const { return !runtime_.deathFinished; }
    float GetHP() const { return runtime_.hp; }

    const Transform &GetVisualTransform() const { return visualTf_; }
    const Transform &GetBodyTransform() const { return bodyTf_; }
    const Transform &GetLeftHandTransform() const { return leftHandTf_; }
    const Transform &GetRightHandTransform() const { return rightHandTf_; }

    OBB GetBodyOBB() const;
    OBB GetLeftHandOBB() const;
    OBB GetRightHandOBB() const;

    ActionKind GetActionKind() const { return runtime_.action.kind; }
    ActionId GetActionId() const { return runtime_.action.id; }
    ActionStep GetActionStep() const { return runtime_.action.step; }
    float GetActionTimerForPresentation() const { return runtime_.stateTimer; }
    float GetReleaseAnticipationRatio() const;
    float GetChargeWeakPointTimeLimitForPresentation() const;
    float GetChargeWeakPointTimeRemainingForPresentation() const;
    float GetTelegraphYaw() const;
    bool IsNovaImpactPending() const;
    bool IsNovaImpactWindow() const;
    float GetNovaImpactRadius() const;
    float GetNovaImpactDamage() const;
    float GetNovaImpactKnockback() const;
    bool IsPunishableRecovery() const;
    float GetRecoveryProgressForPresentation() const;
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
    bool IsDualCounterAction() const;
    bool IsDualCounterWindow() const;
    bool IsDualCounterHandStage() const;
    bool IsBladeClashAction() const;
    bool IsBladeClashWindow() const;
    void NotifyBladeClashLanded();

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
    bool HasWarpDeparturePos() const { return runtime_.warp.hasDeparturePos; }
    WarpType GetWarpType() const { return runtime_.warp.type; }
    bool IsWarpCollisionDisabled() const { return runtime_.warp.collisionDisabled; }

    const std::vector<EnemyWave> &GetWaves() const { return runtime_.waves; }
    const EnemyCage &GetCage() const { return runtime_.cage; }
    bool DamageCage(float amount);
    void DestroyWave(size_t index);
    void ReflectWave(size_t index, const DirectX::XMFLOAT3 &targetPos);

    float GetBulletDamage() const { return config_.attacks.shot.attack.damage; }
    float GetWaveDamage() const { return config_.attacks.wave.attack.damage; }

    float GetSmashKnockback() const { return config_.attacks.smash.melee.base.attack.knockback; }
    float GetSweepKnockback() const { return config_.attacks.sweep.melee.base.attack.knockback; }
    float GetBulletKnockback() const { return config_.attacks.shot.attack.knockback; }
    float GetWaveKnockback() const { return config_.attacks.wave.attack.knockback; }

    DirectX::XMFLOAT3 GetBulletHitBoxSize() const {
        return config_.attacks.shot.attack.hitBoxSize;
    }
    DirectX::XMFLOAT3 GetWaveHitBoxSize() const {
        return config_.attacks.wave.attack.hitBoxSize;
    }
    DirectX::XMFLOAT3 GetSmashAttackBoxSize() const {
        return config_.attacks.smash.melee.base.attack.hitBoxSize;
    }
    DirectX::XMFLOAT3 GetSweepAttackBoxSize() const {
        return config_.attacks.sweep.melee.base.attack.hitBoxSize;
    }

    float GetCurrentAttackDamage() const;
    float GetCurrentAttackKnockback() const;
    DirectX::XMFLOAT3 GetCurrentAttackHitBoxSize() const;

    const AttackParam &GetSmashParam() const { return config_.attacks.smash.melee.base.attack; }
    const AttackParam &GetSweepParam() const { return config_.attacks.sweep.melee.base.attack; }
    const AttackParam &GetBulletParam() const { return config_.attacks.shot.attack; }
    const AttackParam &GetWaveParam() const { return config_.attacks.wave.attack; }

    const AttackTimingParam &GetSmashTiming() const { return config_.attacks.smash.melee.base.timing; }
    const AttackTimingParam &GetSweepTiming() const { return config_.attacks.sweep.melee.base.timing; }

  private:
    Transform tf_{};

    Transform visualTf_{};
    Transform bodyTf_{};
    Transform leftHandTf_{};
    Transform rightHandTf_{};

    uint32_t modelId_ = 0;
    uint32_t projectileModelId_ = 0;

    DirectX::XMFLOAT3 bodySize_ = {1.45f, 1.8f, 1.25f};
    DirectX::XMFLOAT3 handSize_ = {0.82f, 0.82f, 0.82f};

    EnemyRuntimeState runtime_{};
    float &hp_ = runtime_.hp;
    bool &isDying_ = runtime_.isDying;
    bool &deathFinished_ = runtime_.deathFinished;
    float &hitReactionTimer_ = runtime_.hitReactionTimer;
    float hitReactionDuration_ = 0.16f;
    float hitReactionMoveSpeed_ = 0.0f;
    float &counterRecoilTimer_ = runtime_.counterRecoilTimer;
    float counterRecoilDuration_ = 0.82f;
    float counterRecoilPitchRad_ = 0.14f;
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

    std::vector<EnemyBullet> &bullets_ = runtime_.bullets;

    int &shotsRemaining_ = runtime_.shotsRemaining;
    float &shotIntervalTimer_ = runtime_.shotIntervalTimer;
    bool &shotWarpedToArenaEdge_ = runtime_.shotWarpedToArenaEdge;

    WarpContext &warp_ = runtime_.warp;
    ChainContext &chain_ = runtime_.chain;
    bool &isVisible_ = runtime_.isVisible;

    std::vector<EnemyWave> &waves_ = runtime_.waves;

    int &farActionIndex_ = runtime_.farActionIndex;
    int &nearActionIndex_ = runtime_.nearActionIndex;

    float &lastDistanceToPlayer_ = runtime_.lastDistanceToPlayer;
    float &stagnantTimer_ = runtime_.stagnantTimer;
    bool &isDistanceStagnant_ = runtime_.isDistanceStagnant;

    SmashStyle &smashStyle_ = runtime_.smashStyle;
    SweepStyle &sweepStyle_ = runtime_.sweepStyle;

    PostActionOption &postActionOption_ = runtime_.postActionOption;
    TacticState &tactic_ = runtime_.tactic;
    BossPhase &phase_ = runtime_.phase;
    EnemyConfig config_{};
    bool &phaseTransitionActive_ = runtime_.phaseTransitionActive;
    float &phaseTransitionTimer_ = runtime_.phaseTransitionTimer;
    float phaseTransitionDuration_ = 3.40f;

    bool &holdConfigured_ = runtime_.holdConfigured;
    float &currentHoldDuration_ = runtime_.currentHoldDuration;

    HoldBranchType &holdBranchType_ = runtime_.holdBranchType;

    float &holdBranchDecisionTime_ = runtime_.holdBranchDecisionTime;
    bool &holdBranchDecided_ = runtime_.holdBranchDecided;

    float smashHoldBranchWarpChance_ = 0.18f;
    float sweepHoldBranchWarpChance_ = 0.14f;

    bool isPhaseChanging_ = false;

    // ============================================================
    // Step2: Tell / FakeCommit / FreezeHold
    // ============================================================
    bool &tellActive_ = runtime_.tellActive;
    bool &fakeCommitActive_ = runtime_.fakeCommitActive;
    bool &freezeHoldActive_ = runtime_.freezeHoldActive;

    float &tellDuration_ = runtime_.tellDuration;
    float &fakeCommitDuration_ = runtime_.fakeCommitDuration;
    float &freezeHoldDuration_ = runtime_.freezeHoldDuration;

    float smashTellTime_ = 0.18f;
    float sweepTellTime_ = 0.16f;

    float smashFakeCommitChance_ = 0.68f;
    float sweepFakeCommitChance_ = 0.54f;

    float smashFakeCommitTime_ = 0.34f;
    float sweepFakeCommitTime_ = 0.30f;

    float smashFreezeHoldTimeMin_ = 0.30f;
    float smashFreezeHoldTimeMax_ = 0.54f;
    float sweepFreezeHoldTimeMin_ = 0.26f;
    float sweepFreezeHoldTimeMax_ = 0.48f;

    // ============================================================
    // Step3: Recovery -> Recommit / DelayedSecond / EscapeFakeout
    // ============================================================
    RecoveryBranchType &recoveryBranchType_ = runtime_.recoveryBranchType;

    float recommitChance_ = 0.18f;
    float delayedSecondChance_ = 0.20f;
    float escapeFakeoutChance_ = 0.16f;
    float phase2RecommitBonus_ = 0.12f;
    float phase2DelayedSecondBonus_ = 0.08f;

    float recommitDelayMin_ = 0.10f;
    float recommitDelayMax_ = 0.20f;

    float delayedSecondDelayMin_ = 0.16f;
    float delayedSecondDelayMax_ = 0.28f;
    float margitComboFollowupDelayMin_ = 0.08f;
    float margitComboFollowupDelayMax_ = 0.18f;

    ActionKind &recoveryFollowupKind_ = runtime_.recoveryFollowupKind;
    ActionStep &recoveryFollowupStep_ = runtime_.recoveryFollowupStep;
    float &recoveryFollowupDelayTimer_ = runtime_.recoveryFollowupDelayTimer;
    bool &isMargitComboATransition_ = runtime_.isMargitComboATransition;

    float phase2PressureMaxDistanceBonus_ = 1.2f;
    float phase2MidPressureTacticChance_ = 0.68f;
    float phase2DelaySmashBonus_ = 0.22f;
    float sweepRecoveryTime_ = 1.0f;
    bool &isDoubleSweepSecondStage_ = runtime_.isDoubleSweepSecondStage;
    float shotWarpFollowupChance_ = 0.20f;
    float phase2ShotWarpFollowupBonus_ = 0.18f;
    float shotWarpMinDistance_ = 4.0f;
    float shotWarpMaxDistance_ = 9.0f;
    float shotWarpFollowupDelayMin_ = 0.08f;
    float shotWarpFollowupDelayMax_ = 0.18f;
    bool &currentActionConnected_ = runtime_.currentActionConnected;
    bool &currentActionGuarded_ = runtime_.currentActionGuarded;
    int &dualCounterStage_ = runtime_.dualCounterStage;
    bool &dualCounterFirstHand_ = runtime_.dualCounterFirstHand;
    bool &dualCounterStageResolved_ = runtime_.dualCounterStageResolved;
    float delaySmashWhiffRecoveryBonus_ = 0.34f;
    float punishWindowTurnSpeedScale_ = 0.55f;
    float smashActiveLungeSpeed_ = 1.18f;
    float sweepActiveLungeSpeed_ = 0.92f;
    float phase2ActiveLungeScale_ = 1.12f;
    float phase2RecoveryBranchChanceBonus_ = 0.08f;

    bool suspendWarpForPresentation_ = false;
    float bladeClashPresentationTime_ = 0.0f;
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
    int warpApproachWeight_ = 8;
    float farDistanceTimer_ = 0.0f;
    float farDistanceWarpTimeThreshold_ = 2.0f;
    int farDistanceWarpBonus_ = 50;

    float backWarpAfterSmashChance_ = 0.35f;
    float backWarpAfterSweepChance_ = 0.30f;
    float backWarpAfterWaveChance_ = 0.20f;

    float closePressureDistance_ = 3.0f;
    float closePressureTimeThreshold_ = 1.0f;
    float closePressureTimer_ = 0.0f;

    int nearSmashWeight_ = 30;
    int nearSweepWeight_ = 25;
    int phase2NearSmashBonus_ = 8;
    int phase2NearSweepBonus_ = 14;

    int midShotWeight_ = 25;
    int midWaveWeight_ = 30;
    int phase2MidShotBonus_ = 12;

    int farShotWeight_ = 35;
    int farWarpWeight_ = 30;
    int farWaveWeight_ = 40;
    int neutralMidShotBonus_ = 12;
    int neutralMidWaveBonus_ = 10;

    float chargeTurnSpeed_ = 6.0f;
    float recoveryTurnSpeed_ = 2.0f;
    float idleTurnSpeed_ = 8.0f;
    bool &hasTrackingLocked_ = runtime_.hasTrackingLocked;

    float stagnantDistanceThreshold_ = 0.15f;
    float stagnantTimeThreshold_ = 1.2f;
    int stagnantWarpBonus_ = 5;

    CounterAdaptMemory &counterMemory_ = runtime_.counterMemory;
    float &postCounterRhythmTimer_ = runtime_.postCounterRhythmTimer;
    bool &forceEscapeWarpNext_ = runtime_.forceEscapeWarpNext;
    bool &forceCounterBaitNext_ = runtime_.forceCounterBaitNext;

    float stalkDurationMin_ = 0.45f;
    float stalkDurationMax_ = 1.10f;
    float stalkMoveSpeed_ = 1.45f;
    float stalkStrafeRadiusWeight_ = 0.75f;
    float stalkForwardAdjustWeight_ = 0.35f;
    float stalkNearEnterChance_ = 0.28f;
    float stalkPounceDistanceBonus_ = 0.65f;
    float stalkPounceMinTime_ = 0.22f;
    float stalkPounceChance_ = 0.58f;
    float stalkMidEnterChance_ = 0.18f;
    int stalkRepeatLimit_ = 2;

    float shotLeadTimeScale_ = 0.42f;
    float phase2ShotLeadBonus_ = 0.18f;
    float phase2ShotFanOffset_ = 0.55f;
    float phase2WaveFanAngleRad_ = 0.28f;
    float novaPhase2Cooldown_ = 0.0f;
    float novaPhase2CooldownDuration_ = 7.0f;
    float novaPhase2NearChance_ = 0.18f;
    float novaPhase2FarChance_ = 0.16f;
    float cinematicPitch_ = 0.0f;
    float cinematicRoll_ = 0.0f;

    int &stalkRepeatCount_ = runtime_.stalkRepeatCount;
    float &stalkMoveDir_ = runtime_.stalkMoveDir;         // -1:left / +1:right
    float &stalkForwardBias_ = runtime_.stalkForwardBias; // -1:後退 / +1:前進
    float arenaClampRadius_ = 10.8f;

  private:
    void UpdateParts();
    OBB MakeOBB(const Transform &tf, const DirectX::XMFLOAT3 &size) const;
    void ClampToArena();

    void UpdateByAction(float deltaTime);

    void UpdateSmashByStep(float deltaTime);
    void UpdateSweepByStep(float deltaTime);
    void UpdateShotByStep(float deltaTime);
    void UpdateBladeClashByStep(float deltaTime);
    void UpdateWaveByStep(float deltaTime);
    void UpdateCageByStep(float deltaTime);
    void UpdateNovaByStep(float deltaTime);
    void UpdateWarpByStep(float deltaTime);
    void UpdateIdle(float deltaTime);

    TacticState DecideTactic() const;
    void BeginActionFromTactic(TacticState tactic);
    bool TryBeginStalkAction(float chance, float repeatScale);
    ActionKind SelectNeutralAction(float distance) const;
    ActionKind SelectNearPressureAction() const;
    ActionKind SelectChaseAction() const;
    bool TryBeginTacticActionOrFallback(ActionKind preferred, ActionKind fallback);
    void BeginNeutralAction();
    void BeginPressureAction();
    void BeginChaseAction();
    void BeginResetAction();
    bool TryBeginWarpBehindMeleeSkill(bool force);

    void UpdateSmashCharge(float deltaTime);
    void UpdateSmashHold(float deltaTime);
    void UpdateSmashAttack(float deltaTime);
    void UpdateSmashRecovery(float deltaTime);

    void UpdateSweepCharge(float deltaTime);
    void UpdateSweepHold(float deltaTime);
    void UpdateSweepAttack(float deltaTime);
    void UpdateSweepRecovery(float deltaTime);

    void UpdateFacingToPlayer();
    void LockCurrentFacing();
    void UpdateFacingToPlayerWithSpeed(float deltaTime, float turnSpeed);
    float NormalizeAngle(float angle) const;

    void UpdateShotCharge(float deltaTime);
    void UpdateShotFire(float deltaTime);
    void UpdateShotRecovery(float deltaTime);
    void UpdateBladeClashCharge(float deltaTime);
    void UpdateBladeClashActive(float deltaTime);
    void UpdateBladeClashRecovery(float deltaTime);

    void WarpToShotArenaEdge();
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
    void BeginBackWarpPostAction();
    void ResetWarpContext();

    bool TryContinueChain();
    bool DecideNextChainAction(ActionKind finishedKind, ActionKind &outKind,
                               ActionStep &outStep) const;
    bool TryStartPostActionWarpChain(ActionKind finishedKind);
    bool TryStartBackWarpPostAction(ActionKind finishedKind);
    void SetupSweepWarpSmashChain();
    void SetupWaveWarpSmashChain();
    void OverrideWarpFollowupByChain();
    void ResetChainContext();
    void ResetPostActionState();
    void FinishCurrentAction();

    void UpdateWaveCharge(float deltaTime);
    void UpdateWaveFire(float deltaTime);
    void UpdateWaveRecovery(float deltaTime);
    void UpdateCageCharge(float deltaTime);
    void UpdateCageFire(float deltaTime);
    void UpdateCageRecovery(float deltaTime);

    void SpawnWave();
    void SpawnCageTrap();
    void UpdateWaves(float deltaTime);
    void UpdateCageTrap(float deltaTime);
    void UpdateNovaCharge(float deltaTime);
    void UpdateNovaActive(float deltaTime);
    void UpdateNovaRecovery(float deltaTime);
    void SpawnNovaRing(int ringIndex);
    void SpawnNovaSkyBullets();

    void UpdateStalkByStep(float deltaTime);
    void UpdateStalkMove(float deltaTime);
    void BeginStalkAction();

    float GetCurrentActionTime() const;
    float GetCurrentSmashChargeTime() const;
    float GetCurrentSweepChargeTime() const;
    bool TryBeginDoubleSweepSecondStage();

    OBB GetSmashAttackOBB() const;
    OBB GetSweepAttackOBB() const;
    float GetVisualYaw() const;

    const AttackTimingParam *GetCurrentAttackTiming() const;
    AttackParam *GetCurrentAttackParam();
    const AttackParam *GetCurrentAttackParam() const;

    bool IsCurrentAttackInActiveWindow() const;
    bool IsCurrentAttackInRecoveryWindow() const;
    bool ShouldUseLockedAttackYaw() const;

    ActionId MakeDefaultActionId(ActionKind kind) const;

    bool TryBeginTacticAction(ActionKind kind);
    void BeginAction(ActionKind kind, ActionStep step);
    void ChangeActionStep(ActionStep step);
    void EndAttack();

    bool HasReachedTrackingEnd() const;
    bool HasReachedHitStart() const;
    bool HasReachedHitEnd() const;
    bool HasReachedRecoveryStart() const;

    void ValidateTiming(AttackTimingParam &timing, float chargeTime);
    void ValidateAllTimings();
    void UpdateBossPhase();

    CounterReadAxis GetCounterReadAxis(ActionKind kind) const;
    bool ShouldEnterSmashHold() const;
    bool ShouldEnterSweepHold() const;
    void EnterHold(float duration);

    void DecideHoldBranch(ActionKind kind);
    bool TryExecuteHoldBranch(ActionKind kind);

    void EnterTell(ActionKind kind);
    bool IsTellFinished() const;

    bool ShouldDoFakeCommit(ActionKind kind) const;
    void EnterFakeCommit(ActionKind kind);
    bool IsFakeCommitFinished() const;

    void EnterFreezeHold(ActionKind kind);
    bool IsFreezeHoldFinished() const;

    void ResetPreAttackPresentationState();

    bool TryBranchFromRecovery(ActionKind finishedKind);
    void ResetRecoveryBranchState();
    bool IsCounterFailObserved() const;
    float RandomRange(float minValue, float maxValue) const;

    void UpdateCounterAdaptation(float deltaTime);
    void RegisterCounterSuccessReaction();
    bool ApplyCounterBreakReaction(float vulnerabilityDuration = 0.0f);
    float GetAdaptiveHoldChance(ActionKind kind) const;
    float GetAdaptiveChargeOffset(ActionKind kind) const;
    bool ShouldSnapReleaseFromRead() const;
    ActionKind DecideAdaptiveCounterBaitAction() const;
};
