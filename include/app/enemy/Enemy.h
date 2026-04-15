#pragma once
#include "Camera.h"
#include "EnemyActionData.h"
#include "EnemyTuningPreset.h"
#include "OBB.h"
#include "Player.h"
#include "Transform.h"
#include <cstddef>
#include <cstdint>
#include <vector>

class ModelManager;

enum class EnemyState {
    Idle,

    SmashCharge,
    SmashAttack,
    SmashRecovery,

    SweepCharge,
    SweepAttack,
    SweepRecovery,

    ShotCharge,
    ShotFire,
    ShotRecovery,

    WarpStart,
    WarpMove,
    WarpEnd,

    WaveCharge,
    WaveFire,
    WaveRecovery,

    GuardMove,
    GuardHold,
    GuardRecovery
};

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
enum class WarpApproachSlot { None, FrontLeft, FrontRight, LongFront };

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
};

struct EnemyWave {
    DirectX::XMFLOAT3 position = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 direction = {0.0f, 0.0f, 1.0f};
    float speed = 0.0f;
    float traveledDistance = 0.0f;
    float maxDistance = 0.0f;
    bool isAlive = false;
};

struct WarpTrailGhost {
    DirectX::XMFLOAT3 position = {0.0f, 0.0f, 0.0f};
    float life = 0.0f;
    float scale = 1.0f;
    bool isActive = false;
};

// 仕様書に合わせて BodyCenter -> BodyRight に整理
enum class GuardTarget { None, Face, BodyCenter, BodyLeft, BodyRight };

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
    Neutral,
    Pressure,
    CounterBait,
    CounterPunish,
    AntiGuard,
    Chase,
    Reset
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

enum class SmashStyle { Normal, Delay };
enum class SweepStyle { Normal, Double, Advance };
enum class PostActionOption { None, BackWarp };
enum class BackWarpFollowup { None, Shot, Wave, Rush };

enum class HoldBranchType { None, Active, Warp, Guard, Rush };

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

class Enemy {
  public:
    void Initialize(uint32_t modelId, uint32_t projectileModelId = 0);

    // 既存互換
    void Update(const DirectX::XMFLOAT3 &playerPos, float deltaTime,
                bool playerGuarding);

    // 新版：観測情報ごと渡す
    void Update(const PlayerCombatObservation &playerObs, float deltaTime);

    void Draw(ModelManager *modelManager, const Camera &camera);
    void TakeDamage(float damage);
    void ConsumeBullet(size_t index);
    void ConsumeWave(size_t index);
    void NotifyAttackConnected();
    void NotifyAttackGuarded();

    const Transform &GetTransform() const { return tf_; }
    bool IsAlive() const { return !deathFinished_; }

    const Transform &GetBodyTransform() const { return bodyTf_; }
    const Transform &GetLeftHandTransform() const { return leftHandTf_; }
    const Transform &GetRightHandTransform() const { return rightHandTf_; }

    OBB GetBodyOBB() const;
    OBB GetLeftHandOBB() const;
    OBB GetRightHandOBB() const;

    ActionKind GetActionKind() const { return action_.kind; }
    ActionId GetActionId() const { return action_.id; }
    ActionStep GetActionStep() const { return action_.step; }
    TacticState GetTacticState() const { return tactic_; }
    BossPhase GetBossPhase() const { return phase_; }
    bool IsPhaseTransitionActive() const { return phaseTransitionActive_; }
    float GetPhaseTransitionRatio() const {
        if (phaseTransitionDuration_ <= 0.0001f) {
            return 1.0f;
        }
        float t = phaseTransitionTimer_ / phaseTransitionDuration_;
        if (t < 0.0f) {
            t = 0.0f;
        }
        if (t > 1.0f) {
            t = 1.0f;
        }
        return t;
    }

    bool IsAttackActive() const { return isAttackActive_; }
    OBB GetAttackOBB() const;

    float GetDistanceToPlayer() const;

    float GetFacingYaw() const { return facingYaw_; }
    float GetLockedAttackYaw() const { return lockedAttackYaw_; }
    float GetStagnantTimer() const { return stagnantTimer_; }
    bool IsDistanceStagnant() const { return isDistanceStagnant_; }
    float GetLastDistanceToPlayer() const { return lastDistanceToPlayer_; }
    float GetStagnantDistanceThreshold() const {
        return stagnantDistanceThreshold_;
    }
    float GetStagnantTimeThreshold() const { return stagnantTimeThreshold_; }
    int GetStagnantWarpBonus() const { return stagnantWarpBonus_; }

