#pragma once
#include "OBB.h"
#include "SwordPose.h"
#include "Transform.h"
#include <DirectXMath.h>

class ModelManager;
class Camera;

enum class SwordCounterAxis { None, Vertical, Horizontal };

class Sword {
  public:
    void Initialize(uint32_t modelId);
    void Update(const Transform &transform, const SwordPose &pose,
                float deltaTime);

    void Draw(ModelManager *modelManager, const Camera &camera,
              float visualScale = 1.0f);

    const Transform &GetTransform() const { return tf_; }
    OBB GetOBB() const;

    DirectX::XMFLOAT3 GetVisualBladeRootWorld() const;
    DirectX::XMFLOAT3 GetVisualBladeTipWorld() const;

    bool IsSlashMode() const { return isSlashMode_; }
    bool CanSlashCounter() const { return isSlashMode_; }

    const DirectX::XMFLOAT2 &GetSlashDirection() const { return slashDir_; }
    SwordCounterAxis GetSlashAxis() const;

  private:
    Transform BuildVisualTransform() const;
    void UpdateSlashFollowThrough(float deltaTime);
    void ApplySlashFollowThrough(Transform &drawTransform) const;
    SwordCounterAxis ComputeSlashAxis() const;

  private:
    static constexpr float kSwordLength = 1.2f;
    static constexpr float kSlashHitDepthExtension = 1.55f;
    DirectX::XMFLOAT3 size_{0.34f, 0.34f, 0.78f};

    uint32_t modelId_ = 0;
    Transform tf_;

    bool isSlashMode_ = false;

    DirectX::XMFLOAT2 slashDir_{};
    DirectX::XMFLOAT4 orientation_{0, 0, 0, 1};

    float slashFollowThroughTimer_ = 0.0f;
    bool slashFollowThroughStarted_ = false;
    DirectX::XMFLOAT2 slashFollowThroughDir_{};
    DirectX::XMFLOAT2 slashFollowThroughAngles_{};
    float slashFollowThroughRoll_ = 0.0f;
    float slashFollowThroughSurge_ = 0.0f;
};
