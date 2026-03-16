#pragma once
#include "Camera.h"
#include "OBB.h"
#include "Transform.h"
#include <cstdint>
#include <vector>
#include "EnemyTuningPreset.h"
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

struct AttackParam {
    float damage = 0.0f;
    float knockback = 0.0f;
    DirectX::XMFLOAT3 hitBoxSize = {1.0f, 1.0f, 1.0f};
};

struct AttackTimingParam {
    float totalTime = 1.0f;
    float trackingEndTime = 0.0f;
    float activeStartTime = 0.0f;
    float activeEndTime = 0.0f;
    float recoveryStartTime = 0.0f;
};

enum class AttackType { None, Smash, Sweep, Shot, Wave };
enum class AttackPhase { None, Charge, Active, Recovery };

class Enemy {

  public:
    /// <summary>
    /// 初期化処理
    /// </summary>
    /// <param name="modelId">敵のモデルID</param>
    void Initialize(uint32_t modelId);

    /// <summary>
    /// 更新処理
    /// </summary>
    void Update(const DirectX::XMFLOAT3 &playerPos, float deltaTime);

    /// <summary>
    /// 描画処理
    /// </summary>
    /// <param name="modelManager">ModelManagerインスタンス</param>
    /// <param name="camera">描画に使用するカメラ</param>
    void Draw(ModelManager *modelManager, const Camera &camera);

    /// <summary>
    /// ダメージを与える
    /// </summary>
    /// <param name="damage">与えるダメージ量</param>
    void TakeDamage(float damage);

    //************************
    // Getter
    //************************
    const Transform &GetTransform() const { return tf_; }
    bool IsAlive() const { return hp_ > 0.0f; }

    const Transform &GetBodyTransform() const { return bodyTf_; }
    const Transform &GetLeftHandTransform() const { return leftHandTf_; }
    const Transform &GetRightHandTransform() const { return rightHandTf_; }

    OBB GetBodyOBB() const;
    OBB GetLeftHandOBB() const;
    OBB GetRightHandOBB() const;

    // 行動管理getter
    EnemyState GetState() const { return state_; }
    bool IsAttackActive() const { return isAttackActive_; }
    OBB GetAttackOBB() const;

    // プレイヤーとの距離getter
    float GetDistanceToPlayer() const;

    // 向きetter
    float GetFacingYaw() const { return facingYaw_; }
    float GetLockedAttackYaw() const { return lockedAttackYaw_; }

    // 弾getter
    const std::vector<EnemyBullet> &GetBullets() const { return bullets_; }

    // ワープgetter
    bool IsVisible() const { return isVisible_; }
    const DirectX::XMFLOAT3 &GetWarpTargetPos() const { return warpTargetPos_; }

    const std::vector<EnemyWave> &GetWaves() const { return waves_; }

    // ガードgetter
    bool IsGuardActive() const { return isGuardActive_; }
    GuardTarget GetGuardTarget() const { return guardTarget_; }

   float GetSmashDamage() const { return smashParam_.damage; }
    float GetSweepDamage() const { return sweepParam_.damage; }
    float GetBulletDamage() const { return bulletParam_.damage; }
    float GetWaveDamage() const { return waveParam_.damage; }

    float GetSmashKnockback() const { return smashParam_.knockback; }
    float GetSweepKnockback() const { return sweepParam_.knockback; }
    float GetBulletKnockback() const { return bulletParam_.knockback; }
    float GetWaveKnockback() const { return waveParam_.knockback; }

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

    const AttackParam &GetSmashParam() const { return smashParam_; }
    const AttackParam &GetSweepParam() const { return sweepParam_; }
    const AttackParam &GetBulletParam() const { return bulletParam_; }
    const AttackParam &GetWaveParam() const { return waveParam_; }
    const AttackTimingParam &GetSmashTiming() const { return smashTiming_; }
    const AttackTimingParam &GetSweepTiming() const { return sweepTiming_; }

    // ImGui調整用
    AttackParam &EditSmashParam() { return smashParam_; }
    AttackParam &EditSweepParam() { return sweepParam_; }
    AttackParam &EditBulletParam() { return bulletParam_; }
    AttackParam &EditWaveParam() { return waveParam_; }

    float &EditNearAttackDistance() { return nearAttackDistance_; }
    float &EditFarAttackDistance() { return farAttackDistance_; }

    float &EditSmashChargeTime() { return smashChargeTime_; }
   /* float &EditSmashAttackTime() { return smashAttackTime_; }
    float &EditSmashRecoveryTime() { return smashRecoveryTime_; }*/