    const std::vector<EnemyBullet> &GetBullets() const { return bullets_; }

    bool IsVisible() const { return isVisible_; }
    const DirectX::XMFLOAT3 &GetWarpTargetPos() const {
        return warp_.targetPos;
    }
    const DirectX::XMFLOAT3 &GetWarpDeparturePos() const {
        return warp_.departurePos;
    }
    bool HasWarpDeparturePos() const { return warp_.hasDeparturePos; }
    WarpType GetWarpType() const { return warp_.type; }
    bool IsWarpCollisionDisabled() const { return warp_.collisionDisabled; }

    const std::vector<EnemyWave> &GetWaves() const { return waves_; }

    bool IsGuardActive() const { return isGuardActive_; }
    GuardTarget GetGuardTarget() const { return guardTarget_; }

    float GetSmashDamage() const { return smashParam_.damage; }
    float GetSweepDamage() const { return sweepParam_.damage; }
    float GetBulletDamage() const { return bulletParam_.damage; }
    float GetWaveDamage() const { return waveParam_.damage; }
    float GetRushDamage() const { return rushParam_.damage; }

    float GetSmashKnockback() const { return smashParam_.knockback; }
    float GetSweepKnockback() const { return sweepParam_.knockback; }
    float GetBulletKnockback() const { return bulletParam_.knockback; }
    float GetWaveKnockback() const { return waveParam_.knockback; }
    float GetRushKnockback() const { return rushParam_.knockback; }

    DirectX::XMFLOAT3 GetBulletHitBoxSize() const {
        return bulletParam_.hitBoxSize;
    }
    DirectX::XMFLOAT3 GetWaveHitBoxSize() const {
        return waveParam_.hitBoxSize;
    }
    DirectX::XMFLOAT3 GetSmashAttackBoxSize() const {
        return smashParam_.hitBoxSize;
    }
    DirectX::XMFLOAT3 GetSweepAttackBoxSize() const {
        return sweepParam_.hitBoxSize;
    }
    DirectX::XMFLOAT3 GetRushAttackBoxSize() const {
        return rushParam_.hitBoxSize;
    }

    float GetCurrentAttackDamage() const;
    float GetCurrentAttackKnockback() const;
    DirectX::XMFLOAT3 GetCurrentAttackHitBoxSize() const;

    const AttackParam &GetSmashParam() const { return smashParam_; }
    const AttackParam &GetSweepParam() const { return sweepParam_; }
    const AttackParam &GetBulletParam() const { return bulletParam_; }
    const AttackParam &GetWaveParam() const { return waveParam_; }
    const AttackParam &GetRushParam() const { return rushParam_; }

    const AttackTimingParam &GetSmashTiming() const { return smashTiming_; }
    const AttackTimingParam &GetSweepTiming() const { return sweepTiming_; }
    const AttackTimingParam &GetRushTiming() const { return rushTiming_; }

    AttackParam &EditSmashParam() { return smashParam_; }
    AttackParam &EditSweepParam() { return sweepParam_; }
    AttackParam &EditBulletParam() { return bulletParam_; }
    AttackParam &EditWaveParam() { return waveParam_; }
    AttackParam &EditRushParam() { return rushParam_; }

    float &EditNearAttackDistance() { return nearAttackDistance_; }
    float &EditFarAttackDistance() { return farAttackDistance_; }

    float &EditSmashChargeTime() { return smashChargeTime_; }
    float &EditSweepChargeTime() { return sweepChargeTime_; }

    float &EditShotChargeTime() { return shotChargeTime_; }
    float &EditShotRecoveryTime() { return shotRecoveryTime_; }
    float &EditShotInterval() { return shotInterval_; }
    float &EditBulletSpeed() { return bulletSpeed_; }
    float &EditBulletLifeTime() { return bulletLifeTime_; }

    float &EditWaveChargeTime() { return waveChargeTime_; }
    float &EditWaveRecoveryTime() { return waveRecoveryTime_; }
    float &EditWaveSpeed() { return waveSpeed_; }
    float &EditWaveMaxDistance() { return waveMaxDistance_; }

