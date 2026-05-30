#include "Player.h"
#include "Input.h"
#include "ModelManager.h"
#include "SwordPose.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace {
constexpr float kPlayerVisualScaleMultiplier = 1.45f;
constexpr float kBaseSwordAttackDamage = 8.0f;
constexpr float kRangedAttackWindupSeconds = 0.42f;
constexpr float kRangedAttackChargeSeconds = 1.15f;
constexpr float kRangedAttackRecoverySeconds = 0.55f;
constexpr float kRangedAttackCooldownSeconds = 0.35f;
constexpr float kHandRangedJoinDistance = 0.18f;
constexpr float kHandRangedReleaseDistance = 0.26f;
constexpr float kRangedHandAimYawRange = 0.95f;
}

void Player::Initialize(uint32_t playerModelId, uint32_t swordModelId) {
    modelId_ = playerModelId;

    tf_.position = {0, 0, 0};
    tf_.scale = {1, 1, 1};
    tf_.rotation = {0, 0, 0, 1};

    leftSword_.Initialize(swordModelId);
    rightSword_.Initialize(swordModelId);
    hp_ = 100.0f;
    damageFlashTimer_ = 0.0f;
    velocity_ = {0.0f, 0.0f, 0.0f};
    leftSwordAttackDamage_ = kBaseSwordAttackDamage;
    rightSwordAttackDamage_ = kBaseSwordAttackDamage;
    defeatPoseRatio_ = 0.0f;
    bladeClashPoseActive_ = false;
    bladeClashPosePushRatio_ = 0.5f;
    useGamepadCameraLook_ = true;
    keyboardLeftSwordState_ = {};
    autoMoveOrbitDir_ = 1.0f;
    autoMoveOrbitTimer_ = 0.0f;
    rangedAttackState_ = RangedAttackState::Idle;
    rangedAttackTimer_ = 0.0f;
    rangedChargeRatio_ = 0.0f;
    rangedAttackCooldown_ = 0.0f;
    rangedShotPending_ = false;
    pendingRangedShot_ = {};
    rangedAimDirection_ = {0.0f, 0.0f, 1.0f};
    handRangedIntentActive_ = false;
    leftSword_.Update(BuildSwordTransform(MakeIdleSwordPose(true), true),
                      MakeIdleSwordPose(true), 0.0f);
    rightSword_.Update(BuildSwordTransform(MakeIdleSwordPose(false), false),
                       MakeIdleSwordPose(false), 0.0f);
}

void Player::SetInputCalibration(const SwordInputCalibration &calibration) {
    inputCalibration_ = calibration;
    swordUdpController_.SetCalibration(inputCalibration_);
}

