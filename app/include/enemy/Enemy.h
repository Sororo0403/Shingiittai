#pragma once
#include "Camera.h"
#include "EnemyActionData.h"
#include "OBB.h"
#include "Player.h"
#include "Transform.h"
#include <cstddef>
#include <cstdint>

class ModelManager;
struct ModelDrawEffect;

/// <summary>
/// 敵行動内の局所的な進行段階
/// </summary>
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

/// <summary>
/// ワープ後にプレイヤーへ接近する方向
/// </summary>
enum class WarpApproachSlot { None, Front, Back };

// Enemy combat is driven by a two-part FSM:
// - ActionKind decides which behavior is currently running.
// - ActionStep tracks the local phase within that behavior.
/// <summary>
/// 現在の敵行動と、その行動内の段階を保持する
/// </summary>
struct ActionState {
    ActionKind kind = ActionKind::None;
    ActionStep step = ActionStep::None;
};

/// <summary>
/// プレイヤーへ提示する敵攻撃キューの種類
/// </summary>
enum class EnemyAttackCueType {
    None,
    Cancel,
    Telegraph,
    Feint,
    Release,
};

/// <summary>
/// 攻撃予兆の方向、表示時間、発行順を通知するイベント
/// </summary>
struct EnemyAttackCueEvent {
    EnemyAttackCueType type = EnemyAttackCueType::None;
    ActionKind kind = ActionKind::None;
    float yaw = 0.0f;
    float duration = 0.0f;
    uint32_t sequence = 0;
};

/// <summary>
/// ワープ開始から追撃へ引き継ぐ一時状態を保持する
/// </summary>
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

/// <summary>
/// ワープ残像一体分の姿勢と寿命を保持する
/// </summary>
struct EnemyAfterimageGhost {
    Transform visual{};
    float life = 0.0f;
    float maxLife = 0.0f;
    bool isActive = false;
};

/// <summary>
/// 三連居合演出に使用する分身一体分の状態を保持する
/// </summary>
struct EnemyTripleIaiClone {
    Transform visual{};
    DirectX::XMFLOAT3 targetPosition = {0.0f, 0.0f, 0.0f};
    ActionKind slashKind = ActionKind::Smash;
    bool isActive = false;
};

// 仕様書に合わせて BodyCenter -> BodyRight に整理

// 上位戦術
/// <summary>
/// 敵AIが選択する上位戦術
/// </summary>
enum class TacticState { Melee, Chase };

/// <summary>
/// 体力と演出に応じて切り替わるボスフェーズ
/// </summary>
enum class BossPhase { Phase1, Phase2, Phase3 };

/// <summary>
/// 敵AIが意思決定に利用するプレイヤー観測値
/// </summary>
struct PlayerCombatObservation {
    DirectX::XMFLOAT3 position = {0.0f, 0.0f, 0.0f};
    bool isAttacking = false;
};

/// <summary>
/// 敵攻撃の予備動作、攻撃判定、硬直時間を保持する
/// </summary>
struct AttackTimingParam {
    float totalTime = 1.0f;
    float trackingEndTime = 0.0f;
    float activeStartTime = 0.0f;
    float activeEndTime = 0.0f;
    float recoveryStartTime = 0.0f;
};

/// <summary>
/// 乱数抽選に使用する浮動小数点の最小値と最大値
/// </summary>
struct RangeF {
    float min = 0.0f;
    float max = 0.0f;
};

/// <summary>
/// 敵の体力、移動速度、基本間合いを保持する
/// </summary>
struct EnemyCoreConfig {
    float maxHp = 1080.0f;
    float phase2HealthRatioThreshold = 0.74f;
    float phase3HealthRatioThreshold = 0.37f;
    float nearAttackDistance = 4.0f;
};

/// <summary>
/// 単一攻撃の時間、威力、ノックバックを保持する
/// </summary>
struct EnemyAttackProfile {
    AttackParam attack{};
    AttackTimingParam timing{};
    float chargeTime = 0.0f;
};

/// <summary>
/// 近接攻撃の基本値とリーチを保持する
/// </summary>
struct EnemyMeleeAttackProfile {
    EnemyAttackProfile base{};
    RangeF holdTime{};
    float feintChance = 0.0f;
};

/// <summary>
/// 振り下ろし攻撃固有の調整値を保持する
/// </summary>
struct EnemySmashConfig {
    EnemyMeleeAttackProfile melee{};
    float attackForwardOffset = 0.0f;
    float attackHeightOffset = 0.0f;
};

/// <summary>
/// 横薙ぎ攻撃固有の調整値を保持する
/// </summary>
struct EnemySweepConfig {
    EnemyMeleeAttackProfile melee{};
    float attackSideOffset = 0.0f;
    float attackHeightOffset = 0.0f;
};

/// <summary>
/// 鍔迫り合いの開始条件と進行時間を保持する
/// </summary>
struct EnemyBladeClashConfig {
    EnemyAttackProfile profile = {{12.0f, 4.0f, {1.75f, 1.60f, 1.95f}},
                                  {1.46f, 0.72f, 0.0f, 0.88f, 0.88f},
                                  0.72f};
    float advanceSpeed = 3.8f;
};

