#pragma once
#include "Camera.h"
#include "JoyCon.h"
#include "Sword.h"
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

    void Initialize(uint32_t playerModelId, uint32_t swordModelId);

    void Update(Input *input, float deltaTime,
                const DirectX::XMFLOAT3 &lookTarget);

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
    std::array<DirectX::XMFLOAT2, kSwordCount> GetSwordSlashDirections() const {
        return {leftSwordSlashDir_, rightSwordSlashDir_};
    }
    const DirectX::XMFLOAT2 &GetLeftSwordSlashDirection() const {
        return leftSwordSlashDir_;
    }
    const DirectX::XMFLOAT2 &GetRightSwordSlashDirection() const {
        return rightSwordSlashDir_;
    }
    OBB GetOBB() const;

    Sword &GetSword() { return rightSword_; }
    Sword &GetLeftSword() { return leftSword_; }
    Sword &GetRightSword() { return rightSword_; }
    bool IsGuarding() const;
    const Transform &GetTransform() const { return tf_; }

    float GetHP() const { return hp_; }
    void TakeDamage(float damage);

    void AddKnockback(const DirectX::XMFLOAT3 &velocity);

  private:
    Transform BuildSwordTransform(const SwordPose &pose, bool isLeft) const;
    SwordPose MakeIdleSwordPose(bool isLeft) const;
    void UpdateMovement(Input *input, float deltaTime);
    void LookAt(const DirectX::XMFLOAT3 &target);

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
    bool leftSwordSlashMode_ = false;
    bool rightSwordSlashMode_ = false;
    DirectX::XMFLOAT2 leftSwordSlashDir_{};
    DirectX::XMFLOAT2 rightSwordSlashDir_{};
    bool leftSwordVisible_ = false;
    bool rightSwordVisible_ = false;
    bool isGuarding_ = false;

    float moveSpeed_ = 5.0f;

    float hp_ = 100.0f;
    DirectX::XMFLOAT3 knockbackVelocity_ = {0.0f, 0.0f, 0.0f};
    float yaw_ = 0.0f;
};