void Player::Update(Input *input, float deltaTime, const XMFLOAT3 &lookTarget,
                    float cameraYaw, float controlDeltaTime,
                    bool suppressLookAt, bool suppressMovement) {
    const float inputDeltaTime =
        controlDeltaTime > 0.0f ? controlDeltaTime : deltaTime;

    if (input->IsGamepadConnected() &&
        input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_START)) {
        ToggleGamepadControlMode();
    }

    if (suppressMovement) {
        velocity_ = {0.0f, 0.0f, 0.0f};
        knockbackVelocity_ = {0.0f, 0.0f, 0.0f};
    } else {
        UpdateMovement(deltaTime, lookTarget);
        KeepDistanceFromTarget(lookTarget);
    }
    if (!suppressLookAt) {
        LookAt(lookTarget);
    }
    if (damageFlashTimer_ > 0.0f) {
        damageFlashTimer_ = (std::max)(0.0f, damageFlashTimer_ - deltaTime);
    }

    const InputControlType controlType = inputCalibration_.controlType;
    const bool useKeyboardMouse =
        controlType == InputControlType::KeyboardMouse;
    const bool useUdpSword = controlType == InputControlType::Hand;
    const bool useMouseRightSword = useKeyboardMouse;
    if (useUdpSword) {
        swordUdpController_.Update(inputDeltaTime);
    }
    UpdateRangedAttack(input, inputDeltaTime, cameraYaw, useKeyboardMouse,
                       useUdpSword);

    SwordPose leftPose = MakeIdleSwordPose(true);
    if (useUdpSword && swordUdpController_.IsActive(0)) {
        leftPose = swordUdpController_.GetPose(0);
    } else if (useKeyboardMouse) {
        leftPose = UpdateKeyboardLeftSword(input, inputDeltaTime);
    }

    SwordPose rightPose = MakeIdleSwordPose(false);
    if (useUdpSword) {
        if (swordUdpController_.IsActive(1)) {
            rightPose = swordUdpController_.GetPose(1);
        }
    } else if (useMouseRightSword) {
        swordMouseController_.Update(input, inputDeltaTime,
                                     rightSword_.GetTransform());
        rightPose = swordMouseController_.GetPose();
    }

    UpdateWeaponRules(input, leftPose, rightPose,
                      useUdpSword || useKeyboardMouse, deltaTime);

    if (bladeClashPoseActive_) {
        const float push = std::clamp(bladeClashPosePushRatio_, 0.0f, 1.0f);
        const float leanPitch = -0.11f - 0.15f * push;
        auto makeClashOrientation = [&](bool isLeft) {
            const float inwardYaw = isLeft ? 0.28f + 0.10f * push
                                           : -0.28f - 0.10f * push;
            const float roll = isLeft ? -0.22f : 0.22f;
            XMVECTOR qPitch =
                XMQuaternionRotationAxis(XMVectorSet(1, 0, 0, 0), leanPitch);
            XMVECTOR qYaw =
                XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 0), inwardYaw);
            XMVECTOR qRoll =
                XMQuaternionRotationAxis(XMVectorSet(0, 0, 1, 0), roll);
            XMFLOAT4 result{};
            XMStoreFloat4(&result, XMQuaternionNormalize(XMQuaternionMultiply(
                                       XMQuaternionMultiply(qPitch, qYaw),
                                       qRoll)));
            return result;
        };
        leftPose.orientation = makeClashOrientation(true);
        rightPose.orientation = makeClashOrientation(false);
    }

    if (suppressCameraSwordSlash_ && controlType == InputControlType::Hand) {
        leftPose.isSlashMode = false;
        rightPose.isSlashMode = false;
    }
    if (IsChargingRangedAttack() ||
        rangedAttackState_ == RangedAttackState::Recovery) {
        leftPose.isSlashMode = false;
        rightPose.isSlashMode = false;
    }

    leftSword_.Update(BuildSwordTransform(leftPose, true), leftPose,
                      inputDeltaTime);
    rightSword_.Update(BuildSwordTransform(rightPose, false), rightPose,
                       inputDeltaTime);

    leftSwordSlashMode_ = leftPose.isSlashMode;
    rightSwordSlashMode_ = rightPose.isSlashMode;
    leftSwordVisible_ = true;
    rightSwordVisible_ = true;
}

void Player::UpdateDebugSwordPoses(const SwordPose &leftPoseInput,
                                   const SwordPose &rightPoseInput,
                                   float deltaTime,
                                   const XMFLOAT3 &position, float yaw) {
    tf_.position = position;
    SetYaw(yaw);
    velocity_ = {0.0f, 0.0f, 0.0f};
    knockbackVelocity_ = {0.0f, 0.0f, 0.0f};

    SwordPose leftPose = leftPoseInput;
    SwordPose rightPose = rightPoseInput;
    UpdateWeaponRules(nullptr, leftPose, rightPose, true, deltaTime);

    leftSword_.Update(BuildSwordTransform(leftPose, true), leftPose,
                      deltaTime);
    rightSword_.Update(BuildSwordTransform(rightPose, false), rightPose,
                       deltaTime);

    leftSwordSlashMode_ = leftPose.isSlashMode;
    rightSwordSlashMode_ = rightPose.isSlashMode;
    leftSwordVisible_ = true;
    rightSwordVisible_ = true;
}

void Player::UpdateDemo(float deltaTime, const XMFLOAT3 &lookTarget) {
    UpdateMovement(deltaTime, lookTarget);
    KeepDistanceFromTarget(lookTarget);
    LookAt(lookTarget);

    SwordPose leftPose = MakeIdleSwordPose(true);
    SwordPose rightPose = MakeIdleSwordPose(false);
    leftPose.isSlashMode = false;
    rightPose.isSlashMode = false;

    leftSword_.Update(BuildSwordTransform(leftPose, true), leftPose,
                      deltaTime);
    rightSword_.Update(BuildSwordTransform(rightPose, false), rightPose,
                       deltaTime);

    leftSwordSlashMode_ = false;
    rightSwordSlashMode_ = false;
    leftSwordVisible_ = true;
    rightSwordVisible_ = true;
}