/// <summary>
/// 通常レーザー攻撃の射程、判定、演出時間を保持する
/// </summary>
struct EnemyArcaneLaserConfig {
    EnemyAttackProfile profile = {{18.0f, 5.2f, {2.35f, 2.25f, 15.5f}},
                                  {2.70f, 1.28f, 0.0f, 0.72f, 0.72f},
                                  1.28f};
    float range = 15.5f;
    float radius = 1.72f;
    float muzzleForwardOffset = 1.55f;
    float muzzleHeightOffset = 1.42f;
    float recoveryDuration = 0.64f;
};

/// <summary>
/// 最終フェーズ用レーザー攻撃の調整値を保持する
/// </summary>
struct EnemyCataclysmLaserConfig {
    EnemyAttackProfile profile = {{26.0f, 7.2f, {5.6f, 4.2f, 25.0f}},
                                  {6.85f, 1.72f, 0.0f, 3.95f, 3.95f},
                                  1.72f};
    float range = 25.0f;
    float radius = 2.45f;
    float muzzleForwardOffset = 1.85f;
    float muzzleHeightOffset = 1.58f;
    float recoveryDuration = 1.08f;
};

/// <summary>
/// 敵が利用する全攻撃パラメーターをまとめて保持する
/// </summary>
struct EnemyAttackSet {
    EnemySmashConfig smash = {{{{15.0f, 4.0f, {2.8f, 2.1f, 3.2f}},
                                {1.20f, 0.86f, 0.05f, 0.22f, 0.38f},
                                1.64f},
                               {0.42f, 0.86f},
                               0.62f},
                              1.4f,
                              0.8f};
    EnemySweepConfig sweep = {{{{15.0f, 4.0f, {5.0f, 1.65f, 2.6f}},
                                {1.12f, 0.78f, 0.05f, 0.20f, 0.36f},
                                1.52f},
                               {0.38f, 0.78f},
                               0.48f},
                              0.2f,
                              0.8f};
    EnemyBladeClashConfig bladeClash{};
    EnemyArcaneLaserConfig arcaneLaser{};
    EnemyCataclysmLaserConfig cataclysmLaser{};
};

/// <summary>
/// ワープ距離、時間、再使用間隔を保持する
/// </summary>
struct EnemyWarpConfig {
    float startTime = 0.55f;
    float moveTime = 0.24f;
    float endTime = 0.48f;
};

/// <summary>
/// 敵の基本能力、攻撃、ワープ設定を集約する
/// </summary>
struct EnemyConfig {
    EnemyCoreConfig core{};
    EnemyAttackSet attacks{};
    EnemyWarpConfig warp{};
};

/// <summary>
/// 敵AIと戦闘演出が共有する実行時状態を保持する
/// </summary>
struct EnemyRuntimeState {
    float hp = 1000.0f;
    bool isDying = false;
    bool deathFinished = false;
    float hitReactionTimer = 0.0f;
    float damageFlashTimer = 0.0f;
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
    static constexpr int kAfterimageGhostCount = 8;
    EnemyAfterimageGhost afterimageGhosts[kAfterimageGhostCount]{};
    static constexpr int kTripleIaiCloneCount = 4;
    EnemyTripleIaiClone tripleIaiClones[kTripleIaiCloneCount]{};

    TacticState tactic = TacticState::Chase;
    BossPhase phase = BossPhase::Phase1;
    bool phaseTransitionActive = false;
    float phaseTransitionTimer = 0.0f;
    bool phase2BladeClashPending = false;
    bool holdConfigured = false;
    float currentHoldDuration = 0.0f;
    bool quickSlashActive = false;
    bool farSlashActive = false;
    bool warpFeintFollowupLocked = false;
    bool warpFeintImmediate = false;
    bool warpFeintDecisionMade = false;
    bool directionFeintDecisionMade = false;
    bool attackReleaseCueIssued = false;
    float phantomWarpCooldown = 0.0f;
    float tripleIaiSlashCooldown = 0.0f;
    bool tripleIaiSlashActive = false;
    int tripleIaiSlashesRemaining = 0;
    int tripleIaiSlashIndex = 0;
    int tripleIaiSlashOrder[kTripleIaiCloneCount] = {0, 1, 2, 3};
    DirectX::XMFLOAT3 tripleIaiCenterFocusPosition = {0.0f, 0.0f, 0.0f};
    bool tripleIaiReturnCameraToCenter = false;
    bool tripleIaiIntroActive = false;
    float tripleIaiIntroTimer = 0.0f;
    DirectX::XMFLOAT3 tripleIaiIntroStartPosition = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 tripleIaiIntroLiftPosition = {0.0f, 0.0f, 0.0f};
    float phantomFinalLockTimer = 0.0f;
    float arcaneLaserCooldown = 0.0f;
    DirectX::XMFLOAT3 arcaneLaserDirection = {0.0f, 0.0f, 1.0f};
    float cataclysmLaserCooldown = 0.0f;
    DirectX::XMFLOAT3 cataclysmLaserDirection = {0.0f, 0.0f, 1.0f};
    bool rangedReengagePending = false;
    float sharedRangedAttackCooldown = 0.0f;
    float rangedAttackLockoutTimer = 0.0f;
    int consecutiveRangedAttackCount = 0;