    float &EditRushChargeTime() { return rushChargeTime_; }
    float &EditRushSpeed() { return rushSpeed_; }
    float &EditRushMoveDuration() { return rushMoveDuration_; }

    int &EditNearSmashWeight() { return nearSmashWeight_; }
    int &EditNearSweepWeight() { return nearSweepWeight_; }
    int &EditNearGuardWeight() { return nearGuardWeight_; }
    int &EditNearRushWeight() { return nearRushWeight_; }

    int &EditMidRushWeight() { return midRushWeight_; }
    int &EditMidShotWeight() { return midShotWeight_; }
    int &EditMidWaveWeight() { return midWaveWeight_; }

    int &EditFarShotWeight() { return farShotWeight_; }
    int &EditFarWarpWeight() { return farWarpWeight_; }
    int &EditFarWaveWeight() { return farWaveWeight_; }

    float &EditSweepWarpSmashMaxDistance() {
        return sweepWarpSmashMaxDistance_;
    }
    float &EditSweepWarpSmashChance() { return sweepWarpSmashChance_; }
    float &EditWaveWarpSmashMinDistance() { return waveWarpSmashMinDistance_; }
    float &EditWaveWarpSmashChance() { return waveWarpSmashChance_; }

    AttackTimingParam &EditSmashTiming() { return smashTiming_; }
    AttackTimingParam &EditSweepTiming() { return sweepTiming_; }
    AttackTimingParam &EditRushTiming() { return rushTiming_; }

    float &EditRushTurnSpeed() { return rushTurnSpeed_; }
    float &EditRushStartCurveAngleDeg() { return rushStartCurveAngleDeg_; }

    float &EditSmashFeintChance() { return smashFeintChance_; }
    float &EditSweepFeintChance() { return sweepFeintChance_; }
    float &EditSmashHoldTimeMin() { return smashHoldTimeMin_; }
    float &EditSmashHoldTimeMax() { return smashHoldTimeMax_; }
    float &EditSweepHoldTimeMin() { return sweepHoldTimeMin_; }
    float &EditSweepHoldTimeMax() { return sweepHoldTimeMax_; }

    EnemyTuningPreset CreateTuningPreset() const;
    void ApplyTuningPreset(const EnemyTuningPreset &preset);
    void ResetTuningPreset();

    float GetCurrentActionTimePublic() const { return GetCurrentActionTime(); }
    const AttackTimingParam *GetCurrentAttackTimingPublic() const {
        return GetCurrentAttackTiming();
    }

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

    float hp_ = 1000.0f;
    float maxHp_ = 1000.0f;
    bool isDying_ = false;
    bool deathFinished_ = false;
    float hitReactionTimer_ = 0.0f;
    float hitReactionDuration_ = 0.12f;
    float hitReactionMoveSpeed_ = 3.5f;
    float deathTimer_ = 0.0f;
    float deathDuration_ = 0.75f;
    float deathSinkDistance_ = 2.2f;
    float deathStartY_ = 0.0f;

    ActionState action_{};
    float stateTimer_ = 0.0f;
    ActionKind lastActionKind_ = ActionKind::None;

    bool isAttackActive_ = false;

    DirectX::XMFLOAT3 playerPos_ = {0.0f, 0.0f, 0.0f};
    bool playerGuarding_ = false;
    PlayerCombatObservation playerObs_{};

    float facingYaw_ = 0.0f;
    float lockedAttackYaw_ = 0.0f;

    std::vector<EnemyBullet> bullets_{};

    int shotsRemaining_ = 0;
    float shotIntervalTimer_ = 0.0f;

    WarpContext warp_{};
    ChainContext chain_{};
    bool isVisible_ = true;

    std::vector<EnemyWave> waves_{};

    int farActionIndex_ = 0;
    int nearActionIndex_ = 0;

    GuardTarget guardTarget_ = GuardTarget::None;
    bool isGuardActive_ = false;

    float lastDistanceToPlayer_ = 0.0f;
    float stagnantTimer_ = 0.0f;
    bool isDistanceStagnant_ = false;

    SmashStyle smashStyle_ = SmashStyle::Normal;
    SweepStyle sweepStyle_ = SweepStyle::Normal;

    PostActionOption postActionOption_ = PostActionOption::None;
    BackWarpFollowup backWarpFollowup_ = BackWarpFollowup::None;