void Player::Draw(ModelManager *modelManager, const Camera &camera,
                  bool drawBody, bool forceOpaque, float visualScale) {
    const float flashRatio =
        damageFlashDuration_ > 0.0001f
            ? std::clamp(damageFlashTimer_ / damageFlashDuration_, 0.0f, 1.0f)
            : 0.0f;
    const float flashGate =
        flashRatio > 0.0f
            ? (std::sinf((damageFlashDuration_ - damageFlashTimer_) * 92.0f) >
                       -0.18f
                   ? 1.0f
                   : 0.0f)
            : 0.0f;
    const bool isDamageFlashing = flashGate > 0.0f;

    auto makeFlashEffect = [&]() {
        ModelDrawEffect effect{};
        effect.enabled = true;
        effect.forceOpaqueMaterial = forceOpaque;
        effect.color = {1.0f, 0.96f, 0.82f, 0.92f};
        effect.intensity = 0.72f + 0.42f * flashRatio;
        effect.fresnelPower = 1.45f;
        effect.noiseAmount = 0.06f;
        effect.time = damageFlashDuration_ - damageFlashTimer_;
        effect.surfaceTint = 0.66f + 0.28f * flashRatio;
        effect.alphaBoost = 0.80f;
        return effect;
    };

    Transform playerVisual = tf_;
    playerVisual.scale.x *= kPlayerVisualScaleMultiplier * visualScale;
    playerVisual.scale.y *= kPlayerVisualScaleMultiplier * visualScale;
    playerVisual.scale.z *= kPlayerVisualScaleMultiplier * visualScale;
    if (bladeClashPoseActive_) {
        const float push = std::clamp(bladeClashPosePushRatio_, 0.0f, 1.0f);
        const float leanDirection = 1.0f;
        const float lean =
            (bladeClashPoseForwardLean_ ? 0.28f + 0.46f * push
                                        : 0.12f + 0.34f * push) *
            leanDirection;
        XMVECTOR baseRot = XMLoadFloat4(&playerVisual.rotation);
        XMVECTOR qLean =
            XMQuaternionRotationAxis(XMVectorSet(1, 0, 0, 0), lean);
        XMStoreFloat4(&playerVisual.rotation,
                      XMQuaternionNormalize(XMQuaternionMultiply(qLean, baseRot)));
        playerVisual.position.y -=
            (bladeClashPoseForwardLean_ ? 0.04f : 0.10f) * push;
        const float forwardShift = bladeClashPoseForwardLean_ ? 0.18f : 0.10f;
        playerVisual.position.z += std::cosf(yaw_) * forwardShift * push;
        playerVisual.position.x += std::sinf(yaw_) * forwardShift * push;
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
        if (isDamageFlashing) {
            modelManager->SetDrawEffect(makeFlashEffect());
        } else if (forceOpaque) {
            ModelDrawEffect opaqueEffect{};
            opaqueEffect.enabled = true;
            opaqueEffect.forceOpaqueMaterial = true;
            modelManager->SetDrawEffect(opaqueEffect);
        }
        modelManager->Draw(modelId_, playerVisual, camera);
        modelManager->ClearDrawEffect();
    }

    auto drawSword = [&](Sword &sword) {
        if (isDamageFlashing) {
            modelManager->SetDrawEffect(makeFlashEffect());
        } else if (forceOpaque) {
            ModelDrawEffect opaqueEffect{};
            opaqueEffect.enabled = true;
            opaqueEffect.forceOpaqueMaterial = true;
            modelManager->SetDrawEffect(opaqueEffect);
        }
        sword.Draw(modelManager, camera, visualScale);
        if (isDamageFlashing || forceOpaque) {
            modelManager->ClearDrawEffect();
        }
    };

    if (leftSwordVisible_) {
        drawSword(leftSword_);
    }
    if (rightSwordVisible_) {
        drawSword(rightSword_);
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

void Player::SetYaw(float yaw) {
    yaw_ = yaw;
    XMVECTOR q = XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 0), yaw_);
    XMStoreFloat4(&tf_.rotation, q);
}