    bool tellActive = false;
    float tellDuration = 0.0f;
    EnemyAttackCueEvent pendingAttackCue{};
    uint32_t attackCueSequence = 0;

    bool hasTrackingLocked = false;
    DirectX::XMFLOAT3 farSlashLungeStartPos = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 farSlashLungeTargetPos = {0.0f, 0.0f, 0.0f};
    float farSlashLungeDuration = 0.0f;
    bool hasFarSlashLungeTarget = false;
};

/// <summary>
/// ボス敵の意思決定、攻撃進行、ダメージ、描画状態を管理する
/// </summary>
class Enemy {
  public:
    /// <summary>
    /// 使用するリソースと初期状態を準備する
    /// </summary>
    void Initialize(uint32_t modelId);
    /// <summary>
    /// SetDifficultyに対応する状態を設定する
    /// </summary>
    void SetDifficulty(float difficulty);
    /// <summary>
    /// GetDifficultyに対応する現在値を取得する
    /// </summary>
    float GetDifficulty() const { return difficulty_; }

    /// <summary>
    /// 入力と状態を1フレーム進める
    /// </summary>
    void Update(const PlayerCombatObservation &playerObs, float deltaTime);
    /// <summary>
    /// UpdateTutorialに対応する公開処理を実行する
    /// </summary>
    void UpdateTutorial(const PlayerCombatObservation &playerObs,
                        float deltaTime);
    /// <summary>
    /// BeginTutorialAttackに対応する処理を開始する
    /// </summary>
    void BeginTutorialAttack(ActionKind kind);
    /// <summary>
    /// BeginDifficultyNineOpeningCutInに対応する処理を開始する
    /// </summary>
    void
    BeginDifficultyNineOpeningCutIn(const DirectX::XMFLOAT3 &targetPosition);
    /// <summary>
    /// ResetTutorialStateが管理する状態を初期値へ戻す
    /// </summary>
    void ResetTutorialState();
    /// <summary>
    /// SetTutorialPositionに対応する状態を設定する
    /// </summary>
    void SetTutorialPosition(const DirectX::XMFLOAT3 &position);

    /// <summary>
    /// 現在の状態を描画する
    /// </summary>
    void Draw(ModelManager *modelManager, const Camera &camera,
              float visualScale = 1.0f);
    /// <summary>
    /// 指定したダメージを適用し、実際の減少量を返す
    /// </summary>
    float TakeDamage(float damage);
    /// <summary>
    /// 指定したダメージを適用し、実際の減少量を返す
    /// </summary>
    float TakeDamageNoReaction(float damage);
    /// <summary>
    /// 指定したダメージを適用し、実際の減少量を返す
    /// </summary>
    float TakeDamageDeferTransitions(float damage);
    /// <summary>
    /// 指定したダメージを適用し、実際の減少量を返す
    /// </summary>
    float TakeDamageDeferTransitionsNoReaction(float damage);
    /// <summary>
    /// ResolveDeferredDamageTransitionsに対応する保留状態を確定する
    /// </summary>
    void ResolveDeferredDamageTransitions();
    /// <summary>
    /// ForcePunishReleaseに対応する状態へ強制的に遷移する
    /// </summary>
    void ForcePunishRelease();
    /// <summary>
    /// NotifyCounteredに対応するイベントを通知する
    /// </summary>
    bool NotifyCountered(float vulnerabilityDuration);
    /// <summary>
    /// NotifyTripleIaiAttackResolvedForCameraに対応するイベントを通知する
    /// </summary>
    void NotifyTripleIaiAttackResolvedForCamera() {
        if (runtime_.tripleIaiSlashActive &&
            runtime_.tripleIaiSlashesRemaining > 0 && runtime_.farSlashActive &&
            (runtime_.action.kind == ActionKind::Smash ||
             runtime_.action.kind == ActionKind::Sweep)) {
            runtime_.tripleIaiReturnCameraToCenter = true;
        }
    }
    /// <summary>
    /// IsBladeClashActionの条件を満たすか判定する
    /// </summary>
    bool IsBladeClashAction() const;
    /// <summary>
    /// IsBladeClashWindowの条件を満たすか判定する
    /// </summary>
    bool IsBladeClashWindow() const;
    /// <summary>
    /// ResolveBladeClashに対応する保留状態を確定する
    /// </summary>
    void ResolveBladeClash(bool playerWon);
    /// <summary>
    /// NotifyBladeClashLandedに対応するイベントを通知する
    /// </summary>
    void NotifyBladeClashLanded();
    /// <summary>
    /// FinishCounterRecoilに対応する進行状態を完了させる
    /// </summary>
    void FinishCounterRecoil();
    /// <summary>
    /// ApplyVictoryDefeatPoseに対応する結果を適用する
    /// </summary>
    void ApplyVictoryDefeatPose(float ratio,
                                const DirectX::XMFLOAT3 &startPosition,
                                const DirectX::XMFLOAT3 &playerPosition);
    /// <summary>
    /// SetBossPhaseForPresentationに対応する状態を設定する
    /// </summary>
    void SetBossPhaseForPresentation(BossPhase phase) {
        runtime_.phase = phase;
    }
    /// <summary>
    /// DebugForceBossPhaseに対応する公開処理を実行する
    /// </summary>
    void DebugForceBossPhase(BossPhase phase, bool playTransition);
    /// <summary>
    /// SetCinematicTransformに対応する状態を設定する
    /// </summary>
    void SetCinematicTransform(const DirectX::XMFLOAT3 &position, float yaw);
    /// <summary>
    /// SetCinematicTransformに対応する状態を設定する
    /// </summary>
    void SetCinematicTransform(const DirectX::XMFLOAT3 &position, float yaw,
                               float pitch, float roll);
    /// <summary>
    /// FaceTargetImmediatelyに対応する公開処理を実行する
    /// </summary>
    void FaceTargetImmediately(const DirectX::XMFLOAT3 &targetPosition);
    /// <summary>
    /// GetTransformに対応する現在値を取得する
    /// </summary>
    const Transform &GetTransform() const { return tf_; }
    /// <summary>
    /// GetHPに対応する現在値を取得する
    /// </summary>
    float GetHP() const { return runtime_.hp; }
    /// <summary>
    /// GetMaxHPに対応する現在値を取得する
    /// </summary>
    float GetMaxHP() const { return config_.core.maxHp; }

