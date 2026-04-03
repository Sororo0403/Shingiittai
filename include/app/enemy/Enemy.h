#pragma once
#include "Camera.h"
#include "EnemyActionData.h"
#include "EnemyTuningPreset.h"
#include "OBB.h"
#include "Player.h"
#include "Transform.h"
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

struct ActionState {
    ActionKind kind = ActionKind::None;
    ActionId id = ActionId::None;
    ActionStep step = ActionStep::None;
};

struct WarpContext {
    WarpType type = WarpType::None;
    DirectX::XMFLOAT3 targetPos = {0.0f, 0.0f, 0.0f};

    // ワープ後に直接つなぎたい行動
    ActionKind followupKind = ActionKind::None;
    ActionStep followupStep = ActionStep::None;

    // ワープ中の判定制御
    bool collisionDisabled = false;

    // ターゲット確定済みか
    bool hasValidTarget = false;
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

    // 何を起点に始まった連携か
    ChainStarter starter = ChainStarter::None;

    // 連携の現在段数
    int stepCount = 0;

    // 最大段数
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

enum class GuardTarget { None, Face, BodyCenter, BodyLeft };

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

class Enemy {
  public:
    void Initialize(uint32_t modelId);
    void Update(const DirectX::XMFLOAT3 &playerPos, float deltaTime,
                bool playerGuarding);
    void Draw(ModelManager *modelManager, const Camera &camera);
    void TakeDamage(float damage);

    // Getter
    const Transform &GetTransform() const { return tf_; }
    bool IsAlive() const { return hp_ > 0.0f; }

    const Transform &GetBodyTransform() const { return bodyTf_; }
    const Transform &GetLeftHandTransform() const { return leftHandTf_; }
    const Transform &GetRightHandTransform() const { return rightHandTf_; }

    OBB GetBodyOBB() const;
    OBB GetLeftHandOBB() const;
    OBB GetRightHandOBB() const;

    // 行動管理 getter
    ActionKind GetActionKind() const { return action_.kind; }
    ActionId GetActionId() const { return action_.id; }
    ActionStep GetActionStep() const { return action_.step; }

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

    // ImGui調整用
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

    EnemyTuningPreset CreateTuningPreset() const;
    void ApplyTuningPreset(const EnemyTuningPreset &preset);
    void ResetTuningPreset();

  private:
    Transform tf_{};

    Transform bodyTf_{};
    Transform leftHandTf_{};
    Transform rightHandTf_{};

    uint32_t modelId_ = 0;

    DirectX::XMFLOAT3 bodySize_ = {0.8f, 1.4f, 0.6f};
    DirectX::XMFLOAT3 handSize_ = {0.45f, 0.45f, 0.45f};

    float hp_ = 1000.0f;

    ActionState action_{};
    float stateTimer_ = 0.0f;
    ActionKind lastActionKind_ = ActionKind::None;

    bool isAttackActive_ = false;
    DirectX::XMFLOAT3 playerPos_ = {0.0f, 0.0f, 0.0f};
    bool playerGuarding_ = false;

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

    //********************************
    // 敵の連携用
    //********************************
    SmashStyle smashStyle_ = SmashStyle::Normal;
    SweepStyle sweepStyle_ = SweepStyle::Normal;

    PostActionOption postActionOption_ = PostActionOption::None;
    BackWarpFollowup backWarpFollowup_ = BackWarpFollowup::None;

    //********************************
    // 調整用パラメータ群
    //********************************
    float nearAttackDistance_ = 4.0f;
    float farAttackDistance_ = 4.0f;

    // 振り下ろし
    AttackParam smashParam_ = {10.0f, 4.0f, {1.5f, 1.8f, 1.5f}};
    float smashChargeTime_ = 0.45f;
    AttackTimingParam smashTiming_ = {
        0.88f, // totalTime
        0.28f, // trackingEndTime
        0.04f, // activeStartTime
        0.10f, // activeEndTime
        0.18f  // recoveryStartTime
    };
    float smashAttackForwardOffset_ = 1.4f;
    float smashAttackHeightOffset_ = 0.8f;
    float delaySmashChance_ = 0.35f;
    float delaySmashExtraChargeTime_ = 1.0f;

    // 薙ぎ払い
    AttackParam sweepParam_ = {10.0f, 4.0f, {3.2f, 1.2f, 1.4f}};
    AttackTimingParam sweepTiming_ = {
        1.27f, // totalTime
        0.36f, // trackingEndTime
        0.12f, // activeStartTime
        0.24f, // activeEndTime
        0.32f  // recoveryStartTime
    };
    float sweepChargeTime_ = 0.65f;
    float sweepRecoveryTime_ = 1.0f;
    float sweepAttackSideOffset_ = 0.2f;
    float sweepAttackHeightOffset_ = 0.8f;
    float doubleSweepChance_ = 0.30f;
    float doubleSweepSecondDelay_ = 0.18f;
    float doubleSweepSecondChargeScale_ = 0.55f;
    bool isDoubleSweepSecondStage_ = false;

    // 弾
    AttackParam bulletParam_ = {5.0f, 2.5f, {0.4f, 0.4f, 0.4f}};
    float shotChargeTime_ = 0.6f;
    float shotRecoveryTime_ = 0.8f;
    float shotInterval_ = 0.2f;
    int shotMinCount_ = 3;
    int shotMaxCount_ = 5;
    float bulletSpeed_ = 6.0f;
    float bulletLifeTime_ = 2.0f;
    float bulletSpawnHeightOffset_ = 0.2f;

