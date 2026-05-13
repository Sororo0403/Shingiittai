#include "Player.h"
#include "Input.h"
#include "ModelManager.h"
#include "SwordPose.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace {
constexpr float kPlayerVisualScaleMultiplier = 1.45f;
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
    leftSwordAttackDamage_ = 4.0f;
    rightSwordAttackDamage_ = 4.0f;
    prevLeftSwordSlashMode_ = false;
    prevRightSwordSlashMode_ = false;
    leftSlashHitConfirmed_ = false;
    rightSlashHitConfirmed_ = false;
    recoveryVulnerableFlashTimer_ = 0.0f;
    overSwingCount_ = 0;
    overSwingResetTimer_ = 0.0f;
    damageTakenScale_ = 3.34f;
    swingComboCount_ = 0;
    swingComboTimer_ = 0.0f;
    greatSwordCharge_ = 0.0f;
    greatSwordSwingTimer_ = 0.0f;
    greatSwordSwingDamage_ = 18.0f;
    greatSwordFullChargeCounterReady_ = false;
    defeatPoseRatio_ = 0.0f;
    dualNextManualLeft_ = true;
    gamepadControlMode_ = PlayerGamepadControlMode::Hunter;
    gamepadSwordState_ = {};
    gamepadSwordYaw_ = 0.0f;
    gamepadSwordPitch_ = 0.0f;
    hunterGamepadAttackKind_ = HunterGamepadAttackKind::None;
    hunterGamepadAttackTimer_ = 0.0f;
    hunterGamepadAttackDuration_ = 0.0f;
    hunterNextSideSlashLeft_ = true;
    dodgeTimer_ = 0.0f;
    dodgeCooldownTimer_ = 0.0f;
    dodgeInvulnerableTimer_ = 0.0f;
    dodgeDirection_ = {0.0f, -1.0f};
    autoMoveOrbitDir_ = 1.0f;
    autoMoveOrbitTimer_ = 0.0f;
    autoDodgeSide_ = 1.0f;
    leftSword_.Update(BuildSwordTransform(MakeIdleSwordPose(true), true),
                      MakeIdleSwordPose(true), 0.0f);
    rightSword_.Update(BuildSwordTransform(MakeIdleSwordPose(false), false),
                       MakeIdleSwordPose(false), 0.0f);
}

