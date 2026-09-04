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

/// <summary>
/// プレイヤーの移動、双剣入力、体力、戦闘姿勢を管理する
/// </summary>
class Player {
  public:
    static constexpr size_t kSwordCount = 2;

  public:
    /// <summary>
    /// 使用するリソースと初期状態を準備する
    /// </summary>
    void Initialize(uint32_t playerModelId, uint32_t swordModelId);
    /// <summary>
    /// SetInputCalibrationに対応する状態を設定する
    /// </summary>
    void SetInputCalibration(const SwordInputCalibration &calibration);
    /// <summary>
    /// SetCameraSwordSlashSuppressedに対応する状態を設定する
    /// </summary>
    void SetCameraSwordSlashSuppressed(bool suppressed) {
        suppressCameraSwordSlash_ = suppressed;
    }
    /// <summary>
    /// SetHandPostSlashCooldownEnabledに対応する状態を設定する
    /// </summary>
    void SetHandPostSlashCooldownEnabled(bool enabled) {
        swordUdpController_.SetPostSlashCooldownEnabled(enabled);
    }
    /// <summary>
    /// SetMovementSpeedMultiplierに対応する状態を設定する
    /// </summary>
    void SetMovementSpeedMultiplier(float multiplier) {
        movementSpeedMultiplier_ = multiplier;
    }
    /// <summary>
    /// IsHandActiveの条件を満たすか判定する
    /// </summary>
    bool IsHandActive(size_t handIndex) const {
        return swordUdpController_.GetDebugHandState(handIndex).active;
    }
    /// <summary>
    /// HasFreshHandInputの条件を満たすか判定する
    /// </summary>
    bool HasFreshHandInput() const {
        return swordUdpController_.HasFreshInput();
    }

    /// <summary>
    /// 入力と状態を1フレーム進める
    /// </summary>
    void Update(Input *input, float deltaTime,
                const DirectX::XMFLOAT3 &lookTarget, float cameraYaw,
                float controlDeltaTime = -1.0f, bool suppressLookAt = false,
                bool suppressMovement = false);
    /// <summary>
    /// UpdateDebugSwordPosesに対応する公開処理を実行する
    /// </summary>
    void UpdateDebugSwordPoses(const SwordPose &leftPose,
                               const SwordPose &rightPose, float deltaTime,
                               const DirectX::XMFLOAT3 &position, float yaw);
    /// <summary>
    /// UpdateDemoに対応する公開処理を実行する
    /// </summary>
    void UpdateDemo(float deltaTime, const DirectX::XMFLOAT3 &lookTarget);

    /// <summary>
    /// 現在の状態を描画する
    /// </summary>
    void Draw(ModelManager *modelManager, const Camera &camera,
              bool drawBody = true, bool forceOpaque = false,
              float visualScale = 1.0f);

    /// <summary>
    /// GetSwordsに対応する現在値を取得する
    /// </summary>
    std::array<const Sword *, kSwordCount> GetSwords() const {
        return {leftSwordVisible_ ? &leftSword_ : nullptr,
                rightSwordVisible_ ? &rightSword_ : nullptr};
    }
    /// <summary>
    /// GetSwordSlashStatesに対応する現在値を取得する
    /// </summary>
    std::array<bool, kSwordCount> GetSwordSlashStates() const {
        return {leftSwordSlashMode_, rightSwordSlashMode_};
    }
    /// <summary>
    /// GetSwordAttackDamagesに対応する現在値を取得する
    /// </summary>
    std::array<float, kSwordCount> GetSwordAttackDamages() const {
        return {leftSwordAttackDamage_, rightSwordAttackDamage_};
    }
    struct ChargedShot {
        bool fired = false;
        DirectX::XMFLOAT3 origin{0.0f, 0.0f, 0.0f};
        DirectX::XMFLOAT3 direction{0.0f, 0.0f, 1.0f};
        float chargeRatio = 0.0f;
    };
    /// <summary>
    /// ConsumeChargedShotに対応する保留イベントを取得して消費する
    /// </summary>
    ChargedShot ConsumeChargedShot();
    /// <summary>
    /// IsChargingRangedAttackの条件を満たすか判定する
    /// </summary>
    bool IsChargingRangedAttack() const { return false; }
    /// <summary>
    /// GetRangedAttackChargeRatioに対応する現在値を取得する
    /// </summary>
    float GetRangedAttackChargeRatio() const { return 0.0f; }
    /// <summary>
    /// GetOBBに対応する現在値を取得する
    /// </summary>
    OBB GetOBB() const;

    /// <summary>
    /// GetCounterDamageMultiplierに対応する現在値を取得する
    /// </summary>
    float GetCounterDamageMultiplier() const;
    /// <summary>
    /// GetCounterVulnerabilityDurationに対応する現在値を取得する
    /// </summary>
    float GetCounterVulnerabilityDuration() const;
    /// <summary>
    /// GetTransformに対応する現在値を取得する
    /// </summary>
    const Transform &GetTransform() const { return tf_; }
    /// <summary>
    /// LockPositionに対応する状態を固定する
    /// </summary>
    void LockPosition(const DirectX::XMFLOAT3 &position) {
        tf_.position = position;
        velocity_ = {0.0f, 0.0f, 0.0f};
        knockbackVelocity_ = {0.0f, 0.0f, 0.0f};
    }
    /// <summary>
    /// GetYawに対応する現在値を取得する
    /// </summary>
    float GetYaw() const { return yaw_; }
    /// <summary>
    /// SetYawに対応する状態を設定する
    /// </summary>
    void SetYaw(float yaw);
    /// <summary>
    /// SetCinematicBladeClashPoseに対応する状態を設定する
    /// </summary>
    void SetCinematicBladeClashPose(const DirectX::XMFLOAT3 &position,
                                    float yaw, float pushRatio,
                                    bool forwardLean = false);

    /// <summary>
    /// GetHPに対応する現在値を取得する
    /// </summary>
    float GetHP() const { return hp_; }
    /// <summary>
    /// 指定したダメージを適用し、実際の減少量を返す
    /// </summary>
    float TakeDamage(float damage);

    /// <summary>
    /// AddKnockbackに対応する要素を追加する
    /// </summary>
    void AddKnockback(const DirectX::XMFLOAT3 &velocity);
    /// <summary>
    /// GetVelocityに対応する現在値を取得する
    /// </summary>
    const DirectX::XMFLOAT3 &GetVelocity() const { return velocity_; }

    /// <summary>
    /// SetDefeatPoseRatioに対応する状態を設定する
    /// </summary>
    void SetDefeatPoseRatio(float ratio) { defeatPoseRatio_ = ratio; }
    /// <summary>
    /// SetBladeClashPoseに対応する状態を設定する
    /// </summary>
    void SetBladeClashPose(bool active, float pushRatio = 0.5f) {
        bladeClashPoseActive_ = active;
        bladeClashPosePushRatio_ = pushRatio;
        bladeClashPoseForwardLean_ = false;
    }
    /// <summary>
    /// UsesGamepadCameraLookの条件を満たすか判定する
    /// </summary>
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
