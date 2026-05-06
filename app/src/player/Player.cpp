#include "Player.h"
#include "Input.h"
#include "ModelManager.h"
#include "SwordPose.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace {
constexpr float kPlayerVisualScaleMultiplier = 3.0f;

DirectX::XMFLOAT2 NormalizeCounterDir(float x, float y) {
    const float lenSq = x * x + y * y;
    if (lenSq <= 0.001f) {
        return {0.0f, 1.0f};
    }

    const float invLen = 1.0f / std::sqrt(lenSq);
    return {x * invLen, y * invLen};
}
}

void Player::Initialize(uint32_t playerModelId, uint32_t swordModelId,
                        PlayerWeaponType weaponType) {
    modelId_ = playerModelId;
    weaponType_ = weaponType;

    tf_.position = {0, 0, 0};
    tf_.scale = {1, 1, 1};
    tf_.rotation = {0, 0, 0, 1};

    leftJoyCon_.Initialize(true);
    rightJoyCon_.Initialize(false);

    leftSword_.Initialize(swordModelId);
    rightSword_.Initialize(swordModelId);
    hp_ = maxHp_;
    velocity_ = {0.0f, 0.0f, 0.0f};
    postSlashRecoveryTimer_ = 0.0f;
    leftSlashRecoveryTimer_ = 0.0f;
    rightSlashRecoveryTimer_ = 0.0f;
    leftSwordAttackDamage_ = 10.0f;
    rightSwordAttackDamage_ = 10.0f;
    prevLeftSwordSlashMode_ = false;
    prevRightSwordSlashMode_ = false;
    greatSwordCharge_ = 0.0f;
    greatSwordSwingTimer_ = 0.0f;
    greatSwordSwingDamage_ = 18.0f;
    greatSwordFullChargeCounterReady_ = false;
    leftManualCounterFrames_ = 0;
    rightManualCounterFrames_ = 0;
    dualNextManualLeft_ = true;
    leftSword_.Update(BuildSwordTransform(MakeIdleSwordPose(true), true),
                      MakeIdleSwordPose(true), 0.0f);
    rightSword_.Update(BuildSwordTransform(MakeIdleSwordPose(false), false),
                       MakeIdleSwordPose(false), 0.0f);
}