void Player::Update(Input *input, float deltaTime, const XMFLOAT3 &lookTarget,
                    float cameraYaw, bool forceRangedReflectMove,
                    float controlDeltaTime) {
    const float inputDeltaTime =
        controlDeltaTime > 0.0f ? controlDeltaTime : deltaTime;

    UpdateJoyConCalibrationInput(input, inputDeltaTime);

    if (input->IsGamepadConnected() &&
        input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_START)) {
        ToggleGamepadControlMode();
    }

    UpdateDodgeInput(input, deltaTime, cameraYaw, lookTarget);
    UpdateMovement(input, deltaTime, cameraYaw, lookTarget,
                   forceRangedReflectMove);
    UpdateSwingCombo(deltaTime);
    UpdateOverSwing(deltaTime);
    KeepDistanceFromTarget(lookTarget);
    LookAt(lookTarget);

    const bool hasLeftJoyCon = leftJoyCon_.IsConnected();
    const bool hasRightJoyCon = rightJoyCon_.IsConnected();
    const bool useGamepadRightSword =
        !hasLeftJoyCon && !hasRightJoyCon && input->IsGamepadConnected();
    const bool useHunterGamepadControls =
        useGamepadRightSword &&
        gamepadControlMode_ == PlayerGamepadControlMode::Hunter;
    const bool useMouseRightSword =
        !hasLeftJoyCon && !hasRightJoyCon && !useGamepadRightSword;
    const bool useUdpRightSword = useMouseRightSword;

    SwordPose leftPose = MakeIdleSwordPose(true);
    if (hasLeftJoyCon) {
        leftSwordJoyConController_.Update(&leftJoyCon_, inputDeltaTime,
                                          leftSword_.GetTransform());
        leftPose = leftSwordJoyConController_.GetPose();
    }

    SwordPose rightPose = MakeIdleSwordPose(false);
    if (hasRightJoyCon) {
        rightSwordJoyConController_.Update(&rightJoyCon_, inputDeltaTime,
                                           rightSword_.GetTransform());
        rightPose = rightSwordJoyConController_.GetPose();
    } else if (useHunterGamepadControls) {
        rightPose = UpdateHunterGamepadSword(input, inputDeltaTime);
    } else if (useGamepadRightSword) {
        rightPose =
            UpdateGamepadSword(input, inputDeltaTime, rightSword_.GetTransform());
    } else if (useUdpRightSword) {
        swordUdpController_.Update(inputDeltaTime);
        if (swordUdpController_.IsActive()) {
            rightPose = swordUdpController_.GetPose();
        } else {
            swordMouseController_.Update(input, inputDeltaTime,
                                         rightSword_.GetTransform());
            rightPose = swordMouseController_.GetPose();
        }
    } else if (useMouseRightSword) {
        swordMouseController_.Update(input, inputDeltaTime,
                                     rightSword_.GetTransform());
        rightPose = swordMouseController_.GetPose();
    }

    UpdateWeaponRules(input, leftPose, rightPose, hasLeftJoyCon,
                      hasRightJoyCon, useGamepadRightSword,
                      useHunterGamepadControls, deltaTime);

    if (postSlashRecoveryTimer_ > 0.0f) {
        postSlashRecoveryTimer_ -= deltaTime;
        if (postSlashRecoveryTimer_ < 0.0f) {
            postSlashRecoveryTimer_ = 0.0f;
        }
    }

    const bool isInPostSlashRecovery = postSlashRecoveryTimer_ > 0.0f;
    const bool isDodging = dodgeTimer_ > 0.0f;
    if (isInPostSlashRecovery || isDodging) {
        leftPose.isSlashMode = false;
        rightPose.isSlashMode = false;
        leftPose.isGuard = false;
        rightPose.isGuard = false;
        leftPose.isCounter = false;
        rightPose.isCounter = false;
    }

    ApplyHandRecovery(leftPose, leftSlashRecoveryTimer_, deltaTime);
    ApplyHandRecovery(rightPose, rightSlashRecoveryTimer_, deltaTime);

    leftSword_.SetRecoveryReaction(GetSlashRecoveryRatio(leftSlashRecoveryTimer_));
    rightSword_.SetRecoveryReaction(
        (std::max)(GetSlashRecoveryRatio(rightSlashRecoveryTimer_),
                   (kPostSlashRecoveryDuration > 0.0f)
                       ? std::clamp(postSlashRecoveryTimer_ /
                                        kPostSlashRecoveryDuration,
                                    0.0f, 1.0f)
                       : 0.0f));

    leftSword_.Update(BuildSwordTransform(leftPose, true), leftPose,
                      inputDeltaTime);
    rightSword_.Update(BuildSwordTransform(rightPose, false), rightPose,
                       inputDeltaTime);

    if (postSlashRecoveryTimer_ <= 0.0f && JustCounterFailed()) {
        postSlashRecoveryTimer_ = kPostSlashRecoveryDuration;
        leftSword_.SetRecoveryReaction(1.0f);
        rightSword_.SetRecoveryReaction(1.0f);
    }

    leftSwordSlashMode_ = leftPose.isSlashMode;
    rightSwordSlashMode_ = rightPose.isSlashMode;
    if (prevLeftSwordSlashMode_ && !leftSwordSlashMode_) {
        if (!leftSlashHitConfirmed_) {
            RegisterAttackWhiff();
        }
        leftSlashRecoveryTimer_ =
            (std::max)(leftSlashRecoveryTimer_,
                       GetSlashRecoveryDuration(leftSlashHitConfirmed_));
        leftSlashHitConfirmed_ = false;
    } else if (!prevLeftSwordSlashMode_ && leftSwordSlashMode_) {
        leftSlashHitConfirmed_ = false;
    }
    if (prevRightSwordSlashMode_ && !rightSwordSlashMode_) {
        if (!rightSlashHitConfirmed_) {
            RegisterAttackWhiff();
        }
        rightSlashRecoveryTimer_ =
            (std::max)(rightSlashRecoveryTimer_,
                       GetSlashRecoveryDuration(rightSlashHitConfirmed_));
        rightSlashHitConfirmed_ = false;
    } else if (!prevRightSwordSlashMode_ && rightSwordSlashMode_) {
        rightSlashHitConfirmed_ = false;
    }
    prevLeftSwordSlashMode_ = leftSwordSlashMode_;
    prevRightSwordSlashMode_ = rightSwordSlashMode_;

    leftSwordSlashDir_ = leftPose.slashDir;
    rightSwordSlashDir_ = rightPose.slashDir;
    leftSwordVisible_ = weaponType_ == PlayerWeaponType::Dual;
    rightSwordVisible_ = hasRightJoyCon || useGamepadRightSword ||
                         useMouseRightSword;
    isGuarding_ = false;
    if (IsAttackRecovery()) {
        recoveryVulnerableFlashTimer_ += deltaTime;
    } else {
        recoveryVulnerableFlashTimer_ = 0.0f;
    }

}

void Player::UpdateJoyConCalibrationInput(Input *input, float deltaTime) {
    if (input->IsKeyTrigger(DIK_C)) {
        leftJoyCon_.StartCalibration();
        rightJoyCon_.StartCalibration();
        leftSwordJoyConController_.ResetTracking(&leftJoyCon_);
        rightSwordJoyConController_.ResetTracking(&rightJoyCon_);
    }

    if (input->IsKeyTrigger(DIK_R)) {
        leftJoyCon_.SetBaseOrientation();
        rightJoyCon_.SetBaseOrientation();
        leftSwordJoyConController_.ResetTracking(&leftJoyCon_);
        rightSwordJoyConController_.ResetTracking(&rightJoyCon_);
    }
    if (leftJoyCon_.IsConnected() && leftJoyCon_.IsButtonTrigger(JSMASK_ZL)) {
        leftJoyCon_.SetBaseOrientation();
        leftSwordJoyConController_.ResetTracking(&leftJoyCon_);
    }
    if (rightJoyCon_.IsConnected() && rightJoyCon_.IsButtonTrigger(JSMASK_ZR)) {
        rightJoyCon_.SetBaseOrientation();
        rightSwordJoyConController_.ResetTracking(&rightJoyCon_);
    }

    leftJoyCon_.Update(deltaTime);
    rightJoyCon_.Update(deltaTime);
}

