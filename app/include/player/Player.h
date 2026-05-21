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

enum class PlayerGamepadControlMode {
    MotionSword,
    Hunter,
};

enum class HunterGamepadAttackKind {
    None,
    SideLeft,
    SideRight,
    Overhead,
    Thrust,
};

class Player {
  public:
    static constexpr size_t kSwordCount = 2;

    void Initialize(uint32_t playerModelId, uint32_t swordModelId);
    void SetInputCalibration(const SwordInputCalibration &calibration);

    void Update(Input *input, float deltaTime,
                const DirectX::XMFLOAT3 &lookTarget, float cameraYaw,
                bool forceRangedReflectMove = false,
                float controlDeltaTime = -1.0f,
                bool suppressLookAt = false);
    void UpdateJoyConCalibrationInput(Input *input, float deltaTime);

    void Draw(ModelManager *modelManager, const Camera &camera,
              bool drawBody = true, bool forceOpaque = false,
              float visualScale = 1.0f);

    const Sword &GetSword() const { return rightSword_; }
    const Sword &GetLeftSword() const { return leftSword_; }
    const Sword &GetRightSword() const { return rightSword_; }
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

    Sword &GetSword() { return rightSword_; }
    Sword &GetLeftSword() { return leftSword_; }
    Sword &GetRightSword() { return rightSword_; }
    bool IsGuarding() const;
    float GetGuardDamageMultiplier() const;
    float GetCounterDamageMultiplier() const;
    float GetCounterVulnerabilityDuration() const;
    const Transform &GetTransform() const { return tf_; }
    void SetPosition(const DirectX::XMFLOAT3 &position) { tf_.position = position; }
    void LockPosition(const DirectX::XMFLOAT3 &position) {
        tf_.position = position;
        velocity_ = {0.0f, 0.0f, 0.0f};
        knockbackVelocity_ = {0.0f, 0.0f, 0.0f};
    }
    void SetYaw(float yaw);
    void SetCinematicBladeClashPose(const DirectX::XMFLOAT3 &position,
                                    float yaw, float pushRatio);
    void SetCinematicDualBladeBarragePose(const DirectX::XMFLOAT3 &position,
                                          float yaw, float phase,
                                          float intensity);
    bool IsAttackRecovery() const {
        return postSlashRecoveryTimer_ > 0.0f || leftSlashRecoveryTimer_ > 0.0f ||
               rightSlashRecoveryTimer_ > 0.0f;
    }
    float GetYaw() const { return yaw_; }

    float GetHP() const { return hp_; }
    float TakeDamage(float damage);
    void NotifyAttackHit(float damage);
    void NotifyAttackHit(size_t swordIndex, float damage);

    void AddKnockback(const DirectX::XMFLOAT3 &velocity);
    const DirectX::XMFLOAT3 &GetVelocity() const { return velocity_; }

    bool IsCounterStance() const {
        return leftSword_.IsCounterStance() || rightSword_.IsCounterStance();
    }
    bool JustCountered() const {
        return leftSword_.JustCountered() || rightSword_.JustCountered();
    }
    bool JustCounterFailed() const {
        return leftSword_.JustCounterFailed() || rightSword_.JustCounterFailed();
    }
    bool JustCounterEarly() const {
        return leftSword_.JustCounterEarly() || rightSword_.JustCounterEarly();
    }
    bool JustCounterLate() const {
        return leftSword_.JustCounterLate() || rightSword_.JustCounterLate();
    }
    SwordCounterAxis GetCounterAxis() const {
        if (rightSword_.IsCounterStance()) {
            return rightSword_.GetCounterAxis();
        }
        if (leftSword_.IsCounterStance()) {
            return leftSword_.GetCounterAxis();
        }
        return SwordCounterAxis::None;
    }
    void NotifyCounterSuccess();
    void NotifyCounterSuccess(size_t swordIndex);
    void SetDefeatPoseRatio(float ratio) { defeatPoseRatio_ = ratio; }
    void SetBladeClashPose(bool active, float pushRatio = 0.5f) {
        bladeClashPoseActive_ = active;
        bladeClashPosePushRatio_ = pushRatio;
        bladeClashCinematicSlashRatio_ = 0.0f;
    }
    bool UsesGamepadCameraLook() const {
        return gamepadControlMode_ == PlayerGamepadControlMode::Hunter;
    }
    bool UsesJoyConControls() const {
        return leftJoyCon_.IsConnected() || rightJoyCon_.IsConnected();
    }