    TacticState tactic_ = TacticState::Neutral;
    BossPhase phase_ = BossPhase::Phase1;
    float phase2HealthRatioThreshold_ = 0.60f;
    bool phaseTransitionActive_ = false;
    float phaseTransitionTimer_ = 0.0f;
    float phaseTransitionDuration_ = 0.90f;

    bool holdConfigured_ = false;
    float currentHoldDuration_ = 0.0f;

    HoldBranchType holdBranchType_ = HoldBranchType::None;

    float holdBranchDecisionTime_ = 0.0f;
    bool holdBranchDecided_ = false;

    float smashHoldBranchWarpChance_ = 0.18f;
    float smashHoldBranchGuardChance_ = 0.12f;
    float smashHoldBranchRushChance_ = 0.10f;

    float sweepHoldBranchWarpChance_ = 0.14f;
    float sweepHoldBranchGuardChance_ = 0.10f;
    float sweepHoldBranchRushChance_ = 0.16f;

    // ============================================================
    // Step2: Tell / FakeCommit / FreezeHold
    // ============================================================
    bool tellActive_ = false;
    bool fakeCommitActive_ = false;
    bool freezeHoldActive_ = false;

    float tellDuration_ = 0.0f;
    float fakeCommitDuration_ = 0.0f;
    float freezeHoldDuration_ = 0.0f;

    float smashTellTime_ = 0.12f;
    float sweepTellTime_ = 0.10f;

    float smashFakeCommitChance_ = 0.45f;
    float sweepFakeCommitChance_ = 0.32f;

    float smashFakeCommitTime_ = 0.10f;
    float sweepFakeCommitTime_ = 0.08f;

    float smashFreezeHoldTimeMin_ = 0.10f;
    float smashFreezeHoldTimeMax_ = 0.18f;
    float sweepFreezeHoldTimeMin_ = 0.08f;
    float sweepFreezeHoldTimeMax_ = 0.14f;

    // ============================================================
    // Step3: Recovery -> Recommit / DelayedSecond / EscapeFakeout
    // ============================================================
    RecoveryBranchType recoveryBranchType_ = RecoveryBranchType::None;

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

    ActionKind recoveryFollowupKind_ = ActionKind::None;
    ActionStep recoveryFollowupStep_ = ActionStep::None;
    float recoveryFollowupDelayTimer_ = 0.0f;
    bool isMargitComboATransition_ = false;
    bool isMargitComboBTransition_ = false;

    float nearAttackDistance_ = 4.0f;
    float farAttackDistance_ = 6.5f;
    float phase2PressureMaxDistanceBonus_ = 1.2f;
    float phase2MidPressureTacticChance_ = 0.68f;

    AttackParam smashParam_ = {10.0f, 4.0f, {1.5f, 1.8f, 1.5f}};
    float smashChargeTime_ = 0.45f;
    AttackTimingParam smashTiming_ = {0.88f, 0.28f, 0.04f, 0.10f, 0.18f};
    float smashAttackForwardOffset_ = 1.4f;
    float smashAttackHeightOffset_ = 0.8f;
    float delaySmashChance_ = 0.35f;
    float delaySmashExtraChargeTime_ = 0.30f;
    float phase2DelaySmashBonus_ = 0.22f;

    float smashFeintChance_ = 0.45f;
    float smashHoldTimeMin_ = 0.12f;
    float smashHoldTimeMax_ = 0.40f;

    AttackParam sweepParam_ = {10.0f, 4.0f, {3.2f, 1.2f, 1.4f}};
    AttackTimingParam sweepTiming_ = {1.27f, 0.36f, 0.12f, 0.24f, 0.32f};
    float sweepChargeTime_ = 0.65f;
    float sweepRecoveryTime_ = 1.0f;
    float sweepAttackSideOffset_ = 0.2f;
    float sweepAttackHeightOffset_ = 0.8f;
    float doubleSweepChance_ = 0.30f;
    float doubleSweepSecondDelay_ = 0.18f;
    float doubleSweepSecondChargeScale_ = 0.55f;
    bool isDoubleSweepSecondStage_ = false;

    float sweepFeintChance_ = 0.28f;
    float sweepHoldTimeMin_ = 0.10f;
    float sweepHoldTimeMax_ = 0.30f;