void Player::Draw(ModelManager *modelManager, const Camera &camera,
                  bool drawBody) {
    const bool isInPostSlashRecovery = postSlashRecoveryTimer_ > 0.0f;
    const float attackRecoveryRatio = GetAttackRecoveryRatio();
    const bool isAttackRecovery = attackRecoveryRatio > 0.0f;
    const float vulnerablePulse =
        0.5f + 0.5f * std::sinf(recoveryVulnerableFlashTimer_ * 30.0f);
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

    }
    if (isAttackRecovery) {
        const float phase = recoveryVulnerableFlashTimer_ * 42.0f;
        const float shake = (0.020f + 0.045f * vulnerablePulse) *
                            attackRecoveryRatio;
        playerVisual.position.x += std::sinf(phase) * shake;
        playerVisual.position.z += std::cosf(phase * 1.53f) * shake;
        playerVisual.position.y -= 0.055f * attackRecoveryRatio;
        playerVisual.scale.x *= 1.0f + 0.045f * attackRecoveryRatio;
        playerVisual.scale.y *= 1.0f - 0.075f * attackRecoveryRatio;
        playerVisual.scale.z *= 1.0f + 0.045f * attackRecoveryRatio;
    }
    if (dodgeTimer_ > 0.0f) {
        const float dodgeRatio =
            std::clamp(dodgeTimer_ / kDodgeDuration, 0.0f, 1.0f);
        const float lean = 0.22f * std::sinf((1.0f - dodgeRatio) * 3.14159265f);
        XMVECTOR baseRot = XMLoadFloat4(&playerVisual.rotation);
        XMVECTOR qLean =
            XMQuaternionRotationAxis(XMVectorSet(1, 0, 0, 0), lean);
        XMStoreFloat4(&playerVisual.rotation,
                      XMQuaternionNormalize(XMQuaternionMultiply(qLean, baseRot)));
        playerVisual.position.y -= 0.10f * std::sinf((1.0f - dodgeRatio) *
                                                     3.14159265f);
    }
    if (defeatPoseRatio_ > 0.0f) {
        const float fall = std::clamp(defeatPoseRatio_, 0.0f, 1.0f);
        const float eased = fall * fall * (3.0f - 2.0f * fall);
        XMVECTOR baseRot = XMLoadFloat4(&playerVisual.rotation);
        XMVECTOR qFall =
            XMQuaternionRotationAxis(XMVectorSet(1, 0, 0, 0), -1.34f * eased);
        XMStoreFloat4(&playerVisual.rotation,
                      XMQuaternionNormalize(XMQuaternionMultiply(qFall, baseRot)));
        playerVisual.position.y -= 0.48f * eased;
        playerVisual.scale.x *= 1.0f + 0.08f * eased;
        playerVisual.scale.y *= 1.0f - 0.24f * eased;
        playerVisual.scale.z *= 1.0f + 0.10f * eased;
    }

    if (drawBody) {
        if (isAttackRecovery) {
            ModelDrawEffect recoveryEffect{};
            recoveryEffect.enabled = true;
            recoveryEffect.additiveBlend = false;
            recoveryEffect.color = {1.0f, 0.08f, 0.02f, 0.82f};
            recoveryEffect.intensity =
                0.42f + 0.48f * vulnerablePulse * attackRecoveryRatio;
            recoveryEffect.fresnelPower = 1.35f;
            recoveryEffect.noiseAmount = 0.34f + 0.18f * vulnerablePulse;
            recoveryEffect.time = recoveryVulnerableFlashTimer_;
            modelManager->SetDrawEffect(recoveryEffect);
        }
        modelManager->Draw(modelId_, playerVisual, camera);
        if (isAttackRecovery) {
            modelManager->ClearDrawEffect();
        }
    }
    modelManager->ClearDrawEffect();

    auto drawSwordWithRecovery = [&](Sword &sword, float recoveryRatio) {
        if (recoveryRatio > 0.0f) {
            ModelDrawEffect recoveryEffect{};
            recoveryEffect.enabled = true;
            recoveryEffect.additiveBlend = true;
            recoveryEffect.color = {0.18f, 0.78f, 1.0f, 0.72f};
            recoveryEffect.intensity =
                0.42f + 0.26f * vulnerablePulse * recoveryRatio;
            recoveryEffect.fresnelPower = 1.0f;
            recoveryEffect.noiseAmount = 0.10f;
            recoveryEffect.time = recoveryVulnerableFlashTimer_;
            modelManager->SetDrawEffect(recoveryEffect);
        }
        sword.Draw(modelManager, camera);
        if (recoveryRatio > 0.0f) {
            modelManager->ClearDrawEffect();
        }
    };

    if (leftSwordVisible_) {
        drawSwordWithRecovery(leftSword_, GetSlashRecoveryRatio(leftSlashRecoveryTimer_));
    }
    if (rightSwordVisible_) {
        const float rightRecoveryRatio =
            (std::max)(GetSlashRecoveryRatio(rightSlashRecoveryTimer_),
                       recoveryRatio);
        drawSwordWithRecovery(rightSword_, rightRecoveryRatio);
    }

    modelManager->ClearDrawEffect();
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

DirectX::XMFLOAT2 Player::ReadMovementInput(Input *input) const {
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

    return {inputX, inputZ};
}

