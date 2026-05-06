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
    void SetRecoveryReaction(float reaction);

    void Draw(ModelManager *modelManager, const Camera &camera);

    const Transform &GetTransform() const { return tf_; }
    OBB GetOBB() const;
    OBB GetCounterOBB() const;

    bool IsSlashMode() const { return isSlashMode_; }
    bool IsGuard() const { return isGuard_; }

    bool IsCounterStance() const { return isCounterStance_; }
    bool JustCountered() const { return justCountered_; }
    bool JustCounterFailed() const { return justCounterFailed_; }
    bool JustCounterEarly() const { return justCounterEarly_; }
    bool JustCounterLate() const { return justCounterLate_; }

    SwordCounterAxis GetCounterAxis() const { return counterAxis_; }
    void NotifyCounterSuccess();

  private:
    void UpdateCounterObservation(float deltaTime);
    void ImGuiDraw();

  private:
    static constexpr float kSwordLength = 1.2f;
    static constexpr float kSlashHitDepthExtension = 0.85f;
    DirectX::XMFLOAT3 size_{0.2f, 0.2f, 0.6f};
    DirectX::XMFLOAT3 counterSize_{1.0f, 0.8f, 1.6f};

    uint32_t modelId_ = 0;
    Transform tf_;

    bool isSlashMode_ = false;
    bool isGuard_ = false;
    bool isCounter_ = false;
    bool isMouse = false;
    bool isJoyCon = false;

    DirectX::XMFLOAT2 slashDir_{};
    DirectX::XMFLOAT4 orientation_{0, 0, 0, 1};

    bool prevIsCounter_ = false;

    bool isCounterStance_ = false;
    bool justCountered_ = false;
    bool justCounterFailed_ = false;
    bool justCounterEarly_ = false;
    bool justCounterLate_ = false;

    float counterStateTimer_ = 0.0f;
    float counterEarlyThreshold_ = 0.10f;
    float counterLateThreshold_ = 0.45f;

    SwordCounterAxis counterAxis_ = SwordCounterAxis::None;
    float recoveryReaction_ = 0.0f;
};
