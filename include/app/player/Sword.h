#pragma once
#include "OBB.h"
#include "Transform.h"
#include "SwordJoyConController.h"
#include "SwordMouseController.h"
#include <DirectXMath.h>
#include <cstdint>

class Input;
class ModelManager;
class Camera;

enum class SwordHand : uint8_t {
    Left,
    Right,
};

class Sword {
  public:
    /// <summary>
    /// 初期化処理
    /// </summary>
    /// <param name="modelId"></param>
    void Initialize(uint32_t modelId, SwordHand hand);

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

    // Getter関数
    const Transform &GetTransform() const { return tf_; }
    bool GetCounter() const { return isCounter_; }
    bool GetSlashMode() const { return isSlashMode_; }
    OBB GetOBB() const;

    // 剣を振っているかの判定
    bool IsSlashMode() const { return isSlashMode_; }
    bool IsGuard() const { return isGuard_; }

    // Setter関数
    void SetCounter(bool isCounter);

  private:
    // メンバ関数
    void UpdateTransform(const DirectX::XMFLOAT3 &playerPos,
                         const DirectX::XMFLOAT4 &playerRotation,
                         float playerArmLength, float playerHandHeight);

    // メンバ変数
    SwordJoyConController swordJoyConController_;
    SwordMouseController swordMouseController_;
    static constexpr float kSwordLength = 1.2f;
    static constexpr float kHandOffsetX = 0.35f;
    DirectX::XMFLOAT3 size_{0.2f, 0.2f, 0.6f};

    uint32_t modelId_ = 0;
    Transform tf_;
    SwordHand hand_ = SwordHand::Right;

    bool isSlashMode_ = false;
    bool isGuard_ = false;
    bool isCounter_ = false;
    bool isMouse_ = false;
    bool isJoyCon_ = false;

    DirectX::XMFLOAT2 slashDir_{};
    DirectX::XMFLOAT4 orientation_{0, 0, 0, 1};

    // メンバ関数
    void ImGuiDraw();
};
