#pragma once
#include "Camera.h"
#include "JoyCon.h"
#include "PlayerWeaponType.h"
#include "Sword.h"
#include "SwordControllerState.h"
#include "SwordJoyConController.h"
#include "SwordMouseController.h"
#include "Transform.h"
#include <array>
#include <cstdint>

class ModelManager;
class Input;

class Player {
  public:
    static constexpr size_t kSwordCount = 2;

    void Initialize(uint32_t playerModelId, uint32_t swordModelId,
                    PlayerWeaponType weaponType = PlayerWeaponType::Standard);

    void Update(Input *input, float deltaTime,
                const DirectX::XMFLOAT3 &lookTarget, float cameraYaw);

    void Draw(ModelManager *modelManager, const Camera &camera);

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
    float GetGreatSwordChargeRatio() const { return greatSwordCharge_; }
    PlayerWeaponType GetWeaponType() const { return weaponType_; }
    const Transform &GetTransform() const { return tf_; }

    float GetHP() const { return hp_; }
    void TakeDamage(float damage);

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

  private:
    Transform BuildSwordTransform(const SwordPose &pose, bool isLeft) const;
    SwordPose MakeIdleSwordPose(bool isLeft) const;
    SwordPose MakeMirroredSwordPose(const SwordPose &source) const;
    SwordPose UpdateGamepadSword(Input *input, float deltaTime,
                                 const Transform &swordTransform);
    void UpdateGamepadSwordOrientation(Input *input, float deltaTime);
    void UpdateGamepadSwordGuard(Input *input);
    void UpdateGamepadSwordCounter(Input *input);
    void UpdateGamepadSwordSlash(Input *input, float deltaTime);
    void UpdateMovement(Input *input, float deltaTime, float cameraYaw);
    void KeepDistanceFromTarget(const DirectX::XMFLOAT3 &target);
    void LookAt(const DirectX::XMFLOAT3 &target);
    void UpdateWeaponRules(Input *input, SwordPose &leftPose,
                           SwordPose &rightPose, bool hasLeftJoyCon,
                           bool hasRightJoyCon, bool useGamepadRightSword,
                           float deltaTime);
    void ApplyHandRecovery(SwordPose &pose, float &timer, float deltaTime);
    void BeginDualManualCounter(bool preferLeft, const DirectX::XMFLOAT2 &dir);
    void UpdateDualManualCounter(SwordPose &leftPose, SwordPose &rightPose,
                                 float deltaTime);
    float GetSlashRecoveryDuration() const;
    float ComputeGreatSwordAttackDamage(float chargeRatio) const;

  private:
    static constexpr float kHandHeight = 1.0f;
    static constexpr float kArmLength = 1.0f;

    Transform tf_;
    uint32_t modelId_ = 0;
    PlayerWeaponType weaponType_ = PlayerWeaponType::Standard;

    DirectX::XMFLOAT3 size_ = {0.5f, 1.0f, 0.5f};

    Sword leftSword_;
    Sword rightSword_;
    JoyCon leftJoyCon_;
    JoyCon rightJoyCon_;
    SwordJoyConController leftSwordJoyConController_;
    SwordJoyConController rightSwordJoyConController_;
    SwordMouseController swordMouseController_;
    SwordControllerState gamepadSwordState_{};
    bool leftSwordSlashMode_ = false;
    bool rightSwordSlashMode_ = false;
    DirectX::XMFLOAT2 leftSwordSlashDir_{};
    DirectX::XMFLOAT2 rightSwordSlashDir_{};
    bool leftSwordVisible_ = false;
    bool rightSwordVisible_ = false;
    bool isGuarding_ = false;
    float postSlashRecoveryTimer_ = 0.0f;
    static constexpr float kPostSlashRecoveryDuration = 0.25f;
    float leftSlashRecoveryTimer_ = 0.0f;
    float rightSlashRecoveryTimer_ = 0.0f;
    float leftSwordAttackDamage_ = 10.0f;
    float rightSwordAttackDamage_ = 10.0f;
    bool prevLeftSwordSlashMode_ = false;
    bool prevRightSwordSlashMode_ = false;

    float greatSwordCharge_ = 0.0f;
    float greatSwordSwingTimer_ = 0.0f;
    float greatSwordSwingDamage_ = 18.0f;
    bool greatSwordFullChargeCounterReady_ = false;
    static constexpr float kGreatSwordMinSwingCharge = 0.32f;
    static constexpr float kGreatSwordChargeRate = 0.55f;
    static constexpr float kGreatSwordSwingDuration = 0.32f;

    int leftManualCounterFrames_ = 0;
    int rightManualCounterFrames_ = 0;
    DirectX::XMFLOAT2 leftManualCounterDir_{0.0f, 1.0f};
    DirectX::XMFLOAT2 rightManualCounterDir_{0.0f, 1.0f};
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