    /// <summary>
    /// GetBodyOBBに対応する現在値を取得する
    /// </summary>
    OBB GetBodyOBB() const;
    /// <summary>
    /// GetLeftHandOBBに対応する現在値を取得する
    /// </summary>
    OBB GetLeftHandOBB() const;
    /// <summary>
    /// GetRightHandOBBに対応する現在値を取得する
    /// </summary>
    OBB GetRightHandOBB() const;

    /// <summary>
    /// GetActionKindに対応する現在値を取得する
    /// </summary>
    ActionKind GetActionKind() const { return runtime_.action.kind; }
    /// <summary>
    /// GetActionStepに対応する現在値を取得する
    /// </summary>
    ActionStep GetActionStep() const { return runtime_.action.step; }
    /// <summary>
    /// ConsumeAttackCueEventに対応する保留イベントを取得して消費する
    /// </summary>
    bool ConsumeAttackCueEvent(EnemyAttackCueEvent &event);
    /// <summary>
    /// GetActionTimerForPresentationに対応する現在値を取得する
    /// </summary>
    float GetActionTimerForPresentation() const { return runtime_.stateTimer; }
    /// <summary>
    /// GetReleaseAnticipationRatioに対応する現在値を取得する
    /// </summary>
    float GetReleaseAnticipationRatio() const;
    /// <summary>
    /// GetTelegraphYawに対応する現在値を取得する
    /// </summary>
    float GetTelegraphYaw() const;
    /// <summary>
    /// GetBossPhaseに対応する現在値を取得する
    /// </summary>
    BossPhase GetBossPhase() const { return runtime_.phase; }
    /// <summary>
    /// IsPhaseTransitionActiveの条件を満たすか判定する
    /// </summary>
    bool IsPhaseTransitionActive() const {
        return runtime_.phaseTransitionActive;
    }
    /// <summary>
    /// GetIsPhaseChangingに対応する現在値を取得する
    /// </summary>
    bool GetIsPhaseChanging() const { return isPhaseChanging_; }
    /// <summary>
    /// SetIsPhaseChangingに対応する状態を設定する
    /// </summary>
    void SetIsPhaseChanging(bool changing) { isPhaseChanging_ = changing; }
    /// <summary>
    /// GetPhaseTransitionRatioに対応する現在値を取得する
    /// </summary>
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