bool Player::IsDodgeInputTriggered(Input *input) const {
    const bool keyboardDodge = input->IsKeyTrigger(DIK_SPACE);
    const bool gamepadDodge =
        input->IsGamepadConnected() &&
        input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_B);
    const bool leftJoyConDodge =
        leftJoyCon_.IsConnected() &&
        (leftJoyCon_.IsButtonTrigger(JSMASK_S) ||
         leftJoyCon_.IsButtonTrigger(JSMASK_LCLICK));
    const bool rightJoyConDodge =
        rightJoyCon_.IsConnected() &&
        (rightJoyCon_.IsButtonTrigger(JSMASK_S) ||
         rightJoyCon_.IsButtonTrigger(JSMASK_RCLICK));

    return keyboardDodge || gamepadDodge || leftJoyConDodge || rightJoyConDodge;
}

bool Player::UsesJoyConAutoMovement() const {
    return leftJoyCon_.IsConnected() || rightJoyCon_.IsConnected();
}

void Player::UpdateDodgeInput(Input *input, float deltaTime, float cameraYaw,
                              const XMFLOAT3 &lookTarget) {
    if (dodgeCooldownTimer_ > 0.0f) {
        dodgeCooldownTimer_ -= deltaTime;
        if (dodgeCooldownTimer_ < 0.0f) {
            dodgeCooldownTimer_ = 0.0f;
        }
    }
    if (dodgeTimer_ > 0.0f) {
        dodgeTimer_ -= deltaTime;
        if (dodgeTimer_ < 0.0f) {
            dodgeTimer_ = 0.0f;
        }
    }
    if (dodgeInvulnerableTimer_ > 0.0f) {
        dodgeInvulnerableTimer_ -= deltaTime;
        if (dodgeInvulnerableTimer_ < 0.0f) {
            dodgeInvulnerableTimer_ = 0.0f;
        }
    }

    if (dodgeTimer_ > 0.0f || dodgeCooldownTimer_ > 0.0f ||
        !IsDodgeInputTriggered(input)) {
        return;
    }

    XMFLOAT2 inputDir = ReadMovementInput(input);
    const float lenSq = inputDir.x * inputDir.x + inputDir.y * inputDir.y;
    float worldX = 0.0f;
    float worldZ = 0.0f;
    if (lenSq > 0.01f && !UsesJoyConAutoMovement()) {
        const float sinYaw = std::sinf(cameraYaw);
        const float cosYaw = std::cosf(cameraYaw);
        worldX = sinYaw * inputDir.y + cosYaw * inputDir.x;
        worldZ = cosYaw * inputDir.y - sinYaw * inputDir.x;
    } else if (UsesJoyConAutoMovement()) {
        float toTargetX = lookTarget.x - tf_.position.x;
        float toTargetZ = lookTarget.z - tf_.position.z;
        float distSq = toTargetX * toTargetX + toTargetZ * toTargetZ;
        if (distSq < 0.0001f) {
            toTargetX = std::sinf(yaw_);
            toTargetZ = std::cosf(yaw_);
            distSq = 1.0f;
        }

        const float invDist = 1.0f / std::sqrt(distSq);
        const float towardX = toTargetX * invDist;
        const float towardZ = toTargetZ * invDist;
        const float rightX = towardZ;
        const float rightZ = -towardX;
        const float distance = std::sqrt(distSq);
        const bool tooClose = distance < kJoyConAutoMoveNearDistance;

        worldX = rightX * autoDodgeSide_;
        worldZ = rightZ * autoDodgeSide_;
        if (tooClose) {
            worldX -= towardX * 0.75f;
            worldZ -= towardZ * 0.75f;
        }
        autoDodgeSide_ *= -1.0f;
    } else {
        worldX = -std::sinf(yaw_);
        worldZ = -std::cosf(yaw_);
    }

    const float worldLenSq = worldX * worldX + worldZ * worldZ;
    if (worldLenSq > 0.001f) {
        const float invLen = 1.0f / std::sqrt(worldLenSq);
        worldX *= invLen;
        worldZ *= invLen;
    }

    dodgeDirection_ = {worldX, worldZ};
    dodgeTimer_ = kDodgeDuration;
    dodgeInvulnerableTimer_ = kDodgeInvulnerableDuration;
    dodgeCooldownTimer_ = kDodgeCooldownDuration;
    postSlashRecoveryTimer_ = 0.0f;
}