  private:
    Transform BuildSwordTransform(const SwordPose &pose, bool isLeft) const;
    SwordPose MakeIdleSwordPose(bool isLeft) const;
    SwordPose MakeMirroredSwordPose(const SwordPose &source) const;
    SwordPose UpdateGamepadSword(Input *input, float deltaTime,
                                 const Transform &swordTransform);
    SwordPose UpdateHunterGamepadSword(Input *input, float deltaTime);
    SwordPose UpdateKeyboardLeftSword(Input *input, float deltaTime);
    void UpdateGamepadSwordOrientation(Input *input, float deltaTime);
    void UpdateGamepadSwordGuard(Input *input);
    void UpdateGamepadSwordSlash(Input *input, float deltaTime);
    void UpdateHunterGamepadSwordOrientation();
    void UpdateHunterGamepadSwordGuard(Input *input);
    void UpdateHunterGamepadSwordCounter(Input *input);
    void UpdateHunterGamepadSwordSlash(Input *input, float deltaTime);
    void BeginHunterGamepadAttack(HunterGamepadAttackKind attackKind);
    HunterGamepadAttackKind ReadHunterGamepadAttack(Input *input) const;
    bool IsHunterGamepadAttacking() const {
        return hunterGamepadAttackKind_ != HunterGamepadAttackKind::None;
    }
    float GetHunterGamepadAttackRatio() const;
    float GetHunterGamepadAttackDuration(HunterGamepadAttackKind attackKind) const;
    DirectX::XMFLOAT2 GetHunterGamepadSlashDir(Input *input) const;
    DirectX::XMFLOAT2 GetHunterGamepadSlashDir(
        HunterGamepadAttackKind attackKind) const;
    void ToggleGamepadControlMode();
    DirectX::XMFLOAT2 ReadMovementInput(Input *input) const;
    bool UsesJoyConAutoMovement() const;
    void UpdateMovement(Input *input, float deltaTime, float cameraYaw,
                        const DirectX::XMFLOAT3 &lookTarget,
                        bool forceRangedReflectMove);
    void KeepDistanceFromTarget(const DirectX::XMFLOAT3 &target);
    void LookAt(const DirectX::XMFLOAT3 &target);
    void UpdateWeaponRules(Input *input, SwordPose &leftPose,
                           SwordPose &rightPose, bool hasLeftJoyCon,
                           bool hasRightJoyCon, bool useGamepadRightSword,
                           bool useHunterGamepadControls,
                           bool useDualUdpControls, float deltaTime);
    void ApplyHandRecovery(SwordPose &pose, float &timer, float deltaTime);
    void RegisterAttackHit(float damage);
    void RegisterAttackWhiff();
    void ResetOverSwing();
    void UpdateOverSwing(float deltaTime);
    float GetSlashRecoveryDuration(bool hitConfirmed) const;
    float GetHitConfirmRecoveryDuration() const;
    float GetSlashRecoveryRatio(float timer) const;
    float GetAttackRecoveryRatio() const;
    float ComputeJoyConSwingDamageMultiplier(float angularVelocity) const;
    float GetSwingComboDamageMultiplier() const;
    void UpdateSwingCombo(float deltaTime);

  private:
    static constexpr float kHandHeight = 1.0f;
    static constexpr float kArmLength = 1.0f;
    static constexpr float kSwingComboWindow = 1.35f;
    static constexpr int kSwingComboMax = 3;
    static constexpr float kOverSwingResetDuration = 0.95f;
    static constexpr int kOverSwingMax = 3;

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
    SwordControllerState gamepadSwordState_{};
    SwordControllerState keyboardLeftSwordState_{};
    PlayerGamepadControlMode gamepadControlMode_ =
        PlayerGamepadControlMode::Hunter;
    HunterGamepadAttackKind hunterGamepadAttackKind_ =
        HunterGamepadAttackKind::None;
    float hunterGamepadAttackTimer_ = 0.0f;
    float hunterGamepadAttackDuration_ = 0.0f;
    bool hunterNextSideSlashLeft_ = true;
    bool leftSwordSlashMode_ = false;
    bool rightSwordSlashMode_ = false;
    DirectX::XMFLOAT2 leftSwordSlashDir_{};
    DirectX::XMFLOAT2 rightSwordSlashDir_{};
    bool leftSwordVisible_ = false;
    bool rightSwordVisible_ = false;
    bool isGuarding_ = false;
    float postSlashRecoveryTimer_ = 0.0f;
    static constexpr float kPostSlashRecoveryDuration = 0.30f;
    float autoMoveOrbitDir_ = 1.0f;
    float autoMoveOrbitTimer_ = 0.0f;
    static constexpr float kJoyConAutoMoveIdealDistance = 2.45f;
    static constexpr float kJoyConAutoMoveNearDistance = 1.75f;
    static constexpr float kJoyConAutoMoveFarDistance = 3.05f;
    static constexpr float kJoyConAutoMoveOrbitSpeed = 0.85f;
    static constexpr float kJoyConAutoMoveDistanceSpeed = 4.20f;
    float leftSlashRecoveryTimer_ = 0.0f;
    float rightSlashRecoveryTimer_ = 0.0f;
    float leftSwordAttackDamage_ = 8.0f;
    float rightSwordAttackDamage_ = 8.0f;
    bool prevLeftSwordSlashMode_ = false;
    bool prevRightSwordSlashMode_ = false;
    bool leftSlashHitConfirmed_ = false;
    bool rightSlashHitConfirmed_ = false;
    float recoveryVulnerableFlashTimer_ = 0.0f;
    int overSwingCount_ = 0;
    float overSwingResetTimer_ = 0.0f;
    int swingComboCount_ = 0;
    float swingComboTimer_ = 0.0f;

    float defeatPoseRatio_ = 0.0f;
    bool bladeClashPoseActive_ = false;
    float bladeClashPosePushRatio_ = 0.5f;
    float bladeClashCinematicSlashRatio_ = 0.0f;

    bool dualNextManualLeft_ = true;

    float moveSpeed_ = 5.0f;
    float minTargetDistance_ = 2.7f;

    float maxHp_ = 100.0f;
    float hp_ = 100.0f;
    float damageTakenScale_ = 1.0f;
    DirectX::XMFLOAT3 knockbackVelocity_ = {0.0f, 0.0f, 0.0f};
    float yaw_ = 0.0f;
    float gamepadSwordYaw_ = 0.0f;
    float gamepadSwordPitch_ = 0.0f;
    DirectX::XMFLOAT3 velocity_ = {0.0f, 0.0f, 0.0f};
};