    /// <summary>
    /// IsAttackActiveの条件を満たすか判定する
    /// </summary>
    bool IsAttackActive() const { return runtime_.isAttackActive; }
    /// <summary>
    /// GetAttackOBBに対応する現在値を取得する
    /// </summary>
    OBB GetAttackOBB() const;
    /// <summary>
    /// IsFarWarpSlashActiveの条件を満たすか判定する
    /// </summary>
    bool IsFarWarpSlashActive() const { return runtime_.farSlashActive; }
    /// <summary>
    /// IsTripleIaiSlashActiveの条件を満たすか判定する
    /// </summary>
    bool IsTripleIaiSlashActive() const {
        return runtime_.tripleIaiSlashActive;
    }
    /// <summary>
    /// IsTripleIaiCenterCameraHoldの条件を満たすか判定する
    /// </summary>
    bool IsTripleIaiCenterCameraHold() const {
        if (!runtime_.tripleIaiSlashActive) {
            return false;
        }
        if (runtime_.tripleIaiIntroActive) {
            return true;
        }
        if (runtime_.action.kind == ActionKind::Warp &&
            runtime_.warp.farSlashFollowup) {
            return true;
        }
        const bool nonFinalSlash = runtime_.tripleIaiSlashesRemaining > 0 &&
                                   runtime_.farSlashActive &&
                                   (runtime_.action.kind == ActionKind::Smash ||
                                    runtime_.action.kind == ActionKind::Sweep);
        return nonFinalSlash && runtime_.tripleIaiReturnCameraToCenter;
    }
    /// <summary>
    /// GetTripleIaiCueSlotに対応する現在値を取得する
    /// </summary>
    bool GetTripleIaiCueSlot(int index, DirectX::XMFLOAT3 &outPosition,
                             ActionKind &outKind) const {
        if (index < 0 || index >= EnemyRuntimeState::kTripleIaiCloneCount) {
            return false;
        }
        const bool currentAttacker =
            runtime_.tripleIaiSlashActive && runtime_.tripleIaiSlashIndex > 0 &&
            runtime_.tripleIaiSlashIndex <=
                EnemyRuntimeState::kTripleIaiCloneCount &&
            runtime_.tripleIaiSlashOrder[runtime_.tripleIaiSlashIndex - 1] ==
                index;
        if (runtime_.tripleIaiClones[index].isActive) {
            outPosition = runtime_.tripleIaiClones[index].visual.position;
            outKind = runtime_.tripleIaiClones[index].slashKind;
            return true;
        }
        if (currentAttacker) {
            outPosition = tf_.position;
            outKind =
                runtime_.action.kind == ActionKind::Sweep ? ActionKind::Sweep
                : runtime_.action.kind == ActionKind::Smash
                    ? ActionKind::Smash
                    : (index % 2 == 0 ? ActionKind::Smash : ActionKind::Sweep);
            return true;
        }
        return false;
    }
    /// <summary>
    /// GetTripleIaiCenterFocusPositionに対応する現在値を取得する
    /// </summary>
    DirectX::XMFLOAT3 GetTripleIaiCenterFocusPosition() const {
        return runtime_.tripleIaiCenterFocusPosition;
    }
    /// <summary>
    /// ShouldSuppressCounterStaggerの条件を満たすか判定する
    /// </summary>
    bool ShouldSuppressCounterStagger() const {
        return runtime_.tripleIaiSlashActive &&
               runtime_.tripleIaiSlashesRemaining > 0 &&
               runtime_.farSlashActive &&
               (runtime_.action.kind == ActionKind::Smash ||
                runtime_.action.kind == ActionKind::Sweep);
    }
    /// <summary>
    /// GetFarWarpSlashStanceHoldDurationに対応する現在値を取得する
    /// </summary>
    float GetFarWarpSlashStanceHoldDuration() const {
        return runtime_.farSlashActive ? 0.50f + runtime_.farSlashLungeDuration
                                       : 0.0f;
    }
    /// <summary>
    /// ShouldSuppressRedAttackCueの条件を満たすか判定する
    /// </summary>
    bool ShouldSuppressRedAttackCue() const {
        return runtime_.quickSlashActive || runtime_.farSlashActive ||
               runtime_.warpFeintImmediate;
    }
    /// <summary>
    /// IsWarpCollisionDisabledの条件を満たすか判定する
    /// </summary>
    bool IsWarpCollisionDisabled() const {
        return runtime_.warp.collisionDisabled;
    }
    /// <summary>
    /// ShouldLockPlayerForArcaneLaserの条件を満たすか判定する
    /// </summary>
    bool ShouldLockPlayerForArcaneLaser() const {
        return runtime_.action.kind == ActionKind::ArcaneLaser ||
               runtime_.action.kind == ActionKind::CataclysmLaser ||
               (runtime_.action.kind == ActionKind::Warp &&
                (runtime_.warp.followupKind == ActionKind::ArcaneLaser ||
                 runtime_.warp.followupKind == ActionKind::CataclysmLaser));
    }
    /// <summary>
    /// ShouldLockPlayerForFarWarpSlashの条件を満たすか判定する
    /// </summary>
    bool ShouldLockPlayerForFarWarpSlash() const {
        const bool isFarWarpStartup =
            runtime_.action.kind == ActionKind::Warp &&
            runtime_.warp.farSlashFollowup;
        const bool isFarSlashCommit =
            runtime_.farSlashActive &&
            (runtime_.action.kind == ActionKind::Smash ||
             runtime_.action.kind == ActionKind::Sweep) &&
            (runtime_.action.step == ActionStep::Charge ||
             runtime_.action.step == ActionStep::Hold ||
             runtime_.action.step == ActionStep::Active);
        return isFarWarpStartup || isFarSlashCommit;
    }