void Player::Update(Input *input, float deltaTime, const XMFLOAT3 &lookTarget,
                    float cameraYaw) {
    if (input->IsKeyTrigger(DIK_C)) {
        leftJoyCon_.StartCalibration();
        rightJoyCon_.StartCalibration();
    }

    if (input->IsKeyTrigger(DIK_R)) {
        leftJoyCon_.SetBaseOrientation();
        rightJoyCon_.SetBaseOrientation();
    }

    leftJoyCon_.Update(deltaTime);
    rightJoyCon_.Update(deltaTime);

    UpdateMovement(input, deltaTime, cameraYaw);
    KeepDistanceFromTarget(lookTarget);
    LookAt(lookTarget);

    const bool hasLeftJoyCon = leftJoyCon_.IsConnected();
    const bool hasRightJoyCon = rightJoyCon_.IsConnected();
    const bool useGamepadRightSword =
        !hasLeftJoyCon && !hasRightJoyCon && input->IsGamepadConnected();
    const bool useMouseRightSword =
        !hasLeftJoyCon && !hasRightJoyCon && !useGamepadRightSword;

    SwordPose leftPose = MakeIdleSwordPose(true);
    if (hasLeftJoyCon) {
        leftSwordJoyConController_.Update(&leftJoyCon_, deltaTime,
                                          leftSword_.GetTransform());
        leftPose = leftSwordJoyConController_.GetPose();
    }

    SwordPose rightPose = MakeIdleSwordPose(false);
    if (hasRightJoyCon) {
        rightSwordJoyConController_.Update(&rightJoyCon_, deltaTime,
                                           rightSword_.GetTransform());
        rightPose = rightSwordJoyConController_.GetPose();
    } else if (useGamepadRightSword) {
        rightPose =
            UpdateGamepadSword(input, deltaTime, rightSword_.GetTransform());
    } else if (useMouseRightSword) {
        swordMouseController_.Update(input, deltaTime, rightSword_.GetTransform());
        rightPose = swordMouseController_.GetPose();
    }

    UpdateWeaponRules(input, leftPose, rightPose, hasLeftJoyCon,
                      hasRightJoyCon, useGamepadRightSword, deltaTime);

    if (postSlashRecoveryTimer_ > 0.0f) {
        postSlashRecoveryTimer_ -= deltaTime;
        if (postSlashRecoveryTimer_ < 0.0f) {
            postSlashRecoveryTimer_ = 0.0f;
        }
    }

    const bool isInPostSlashRecovery = postSlashRecoveryTimer_ > 0.0f;
    if (isInPostSlashRecovery) {
        leftPose.isSlashMode = false;
        rightPose.isSlashMode = false;
        leftPose.isGuard = false;
        rightPose.isGuard = false;
        leftPose.isCounter = false;
        rightPose.isCounter = false;
    }

    ApplyHandRecovery(leftPose, leftSlashRecoveryTimer_, deltaTime);
    ApplyHandRecovery(rightPose, rightSlashRecoveryTimer_, deltaTime);

    const float recoveryRatio =
        (kPostSlashRecoveryDuration > 0.0f)
            ? std::clamp(postSlashRecoveryTimer_ / kPostSlashRecoveryDuration,
                         0.0f, 1.0f)
            : 0.0f;
    leftSword_.SetRecoveryReaction(recoveryRatio);
    rightSword_.SetRecoveryReaction(recoveryRatio);

    leftSword_.Update(BuildSwordTransform(leftPose, true), leftPose, deltaTime);
    rightSword_.Update(BuildSwordTransform(rightPose, false), rightPose,
                       deltaTime);

    if (postSlashRecoveryTimer_ <= 0.0f && JustCounterFailed()) {
        postSlashRecoveryTimer_ = kPostSlashRecoveryDuration;
        leftSword_.SetRecoveryReaction(1.0f);
        rightSword_.SetRecoveryReaction(1.0f);
    }

    leftSwordSlashMode_ = leftPose.isSlashMode;
    rightSwordSlashMode_ = rightPose.isSlashMode;
    if (prevLeftSwordSlashMode_ && !leftSwordSlashMode_) {
        leftSlashRecoveryTimer_ =
            (std::max)(leftSlashRecoveryTimer_, GetSlashRecoveryDuration());
    }
    if (prevRightSwordSlashMode_ && !rightSwordSlashMode_) {
        rightSlashRecoveryTimer_ =
            (std::max)(rightSlashRecoveryTimer_, GetSlashRecoveryDuration());
    }
    prevLeftSwordSlashMode_ = leftSwordSlashMode_;
    prevRightSwordSlashMode_ = rightSwordSlashMode_;

    leftSwordSlashDir_ = leftPose.slashDir;
    rightSwordSlashDir_ = rightPose.slashDir;
    leftSwordVisible_ = weaponType_ == PlayerWeaponType::Dual;
    rightSwordVisible_ = hasRightJoyCon || useGamepadRightSword ||
                         useMouseRightSword;
    isGuarding_ = leftPose.isGuard || rightPose.isGuard;

}

