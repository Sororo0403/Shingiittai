#pragma once
#include "OBB.h"
#include "Transform.h"
#include <DirectXMath.h>
#include <cstdint>

class ModelManager;
class Input;
class Camera;

class Sword {
  public:
    /// <summary>
    /// 初期化処理
    /// </summary>
    /// <param name="modelId">剣のモデルID</param>
    void Initialize(uint32_t modelId);

    /// <summary>
    /// 更新処理
    /// </summary>
    /// <param name="input">Inputインスタンス</param>
    /// <param name="playerPos">プレイヤーの座標</param>
    void Update(Input *input, float dt, const DirectX::XMFLOAT3 &playerPos);

    /// <summary>
    /// 描画処理
    /// </summary>
    /// <param name="modelManager">ModelManagerインスタンス</param>
    /// <param name="camera">描画に使用するカメラ</param>
    void Draw(ModelManager *modelManager, const Camera &camera);

    // Getter
    const Transform &GetTransform() const { return tf_; }
    OBB GetOBB() const;

  private:
    static constexpr float kHandHeight = 0.8f;
    static constexpr float kSwordLength = 1.0f;

    Transform tf_;
    uint32_t modelId_ = 0;

    DirectX::XMFLOAT3 size_ = {0.1f, 0.1f, 0.5f};

    bool isSlashMode_ = false;
    float slashTimer_ = 0.0f;
    DirectX::XMFLOAT4 prevOrientation_ = {0.0f, 0.0f, 0.0f, 1.0f};
    const float kSlashHold = 720.0f;
    const float kTimeLimit = 1.0f;

    bool isGuard_ = false;

    // メンバ関数
    void ImGuiDraw();
};