    /// <summary>
    /// GetCurrentAttackDamageに対応する現在値を取得する
    /// </summary>
    float GetCurrentAttackDamage() const;
    /// <summary>
    /// GetCurrentAttackKnockbackに対応する現在値を取得する
    /// </summary>
    float GetCurrentAttackKnockback() const;
    /// <summary>
    /// GetArcaneLaserMuzzlePositionに対応する現在値を取得する
    /// </summary>
    DirectX::XMFLOAT3 GetArcaneLaserMuzzlePosition() const;
    /// <summary>
    /// GetArcaneLaserDirectionに対応する現在値を取得する
    /// </summary>
    DirectX::XMFLOAT3 GetArcaneLaserDirection() const {
        return runtime_.arcaneLaserDirection;
    }
    /// <summary>
    /// GetArcaneLaserRangeに対応する現在値を取得する
    /// </summary>
    float GetArcaneLaserRange() const {
        return config_.attacks.arcaneLaser.range;
    }
    /// <summary>
    /// GetArcaneLaserRadiusに対応する現在値を取得する
    /// </summary>
    float GetArcaneLaserRadius() const {
        return config_.attacks.arcaneLaser.radius;
    }
    /// <summary>
    /// GetArcaneLaserChargeRatioに対応する現在値を取得する
    /// </summary>
    float GetArcaneLaserChargeRatio() const;
    /// <summary>
    /// GetCataclysmLaserMuzzlePositionに対応する現在値を取得する
    /// </summary>
    DirectX::XMFLOAT3 GetCataclysmLaserMuzzlePosition() const;
    /// <summary>
    /// GetCataclysmLaserDirectionに対応する現在値を取得する
    /// </summary>
    DirectX::XMFLOAT3 GetCataclysmLaserDirection() const {
        return runtime_.cataclysmLaserDirection;
    }
    /// <summary>
    /// GetCataclysmLaserRangeに対応する現在値を取得する
    /// </summary>
    float GetCataclysmLaserRange() const {
        return config_.attacks.cataclysmLaser.range;
    }
    /// <summary>
    /// GetCataclysmLaserRadiusに対応する現在値を取得する
    /// </summary>
    float GetCataclysmLaserRadius() const {
        return config_.attacks.cataclysmLaser.radius;
    }
    /// <summary>
    /// GetCataclysmLaserChargeRatioに対応する現在値を取得する
    /// </summary>
    float GetCataclysmLaserChargeRatio() const;

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
    float &damageFlashTimer_ = runtime_.damageFlashTimer;
    float damageFlashDuration_ = 0.11f;
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
    bool &attackReleaseCueIssued_ = runtime_.attackReleaseCueIssued;
    DirectX::XMFLOAT3 &farSlashLungeStartPos_ = runtime_.farSlashLungeStartPos;
    DirectX::XMFLOAT3 &farSlashLungeTargetPos_ =
        runtime_.farSlashLungeTargetPos;
    float &farSlashLungeDuration_ = runtime_.farSlashLungeDuration;
    bool &hasFarSlashLungeTarget_ = runtime_.hasFarSlashLungeTarget;
    float &phantomWarpCooldown_ = runtime_.phantomWarpCooldown;
    float &tripleIaiSlashCooldown_ = runtime_.tripleIaiSlashCooldown;
    bool &tripleIaiSlashActive_ = runtime_.tripleIaiSlashActive;
    int &tripleIaiSlashesRemaining_ = runtime_.tripleIaiSlashesRemaining;
    int &tripleIaiSlashIndex_ = runtime_.tripleIaiSlashIndex;
    static constexpr int kTripleIaiCloneCount_ =
        EnemyRuntimeState::kTripleIaiCloneCount;
    EnemyTripleIaiClone (&tripleIaiClones_)[kTripleIaiCloneCount_] =
        runtime_.tripleIaiClones;
    int (&tripleIaiSlashOrder_)[kTripleIaiCloneCount_] =
        runtime_.tripleIaiSlashOrder;
    bool &tripleIaiIntroActive_ = runtime_.tripleIaiIntroActive;
    float &tripleIaiIntroTimer_ = runtime_.tripleIaiIntroTimer;
    DirectX::XMFLOAT3 &tripleIaiIntroStartPosition_ =
        runtime_.tripleIaiIntroStartPosition;
    DirectX::XMFLOAT3 &tripleIaiIntroLiftPosition_ =
        runtime_.tripleIaiIntroLiftPosition;
    float &phantomFinalLockTimer_ = runtime_.phantomFinalLockTimer;
    float &arcaneLaserCooldown_ = runtime_.arcaneLaserCooldown;
    DirectX::XMFLOAT3 &arcaneLaserDirection_ = runtime_.arcaneLaserDirection;
    float &cataclysmLaserCooldown_ = runtime_.cataclysmLaserCooldown;
    DirectX::XMFLOAT3 &cataclysmLaserDirection_ =
        runtime_.cataclysmLaserDirection;
    bool &rangedReengagePending_ = runtime_.rangedReengagePending;
    float &sharedRangedAttackCooldown_ = runtime_.sharedRangedAttackCooldown;
    float &rangedAttackLockoutTimer_ = runtime_.rangedAttackLockoutTimer;
    int &consecutiveRangedAttackCount_ = runtime_.consecutiveRangedAttackCount;

    bool isPhaseChanging_ = false;

    // ============================================================
    // Step2: Tell
    // ============================================================
    bool &tellActive_ = runtime_.tellActive;

    float &tellDuration_ = runtime_.tellDuration;
    EnemyAttackCueEvent &pendingAttackCue_ = runtime_.pendingAttackCue;
    uint32_t &attackCueSequence_ = runtime_.attackCueSequence;