void Player::SetCinematicBladeClashPose(const XMFLOAT3 &position, float yaw,
                                        float pushRatio, bool forwardLean) {
    LockPosition(position);
    SetYaw(yaw);

    bladeClashPoseActive_ = true;
    bladeClashPosePushRatio_ = std::clamp(pushRatio, 0.0f, 1.0f);
    bladeClashPoseForwardLean_ = forwardLean;

    const float push = bladeClashPosePushRatio_;
    auto makeFinishPose = [&](bool isLeft) {
        SwordPose pose = MakeIdleSwordPose(isLeft);
        const float side = isLeft ? -1.0f : 1.0f;
        if (forwardLean) {
            XMVECTOR qPitch =
                XMQuaternionRotationAxis(XMVectorSet(1, 0, 0, 0), -0.24f);
            XMVECTOR qYaw = XMQuaternionRotationAxis(
                XMVectorSet(0, 1, 0, 0), 3.14159265f - side * 0.72f);
            XMVECTOR qRoll = XMQuaternionRotationAxis(
                XMVectorSet(0, 0, 1, 0), side * 0.58f);
            XMStoreFloat4(&pose.orientation,
                          XMQuaternionNormalize(XMQuaternionMultiply(
                              XMQuaternionMultiply(qPitch, qYaw), qRoll)));
            pose.isSlashMode = false;
            return pose;
        }
        const float finish = std::clamp((push - 0.52f) / 0.48f, 0.0f, 1.0f);
        const float swingOut = 0.72f + 0.28f * push;
        const float yawOut =
            side * (1.92f + 0.38f * swingOut) * (1.0f - finish) +
            (3.14159265f - side * (0.26f + 0.16f * push)) * finish;
        const float pitchDown =
            (0.22f + 0.20f * swingOut) * (1.0f - finish) +
            (-0.36f - 0.18f * push) * finish;
        const float rollThrough =
            side * ((0.78f + 0.38f * swingOut) * (1.0f - finish) +
                    (1.14f + 0.28f * push) * finish);
        XMVECTOR qPitch =
            XMQuaternionRotationAxis(XMVectorSet(1, 0, 0, 0), pitchDown);
        XMVECTOR qYaw =
            XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 0), yawOut);
        XMVECTOR qRoll =
            XMQuaternionRotationAxis(XMVectorSet(0, 0, 1, 0), rollThrough);
        XMStoreFloat4(&pose.orientation,
                      XMQuaternionNormalize(XMQuaternionMultiply(
                          XMQuaternionMultiply(qPitch, qYaw), qRoll)));
        pose.isSlashMode = false;
        return pose;
    };

    SwordPose leftPose = makeFinishPose(true);
    SwordPose rightPose = makeFinishPose(false);
    leftSword_.Update(BuildSwordTransform(leftPose, true), leftPose, 0.0f);
    rightSword_.Update(BuildSwordTransform(rightPose, false), rightPose, 0.0f);
    leftSwordVisible_ = true;
    rightSwordVisible_ = true;
    leftSwordSlashMode_ = false;
    rightSwordSlashMode_ = false;
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

