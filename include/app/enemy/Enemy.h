#pragma once
#include "Camera.h"
#include "OBB.h"
#include "Transform.h"
#include "Player.h"
#include <cstdint>

class ModelManager;

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
    void Update(const Transform& playerTf);

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

    // 薙ぎ払い用
    void SweepAttack(const DirectX::XMFLOAT3 &playerPos);

    // Getter
    const Transform &GetTransform() const { return tf_; }
    bool IsAlive() const { return hp_ > 0.0f; }
    OBB GetOBB() const;
    OBB GetAttackOBB() const;
    bool GetIsAttacking() const { return isAttacking_; }

    void SetIsHit(bool isHit);

  private:
    Transform tf_;
    uint32_t modelId_ = 0;

    DirectX::XMFLOAT3 size_ = {0.5f, 1.2f, 0.5f};

    float hp_ = 100.0f;

    // タイマー
    int sweepTimer_ = 0;

    // 攻撃中か
    bool isSweeping_ = false;

    float moveSpeed_ = 0.05f;
    float stopDistance_ = 3.0f;

    int waitTimer_ = 0;
    int phase_ = 0;
    DirectX::XMFLOAT3 attackSize_ = {2.0f, 0.5f, 1.0f};
    bool isAttacking_ = false;

    //敵の攻撃がプレイヤーに当たってるか
    bool isHit_ = false;

     void ImGuiDraw();

};