    float &EditSweepChargeTime() { return sweepChargeTime_; }
   /* float &EditSweepAttackTime() { return sweepAttackTime_; }
    float &EditSweepRecoveryTime() { return sweepRecoveryTime_; }*/

    float &EditShotChargeTime() { return shotChargeTime_; }
    float &EditShotRecoveryTime() { return shotRecoveryTime_; }
    float &EditShotInterval() { return shotInterval_; }
    float &EditBulletSpeed() { return bulletSpeed_; }
    float &EditBulletLifeTime() { return bulletLifeTime_; }

    float &EditWaveChargeTime() { return waveChargeTime_; }
    float &EditWaveRecoveryTime() { return waveRecoveryTime_; }
    float &EditWaveSpeed() { return waveSpeed_; }
    float &EditWaveMaxDistance() { return waveMaxDistance_; }

    int &EditNearSmashWeight() { return nearSmashWeight_; }
    int &EditNearSweepWeight() { return nearSweepWeight_; }
    int &EditNearGuardWeight() { return nearGuardWeight_; }

    int &EditFarShotWeight() { return farShotWeight_; }
    int &EditFarWarpWeight() { return farWarpWeight_; }
    int &EditFarWaveWeight() { return farWaveWeight_; }

    /*float &EditSmashActiveStartTime() { return smashActiveStartTime_; }
    float &EditSmashActiveEndTime() { return smashActiveEndTime_; }

    float &EditSweepActiveStartTime() { return sweepActiveStartTime_; }
    float &EditSweepActiveEndTime() { return sweepActiveEndTime_; }*/

    AttackTimingParam &EditSmashTiming() { return smashTiming_; }
    AttackTimingParam &EditSweepTiming() { return sweepTiming_; }

    EnemyTuningPreset CreateTuningPreset() const;
    void ApplyTuningPreset(const EnemyTuningPreset &preset);
    void ResetTuningPreset();

  private:
    // ボス全体の基準Transform
    Transform tf_;

    // 各部位
    Transform bodyTf_;
    Transform leftHandTf_;
    Transform rightHandTf_;

    uint32_t modelId_ = 0;

    // 各部位サイズ
    DirectX::XMFLOAT3 bodySize_ = {0.8f, 1.4f, 0.6f};
    DirectX::XMFLOAT3 handSize_ = {0.45f, 0.45f, 0.45f};

    float hp_ = 1000.0f;

    // 行動管理
    EnemyState state_ = EnemyState::Idle;
    float stateTimer_ = 0.0f;

    // 振り下ろし用
    bool isAttackActive_ = false;
    //DirectX::XMFLOAT3 attackBoxSize_ = {1.5f, 1.5f, 1.5f};

    // 次の行動に移るかどうかのフラグ（振り下ろし→回復の切り替えタイミングで使用）
    //bool useSmashNext_ = true;

     DirectX::XMFLOAT3 playerPos_ = {0.0f, 0.0f, 0.0f};

    //float nearAttackDistance_ = 4.0f;

    float facingYaw_ = 0.0f;       // 現在の向き
    float lockedAttackYaw_ = 0.0f; // 攻撃開始時に固定する向き

    // 遠距離攻撃用弾
    std::vector<EnemyBullet> bullets_;

    //float farAttackDistance_ = 4.0f;
    int shotsRemaining_ = 0;
    float shotIntervalTimer_ = 0.0f;

    // ワープ用
    DirectX::XMFLOAT3 warpTargetPos_ = {0.0f, 0.0f, 0.0f};
    bool isVisible_ = true;
    //bool useShotNext_ = true;

    // 波動攻撃用
    std::vector<EnemyWave> waves_;

    //bool useWaveNext_ = true;

    // デバッグ用行動切り替えインデックス
    int farActionIndex_ = 0;

    //ガード位置
    GuardTarget guardTarget_ = GuardTarget::None;
    bool isGuardActive_ = false;

    // デバッグ用ガード位置切り替えインデックス
    int nearActionIndex_ = 0;

    //********************************
    // 調整用パラーメータ群
    //********************************
    float nearAttackDistance_ = 4.0f;
    float farAttackDistance_ = 4.0f;

    //振り下ろし
    AttackParam smashParam_ = {10.0f, 4.0f, {1.5f, 1.8f, 1.5f}};
    float smashChargeTime_ = 0.45f;

    AttackTimingParam smashTiming_ = {
        0.88f, // totalTime = attack + recovery の合計イメージ
        0.00f, // trackingEndTime
        0.04f, // activeStartTime
        0.10f, // activeEndTime
        0.18f  // recoveryStartTime
    };
 /*   float smashAttackTime_ = 0.24f;
    float smashRecoveryTime_ = 0.75f;*/
    float smashAttackForwardOffset_ = 1.4f;
    float smashAttackHeightOffset_ = 0.8f;

