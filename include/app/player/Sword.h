#pragma once
#include "OBB.h"
#include "Transform.h"
#include <DirectXMath.h>
#include <cstdint>

class Input;
class ModelManager;
class Camera;

class Sword {
  public:
    /// <summary>
    /// 初期化処理
    /// </summary>
    /// <param name="modelId"></param>
    void Initialize(uint32_t modelId);

    /// <summary>
    /// 更新処理
    /// </summary>
    /// <param name="input">入力管理クラス</param>
    /// <param name="deltaTime">前フレームからの経過時間(秒)</param>
    /// <param name="playerPos">プレイヤーのワールド座標</param>
    /// <param name="playerRotation">プレイヤーの回転(クォータニオン)</param>
    /// <param name="playerArmLength">肩から手までの距離</param>
    /// <param name="playerHandHeight">肩の高さ</param>
    void Update(Input *input, float deltaTime,
                const DirectX::XMFLOAT3 &playerPos,
                const DirectX::XMFLOAT4 &playerRotation, float playerArmLength,
                float playerHandHeight);

    /// <summary>
    /// 描画処理
    /// </summary>
    /// <param name="modelManager">ModelManagerインスタンス</param>
    /// <param name="camera">描画に使用するカメラ</param>
    void Draw(ModelManager *modelManager, const Camera &camera);

    // Getter
    OBB GetOBB() const;

  private:
    // Update
    void UpdateOrientation(Input *input, float dt);
    void UpdateGuard(Input *input);
    void UpdateSlash(float dt);
    void UpdateTransform(const DirectX::XMFLOAT3 &playerPos,
                         const DirectX::XMFLOAT4 &playerRotation,
                         float playerArmLength, float playerHandHeight);

  private:
    static constexpr float kSlashHold = 200.0f;
    static constexpr float kTimeLimit = 0.3f;

    static constexpr float kSwordLength = 1.2f;

    uint32_t modelId_ = 0;
    Transform tf_;

    DirectX::XMFLOAT4 orientation_{0, 0, 0, 1};
    DirectX::XMFLOAT4 prevOrientation_{0, 0, 0, 1};

    float angularVelocity_ = 0.0f;

    bool isGuard_ = false;
    bool isSlashMode_ = false;

    float slashTimer_ = 0.0f;

    DirectX::XMFLOAT3 size_{0.2f, 0.2f, 0.6f};
};