void Player::Draw(ModelManager *modelManager, const Camera &camera) {
    const bool isInPostSlashRecovery = postSlashRecoveryTimer_ > 0.0f;
    const float recoveryRatio =
        (kPostSlashRecoveryDuration > 0.0f)
            ? std::clamp(postSlashRecoveryTimer_ / kPostSlashRecoveryDuration,
                         0.0f, 1.0f)
            : 0.0f;

    Transform playerVisual = tf_;
    playerVisual.scale.x *= kPlayerVisualScaleMultiplier;
    playerVisual.scale.y *= kPlayerVisualScaleMultiplier;
    playerVisual.scale.z *= kPlayerVisualScaleMultiplier;
    if (isInPostSlashRecovery) {
        const float phase = (1.0f - recoveryRatio) * 64.0f;
        const float shake = 0.035f * recoveryRatio;
        playerVisual.position.x += std::sinf(phase) * shake;
        playerVisual.position.z += std::cosf(phase * 1.37f) * shake;

        ModelDrawEffect recoveryEffect{};
        recoveryEffect.enabled = true;
        recoveryEffect.color = {1.0f, 0.25f, 0.25f, 0.75f};
        recoveryEffect.intensity = 0.55f + 0.60f * recoveryRatio;
        recoveryEffect.fresnelPower = 3.4f;
        recoveryEffect.noiseAmount = 0.35f * recoveryRatio;
        recoveryEffect.time = phase;
        modelManager->SetDrawEffect(recoveryEffect);
    }

    modelManager->Draw(modelId_, playerVisual, camera);
    if (leftSwordVisible_) {
        leftSword_.Draw(modelManager, camera);
    }
    if (rightSwordVisible_) {
        rightSword_.Draw(modelManager, camera);
    }

    if (isInPostSlashRecovery) {
        modelManager->ClearDrawEffect();
    }
}

OBB Player::GetOBB() const {
    OBB box;

    box.center = {tf_.position.x, tf_.position.y + size_.y * 0.5f,
                  tf_.position.z};
    box.size = size_;
    box.rotation = tf_.rotation;

    return box;
}

void Player::LookAt(const XMFLOAT3 &target) {
    float dx = target.x - tf_.position.x;
    float dz = target.z - tf_.position.z;

    yaw_ = atan2f(dx, dz);

    XMVECTOR q = XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 0), yaw_);
    XMStoreFloat4(&tf_.rotation, q);
}

void Player::KeepDistanceFromTarget(const DirectX::XMFLOAT3 &target) {
    float dx = tf_.position.x - target.x;
    float dz = tf_.position.z - target.z;
    float distSq = dx * dx + dz * dz;
    float minDistSq = minTargetDistance_ * minTargetDistance_;

    if (distSq >= minDistSq) {
        return;
    }

    float dist = std::sqrtf(distSq);
    if (dist < 0.0001f) {
        float fallbackYaw = yaw_ + 3.14159265f;
        dx = std::sinf(fallbackYaw);
        dz = std::cosf(fallbackYaw);
        dist = 1.0f;
    }

    float invDist = 1.0f / dist;
    float nx = dx * invDist;
    float nz = dz * invDist;

    tf_.position.x = target.x + nx * minTargetDistance_;
    tf_.position.z = target.z + nz * minTargetDistance_;

    velocity_.x = 0.0f;
    velocity_.z = 0.0f;
    knockbackVelocity_.x = 0.0f;
    knockbackVelocity_.z = 0.0f;
}

void Player::UpdateMovement(Input *input, float deltaTime, float cameraYaw) {
    float inputX = 0.0f;
    float inputZ = 0.0f;

    if (input->IsKeyPress(DIK_W))
        inputZ += 1.0f;
    if (input->IsKeyPress(DIK_S))
        inputZ -= 1.0f;
    if (input->IsKeyPress(DIK_A))
        inputX -= 1.0f;
    if (input->IsKeyPress(DIK_D))
        inputX += 1.0f;

    if (input->IsGamepadConnected()) {
        inputX += input->GetGamepadLeftStickX();
        inputZ += input->GetGamepadLeftStickY();
    }

    float moveLenSq = inputX * inputX + inputZ * inputZ;
    if (moveLenSq > 1.0f) {
        float invLen = 1.0f / std::sqrt(moveLenSq);
        inputX *= invLen;
        inputZ *= invLen;
    }

    float sinYaw = std::sinf(cameraYaw);
    float cosYaw = std::cosf(cameraYaw);
    float worldMoveX = sinYaw * inputZ + cosYaw * inputX;
    float worldMoveZ = cosYaw * inputZ - sinYaw * inputX;

    velocity_.x = worldMoveX * moveSpeed_;
    velocity_.y = 0.0f;
    velocity_.z = worldMoveZ * moveSpeed_;

    tf_.position.x += velocity_.x * deltaTime;
    tf_.position.z += velocity_.z * deltaTime;

    tf_.position.x += knockbackVelocity_.x * deltaTime;
    tf_.position.y += knockbackVelocity_.y * deltaTime;
    tf_.position.z += knockbackVelocity_.z * deltaTime;

    velocity_.x += knockbackVelocity_.x;
    velocity_.y += knockbackVelocity_.y;
    velocity_.z += knockbackVelocity_.z;

    knockbackVelocity_.x *= 0.85f;
    knockbackVelocity_.y *= 0.85f;
    knockbackVelocity_.z *= 0.85f;

    if (std::fabs(knockbackVelocity_.x) < 0.01f)
        knockbackVelocity_.x = 0.0f;
    if (std::fabs(knockbackVelocity_.y) < 0.01f)
        knockbackVelocity_.y = 0.0f;
    if (std::fabs(knockbackVelocity_.z) < 0.01f)
        knockbackVelocity_.z = 0.0f;
}

