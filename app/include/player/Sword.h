#pragma once
#include "OBB.h"
#include "SwordControllerState.h"
#include "SwordPose.h"
#include "Transform.h"
#include <DirectXMath.h>
#include <array>

class ModelManager;
class Camera;

enum class SwordCounterAxis { None, Vertical, Horizontal };

class Sword {
  public:
    void Initialize(uint32_t modelId);
    void Update(const Transform &transform, const SwordPose &pose,
                float deltaTime, bool allowMotionSlash = true);

    void Draw(ModelManager *modelManager, const Camera &camera,
              float visualScale = 1.0f);

    const Transform &GetTransform() const { return tf_; }
    OBB GetOBB() const;
    std::array<OBB, 3> GetOBBSamples() const;

    DirectX::XMFLOAT3 GetVisualBladeRootWorld() const;
    DirectX::XMFLOAT3 GetVisualBladeTipWorld() const;

    bool IsSlashMode() const { return isSlashMode_; }
    bool CanSlashCounter() const { return isSlashMode_; }

    const DirectX::XMFLOAT2 &GetSlashDirection() const { return slashDir_; }
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