    // Smash の攻撃判定が出る時間帯（Attack状態の中）
  /*  float smashActiveStartTime_ = 0.06f;
    float smashActiveEndTime_ = 0.1f;*/

    //薙ぎ払い
    AttackParam sweepParam_ = {10.0f, 4.0f, {3.2f, 1.2f, 1.4f}};

    AttackTimingParam sweepTiming_ = {
        1.27f, // totalTime = attack + recovery の合計イメージ
        0.00f, // trackingEndTime
        0.12f, // activeStartTime
        0.24f, // activeEndTime
        0.32f  // recoveryStartTime
    };
    float sweepChargeTime_ = 0.65f;
   // float sweepAttackTime_ = 0.3f;
    float sweepRecoveryTime_ = 1.0f;
    float sweepAttackSideOffset_ = 0.2f;
    float sweepAttackHeightOffset_ = 0.8f;

    // Sweep の攻撃判定が出る時間帯（Attack状態の中）
   /* float sweepActiveStartTime_ = 0.12f;
    float sweepActiveEndTime_ = 0.24f;*/

    //弾
    AttackParam bulletParam_ = {5.0f, 2.5f, {0.4f, 0.4f, 0.4f}};
    float shotChargeTime_ = 0.6f;
    float shotRecoveryTime_ = 0.8f;
    float shotInterval_ = 0.2f;

    int shotMinCount_ = 3;
    int shotMaxCount_ = 5;

    float bulletSpeed_ = 6.0f;
    float bulletLifeTime_ = 2.0f;
    float bulletSpawnHeightOffset_ = 0.2f;

    //波状攻撃
    AttackParam waveParam_ = {8.0f, 3.0f, {1.2f, 0.6f, 1.6f}};
    float waveChargeTime_ = 0.6f;
    float waveRecoveryTime_ = 0.8f;

    float waveSpeed_ = 4.0f;
    float waveMaxDistance_ = 8.0f;
 
    float waveSpawnForwardOffset_ = 1.5f;
    float waveSpawnHeightOffset_ = 0.0f;

    //ワープ
    float warpStartTime_ = 0.2f;
    float warpEndTime_ = 0.2f;
    float warpRadius_ = 2.0f;

    //左手ガード
    float guardMoveTime_ = 0.25f;
    float guardHoldTime_ = 0.7f;
    float guardRecoveryTime_ = 0.35f;

    // 近距離行動の重み
    int nearSmashWeight_ = 40;
    int nearSweepWeight_ = 35;
    int nearGuardWeight_ = 25;

    // 遠距離行動の重み
    int farShotWeight_ = 40;
    int farWarpWeight_ = 25;
    int farWaveWeight_ = 35;

    // attckタイプとフェーズ管理
    AttackType currentAttackType_ = AttackType::None;
    AttackPhase currentAttackPhase_ = AttackPhase::None;

  private:
    void UpdateParts();
    OBB MakeOBB(const Transform &tf, const DirectX::XMFLOAT3 &size) const;

    // 行動ごとの更新処理
    void UpdateIdle(float deltaTime);

    // Smash
    void UpdateSmashCharge(float deltaTime);
    void UpdateSmashAttack(float deltaTime);
    void UpdateSmashRecovery(float deltaTime);

    // Sweep
    void UpdateSweepCharge(float deltaTime);
    void UpdateSweepAttack(float deltaTime);
    void UpdateSweepRecovery(float deltaTime);

    // 向き更新とロック
    void UpdateFacingToPlayer();
    void LockCurrentFacing();

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

    void DecideWarpTargetNearPlayer();

    void UpdateWaveCharge(float deltaTime);
    void UpdateWaveFire(float deltaTime);
    void UpdateWaveRecovery(float deltaTime);

    void SpawnWave();
    void UpdateWaves(float deltaTime);

    // ガード
    void UpdateGuardMove(float deltaTime);
    void UpdateGuardHold(float deltaTime);
    void UpdateGuardRecovery(float deltaTime);

    void DecideGuardTarget();
//ここから追加
    float GetCurrentActionTime() const;

    const AttackTimingParam *GetCurrentAttackTiming() const;
    AttackParam *GetCurrentAttackParam();
    const AttackParam *GetCurrentAttackParam() const;

    bool IsCurrentAttackInActiveWindow() const;
    bool IsCurrentAttackInRecoveryWindow() const;
    bool ShouldUseLockedAttackYaw() const;
    bool IsCurrentAttack(AttackType type) const;

    void BeginAttack(AttackType type, AttackPhase phase);
    void ChangeAttackPhase(AttackPhase phase);
    void EndAttack();
};