void Player::UpdateMovement(Input *input, float deltaTime, float cameraYaw,
                            const XMFLOAT3 &lookTarget,
                            bool forceRangedReflectMove) {
    const XMFLOAT2 moveInput = ReadMovementInput(input);
    float inputX = moveInput.x;
    float inputZ = moveInput.y;

    float sinYaw = std::sinf(cameraYaw);
    float cosYaw = std::cosf(cameraYaw);
    float worldMoveX = sinYaw * inputZ + cosYaw * inputX;
    float worldMoveZ = cosYaw * inputZ - sinYaw * inputX;
    const bool useAutoMovement = true;
    const bool hasManualMove = false;
    if (useAutoMovement && !hasManualMove) {
        autoMoveOrbitTimer_ -= deltaTime;
        if (autoMoveOrbitTimer_ <= 0.0f) {
            autoMoveOrbitTimer_ = 1.6f;
            autoMoveOrbitDir_ *= -1.0f;
        }

        float toTargetX = lookTarget.x - tf_.position.x;
        float toTargetZ = lookTarget.z - tf_.position.z;
        float distSq = toTargetX * toTargetX + toTargetZ * toTargetZ;
        if (distSq < 0.0001f) {
            toTargetX = std::sinf(yaw_);
            toTargetZ = std::cosf(yaw_);
            distSq = 1.0f;
        }

        const float distance = std::sqrt(distSq);
        const float invDist = 1.0f / distance;
        const float towardX = toTargetX * invDist;
        const float towardZ = toTargetZ * invDist;
        const float rightX = towardZ;
        const float rightZ = -towardX;
        const float distanceError =
            distance - kJoyConAutoMoveIdealDistance;
        const float distancePush =
            std::clamp(distanceError * 1.15f, -1.0f, 1.0f);
        const float orbitScale =
            distance < kJoyConAutoMoveNearDistance ||
                    distance > kJoyConAutoMoveFarDistance
                ? 0.35f
                : 1.0f;

        if (forceRangedReflectMove) {
            const float retreatTargetDistance = 6.8f;
            const float retreatNeed =
                std::clamp(retreatTargetDistance - distance, 0.0f, 1.0f);
            const float retreatSpeed = 4.8f * retreatNeed;
            const float strafeSpeed = 2.35f;
            worldMoveX = -towardX * retreatSpeed +
                         rightX * autoMoveOrbitDir_ * strafeSpeed;
            worldMoveZ = -towardZ * retreatSpeed +
                         rightZ * autoMoveOrbitDir_ * strafeSpeed;
        } else {
            worldMoveX = rightX * autoMoveOrbitDir_ *
                             kJoyConAutoMoveOrbitSpeed * orbitScale +
                         towardX * distancePush *
                             kJoyConAutoMoveDistanceSpeed;
            worldMoveZ = rightZ * autoMoveOrbitDir_ *
                             kJoyConAutoMoveOrbitSpeed * orbitScale +
                         towardZ * distancePush *
                             kJoyConAutoMoveDistanceSpeed;
        }
    }

    float speedScale = 1.0f;
    if (IsHunterGamepadAttacking()) {
        speedScale = weaponType_ == PlayerWeaponType::GreatSword ? 0.18f : 0.35f;
    }
    if (IsAttackRecovery()) {
        speedScale *= weaponType_ == PlayerWeaponType::GreatSword ? 0.24f : 0.38f;
    }

    if (dodgeTimer_ > 0.0f) {
        velocity_.x = dodgeDirection_.x * kDodgeSpeed;
        velocity_.y = 0.0f;
        velocity_.z = dodgeDirection_.y * kDodgeSpeed;
    } else if (useAutoMovement && !hasManualMove) {
        velocity_.x = worldMoveX * speedScale;
        velocity_.y = 0.0f;
        velocity_.z = worldMoveZ * speedScale;
    } else {
        velocity_.x = worldMoveX * moveSpeed_ * speedScale;
        velocity_.y = 0.0f;
        velocity_.z = worldMoveZ * moveSpeed_ * speedScale;
    }

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
    if (IsDamageInvulnerable()) {
        return;
    }

    hp_ -= damage * damageTakenScale_;
    if (hp_ < 0.0f) {
        hp_ = 0.0f;
    }
}

void Player::NotifyAttackHit(float damage) {
    RegisterAttackHit(damage);
}

void Player::NotifyAttackHit(size_t swordIndex, float damage) {
    RegisterAttackHit(damage);
    const float hitRecovery = GetHitConfirmRecoveryDuration();
    postSlashRecoveryTimer_ = 0.0f;

    if (swordIndex == 0) {
        leftSlashHitConfirmed_ = true;
        if (leftSlashRecoveryTimer_ > hitRecovery) {
            leftSlashRecoveryTimer_ = hitRecovery;
        }
    } else if (swordIndex == 1) {
        rightSlashHitConfirmed_ = true;
        if (rightSlashRecoveryTimer_ > hitRecovery) {
            rightSlashRecoveryTimer_ = hitRecovery;
        }
    }
}

void Player::RegisterAttackHit(float damage) {
    (void)damage;
    ResetOverSwing();
    if (swingComboTimer_ <= 0.0f) {
        swingComboCount_ = 0;
    }

    if (swingComboCount_ < kSwingComboMax) {
        ++swingComboCount_;
    }
    swingComboTimer_ = kSwingComboWindow;
}

void Player::RegisterAttackWhiff() {
    ResetOverSwing();
}

void Player::ResetOverSwing() {
    overSwingCount_ = 0;
    overSwingResetTimer_ = 0.0f;
}

void Player::UpdateOverSwing(float deltaTime) {
    if (overSwingResetTimer_ <= 0.0f) {
        overSwingCount_ = 0;
        return;
    }

    overSwingResetTimer_ -= deltaTime;
    if (overSwingResetTimer_ <= 0.0f) {
        overSwingResetTimer_ = 0.0f;
        overSwingCount_ = 0;
    }
}

float Player::GetSwingComboDamageMultiplier() const {
    return 1.0f;
}

void Player::UpdateSwingCombo(float deltaTime) {
    if (swingComboTimer_ <= 0.0f) {
        swingComboCount_ = 0;
        return;
    }

    swingComboTimer_ -= deltaTime;
    if (swingComboTimer_ <= 0.0f) {
        swingComboTimer_ = 0.0f;
        swingComboCount_ = 0;
    }
}

