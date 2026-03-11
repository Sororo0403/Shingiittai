#pragma once
#include "Camera.h"
#include "OBB.h"
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
    WarpEnd
};

struct EnemyBullet {
    DirectX::XMFLOAT3 position = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 velocity = {0.0f, 0.0f, 0.0f};
    float lifeTime = 0.0f;
    bool isAlive = false;
};

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

    // Getter
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
    DirectX::XMFLOAT3 attackBoxSize_ = {1.5f, 1.5f, 1.5f};

    // 次の行動に移るかどうかのフラグ（振り下ろし→回復の切り替えタイミングで使用）
    bool useSmashNext_ = true;

     DirectX::XMFLOAT3 playerPos_ = {0.0f, 0.0f, 0.0f};

    float nearAttackDistance_ = 4.0f;

    float facingYaw_ = 0.0f;       // 現在の向き
    float lockedAttackYaw_ = 0.0f; // 攻撃開始時に固定する向き

    // 遠距離攻撃用弾
    std::vector<EnemyBullet> bullets_;

    float farAttackDistance_ = 4.0f;
    int shotsRemaining_ = 0;
    float shotIntervalTimer_ = 0.0f;

    // ワープ用
    DirectX::XMFLOAT3 warpTargetPos_ = {0.0f, 0.0f, 0.0f};
    bool isVisible_ = true;
    bool useShotNext_ = true;
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
};