void Player::TakeDamage(float damage) {
    hp_ -= damage * damageTakenScale_;
    if (hp_ < 0.0f) {
        hp_ = 0.0f;
    }
}

void Player::AddKnockback(const DirectX::XMFLOAT3 &velocity) {
    knockbackVelocity_.x += velocity.x;
    knockbackVelocity_.y += velocity.y;
    knockbackVelocity_.z += velocity.z;
}

bool Player::IsGuarding() const {
    return isGuarding_;
}

float Player::GetGuardDamageMultiplier() const {
    switch (weaponType_) {
    case PlayerWeaponType::Dual:
        return 1.0f;
    case PlayerWeaponType::GreatSword:
        return std::clamp(0.80f - greatSwordCharge_ * 0.45f, 0.35f, 0.80f);
    case PlayerWeaponType::Standard:
    default:
        return 0.25f;
    }
}

float Player::GetCounterDamageMultiplier() const {
    switch (weaponType_) {
    case PlayerWeaponType::Dual:
        return 0.85f;
    case PlayerWeaponType::GreatSword:
        return greatSwordFullChargeCounterReady_ ? 4.0f : 1.45f;
    case PlayerWeaponType::Standard:
    default:
        return 1.0f;
    }
}

float Player::GetCounterVulnerabilityDuration() const {
    if (weaponType_ == PlayerWeaponType::GreatSword &&
        greatSwordFullChargeCounterReady_) {
        return 1.35f;
    }
    return 0.35f;
}

void Player::NotifyCounterSuccess() {
    if (leftSword_.IsCounterStance()) {
        leftSword_.NotifyCounterSuccess();
    }
    if (rightSword_.IsCounterStance()) {
        rightSword_.NotifyCounterSuccess();
    }
    if (weaponType_ == PlayerWeaponType::GreatSword &&
        greatSwordFullChargeCounterReady_) {
        greatSwordCharge_ = 0.0f;
    }
    greatSwordFullChargeCounterReady_ = false;
}

Transform Player::BuildSwordTransform(const SwordPose &pose, bool isLeft) const {
    Transform swordTransform{};

    XMVECTOR playerRot = XMQuaternionNormalize(XMLoadFloat4(&tf_.rotation));
    XMVECTOR swordRot = XMQuaternionNormalize(XMLoadFloat4(&pose.orientation));
    XMVECTOR finalRot = XMQuaternionMultiply(swordRot, playerRot);
    XMStoreFloat4(&swordTransform.rotation, finalRot);

    constexpr float kHandOffsetX = 0.35f;
    const float handOffsetX = isLeft ? -kHandOffsetX : kHandOffsetX;

    XMVECTOR playerPos = XMLoadFloat3(&tf_.position);
    XMVECTOR shoulderOffset = XMVector3Rotate(
        XMVectorSet(handOffsetX, kHandHeight, 0, 0), playerRot);
    XMVECTOR shoulderPos = XMVectorAdd(playerPos, shoulderOffset);
    XMVECTOR armVec =
        XMVector3Rotate(XMVectorSet(0, 0, kArmLength, 0), finalRot);

    XMStoreFloat3(&swordTransform.position, XMVectorAdd(shoulderPos, armVec));
    return swordTransform;
}