void Player::UpdateMovement(float deltaTime, const XMFLOAT3 &lookTarget) {
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
    const float distanceError = distance - kAutoMoveIdealDistance;
    const float distancePush = std::clamp(distanceError * 1.15f, -1.0f, 1.0f);
    const float orbitScale =
        distance < kAutoMoveNearDistance ||
                distance > kAutoMoveFarDistance
            ? 0.35f
            : 1.0f;

    const float worldMoveX =
        (rightX * autoMoveOrbitDir_ * kAutoMoveOrbitSpeed * orbitScale +
         towardX * distancePush * kAutoMoveDistanceSpeed) *
        movementSpeedMultiplier_;
    const float worldMoveZ =
        (rightZ * autoMoveOrbitDir_ * kAutoMoveOrbitSpeed * orbitScale +
         towardZ * distancePush * kAutoMoveDistanceSpeed) *
        movementSpeedMultiplier_;

    velocity_.x = worldMoveX;
    velocity_.y = 0.0f;
    velocity_.z = worldMoveZ;
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

void Player::AddKnockback(const DirectX::XMFLOAT3 &velocity) {
    knockbackVelocity_.x += velocity.x;
    knockbackVelocity_.y += velocity.y;
    knockbackVelocity_.z += velocity.z;
}

float Player::GetCounterDamageMultiplier() const {
    return 7.0f;
}

float Player::GetCounterVulnerabilityDuration() const { return 1.35f; }

float Player::TakeDamage(float damage) {
    if (damage <= 0.0f || hp_ <= 0.0f) {
        return 0.0f;
    }

    const float previousHp = hp_;
    hp_ = (std::max)(0.0f, hp_ - damage);
    const float appliedDamage = previousHp - hp_;
    if (appliedDamage > 0.0f) {
        damageFlashTimer_ = damageFlashDuration_;
    }
    return appliedDamage;
}

Player::ChargedShot Player::ConsumeChargedShot() {
    if (!rangedShotPending_) {
        return {};
    }
    rangedShotPending_ = false;
    return pendingRangedShot_;
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

SwordPose Player::UpdateKeyboardLeftSword(Input *input, float deltaTime) {
    float dirX = 0.0f;
    float dirY = 0.0f;
    if (input->IsKeyPress(DIK_A)) {
        dirX -= 1.0f;
    }
    if (input->IsKeyPress(DIK_D)) {
        dirX += 1.0f;
    }
    if (input->IsKeyPress(DIK_W)) {
        dirY += 1.0f;
    }
    if (input->IsKeyPress(DIK_S)) {
        dirY -= 1.0f;
    }

    const bool slashTriggered =
        input->IsKeyTrigger(DIK_A) || input->IsKeyTrigger(DIK_D) ||
        input->IsKeyTrigger(DIK_W) || input->IsKeyTrigger(DIK_S);
    const float lenSq = dirX * dirX + dirY * dirY;
    if (lenSq > 0.0001f) {
        const float invLen = 1.0f / std::sqrt(lenSq);
        dirX *= invLen;
        dirY *= invLen;
        keyboardLeftSwordState_.slashDir = {dirX, dirY};

        const float yaw = dirX * 0.82f;
        const float pitch = -dirY * 0.72f;
        XMVECTOR qYaw = XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 0), yaw);
        XMVECTOR qPitch =
            XMQuaternionRotationAxis(XMVectorSet(1, 0, 0, 0), pitch);
        XMVECTOR q = XMQuaternionNormalize(XMQuaternionMultiply(qPitch, qYaw));
        XMStoreFloat4(&keyboardLeftSwordState_.orientation, q);
    }

    keyboardLeftSwordState_.UpdateSlash(
        slashTriggered ? SwordControllerState::kSlashThreshold + 1.0f : 0.0f,
        deltaTime);
    return keyboardLeftSwordState_.ToPose();
}

void Player::UpdateWeaponRules(Input *input, SwordPose &leftPose,
                               SwordPose &rightPose, bool useDualControls,
                               float deltaTime) {
    (void)input;
    leftSwordAttackDamage_ = kBaseSwordAttackDamage;
    rightSwordAttackDamage_ = kBaseSwordAttackDamage;

    const bool singlePointerControl = !useDualControls;
    if (singlePointerControl && rightPose.isSlashMode) {
        leftPose = MakeMirroredSwordPose(rightPose);
        rightPose.isSlashMode = false;
    }

    (void)deltaTime;
}

