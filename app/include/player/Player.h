#pragma once
#include "Camera.h"
#include "JoyCon.h"
#include "Sword.h"
#include "SwordControllerState.h"
#include "SwordInputCalibration.h"
#include "SwordJoyConController.h"
#include "SwordMouseController.h"
#include "SwordUdpController.h"
#include "Transform.h"
#include <array>
#include <cstdint>

class ModelManager;
class Input;

class Player {
  public:
    static constexpr size_t kSwordCount = 2;

    void Initialize(uint32_t playerModelId, uint32_t swordModelId);
    void SetInputCalibration(const SwordInputCalibration &calibration);
    void SetCameraSwordSlashSuppressed(bool suppressed) {
        suppressCameraSwordSlash_ = suppressed;
    }

    void Update(Input *input, float deltaTime,
                const DirectX::XMFLOAT3 &lookTarget, float cameraYaw,
                float controlDeltaTime = -1.0f,
                bool suppressLookAt = false);
    void UpdateDemo(float deltaTime, const DirectX::XMFLOAT3 &lookTarget);
    void UpdateJoyConCalibrationInput(Input *, float deltaTime);

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
                                    float yaw, float pushRatio);

    float GetHP() const { return hp_; }

    void AddKnockback(const DirectX::XMFLOAT3 &velocity);
    const DirectX::XMFLOAT3 &GetVelocity() const { return velocity_; }

    void SetDefeatPoseRatio(float ratio) { defeatPoseRatio_ = ratio; }
    void SetBladeClashPose(bool active, float pushRatio = 0.5f) {
        bladeClashPoseActive_ = active;
        bladeClashPosePushRatio_ = pushRatio;
    }
    bool UsesGamepadCameraLook() const { return useGamepadCameraLook_; }
  private:
    Transform BuildSwordTransform(const SwordPose &pose, bool isLeft) const;
    SwordPose MakeIdleSwordPose(bool isLeft) const;
    SwordPose MakeMirroredSwordPose(const SwordPose &source) const;
    SwordPose UpdateKeyboardLeftSword(Input *input, float deltaTime);
    void ToggleGamepadControlMode();
    void UpdateMovement(float deltaTime, const DirectX::XMFLOAT3 &lookTarget);
    void KeepDistanceFromTarget(const DirectX::XMFLOAT3 &target);
    void LookAt(const DirectX::XMFLOAT3 &target);
    void UpdateWeaponRules(Input *input, SwordPose &leftPose,
                           SwordPose &rightPose, bool hasLeftJoyCon,
                           bool hasRightJoyCon,
                           bool useDualUdpControls, float deltaTime);
    float ComputeJoyConSwingDamageMultiplier(float angularVelocity) const;

  private:
    static constexpr float kHandHeight = 1.0f;
    static constexpr float kArmLength = 1.0f;

    Transform tf_;
    uint32_t modelId_ = 0;

    DirectX::XMFLOAT3 size_ = {0.5f, 1.0f, 0.5f};

    Sword leftSword_;
    Sword rightSword_;
    JoyCon leftJoyCon_;
    JoyCon rightJoyCon_;
    SwordJoyConController leftSwordJoyConController_;
    SwordJoyConController rightSwordJoyConController_;
    SwordMouseController swordMouseController_;
    SwordUdpController swordUdpController_;
    SwordInputCalibration inputCalibration_{};
    bool applyJoyConBaseOnNextUpdate_ = false;
    bool suppressCameraSwordSlash_ = false;
    SwordControllerState keyboardLeftSwordState_{};
    bool useGamepadCameraLook_ = true;
    bool leftSwordSlashMode_ = false;
    bool rightSwordSlashMode_ = false;
    bool leftSwordVisible_ = false;
    bool rightSwordVisible_ = false;
    float autoMoveOrbitDir_ = 1.0f;
    float autoMoveOrbitTimer_ = 0.0f;
    static constexpr float kJoyConAutoMoveIdealDistance = 2.45f;
    static constexpr float kJoyConAutoMoveNearDistance = 1.75f;
    static constexpr float kJoyConAutoMoveFarDistance = 3.05f;
    static constexpr float kJoyConAutoMoveOrbitSpeed = 0.85f;
    static constexpr float kJoyConAutoMoveDistanceSpeed = 4.20f;
    float leftSwordAttackDamage_ = 8.0f;
    float rightSwordAttackDamage_ = 8.0f;

    float defeatPoseRatio_ = 0.0f;
    bool bladeClashPoseActive_ = false;
    float bladeClashPosePushRatio_ = 0.5f;

    float minTargetDistance_ = 2.7f;

    float hp_ = 100.0f;
    DirectX::XMFLOAT3 knockbackVelocity_ = {0.0f, 0.0f, 0.0f};
    float yaw_ = 0.0f;
    DirectX::XMFLOAT3 velocity_ = {0.0f, 0.0f, 0.0f};
};