SwordPose Player::MakeIdleSwordPose(bool isLeft) const {
    (void)isLeft;
    SwordPose pose{};
    pose.orientation = {0, 0, 0, 1};
    return pose;
}

SwordPose Player::MakeMirroredSwordPose(const SwordPose &source) const {
    SwordPose pose = source;
    pose.slashDir.x = -pose.slashDir.x;
    return pose;
}

void Player::UpdateWeaponRules(Input *input, SwordPose &leftPose,
                               SwordPose &rightPose, bool hasLeftJoyCon,
                               bool hasRightJoyCon, bool useGamepadRightSword,
                               float deltaTime) {
    leftSwordAttackDamage_ = 10.0f;
    rightSwordAttackDamage_ = 10.0f;

    if (weaponType_ == PlayerWeaponType::Standard) {
        leftPose = MakeIdleSwordPose(true);
        return;
    }

    if (weaponType_ == PlayerWeaponType::Dual) {
        leftSwordAttackDamage_ = 7.0f;
        rightSwordAttackDamage_ = 7.0f;
        leftPose.isGuard = false;
        rightPose.isGuard = false;

        const bool singlePointerControl =
            !hasLeftJoyCon && !hasRightJoyCon && !useGamepadRightSword;
        if (singlePointerControl && rightPose.isSlashMode) {
            if (dualNextManualLeft_ && leftSlashRecoveryTimer_ <= 0.0f) {
                leftPose = MakeMirroredSwordPose(rightPose);
                rightPose.isSlashMode = false;
            } else if (rightSlashRecoveryTimer_ <= 0.0f) {
                leftPose = MakeIdleSwordPose(true);
            } else if (leftSlashRecoveryTimer_ <= 0.0f) {
                leftPose = MakeMirroredSwordPose(rightPose);
                rightPose.isSlashMode = false;
            } else {
                leftPose = MakeIdleSwordPose(true);
                rightPose.isSlashMode = false;
            }
        }

        const bool manualCounterTrigger =
            input->IsMouseTrigger(1) ||
            (input->IsGamepadConnected() &&
             input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_B));
        if (manualCounterTrigger) {
            float dirX = static_cast<float>(input->GetMouseDX());
            float dirY = -static_cast<float>(input->GetMouseDY());
            if (input->IsGamepadConnected()) {
                const float stickX = input->GetGamepadRightStickX();
                const float stickY = input->GetGamepadRightStickY();
                if (stickX * stickX + stickY * stickY > 0.04f) {
                    dirX = stickX;
                    dirY = stickY;
                }
            }
            BeginDualManualCounter(dualNextManualLeft_,
                                   NormalizeCounterDir(dirX, dirY));
            dualNextManualLeft_ = !dualNextManualLeft_;
        }

        UpdateDualManualCounter(leftPose, rightPose, deltaTime);
        return;
    }

    if (weaponType_ == PlayerWeaponType::GreatSword) {
        leftPose = MakeIdleSwordPose(true);
        leftSwordAttackDamage_ = 0.0f;

        if (greatSwordSwingTimer_ > 0.0f) {
            greatSwordSwingTimer_ -= deltaTime;
            if (greatSwordSwingTimer_ < 0.0f) {
                greatSwordSwingTimer_ = 0.0f;
            }
        }

        if (rightPose.isGuard) {
            greatSwordCharge_ += deltaTime * kGreatSwordChargeRate;
            greatSwordCharge_ = std::clamp(greatSwordCharge_, 0.0f, 1.0f);
        }

        if (rightPose.isCounter && greatSwordCharge_ >= 0.98f) {
            greatSwordFullChargeCounterReady_ = true;
        }

        if (rightPose.isSlashMode) {
            const bool startsSwing = !prevRightSwordSlashMode_ &&
                                     greatSwordSwingTimer_ <= 0.0f;
            if (startsSwing && greatSwordCharge_ >= kGreatSwordMinSwingCharge) {
                greatSwordSwingDamage_ =
                    ComputeGreatSwordAttackDamage(greatSwordCharge_);
                greatSwordSwingTimer_ = kGreatSwordSwingDuration;
                greatSwordCharge_ = 0.0f;
                greatSwordFullChargeCounterReady_ = false;
            }

            if (greatSwordSwingTimer_ <= 0.0f) {
                rightPose.isSlashMode = false;
            } else {
                rightSwordAttackDamage_ = greatSwordSwingDamage_;
            }
        } else if (greatSwordSwingTimer_ > 0.0f) {
            rightPose.isSlashMode = true;
            rightSwordAttackDamage_ = greatSwordSwingDamage_;
        }
    }
}