float Player::ComputeJoyConSwingDamageMultiplier(float angularVelocity) const {
    const float swingRatio =
        std::clamp((angularVelocity - 520.0f) / 1280.0f, 0.0f, 1.0f);
    return 1.0f + 0.55f * swingRatio;
}

void Player::AddKnockback(const DirectX::XMFLOAT3 &velocity) {
    if (IsDamageInvulnerable()) {
        return;
    }

    knockbackVelocity_.x += velocity.x;
    knockbackVelocity_.y += velocity.y;
    knockbackVelocity_.z += velocity.z;
}

bool Player::IsGuarding() const {
    return false;
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
    float multiplier = 10.5f;
    switch (weaponType_) {
    case PlayerWeaponType::Dual:
        multiplier = 9.0f;
        break;
    case PlayerWeaponType::GreatSword:
        multiplier = greatSwordFullChargeCounterReady_ ? 16.0f : 12.0f;
        break;
    case PlayerWeaponType::Standard:
    default:
        multiplier = 10.5f;
        break;
    }

    return multiplier;
}

float Player::GetCounterVulnerabilityDuration() const {
    if (weaponType_ == PlayerWeaponType::GreatSword &&
        greatSwordFullChargeCounterReady_) {
        return 1.65f;
    }
    return 1.35f;
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
    postSlashRecoveryTimer_ = 0.0f;
    leftSlashRecoveryTimer_ = 0.0f;
    rightSlashRecoveryTimer_ = 0.0f;
    leftSlashHitConfirmed_ = true;
    rightSlashHitConfirmed_ = true;
    ResetOverSwing();
    greatSwordFullChargeCounterReady_ = false;
}

