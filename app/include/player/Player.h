#pragma once
#include "Camera.h"
#include "Sword.h"
#include "SwordControllerState.h"
#include "SwordInputCalibration.h"
#include "SwordMouseController.h"
#include "SwordUdpController.h"
#include "Transform.h"
#include <array>
#include <cstdint>

class ModelManager;
struct ModelDrawEffect;
class Input;

class Player {
  public:
    static constexpr size_t kSwordCount = 2;

  public:
    void Initialize(uint32_t playerModelId, uint32_t swordModelId);
    void SetInputCalibration(const SwordInputCalibration &calibration);
    void SetCameraSwordSlashSuppressed(bool suppressed) {
        suppressCameraSwordSlash_ = suppressed;
    }
    void SetHandPostSlashCooldownEnabled(bool enabled) {
        swordUdpController_.SetPostSlashCooldownEnabled(enabled);
    }
    void SetMovementSpeedMultiplier(float multiplier) {
        movementSpeedMultiplier_ = multiplier;
    }
    bool IsHandActive(size_t handIndex) const {
        return swordUdpController_.GetDebugHandState(handIndex).active;
    }
    bool HasFreshHandInput() const {
        return swordUdpController_.HasFreshInput();
    }

    void Update(Input *input, float deltaTime,
                const DirectX::XMFLOAT3 &lookTarget, float cameraYaw,
                float controlDeltaTime = -1.0f, bool suppressLookAt = false,
                bool suppressMovement = false);
    void UpdateDebugSwordPoses(const SwordPose &leftPose,
                               const SwordPose &rightPose, float deltaTime,
                               const DirectX::XMFLOAT3 &position, float yaw);
    void UpdateDemo(float deltaTime, const DirectX::XMFLOAT3 &lookTarget);

    void Draw(ModelManager *modelManager, const Camera &camera,
              bool drawBody = true, bool forceOpaque = false,
              float visualScale = 1.0f);

    std::array<const Sword *, kSwordCount> GetSwords() const {
        return {leftSwordVisible_ ? &leftSword_ : nullptr,
                rightSwordVisible_ ? &rightSword_ : nullptr};
    }
    std::array<bool, kSwordCount> GetSwordSlashStates() const {
        return {leftSwordSlashMode_, rightSwordSlashMode_};
    }
    std::array<float, kSwordCount> GetSwordAttackDamages() const {
        return {leftSwordAttackDamage_, rightSwordAttackDamage_};
    }
    struct ChargedShot {
        bool fired = false;
        DirectX::XMFLOAT3 origin{0.0f, 0.0f, 0.0f};
        DirectX::XMFLOAT3 direction{0.0f, 0.0f, 1.0f};
        float chargeRatio = 0.0f;
    };
    ChargedShot ConsumeChargedShot();
    bool IsChargingRangedAttack() const { return false; }
    float GetRangedAttackChargeRatio() const { return 0.0f; }
    OBB GetOBB() const;

    float GetCounterDamageMultiplier() const;
    float GetCounterVulnerabilityDuration() const;
    const Transform &GetTransform() const { return tf_; }
    void LockPosition(const DirectX::XMFLOAT3 &position) {
        tf_.position = position;
        velocity_ = {0.0f, 0.0f, 0.0f};
        knockbackVelocity_ = {0.0f, 0.0f, 0.0f};
    }
    float GetYaw() const { return yaw_; }
    void SetYaw(float yaw);
    void SetCinematicBladeClashPose(const DirectX::XMFLOAT3 &position,
                                    float yaw, float pushRatio,
                                    bool forwardLean = false);

    float GetHP() const { return hp_; }
    float TakeDamage(float damage);

    void AddKnockback(const DirectX::XMFLOAT3 &velocity);
    const DirectX::XMFLOAT3 &GetVelocity() const { return velocity_; }

    void SetDefeatPoseRatio(float ratio) { defeatPoseRatio_ = ratio; }
    void SetBladeClashPose(bool active, float pushRatio = 0.5f) {
        bladeClashPoseActive_ = active;
        bladeClashPosePushRatio_ = pushRatio;
        bladeClashPoseForwardLean_ = false;
    }
    bool UsesGamepadCameraLook() const { return useGamepadCameraLook_; }