    // 波状攻撃
    AttackParam waveParam_ = {8.0f, 3.0f, {1.2f, 0.6f, 1.6f}};
    float waveChargeTime_ = 0.6f;
    float waveRecoveryTime_ = 0.8f;
    float waveSpeed_ = 4.0f;
    float waveMaxDistance_ = 8.0f;
    float waveSpawnForwardOffset_ = 1.5f;
    float waveSpawnHeightOffset_ = 0.0f;

    // Rush
    AttackParam rushParam_ = {12.0f, 5.0f, {1.2f, 1.4f, 2.2f}};
    AttackTimingParam rushTiming_ = {
        0.75f, // totalTime
        0.20f, // trackingEndTime
        0.08f, // activeStartTime
        0.28f, // activeEndTime
        0.38f  // recoveryStartTime
    };
    float rushChargeTime_ = 0.28f;
    float rushSpeed_ = 8.5f;
    float rushMoveDuration_ = 0.26f;
    float rushAttackForwardOffset_ = 1.1f;
    float rushAttackHeightOffset_ = 0.8f;

    // 曲線Rush用
    float rushTurnSpeed_ = 4.5f;
    float rushStartCurveAngleDeg_ = 32.0f;
    float rushCurrentYaw_ = 0.0f;
    float rushCurveDir_ = 1.0f;

    // ワープ
    float warpStartTime_ = 0.2f;
    float warpEndTime_ = 0.2f;

    // 接近ワープ
    float warpNearRadiusMin_ = 1.8f;
    float warpNearRadiusMax_ = 3.0f;
    int warpApproachWeight_ = 8;
    float farDistanceTimer_ = 0.0f;
    float farDistanceWarpTimeThreshold_ = 2.0f;
    int farDistanceWarpBonus_ = 50;

    // 離脱ワープ
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

    // Warp起点連携
    int warpApproachChainMaxSteps_ = 2;
    int warpEscapeChainMaxSteps_ = 2;
    float approachChainContinueDistance_ = 5.0f;
    float escapeChainContinueDistance_ = 4.5f;

    // Sweep -> Warp -> Smash
    float sweepWarpSmashMaxDistance_ = 5.0f;
    float sweepWarpSmashChance_ = 0.45f;

    // Wave -> 接近Warp -> Smash
    float waveWarpSmashMinDistance_ = 4.5f;
    float waveWarpSmashChance_ = 0.50f;

    // 近距離圧が続いたら離脱ワープを解禁
    float closePressureDistance_ = 3.0f;
    float closePressureTimeThreshold_ = 1.0f;
    float closePressureTimer_ = 0.0f;

    // 左手ガード
    float guardMoveTime_ = 0.25f;
    float guardHoldTime_ = 0.7f;
    float guardRecoveryTime_ = 0.35f;

    // 行動の重み
    int nearSmashWeight_ = 30;
    int nearSweepWeight_ = 25;
    int nearGuardWeight_ = 5;
    int nearRushWeight_ = 40;

    int midRushWeight_ = 45;
    int midShotWeight_ = 25;
    int midWaveWeight_ = 30;

    int farShotWeight_ = 35;
    int farWarpWeight_ = 25;
    int farWaveWeight_ = 40;

    // 向き速度
    float chargeTurnSpeed_ = 6.0f;
    float recoveryTurnSpeed_ = 2.0f;
    float idleTurnSpeed_ = 8.0f;
    bool hasTrackingLocked_ = false;

    float stagnantDistanceThreshold_ = 0.15f;
    float stagnantTimeThreshold_ = 1.2f;
    int stagnantWarpBonus_ = 5;

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

    // Smash
    void UpdateSmashCharge(float deltaTime);
    void UpdateSmashAttack(float deltaTime);
    void UpdateSmashRecovery(float deltaTime);

    // Sweep
    void UpdateSweepCharge(float deltaTime);
    void UpdateSweepAttack(float deltaTime);
    void UpdateSweepRecovery(float deltaTime);

    // Rush
    void UpdateRushCharge(float deltaTime);
    void UpdateRushAttack(float deltaTime);
    void UpdateRushRecovery(float deltaTime);

    // 向き更新とロック
    void UpdateFacingToPlayer();
    void LockCurrentFacing();
    void UpdateFacingToPlayerWithSpeed(float deltaTime, float turnSpeed);
    float NormalizeAngle(float angle) const;

    // Shot
    void UpdateShotCharge(float deltaTime);
    void UpdateShotFire(float deltaTime);
    void UpdateShotRecovery(float deltaTime);

    void SpawnBullet();
    void UpdateBullets(float deltaTime);

    // Warp
    void UpdateWarpStart(float deltaTime);
    void UpdateWarpMove(float deltaTime);
    void UpdateWarpEnd(float deltaTime);

    bool PrepareWarpContext();
    bool DecideWarpTargetNearPlayer(DirectX::XMFLOAT3 &outTarget) const;
    bool DecideWarpTargetFarFromPlayer(DirectX::XMFLOAT3 &outTarget) const;
    void DecideWarpFollowupFromContext();
    void SetupChainFromWarpContext();
    void BeginWarpFollowup();
    void BeginBackWarpPostAction();
    void ResetWarpContext();

    // Chain
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

    // Wave
    void UpdateWaveCharge(float deltaTime);
    void UpdateWaveFire(float deltaTime);
    void UpdateWaveRecovery(float deltaTime);

    void SpawnWave();
    void UpdateWaves(float deltaTime);

    // Guard
    void UpdateGuardMove(float deltaTime);
    void UpdateGuardHold(float deltaTime);
    void UpdateGuardRecovery(float deltaTime);

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
};