    AttackParam bulletParam_ = {5.0f, 2.5f, {0.4f, 0.4f, 0.4f}};
    float shotChargeTime_ = 0.6f;
    float shotRecoveryTime_ = 0.8f;
    float shotInterval_ = 0.2f;
    int shotMinCount_ = 3;
    int shotMaxCount_ = 5;
    float shotRushFollowupChance_ = 0.42f;
    float phase2ShotRushFollowupBonus_ = 0.20f;
    float shotWarpFollowupChance_ = 0.20f;
    float phase2ShotWarpFollowupBonus_ = 0.18f;
    float shotRushMinDistance_ = 3.0f;
    float shotRushMaxDistance_ = 8.5f;
    float shotRushFollowupDelayMin_ = 0.06f;
    float shotRushFollowupDelayMax_ = 0.16f;
    float shotWarpMinDistance_ = 4.0f;
    float shotWarpMaxDistance_ = 9.0f;
    float shotWarpFollowupDelayMin_ = 0.08f;
    float shotWarpFollowupDelayMax_ = 0.18f;
    float bulletSpeed_ = 6.0f;
    float bulletLifeTime_ = 2.0f;
    float bulletSpawnHeightOffset_ = 0.2f;

    AttackParam waveParam_ = {8.0f, 3.0f, {1.2f, 0.6f, 1.6f}};
    float waveChargeTime_ = 0.6f;
    float waveRecoveryTime_ = 0.8f;
    float waveSpeed_ = 4.0f;
    float waveMaxDistance_ = 8.0f;
    float waveSpawnForwardOffset_ = 1.5f;
    float waveSpawnHeightOffset_ = 0.0f;

    AttackParam rushParam_ = {12.0f, 5.0f, {1.2f, 1.4f, 2.2f}};
    AttackTimingParam rushTiming_ = {0.75f, 0.20f, 0.08f, 0.28f, 0.38f};
    float rushChargeTime_ = 0.28f;
    float rushSpeed_ = 8.5f;
    float rushMoveDuration_ = 0.26f;
    float rushAttackForwardOffset_ = 1.1f;
    float rushAttackHeightOffset_ = 0.8f;
    float rushChargeCreepSpeed_ = 1.35f;
    float rushChargeTrackingTurnScale_ = 0.72f;
    float rushChargeGuardTurnScale_ = 0.45f;
    float rushChargeGuardTimeBonus_ = 0.10f;
    float rushSweepFollowupChance_ = 0.34f;
    float phase2RushSweepFollowupBonus_ = 0.18f;
    float rushSweepMinDistance_ = 1.6f;
    float rushSweepMaxDistance_ = 4.6f;
    float rushSweepFollowupDelayMin_ = 0.05f;
    float rushSweepFollowupDelayMax_ = 0.14f;

    float rushTurnSpeed_ = 4.5f;
    float rushStartCurveAngleDeg_ = 32.0f;
    float rushCurveTurnScale_ = 0.60f;
    float rushHomingTurnScale_ = 1.35f;
    float rushBrakeTurnScale_ = 0.45f;
    float rushCurveSpeedScale_ = 0.92f;
    float rushHomingSpeedScale_ = 1.12f;
    float rushBrakeSpeedScale_ = 0.58f;
    float rushCurvePhaseRatio_ = 0.35f;
    float rushBrakeStartRatio_ = 0.78f;
    float rushBrakeDistance_ = 1.35f;
    float rushCurrentYaw_ = 0.0f;
    float rushCurveDir_ = 1.0f;
    bool currentActionConnected_ = false;
    bool currentActionGuarded_ = false;
    bool rushFollowupEvaluated_ = false;
    bool rushWillSweepFollowup_ = false;
    bool rushFromShotCombo_ = false;
    float delaySmashWhiffRecoveryBonus_ = 0.34f;
    float rushWhiffRecoveryBonus_ = 0.22f;
    float punishWindowTurnSpeedScale_ = 0.55f;
    float comboBRushSweepBonus_ = 0.28f;

    float warpStartTime_ = 0.2f;
    float warpMoveTime_ = 0.10f;
    float warpEndTime_ = 0.2f;
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
    float warpTrailEmitTimer_ = 0.0f;
    static constexpr int kWarpTrailGhostCount_ = 4;
    WarpTrailGhost warpTrailGhosts_[kWarpTrailGhostCount_]{};