void Player::UpdateRangedAttack(Input *input, float deltaTime, float cameraYaw,
                                bool useKeyboardMouse, bool useUdpSword) {
    if (rangedAttackCooldown_ > 0.0f) {
        rangedAttackCooldown_ =
            (std::max)(0.0f, rangedAttackCooldown_ - deltaTime);
    }

    bool intentHeld = false;
    bool intentPressed = false;
    bool intentReleased = false;
    DirectX::XMFLOAT3 desiredAim{std::sinf(cameraYaw), 0.0f,
                                 std::cosf(cameraYaw)};

    if (useKeyboardMouse && input != nullptr) {
        intentHeld = input->IsMousePress(1);
        intentPressed = input->IsMouseTrigger(1);
        intentReleased = input->IsMouseRelease(1);
    } else if (useUdpSword) {
        const auto left = swordUdpController_.GetDebugHandState(0);
        const auto right = swordUdpController_.GetDebugHandState(1);
        const bool bothHands = left.active && right.active;
        const float dx = left.calibratedPalm.x - right.calibratedPalm.x;
        const float dy = left.calibratedPalm.y - right.calibratedPalm.y;
        const float distance = std::sqrt(dx * dx + dy * dy);
        const bool joined =
            bothHands &&
            (handRangedIntentActive_ ? distance < kHandRangedReleaseDistance
                                     : distance < kHandRangedJoinDistance);
        intentHeld = joined;
        intentPressed = joined && !handRangedIntentActive_;
        intentReleased = !joined && handRangedIntentActive_;
        handRangedIntentActive_ = joined;

        if (bothHands) {
            const float aimX = std::clamp(
                ((left.calibratedPalm.x + right.calibratedPalm.x) * 0.5f -
                 0.5f) *
                    2.0f,
                -1.0f, 1.0f);
            const float yaw = yaw_ + aimX * kRangedHandAimYawRange;
            desiredAim = {std::sinf(yaw), 0.0f, std::cosf(yaw)};
        }
    } else {
        handRangedIntentActive_ = false;
    }

    const float lenSq = desiredAim.x * desiredAim.x +
                        desiredAim.y * desiredAim.y +
                        desiredAim.z * desiredAim.z;
    if (lenSq > 0.0001f) {
        const float invLen = 1.0f / std::sqrt(lenSq);
        rangedAimDirection_ = {desiredAim.x * invLen, desiredAim.y * invLen,
                               desiredAim.z * invLen};
    }

    switch (rangedAttackState_) {
    case RangedAttackState::Idle:
        rangedChargeRatio_ = 0.0f;
        if (intentPressed && rangedAttackCooldown_ <= 0.0f) {
            rangedAttackState_ = RangedAttackState::Windup;
            rangedAttackTimer_ = 0.0f;
        }
        break;
    case RangedAttackState::Windup:
        rangedAttackTimer_ += deltaTime;
        if (!intentHeld || intentReleased) {
            rangedAttackState_ = RangedAttackState::Recovery;
            rangedAttackTimer_ = 0.0f;
            rangedChargeRatio_ = 0.0f;
            break;
        }
        if (rangedAttackTimer_ >= kRangedAttackWindupSeconds) {
            rangedAttackState_ = RangedAttackState::Charging;
            rangedAttackTimer_ = 0.0f;
        }
        break;
    case RangedAttackState::Charging:
        rangedAttackTimer_ += deltaTime;
        rangedChargeRatio_ =
            std::clamp(rangedAttackTimer_ / kRangedAttackChargeSeconds, 0.0f,
                       1.0f);
        if (!intentHeld || intentReleased) {
            if (rangedChargeRatio_ >= 1.0f) {
                pendingRangedShot_.fired = true;
                pendingRangedShot_.origin = ComputeRangedAttackOrigin();
                pendingRangedShot_.direction = rangedAimDirection_;
                pendingRangedShot_.chargeRatio = rangedChargeRatio_;
                rangedShotPending_ = true;
            }
            rangedAttackState_ = RangedAttackState::Recovery;
            rangedAttackTimer_ = 0.0f;
        }
        break;
    case RangedAttackState::Recovery:
        rangedAttackTimer_ += deltaTime;
        if (rangedAttackTimer_ >= kRangedAttackRecoverySeconds) {
            rangedAttackState_ = RangedAttackState::Idle;
            rangedAttackTimer_ = 0.0f;
            rangedChargeRatio_ = 0.0f;
            rangedAttackCooldown_ = kRangedAttackCooldownSeconds;
        }
        break;
    }
}

DirectX::XMFLOAT3 Player::ComputeRangedAttackOrigin() const {
    DirectX::XMVECTOR playerRot =
        DirectX::XMQuaternionNormalize(DirectX::XMLoadFloat4(&tf_.rotation));
    DirectX::XMVECTOR playerPos = DirectX::XMLoadFloat3(&tf_.position);
    DirectX::XMVECTOR local =
        DirectX::XMVectorSet(0.0f, kHandHeight + 0.18f, kArmLength + 0.35f, 0.0f);
    DirectX::XMFLOAT3 origin{};
    DirectX::XMStoreFloat3(&origin,
                           DirectX::XMVectorAdd(
                               playerPos, DirectX::XMVector3Rotate(local,
                                                                    playerRot)));
    return origin;
}

void Player::ToggleGamepadControlMode() {
    useGamepadCameraLook_ = !useGamepadCameraLook_;
}