void Player::NotifyCounterSuccess(size_t swordIndex) {
    if (swordIndex == 0) {
        leftSword_.NotifyCounterSuccess();
    } else if (swordIndex == 1) {
        rightSword_.NotifyCounterSuccess();
    }

    if (weaponType_ == PlayerWeaponType::GreatSword &&
        greatSwordFullChargeCounterReady_) {
        greatSwordCharge_ = 0.0f;
    }
    postSlashRecoveryTimer_ = 0.0f;
    if (swordIndex == 0) {
        leftSlashRecoveryTimer_ = 0.0f;
        leftSlashHitConfirmed_ = true;
    } else if (swordIndex == 1) {
        rightSlashRecoveryTimer_ = 0.0f;
        rightSlashHitConfirmed_ = true;
    }
    ResetOverSwing();
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
                               bool useHunterGamepadControls,
                               float deltaTime) {
    (void)input;
    leftSwordAttackDamage_ = 4.0f;
    rightSwordAttackDamage_ = 4.0f;
    auto applyJoyConSwingDamage = [&]() {
        if (hasLeftJoyCon && leftPose.isSlashMode) {
            leftSwordAttackDamage_ *= ComputeJoyConSwingDamageMultiplier(
                leftSwordJoyConController_.GetAngularVelocity());
        }
        if (hasRightJoyCon && rightPose.isSlashMode) {
            rightSwordAttackDamage_ *= ComputeJoyConSwingDamageMultiplier(
                rightSwordJoyConController_.GetAngularVelocity());
        }
    };

    if (weaponType_ == PlayerWeaponType::Standard) {
        leftPose = MakeIdleSwordPose(true);
        leftSwordAttackDamage_ = 0.0f;
        applyJoyConSwingDamage();
        return;
    }

    if (weaponType_ == PlayerWeaponType::Dual) {
        leftSwordAttackDamage_ = 3.0f;
        rightSwordAttackDamage_ = 3.0f;
        leftPose.isGuard = false;
        rightPose.isGuard = false;

        const bool singlePointerControl =
            !hasLeftJoyCon && !hasRightJoyCon &&
            (!useGamepadRightSword || useHunterGamepadControls);
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

        (void)deltaTime;
        applyJoyConSwingDamage();
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
            const bool canStartSwing =
                useHunterGamepadControls ||
                greatSwordCharge_ >= kGreatSwordMinSwingCharge;
            if (startsSwing && canStartSwing) {
                const float swingCharge =
                    useHunterGamepadControls
                        ? (std::max)(greatSwordCharge_,
                                     kGreatSwordMinSwingCharge)
                        : greatSwordCharge_;
                greatSwordSwingDamage_ =
                    ComputeGreatSwordAttackDamage(swingCharge);
                greatSwordFullChargeCounterReady_ = swingCharge >= 0.98f;
                greatSwordSwingTimer_ =
                    useHunterGamepadControls
                        ? (std::max)(hunterGamepadAttackDuration_ -
                                         hunterGamepadAttackTimer_,
                                     kGreatSwordSwingDuration)
                        : kGreatSwordSwingDuration;
                greatSwordCharge_ = 0.0f;
            }

            if (greatSwordSwingTimer_ <= 0.0f) {
                rightPose.isSlashMode = false;
                greatSwordFullChargeCounterReady_ = false;
            } else {
                rightSwordAttackDamage_ = greatSwordSwingDamage_;
            }
        } else if (greatSwordSwingTimer_ > 0.0f) {
            rightPose.isSlashMode = true;
            rightSwordAttackDamage_ = greatSwordSwingDamage_;
        }
        applyJoyConSwingDamage();
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

float Player::GetSlashRecoveryDuration(bool hitConfirmed) const {
    (void)hitConfirmed;
    return 0.0f;
}

float Player::GetHitConfirmRecoveryDuration() const {
    return 0.0f;
}

float Player::GetSlashRecoveryRatio(float timer) const {
    const float duration = GetSlashRecoveryDuration(false);
    if (duration <= 0.0f) {
        return 0.0f;
    }

    return std::clamp(timer / duration, 0.0f, 1.0f);
}

float Player::GetAttackRecoveryRatio() const {
    float ratio = 0.0f;
    if (kPostSlashRecoveryDuration > 0.0f) {
        ratio = (std::max)(ratio, std::clamp(postSlashRecoveryTimer_ /
                                                 kPostSlashRecoveryDuration,
                                             0.0f, 1.0f));
    }
    ratio = (std::max)(ratio, GetSlashRecoveryRatio(leftSlashRecoveryTimer_));
    ratio = (std::max)(ratio, GetSlashRecoveryRatio(rightSlashRecoveryTimer_));
    return ratio;
}

float Player::ComputeGreatSwordAttackDamage(float chargeRatio) const {
    const float t = std::clamp(chargeRatio, 0.0f, 1.0f);
    return 18.0f + 52.0f * t * t;
}

SwordPose Player::UpdateGamepadSword(Input *input, float deltaTime,
                                     const Transform &swordTransform) {
    UpdateGamepadSwordOrientation(input, deltaTime);
    UpdateGamepadSwordGuard(input);
    gamepadSwordState_.isCounter = false;
    UpdateGamepadSwordSlash(input, deltaTime);

    gamepadSwordState_.UpdateSlashDir(swordTransform);
    return gamepadSwordState_.ToPose();
}

SwordPose Player::UpdateHunterGamepadSword(Input *input, float deltaTime) {
    UpdateHunterGamepadSwordGuard(input);
    UpdateHunterGamepadSwordCounter(input);
    UpdateHunterGamepadSwordSlash(input, deltaTime);

    UpdateHunterGamepadSwordOrientation();
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

void Player::UpdateHunterGamepadSwordOrientation() {
    const float t = GetHunterGamepadAttackRatio();
    const float windup = std::clamp(t / 0.28f, 0.0f, 1.0f);
    const float swing = std::clamp((t - 0.28f) / 0.34f, 0.0f, 1.0f);
    const float follow = std::clamp((t - 0.62f) / 0.38f, 0.0f, 1.0f);

    float pitch = -0.08f;
    float yaw = 0.0f;
    float roll = 0.0f;

    if (gamepadSwordState_.isGuard) {
        pitch = -0.55f;
        yaw = 0.10f;
        roll = 0.20f;
    }

    if (IsHunterGamepadAttacking()) {
        switch (hunterGamepadAttackKind_) {
        case HunterGamepadAttackKind::SideLeft:
            pitch = -0.18f + 0.22f * windup - 0.12f * follow;
            yaw = 0.95f - 1.85f * swing + 0.28f * follow;
            roll = -0.75f + 1.40f * swing - 0.25f * follow;
            break;
        case HunterGamepadAttackKind::SideRight:
            pitch = -0.18f + 0.22f * windup - 0.12f * follow;
            yaw = -0.95f + 1.85f * swing - 0.28f * follow;
            roll = 0.75f - 1.40f * swing + 0.25f * follow;
            break;
        case HunterGamepadAttackKind::Overhead:
            pitch = -1.10f + 2.05f * swing - 0.40f * follow;
            yaw = 0.10f;
            roll = 0.25f - 0.35f * swing;
            break;
        case HunterGamepadAttackKind::Thrust:
            pitch = -0.16f;
            yaw = 0.05f;
            roll = -0.10f + 0.18f * windup;
            break;
        default:
            break;
        }
    }

    XMVECTOR q = XMQuaternionNormalize(
        XMQuaternionRotationRollPitchYaw(pitch, yaw, roll));
    XMStoreFloat4(&gamepadSwordState_.orientation, q);
}

void Player::UpdateHunterGamepadSwordGuard(Input *input) {
    gamepadSwordState_.isGuard =
        input->GetGamepadLeftTrigger() > 0.2f ||
        input->IsGamepadButtonPress(XINPUT_GAMEPAD_LEFT_SHOULDER);
}

void Player::UpdateHunterGamepadSwordCounter(Input *input) {
    const bool counterTriggered =
        gamepadSwordState_.isGuard &&
        (input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_Y) ||
         input->IsGamepadRightTriggerTrigger(0.2f));
    if (counterTriggered) {
        gamepadSwordState_.isCounter = true;
        gamepadSwordState_.counterTimer = SwordControllerState::kCounterFrames;
        hunterGamepadAttackKind_ = HunterGamepadAttackKind::None;
        hunterGamepadAttackTimer_ = 0.0f;
        hunterGamepadAttackDuration_ = 0.0f;
    }

    gamepadSwordState_.UpdateCounter();
}

void Player::UpdateHunterGamepadSwordSlash(Input *input, float deltaTime) {
    if (gamepadSwordState_.isCounter || gamepadSwordState_.isGuard) {
        gamepadSwordState_.isSlashMode = false;
        gamepadSwordState_.slashTimer = 0.0f;
        return;
    }

    if (!IsHunterGamepadAttacking()) {
        const HunterGamepadAttackKind attackKind =
            ReadHunterGamepadAttack(input);
        if (attackKind != HunterGamepadAttackKind::None) {
            BeginHunterGamepadAttack(attackKind);
        }
    }

    if (!IsHunterGamepadAttacking()) {
        gamepadSwordState_.isSlashMode = false;
        return;
    }

    hunterGamepadAttackTimer_ += deltaTime;
    gamepadSwordState_.isSlashMode = true;
    gamepadSwordState_.slashDir =
        GetHunterGamepadSlashDir(hunterGamepadAttackKind_);
    gamepadSwordState_.slashTimer = hunterGamepadAttackTimer_;

    if (hunterGamepadAttackTimer_ >= hunterGamepadAttackDuration_) {
        hunterGamepadAttackKind_ = HunterGamepadAttackKind::None;
        hunterGamepadAttackTimer_ = 0.0f;
        hunterGamepadAttackDuration_ = 0.0f;
        gamepadSwordState_.isSlashMode = false;
        gamepadSwordState_.slashTimer = 0.0f;
    }
}

void Player::BeginHunterGamepadAttack(HunterGamepadAttackKind attackKind) {
    hunterGamepadAttackKind_ = attackKind;
    hunterGamepadAttackTimer_ = 0.0f;
    hunterGamepadAttackDuration_ =
        GetHunterGamepadAttackDuration(attackKind);
    gamepadSwordState_.slashDir = GetHunterGamepadSlashDir(attackKind);
    if (attackKind == HunterGamepadAttackKind::SideLeft ||
        attackKind == HunterGamepadAttackKind::SideRight) {
        hunterNextSideSlashLeft_ = !hunterNextSideSlashLeft_;
    }
}

HunterGamepadAttackKind Player::ReadHunterGamepadAttack(Input *input) const {
    if (gamepadSwordState_.isGuard) {
        return HunterGamepadAttackKind::None;
    }

    if (input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_Y) ||
        input->IsGamepadRightTriggerTrigger(0.2f)) {
        return HunterGamepadAttackKind::Overhead;
    }

    if (input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_X)) {
        return hunterNextSideSlashLeft_ ? HunterGamepadAttackKind::SideLeft
                                        : HunterGamepadAttackKind::SideRight;
    }

    if (input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_A)) {
        return HunterGamepadAttackKind::Thrust;
    }

    return HunterGamepadAttackKind::None;
}