    float warpNearRadiusMin_ = 1.8f;
    float warpNearRadiusMax_ = 3.0f;
    float warpApproachForwardDistance_ = 2.3f;
    float warpApproachSideDistance_ = 2.1f;
    float warpApproachLongFrontDistance_ = 4.0f;
    int warpApproachWeight_ = 8;
    float farDistanceTimer_ = 0.0f;
    float farDistanceWarpTimeThreshold_ = 2.0f;
    int farDistanceWarpBonus_ = 50;

    float warpFarRadiusMin_ = 5.0f;
    float warpFarRadiusMax_ = 7.5f;
    int warpEscapeWeight_ = 10;
    float warpEscapeCooldown_ = 7.0f;
    float warpEscapeCooldownTimer_ = 0.0f;

    float backWarpAfterSmashChance_ = 0.35f;
    float backWarpAfterSweepChance_ = 0.30f;
    float backWarpAfterWaveChance_ = 0.20f;

    float backWarpShotChance_ = 0.55f;
    float backWarpWaveChance_ = 0.45f;

    int warpApproachChainMaxSteps_ = 2;
    int warpEscapeChainMaxSteps_ = 2;
    float approachChainContinueDistance_ = 5.0f;
    float escapeChainContinueDistance_ = 4.5f;

    float sweepWarpSmashMaxDistance_ = 5.0f;
    float sweepWarpSmashChance_ = 0.45f;

    float waveWarpSmashMinDistance_ = 4.5f;
    float waveWarpSmashChance_ = 0.50f;

    float closePressureDistance_ = 3.0f;
    float closePressureTimeThreshold_ = 1.0f;
    float closePressureTimer_ = 0.0f;

    float guardMoveTime_ = 0.25f;
    float guardHoldTime_ = 0.7f;
    float guardRecoveryTime_ = 0.35f;

    int nearSmashWeight_ = 30;
    int nearSweepWeight_ = 25;
    int nearGuardWeight_ = 5;
    int nearRushWeight_ = 40;
    int phase2NearSmashBonus_ = 8;
    int phase2NearSweepBonus_ = 14;
    int phase2NearGuardPenalty_ = 2;
    int phase2NearRushBonus_ = 6;

    int midRushWeight_ = 45;
    int midShotWeight_ = 25;
    int midWaveWeight_ = 30;
    int phase2MidRushBonus_ = 10;
    int phase2MidShotBonus_ = 12;
    int phase2FarWarpBonus_ = 10;

    int farShotWeight_ = 35;
    int farWarpWeight_ = 25;
    int farWaveWeight_ = 40;

    int counterBaitSmashWeight_ = 45;
    int counterBaitSweepWeight_ = 35;
    int counterBaitGuardWeight_ = 20;
    int phase2CounterBaitMeleeBonus_ = 10;

    int counterPunishSmashWeight_ = 30;
    int counterPunishSweepWeight_ = 20;
    int counterPunishRushWeight_ = 50;
    int phase2CounterPunishSmashBonus_ = 14;
    int phase2CounterPunishSweepBonus_ = 10;

    int antiGuardWaveBonus_ = 20;
    int antiGuardShotBonus_ = 8;
    int antiGuardRushBonus_ = 10;

    float chargeTurnSpeed_ = 6.0f;
    float recoveryTurnSpeed_ = 2.0f;
    float idleTurnSpeed_ = 8.0f;
    bool hasTrackingLocked_ = false;

    float stagnantDistanceThreshold_ = 0.15f;
    float stagnantTimeThreshold_ = 1.2f;
    int stagnantWarpBonus_ = 5;

    CounterAdaptMemory counterMemory_{};
    float postCounterRhythmTimer_ = 0.0f;
    bool forceEscapeWarpNext_ = false;
    bool forceCounterBaitNext_ = false;

    float stalkDurationMin_ = 0.45f;
    float stalkDurationMax_ = 1.10f;
    float stalkMoveSpeed_ = 2.2f;
    float stalkStrafeRadiusWeight_ = 0.75f;
    float stalkForwardAdjustWeight_ = 0.35f;
    float stalkNearEnterChance_ = 0.28f;
    float stalkMidEnterChance_ = 0.18f;
    int stalkRepeatLimit_ = 2;