void Player::ApplyHandRecovery(SwordPose &pose, float &timer,
                               float deltaTime) {
    if (timer <= 0.0f) {
        return;
    }

    timer -= deltaTime;
    if (timer < 0.0f) {
        timer = 0.0f;
    }

    pose.isSlashMode = false;
    pose.isGuard = false;
    pose.isCounter = false;
}

void Player::BeginDualManualCounter(bool preferLeft,
                                    const DirectX::XMFLOAT2 &dir) {
    const bool canUseLeft = leftSlashRecoveryTimer_ <= 0.0f;
    const bool canUseRight = rightSlashRecoveryTimer_ <= 0.0f;
    bool useLeft = preferLeft;

    if (useLeft && !canUseLeft && canUseRight) {
        useLeft = false;
    } else if (!useLeft && !canUseRight && canUseLeft) {
        useLeft = true;
    }

    if (useLeft && canUseLeft) {
        leftManualCounterFrames_ = SwordControllerState::kCounterFrames;
        leftManualCounterDir_ = dir;
    } else if (!useLeft && canUseRight) {
        rightManualCounterFrames_ = SwordControllerState::kCounterFrames;
        rightManualCounterDir_ = dir;
    }
}

void Player::UpdateDualManualCounter(SwordPose &leftPose,
                                     SwordPose &rightPose, float deltaTime) {
    (void)deltaTime;

    if (leftManualCounterFrames_ > 0) {
        leftPose.isSlashMode = false;
        leftPose.isGuard = false;
        leftPose.isCounter = true;
        leftPose.slashDir = leftManualCounterDir_;
        --leftManualCounterFrames_;
    }
    if (rightManualCounterFrames_ > 0) {
        rightPose.isSlashMode = false;
        rightPose.isGuard = false;
        rightPose.isCounter = true;
        rightPose.slashDir = rightManualCounterDir_;
        --rightManualCounterFrames_;
    }
}

float Player::GetSlashRecoveryDuration() const {
    switch (weaponType_) {
    case PlayerWeaponType::Dual:
        return 0.16f;
    case PlayerWeaponType::GreatSword:
        return 0.55f;
    case PlayerWeaponType::Standard:
    default:
        return 0.25f;
    }
}

float Player::ComputeGreatSwordAttackDamage(float chargeRatio) const {
    const float t = std::clamp(chargeRatio, 0.0f, 1.0f);
    return 18.0f + 52.0f * t * t;
}

SwordPose Player::UpdateGamepadSword(Input *input, float deltaTime,
                                     const Transform &swordTransform) {
    UpdateGamepadSwordOrientation(input, deltaTime);
    UpdateGamepadSwordGuard(input);
    UpdateGamepadSwordCounter(input);
    gamepadSwordState_.UpdateCounter();

    if (gamepadSwordState_.isCounter) {
        gamepadSwordState_.isSlashMode = false;
        gamepadSwordState_.slashTimer = 0.0f;
    } else {
        UpdateGamepadSwordSlash(input, deltaTime);
    }

    gamepadSwordState_.UpdateSlashDir(swordTransform);
    return gamepadSwordState_.ToPose();
}

