#pragma once
#include "Camera.h"
#include "Sword.h"
#include "Transform.h"
#include <cstdint>

class ModelManager;
class Input;

class Player {
  public:
    /// <summary>
    /// 初期化処理
    /// </summary>
    /// <param name="playerModelId">プレイヤーモデルのID</param>
    /// <param name="swordModelId">剣モデルのID</param>
    void Initialize(uint32_t playerModelId, uint32_t swordModelId);

    /// <summary>
    /// 更新処理
    /// </summary>
    /// <param name="input">入力管理クラス</param>
    /// <param name="deltaTime">前フレームからの経過時間(秒)</param>
    /// <param name="lookTarget">プレイヤーが向く対象座標</param>
    void Update(Input *input, float deltaTime,
                const DirectX::XMFLOAT3 &lookTarget);

    /// <summary>
    /// 描画処理
    /// </summary>
    /// <param name="modelManager">モデル描画管理クラス</param>
    /// <param name="camera">描画に使用するカメラ</param>
    void Draw(ModelManager *modelManager, const Camera &camera);

    // Getter
    const Sword &GetSword() const { return sword_; }
    OBB GetOBB() const;
    // 書き替え可能版
    Sword &GetSword() { return sword_; }
    const Transform &GetTransform() const { return tf_; }

  private:
    // Update
    void UpdateMovement(Input *input, float deltaTime);

    void LookAt(const DirectX::XMFLOAT3 &target);

  private:
    static constexpr float kHandHeight = 1.0f;
    static constexpr float kArmLength = 1.0f;

    Transform tf_;
    uint32_t modelId_ = 0;

    DirectX::XMFLOAT3 size_ = {0.5f, 1.0f, 0.5f};

    Sword sword_;

    float moveSpeed_ = 5.0f;
    float yaw_ = 0.0f;
};