    float smashTellTime_ = 0.24f;
    float sweepTellTime_ = 0.22f;

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
    float quickSmashChargeTime_ = 0.92f;
    float quickSweepChargeTime_ = 0.86f;
    float directionFeintChance_ = 0.32f;
    float chargeWarpFeintChance_ = 0.34f;
    float farWarpSlashChance_ = 0.74f;
    float farWarpSlashDistance_ = 10.8f;
    float farSlashLungeSpeed_ = 44.0f;
    float farSlashSmashChargeTime_ = 0.36f;
    float farSlashSweepChargeTime_ = 0.32f;
    float phantomWarpChance_ = 0.24f;
    float phantomWarpCooldownDuration_ = 5.8f;
    float tripleIaiSlashChance_ = 0.30f;
    float tripleIaiSlashCooldownDuration_ = 8.8f;
    float tripleIaiSlashSpeedScale_ = 1.0f;
    float tripleIaiCloneLife_ = 1.35f;
    float tripleIaiIntroDuration_ = 1.24f;
    float tripleIaiIntroLiftHeight_ = 8.4f;
    float phantomFinalLockDuration_ = 0.26f;
    float bladeClashChance_ = 0.26f;
    float arcaneLaserChance_ = 0.32f;
    float arcaneLaserSlashFollowupChance_ = 0.42f;
    float arcaneLaserCooldownDuration_ = 7.4f;
    float arcaneLaserMinDistance_ = 4.4f;
    float arcaneLaserWarpDistance_ = 18.5f;
    float arcaneLaserSlashMinDistance_ = 7.5f;
    float cataclysmLaserChance_ = 0.28f;
    float cataclysmLaserCooldownDuration_ = 13.5f;
    float cataclysmLaserMinDistance_ = 8.8f;
    float cataclysmLaserWarpDistance_ = 30.0f;
    float sharedRangedAttackCooldownDuration_ = 4.8f;
    float rangedAttackChainLockoutDuration_ = 9.0f;
    int rangedAttackChainLockoutThreshold_ = 5;

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
    float warpTrailLife_ = 0.28f;
    float warpTrailScaleMax_ = 0.88f;
    float &warpTrailEmitTimer_ = runtime_.warpTrailEmitTimer;
    static constexpr int kAfterimageGhostCount_ =
        EnemyRuntimeState::kAfterimageGhostCount;
    EnemyAfterimageGhost (&afterimageGhosts_)[kAfterimageGhostCount_] =
        runtime_.afterimageGhosts;

  private:
    struct PartPresentationContext {
        float usedYaw = 0.0f;
        float visualYaw = 0.0f;
        float visualPitch = 0.0f;
        float visualRoll = 0.0f;
        float pulse = 0.0f;
        float forwardX = 0.0f;
        float forwardZ = 0.0f;
        float rightX = 0.0f;
        float rightZ = 0.0f;
        bool suppressAttackBodyMotion = false;
        bool suppressActionPresentation = false;
        bool isTelegraphCharge = false;
        bool isFarSlashFlashHold = false;
        bool isFarSlashPostPierceSlash = false;
    };
    void UpdateParts();
    void InitializePartTransforms(const PartPresentationContext &pose);
    void ApplyHitPartPresentation(PartPresentationContext &pose);
    void ApplyChargePartPresentation(const PartPresentationContext &pose);
    void ApplyPhasePartPresentation(PartPresentationContext &pose);
    void ApplyEnemyActionPartPresentation(PartPresentationContext &pose);
    void ApplySmashPartPresentation(PartPresentationContext &pose);
    void ApplySweepPartPresentation(PartPresentationContext &pose);
    void ApplyBladeClashPartPresentation(PartPresentationContext &pose);
    void ApplyArcaneLaserPartPresentation(PartPresentationContext &pose);
    void ApplyCataclysmLaserPartPresentation(PartPresentationContext &pose);
    void ApplyStalkPartPresentation(PartPresentationContext &pose);
    void ApplyWarpPartPresentation(PartPresentationContext &pose);
    void FinalizePartTransforms(PartPresentationContext &pose);
    void ResolveActionDrawStyle(float actionPulse,
                                DirectX::XMFLOAT4 &actionTint,
                                float &actionIntensity,
                                float &actionNoise) const;
    void ApplyActionDrawStyleModifiers(float actionPulse,
                                       DirectX::XMFLOAT4 &actionTint,
                                       float &actionIntensity,
                                       float &actionNoise) const;
    ModelDrawEffect BuildEnemyHitEffect(bool &isHitFlashing) const;
    ModelDrawEffect BuildEnemyBaseEffect(const ModelDrawEffect &hitEffect,
                                         bool isHitFlashing,
                                         const DirectX::XMFLOAT4 &actionTint,
                                         float actionIntensity,
                                         float actionNoise) const;
    void DrawEnemyVisual(ModelManager *modelManager, const Camera &camera,
                         const Transform &visual, float alpha,
                         float visualScale, float actionPulse,
                         bool isHitFlashing,
                         const ModelDrawEffect &baseEffect) const;
    void DrawEnemyAfterimages(ModelManager *modelManager, const Camera &camera,
                              float visualScale) const;
    void UpdateCooldowns(float deltaTime);
    bool UpdateDeathSequence(float deltaTime);
    bool UpdatePhaseTransition(float deltaTime);
    bool UpdateHitReaction(float deltaTime);
    OBB MakeOBB(const Transform &tf, const DirectX::XMFLOAT3 &size) const;

    void UpdateByAction(float deltaTime);

    void UpdateSmashByStep(float deltaTime);
    void UpdateSweepByStep(float deltaTime);
    void UpdateBladeClashByStep(float deltaTime);
    void UpdateWarpByStep(float deltaTime);
    void UpdateIdle(float deltaTime);
    void UpdateArcaneLaserByStep(float deltaTime);
    void UpdateCataclysmLaserByStep(float deltaTime);