  private:
    Transform BuildPlayerVisual(float visualScale) const;
    ModelDrawEffect MakeDamageFlashEffect(bool forceOpaque,
                                          float flashRatio) const;
    void SetPlayerDrawEffect(ModelManager *modelManager, bool damageFlashing,
                             bool forceOpaque, float flashRatio) const;
    void DrawSwordWithEffect(Sword &sword, ModelManager *modelManager,
                             const Camera &camera, bool damageFlashing,
                             bool forceOpaque, float flashRatio,
                             float visualScale) const;
    Transform BuildSwordTransform(const SwordPose &pose, bool isLeft) const;
    SwordPose MakeIdleSwordPose(bool isLeft) const;
    SwordPose MakeMirroredSwordPose(const SwordPose &source) const;
    SwordPose UpdateKeyboardLeftSword(Input *input, float deltaTime);
    void ToggleGamepadControlMode();
    void UpdateMovement(float deltaTime, const DirectX::XMFLOAT3 &lookTarget);
    void KeepDistanceFromTarget(const DirectX::XMFLOAT3 &target);
    void LookAt(const DirectX::XMFLOAT3 &target);
    void UpdateWeaponRules(Input *input, SwordPose &leftPose,
                           SwordPose &rightPose, bool useDualControls,
                           float deltaTime);
    void UpdateBodyState(float deltaTime, const DirectX::XMFLOAT3 &lookTarget,
                         bool suppressLookAt, bool suppressMovement);
    void ResolveInputSwordPoses(Input *input, float inputDeltaTime,
                                bool useUdpSword, bool useKeyboardMouse,
                                SwordPose &leftPose, SwordPose &rightPose);
    void ApplySwordPoseRestrictions(InputControlType controlType,
                                    SwordPose &leftPose,
                                    SwordPose &rightPose) const;
    void UpdateSwords(const SwordPose &leftPose, const SwordPose &rightPose,
                      float inputDeltaTime, bool allowMotionSlash);

  private:
    static constexpr float kHandHeight = 1.0f;
    static constexpr float kArmLength = 1.0f;

    Transform tf_;
    uint32_t modelId_ = 0;

    DirectX::XMFLOAT3 size_ = {0.5f, 1.0f, 0.5f};

    Sword leftSword_;
    Sword rightSword_;
    SwordMouseController swordMouseController_;
    SwordUdpController swordUdpController_;
    SwordInputCalibration inputCalibration_{};
    bool suppressCameraSwordSlash_ = false;
    SwordControllerState keyboardLeftSwordState_{};
    bool useGamepadCameraLook_ = true;
    bool leftSwordSlashMode_ = false;
    bool rightSwordSlashMode_ = false;
    bool leftSwordVisible_ = false;
    bool rightSwordVisible_ = false;
    float autoMoveOrbitDir_ = 1.0f;
    float autoMoveOrbitTimer_ = 0.0f;
    float movementSpeedMultiplier_ = 1.0f;
    static constexpr float kAutoMoveIdealDistance = 2.45f;
    static constexpr float kAutoMoveNearDistance = 1.75f;
    static constexpr float kAutoMoveFarDistance = 3.05f;
    static constexpr float kAutoMoveOrbitSpeed = 0.85f;
    static constexpr float kAutoMoveDistanceSpeed = 4.20f;
    float leftSwordAttackDamage_ = 8.0f;
    float rightSwordAttackDamage_ = 8.0f;
    float defeatPoseRatio_ = 0.0f;
    bool bladeClashPoseActive_ = false;
    float bladeClashPosePushRatio_ = 0.5f;
    bool bladeClashPoseForwardLean_ = false;

    float minTargetDistance_ = 2.7f;

    float hp_ = 100.0f;
    float damageFlashTimer_ = 0.0f;
    float damageFlashDuration_ = 0.32f;
    DirectX::XMFLOAT3 knockbackVelocity_ = {0.0f, 0.0f, 0.0f};
    float yaw_ = 0.0f;
    DirectX::XMFLOAT3 velocity_ = {0.0f, 0.0f, 0.0f};
};