    int stalkRepeatCount_ = 0;
    float stalkMoveDir_ = 1.0f;     // -1:left / +1:right
    float stalkForwardBias_ = 0.0f; // -1:後退 / +1:前進

  private:
    void UpdateParts();
    OBB MakeOBB(const Transform &tf, const DirectX::XMFLOAT3 &size) const;

    void UpdateByAction(float deltaTime);

    void UpdateSmashByStep(float deltaTime);
    void UpdateSweepByStep(float deltaTime);
    void UpdateShotByStep(float deltaTime);
    void UpdateWaveByStep(float deltaTime);
    void UpdateRushByStep(float deltaTime);
    void UpdateWarpByStep(float deltaTime);
    void UpdateGuardByStep(float deltaTime);

    void UpdateIdle(float deltaTime);

    TacticState DecideTactic() const;
    void BeginActionFromTactic(TacticState tactic);
    void BeginPressureAction();
    void BeginCounterBaitAction();
    void BeginCounterPunishAction();
    void BeginAntiGuardAction();
    void BeginChaseAction();
    void BeginResetAction();

    void UpdateSmashCharge(float deltaTime);
    void UpdateSmashHold(float deltaTime);
    void UpdateSmashAttack(float deltaTime);
    void UpdateSmashRecovery(float deltaTime);

    void UpdateSweepCharge(float deltaTime);
    void UpdateSweepHold(float deltaTime);
    void UpdateSweepAttack(float deltaTime);
    void UpdateSweepRecovery(float deltaTime);

    void UpdateRushCharge(float deltaTime);
    void UpdateRushAttack(float deltaTime);
    void UpdateRushRecovery(float deltaTime);

    void UpdateFacingToPlayer();
    void LockCurrentFacing();
    void UpdateFacingToPlayerWithSpeed(float deltaTime, float turnSpeed);
    float NormalizeAngle(float angle) const;

    void UpdateShotCharge(float deltaTime);
    void UpdateShotFire(float deltaTime);
    void UpdateShotRecovery(float deltaTime);

    void SpawnBullet();
    void UpdateBullets(float deltaTime);

    void UpdateWarpStart(float deltaTime);
    void UpdateWarpMove(float deltaTime);
    void UpdateWarpEnd(float deltaTime);
    void UpdateWarpTrails(float deltaTime);
    void EmitWarpTrailGhost(const DirectX::XMFLOAT3 &position, float scale);
    void ResetWarpTrails();

    bool PrepareWarpContext();
    bool DecideWarpTargetNearPlayer(DirectX::XMFLOAT3 &outTarget);
    bool DecideWarpTargetFarFromPlayer(DirectX::XMFLOAT3 &outTarget) const;
    void DecideWarpFollowupFromContext();
    void SetupChainFromWarpContext();
    void BeginWarpFollowup();
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

    void SpawnWave();
    void UpdateWaves(float deltaTime);

    void UpdateGuardMove(float deltaTime);
    void UpdateGuardHold(float deltaTime);
    void UpdateGuardRecovery(float deltaTime);

    void UpdateStalkByStep(float deltaTime);
    void UpdateStalkMove(float deltaTime);
    void BeginStalkAction();

    void DecideGuardTarget();

    float GetCurrentActionTime() const;
    float GetCurrentSmashChargeTime() const;
    float GetCurrentSweepChargeTime() const;
    bool TryBeginDoubleSweepSecondStage();

    OBB GetSmashAttackOBB() const;
    OBB GetSweepAttackOBB() const;
    OBB GetRushAttackOBB() const;
    float GetVisualYaw() const;

    const AttackTimingParam *GetCurrentAttackTiming() const;
    AttackParam *GetCurrentAttackParam();
    const AttackParam *GetCurrentAttackParam() const;

    bool IsCurrentAttackInActiveWindow() const;
    bool IsCurrentAttackInRecoveryWindow() const;
    bool ShouldUseLockedAttackYaw() const;

    ActionId MakeDefaultActionId(ActionKind kind) const;

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
    float GetAdaptiveHoldChance(ActionKind kind) const;
    float GetAdaptiveChargeOffset(ActionKind kind) const;
    bool ShouldSnapReleaseFromRead() const;
    ActionKind DecideAdaptiveCounterBaitAction() const;
};