    TacticState DecideTactic() const;
    void BeginActionFromTactic(TacticState tactic);
    ActionKind SelectNearPressureAction() const;
    bool TryBeginFarWarpSlash(float chance);
    bool TryBeginTripleIaiSlash(float chance);
    bool TryBeginArcaneLaser(float chance);
    bool TryBeginLaserReengageWarp(float chance);
    bool TryBeginCataclysmLaser(float chance);
    bool IsRangedAttackAvailable() const;
    void RegisterRangedAttackCommit();
    void RegisterNonRangedAttackCommit(ActionKind kind);
    void BeginPhantomWarpStep(int viewWarpsRemaining, bool finalBehind,
                              ActionKind followupKind);
    void PrepareTripleIaiSlashClones();
    void BeginTripleIaiSlashIntro();
    void UpdateTripleIaiSlashIntro(float deltaTime);
    void BeginTripleIaiSlashStep();
    bool TryContinueTripleIaiSlash();
    void ResetTripleIaiSlashClones();
    void BeginPressureAction();
    void BeginChaseAction();
    bool ExecutePressureBasicDecision(int decision);
    void ExecutePressureComplexDecision(int decision, bool phase3);
    void ExecuteChaseDecision(int decision);

    void UpdateSmashCharge(float deltaTime);
    bool UpdateChargeTell(ActionKind kind, float deltaTime,
                          float turnSpeedScale);
    bool TryChargeFeint(ActionKind kind);
    void UpdateChargeTracking(float deltaTime, float trackingEnd,
                              float stanceTime);
    void FinishSmashCharge(float currentChargeTime);
    void FinishSweepCharge(float currentChargeTime);
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
    void UpdateArcaneLaserCharge(float deltaTime);
    void UpdateArcaneLaserActive(float deltaTime);
    void UpdateArcaneLaserRecovery(float deltaTime);
    void UpdateCataclysmLaserCharge(float deltaTime);
    void UpdateCataclysmLaserActive(float deltaTime);
    void UpdateCataclysmLaserRecovery(float deltaTime);

    void UpdateFacingToPlayer();
    void LockCurrentFacing();
    void SyncBaseRotationToFacing();
    void UpdateFacingToPlayerWithSpeed(float deltaTime, float turnSpeed);
    float NormalizeAngle(float angle) const;

    void UpdateWarpStart(float deltaTime);
    void UpdateWarpMove(float deltaTime);
    void UpdateWarpEnd(float deltaTime);
    void UpdateWarpEndFacing(float deltaTime);
    float GetWarpEndDuration() const;
    bool ContinuePhantomWarp(int remaining);
    bool BeginWarpMeleeFollowup(ActionKind kind, ActionStep step,
                                bool immediate, bool farSlash,
                                bool feintFollowup, bool phantomChain,
                                bool phantomFinal);
    void ConfigureFarSlashLungeTarget();
    void UpdateFarSlashLunge(float deltaTime);
    void UpdateWarpTrails(float deltaTime);
    void EmitWarpTrailGhost(const DirectX::XMFLOAT3 &position, float scale,
                            float lifeOverride = -1.0f);
    void ResetWarpTrails();
    float GetCurrentWarpStartTime() const;
    float GetWarpVisualAlpha() const;
    bool PrepareWarpContext();
    bool DecideWarpTargetNearPlayer(DirectX::XMFLOAT3 &outTarget);
    bool DecideWarpTargetFarSlash(DirectX::XMFLOAT3 &outTarget);
    bool DecideWarpTargetArcaneLaser(DirectX::XMFLOAT3 &outTarget);
    bool DecideWarpTargetCataclysmLaser(DirectX::XMFLOAT3 &outTarget);
    bool DecideWarpTargetTripleIaiSlash(DirectX::XMFLOAT3 &outTarget,
                                        int slashIndex);
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
    void ConfigureAttackProfiles(float effectiveDifficulty,
                                 float difficultyRatio);
    void ConfigureMeleeTuning(float difficultyRatio);
    void ConfigureRangedTuning(float difficultyRatio);
    void ConfigureMovementTuning(float difficultyRatio);

    OBB GetSmashAttackOBB() const;
    OBB GetSweepAttackOBB() const;
    OBB GetArcaneLaserAttackOBB() const;
    OBB GetCataclysmLaserAttackOBB() const;
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
    void IssueBeginActionCue(ActionKind kind, ActionStep step);
    void ChangeActionStep(ActionStep step);
    void EndAttack();

    void ValidateTiming(AttackTimingParam &timing, float chargeTime);
    void ValidateAllTimings();
    void UpdateBossPhase();
    void IssueAttackCue(EnemyAttackCueType type, ActionKind kind,
                        float duration);
    void IssueReleaseCueIfReady();

    bool ShouldEnterSmashHold() const;
    bool ShouldEnterSweepHold() const;
    void EnterHold(float duration);
    bool CanBeginChargeWarpFeint(ActionKind kind) const;
    bool CanApplyDirectionFeint(ActionKind kind) const;
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
