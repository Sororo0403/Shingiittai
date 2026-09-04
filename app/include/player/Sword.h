#pragma once
#include "OBB.h"
#include "SwordControllerState.h"
#include "SwordPose.h"
#include "Transform.h"
#include <DirectXMath.h>
#include <array>

class ModelManager;
class Camera;

/// <summary>
/// カウンター判定に用いる斬撃方向
/// </summary>
enum class SwordCounterAxis { None, Vertical, Horizontal };

/// <summary>
/// 剣モデルの姿勢、斬撃状態、当たり判定を管理する
/// </summary>
class Sword {
  public:
    /// <summary>
    /// 使用するリソースと初期状態を準備する
    /// </summary>
    void Initialize(uint32_t modelId);
    /// <summary>
    /// 入力と状態を1フレーム進める
    /// </summary>
    void Update(const Transform &transform, const SwordPose &pose,
                float deltaTime, bool allowMotionSlash = true);

    /// <summary>
    /// 現在の状態を描画する
    /// </summary>
    void Draw(ModelManager *modelManager, const Camera &camera,
              float visualScale = 1.0f);

    /// <summary>
    /// GetTransformに対応する現在値を取得する
    /// </summary>
    const Transform &GetTransform() const { return tf_; }
    /// <summary>
    /// GetOBBに対応する現在値を取得する
    /// </summary>
    OBB GetOBB() const;
    /// <summary>
    /// GetOBBSamplesに対応する現在値を取得する
    /// </summary>
    std::array<OBB, 3> GetOBBSamples() const;

    /// <summary>
    /// GetVisualBladeRootWorldに対応する現在値を取得する
    /// </summary>
    DirectX::XMFLOAT3 GetVisualBladeRootWorld() const;
    /// <summary>
    /// GetVisualBladeTipWorldに対応する現在値を取得する
    /// </summary>
    DirectX::XMFLOAT3 GetVisualBladeTipWorld() const;

    /// <summary>
    /// IsSlashModeの条件を満たすか判定する
    /// </summary>
    bool IsSlashMode() const { return isSlashMode_; }
    /// <summary>
    /// CanSlashCounterの条件を満たすか判定する
    /// </summary>
    bool CanSlashCounter() const { return isSlashMode_; }

    /// <summary>
    /// GetSlashDirectionに対応する現在値を取得する
    /// </summary>
    const DirectX::XMFLOAT2 &GetSlashDirection() const { return slashDir_; }
    /// <summary>
    /// GetSlashAxisに対応する現在値を取得する
    /// </summary>
    SwordCounterAxis GetSlashAxis() const;

  private:
    Transform BuildVisualTransform() const;
    OBB BuildOBB(const Transform &transform) const;
    Transform InterpolateTransform(float alpha) const;
    void UpdateSlashFollowThrough(float deltaTime);
    void UpdateMotionSlash(float deltaTime, bool allowMotionSlash);
    void ApplySlashFollowThrough(Transform &drawTransform) const;
    SwordCounterAxis ComputeSlashAxis() const;

  private:
    static constexpr float kSwordLength = 1.2f;
    static constexpr float kSlashHitDepthExtension = 2.35f;
    static constexpr float kSlashHitWidthScale = 2.0f;
    static constexpr float kSlashHitHeightScale = 1.65f;
    DirectX::XMFLOAT3 size_{0.34f, 0.34f, 0.78f};

    uint32_t modelId_ = 0;
    Transform tf_;
    Transform previousTf_;
    bool hasPreviousTransform_ = false;

    bool isSlashMode_ = false;

    DirectX::XMFLOAT2 slashDir_{};
    DirectX::XMFLOAT4 orientation_{0, 0, 0, 1};
    SwordControllerState motionSlashState_{};

    float slashFollowThroughTimer_ = 0.0f;
    bool slashFollowThroughStarted_ = false;
    DirectX::XMFLOAT2 slashFollowThroughDir_{};
    DirectX::XMFLOAT2 slashFollowThroughAngles_{};
    float slashFollowThroughRoll_ = 0.0f;
    float slashFollowThroughSurge_ = 0.0f;
};