void Player::UpdateGamepadSwordOrientation(Input *input, float deltaTime) {
    const float lookX = input->GetGamepadRightStickX();
    const float lookY = input->GetGamepadRightStickY();
    const float lookLenSq = lookX * lookX + lookY * lookY;

    if (lookLenSq > 0.04f) {
        constexpr float kSwordLookSpeed = 3.0f;
        gamepadSwordYaw_ += lookX * kSwordLookSpeed * deltaTime;
        gamepadSwordPitch_ -= lookY * kSwordLookSpeed * deltaTime;
        gamepadSwordPitch_ =
            std::clamp(gamepadSwordPitch_, -1.2f, 1.2f);
    }

    XMVECTOR qYaw =
        XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 0), gamepadSwordYaw_);
    XMVECTOR qPitch =
        XMQuaternionRotationAxis(XMVectorSet(1, 0, 0, 0), gamepadSwordPitch_);
    XMVECTOR q = XMQuaternionNormalize(XMQuaternionMultiply(qPitch, qYaw));
    XMStoreFloat4(&gamepadSwordState_.orientation, q);
}

void Player::UpdateGamepadSwordGuard(Input *input) {
    gamepadSwordState_.isGuard =
        input->GetGamepadLeftTrigger() > 0.2f ||
        input->IsGamepadButtonPress(XINPUT_GAMEPAD_LEFT_SHOULDER);
}

void Player::UpdateGamepadSwordCounter(Input *input) {
    if (!gamepadSwordState_.isGuard) {
        return;
    }

    if (input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_B)) {
        gamepadSwordState_.isCounter = true;
        gamepadSwordState_.counterTimer = SwordControllerState::kCounterFrames;
        gamepadSwordState_.isSlashMode = false;
        gamepadSwordState_.slashTimer = 0.0f;
    }
}

void Player::UpdateGamepadSwordSlash(Input *input, float deltaTime) {
    const bool faceButtonSlash =
        !gamepadSwordState_.isGuard &&
        (input->IsGamepadButtonPress(XINPUT_GAMEPAD_A) ||
         input->IsGamepadButtonPress(XINPUT_GAMEPAD_B) ||
         input->IsGamepadButtonPress(XINPUT_GAMEPAD_X) ||
         input->IsGamepadButtonPress(XINPUT_GAMEPAD_Y));

    const bool slashActive =
        input->GetGamepadRightTrigger() > 0.2f || faceButtonSlash;

    float dirX = input->GetGamepadRightStickX();
    float dirY = input->GetGamepadRightStickY();

    if (input->IsGamepadButtonPress(XINPUT_GAMEPAD_X) ||
        input->IsGamepadButtonPress(XINPUT_GAMEPAD_DPAD_LEFT)) {
        dirX -= 1.0f;
    }
    if (input->IsGamepadButtonPress(XINPUT_GAMEPAD_B) ||
        input->IsGamepadButtonPress(XINPUT_GAMEPAD_DPAD_RIGHT)) {
        dirX += 1.0f;
    }
    if (input->IsGamepadButtonPress(XINPUT_GAMEPAD_Y) ||
        input->IsGamepadButtonPress(XINPUT_GAMEPAD_DPAD_UP)) {
        dirY += 1.0f;
    }
    if (input->IsGamepadButtonPress(XINPUT_GAMEPAD_A) ||
        input->IsGamepadButtonPress(XINPUT_GAMEPAD_DPAD_DOWN)) {
        dirY -= 1.0f;
    }

    const float dirLenSq = dirX * dirX + dirY * dirY;
    if (dirLenSq > 0.001f) {
        const float invLen = 1.0f / std::sqrt(dirLenSq);
        gamepadSwordState_.slashDir = {dirX * invLen, dirY * invLen};
    } else if (slashActive && gamepadSwordState_.slashDir.x == 0.0f &&
               gamepadSwordState_.slashDir.y == 0.0f) {
        gamepadSwordState_.slashDir = {1.0f, 0.0f};
    }

    gamepadSwordState_.UpdateSlash(
        slashActive ? SwordControllerState::kSlashThreshold + 1.0f : 0.0f,
        deltaTime);
}