float Player::GetHunterGamepadAttackRatio() const {
    if (hunterGamepadAttackDuration_ <= 0.0f) {
        return 0.0f;
    }

    return std::clamp(hunterGamepadAttackTimer_ / hunterGamepadAttackDuration_,
                      0.0f, 1.0f);
}

float Player::GetHunterGamepadAttackDuration(
    HunterGamepadAttackKind attackKind) const {
    const bool isGreatSword = weaponType_ == PlayerWeaponType::GreatSword;
    switch (attackKind) {
    case HunterGamepadAttackKind::Overhead:
        return isGreatSword ? 0.72f : 0.46f;
    case HunterGamepadAttackKind::Thrust:
        return isGreatSword ? 0.58f : 0.34f;
    case HunterGamepadAttackKind::SideLeft:
    case HunterGamepadAttackKind::SideRight:
        return isGreatSword ? 0.64f : 0.38f;
    default:
        return 0.0f;
    }
}

DirectX::XMFLOAT2 Player::GetHunterGamepadSlashDir(Input *input) const {
    float dirX = 0.0f;
    float dirY = 0.0f;

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

    const float lenSq = dirX * dirX + dirY * dirY;
    if (lenSq <= 0.001f) {
        return {1.0f, 0.0f};
    }

    const float invLen = 1.0f / std::sqrt(lenSq);
    return {dirX * invLen, dirY * invLen};
}

DirectX::XMFLOAT2
Player::GetHunterGamepadSlashDir(HunterGamepadAttackKind attackKind) const {
    switch (attackKind) {
    case HunterGamepadAttackKind::SideLeft:
        return {-1.0f, 0.0f};
    case HunterGamepadAttackKind::SideRight:
        return {1.0f, 0.0f};
    case HunterGamepadAttackKind::Overhead:
        return {0.0f, -1.0f};
    case HunterGamepadAttackKind::Thrust:
        return {0.0f, 1.0f};
    default:
        return {1.0f, 0.0f};
    }
}

void Player::ToggleGamepadControlMode() {
    gamepadControlMode_ =
        gamepadControlMode_ == PlayerGamepadControlMode::Hunter
            ? PlayerGamepadControlMode::MotionSword
            : PlayerGamepadControlMode::Hunter;
    gamepadSwordState_ = {};
    gamepadSwordYaw_ = 0.0f;
    gamepadSwordPitch_ = 0.0f;
    hunterGamepadAttackKind_ = HunterGamepadAttackKind::None;
    hunterGamepadAttackTimer_ = 0.0f;
    hunterGamepadAttackDuration_ = 0.0f;
    hunterNextSideSlashLeft_ = true;
}

void Player::UpdateGamepadSwordGuard(Input *input) {
    gamepadSwordState_.isGuard =
        input->GetGamepadLeftTrigger() > 0.2f ||
        input->IsGamepadButtonPress(XINPUT_GAMEPAD_LEFT_SHOULDER);
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
