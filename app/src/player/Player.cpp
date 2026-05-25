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
}

void Player::Initialize(uint32_t playerModelId, uint32_t swordModelId) {
    modelId_ = playerModelId;

    tf_.position = {0, 0, 0};
    tf_.scale = {1, 1, 1};
    tf_.rotation = {0, 0, 0, 1};

    leftJoyCon_.Initialize(true);
    rightJoyCon_.Initialize(false);

    leftSword_.Initialize(swordModelId);
    rightSword_.Initialize(swordModelId);
    hp_ = 100.0f;
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
    leftSword_.Update(BuildSwordTransform(MakeIdleSwordPose(true), true),
                      MakeIdleSwordPose(true), 0.0f);
    rightSword_.Update(BuildSwordTransform(MakeIdleSwordPose(false), false),
                       MakeIdleSwordPose(false), 0.0f);
}

void Player::SetInputCalibration(const SwordInputCalibration &calibration) {
    inputCalibration_ = calibration;
    swordUdpController_.SetCalibration(inputCalibration_);
    applyJoyConBaseOnNextUpdate_ = inputCalibration_.resetJoyConBaseOnStart;
}

void Player::Update(Input *input, float deltaTime, const XMFLOAT3 &lookTarget,
                    float cameraYaw, float controlDeltaTime,
                    bool suppressLookAt) {
    const float inputDeltaTime =
        controlDeltaTime > 0.0f ? controlDeltaTime : deltaTime;

    UpdateJoyConCalibrationInput(input, inputDeltaTime);

    if (input->IsGamepadConnected() &&
        input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_START)) {
        ToggleGamepadControlMode();
    }

    (void)cameraYaw;
    UpdateMovement(deltaTime, lookTarget);
    KeepDistanceFromTarget(lookTarget);
    if (!suppressLookAt) {
        LookAt(lookTarget);
    }

    const InputControlType controlType = inputCalibration_.controlType;
    const bool useKeyboardMouse =
        controlType == InputControlType::KeyboardMouse;
    const bool useJoyCon = controlType == InputControlType::JoyCon;
    const bool useUdpSword = controlType == InputControlType::Hand;
    const bool hasLeftJoyCon = useJoyCon && leftJoyCon_.IsConnected();
    const bool hasRightJoyCon = useJoyCon && rightJoyCon_.IsConnected();
    const bool useMouseRightSword = useKeyboardMouse;
    if (useUdpSword) {
        swordUdpController_.Update(inputDeltaTime);
    }

    SwordPose leftPose = MakeIdleSwordPose(true);
    if (hasLeftJoyCon) {
        leftSwordJoyConController_.Update(&leftJoyCon_, inputDeltaTime,
                                          leftSword_.GetTransform());
        leftPose = leftSwordJoyConController_.GetPose();
    } else if (useUdpSword && swordUdpController_.IsActive(1)) {
        leftPose = swordUdpController_.GetPose(1);
    } else if (useKeyboardMouse) {
        leftPose = UpdateKeyboardLeftSword(input, inputDeltaTime);
    }

    SwordPose rightPose = MakeIdleSwordPose(false);
    if (hasRightJoyCon) {
        rightSwordJoyConController_.Update(&rightJoyCon_, inputDeltaTime,
                                           rightSword_.GetTransform());
        rightPose = rightSwordJoyConController_.GetPose();
    } else if (useUdpSword) {
        if (swordUdpController_.IsActive(0)) {
            rightPose = swordUdpController_.GetPose(0);
        }
    } else if (useMouseRightSword) {
        swordMouseController_.Update(input, inputDeltaTime,
                                     rightSword_.GetTransform());
        rightPose = swordMouseController_.GetPose();
    }

    UpdateWeaponRules(input, leftPose, rightPose, hasLeftJoyCon,
                      hasRightJoyCon, useUdpSword || useKeyboardMouse,
                      deltaTime);

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

    leftSword_.Update(BuildSwordTransform(leftPose, true), leftPose,
                      inputDeltaTime);
    rightSword_.Update(BuildSwordTransform(rightPose, false), rightPose,
                       inputDeltaTime);

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

void Player::UpdateJoyConCalibrationInput(Input *, float deltaTime) {
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

    if (applyJoyConBaseOnNextUpdate_) {
        leftJoyCon_.SetBaseOrientation();
        rightJoyCon_.SetBaseOrientation();
        leftSwordJoyConController_.ResetTracking(&leftJoyCon_);
        rightSwordJoyConController_.ResetTracking(&rightJoyCon_);
        applyJoyConBaseOnNextUpdate_ = false;
    }
}

void Player::Draw(ModelManager *modelManager, const Camera &camera,
                  bool drawBody, bool forceOpaque, float visualScale) {
    Transform playerVisual = tf_;
    playerVisual.scale.x *= kPlayerVisualScaleMultiplier * visualScale;
    playerVisual.scale.y *= kPlayerVisualScaleMultiplier * visualScale;
    playerVisual.scale.z *= kPlayerVisualScaleMultiplier * visualScale;
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
        if (forceOpaque) {
            ModelDrawEffect opaqueEffect{};
            opaqueEffect.enabled = true;
            opaqueEffect.forceOpaqueMaterial = true;
            modelManager->SetDrawEffect(opaqueEffect);
        }
        modelManager->Draw(modelId_, playerVisual, camera);
        modelManager->ClearDrawEffect();
    }

    auto drawSword = [&](Sword &sword) {
        if (forceOpaque) {
            ModelDrawEffect opaqueEffect{};
            opaqueEffect.enabled = true;
            opaqueEffect.forceOpaqueMaterial = true;
            modelManager->SetDrawEffect(opaqueEffect);
        }
        sword.Draw(modelManager, camera, visualScale);
        if (forceOpaque) {
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
                                        float pushRatio) {
    LockPosition(position);
    SetYaw(yaw);

    bladeClashPoseActive_ = true;
    bladeClashPosePushRatio_ = std::clamp(pushRatio, 0.0f, 1.0f);

    const float push = bladeClashPosePushRatio_;
    auto makeFinishPose = [&](bool isLeft) {
        SwordPose pose = MakeIdleSwordPose(isLeft);
        const float side = isLeft ? -1.0f : 1.0f;
        const float swingOut = 0.72f + 0.28f * push;
        const float yawOut = side * (1.92f + 0.38f * swingOut);
        const float pitchDown = 0.22f + 0.20f * swingOut;
        const float rollThrough = side * (0.78f + 0.38f * swingOut);
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
    const float distanceError = distance - kJoyConAutoMoveIdealDistance;
    const float distancePush = std::clamp(distanceError * 1.15f, -1.0f, 1.0f);
    const float orbitScale =
        distance < kJoyConAutoMoveNearDistance ||
                distance > kJoyConAutoMoveFarDistance
            ? 0.35f
            : 1.0f;

    const float worldMoveX =
        rightX * autoMoveOrbitDir_ * kJoyConAutoMoveOrbitSpeed * orbitScale +
        towardX * distancePush * kJoyConAutoMoveDistanceSpeed;
    const float worldMoveZ =
        rightZ * autoMoveOrbitDir_ * kJoyConAutoMoveOrbitSpeed * orbitScale +
        towardZ * distancePush * kJoyConAutoMoveDistanceSpeed;

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

float Player::ComputeJoyConSwingDamageMultiplier(float angularVelocity) const {
    const float swingRatio =
        std::clamp((angularVelocity - 520.0f) / 1280.0f, 0.0f, 1.0f);
    return 1.0f + 0.55f * swingRatio;
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
                               SwordPose &rightPose, bool hasLeftJoyCon,
                               bool hasRightJoyCon, bool useDualUdpControls,
                               float deltaTime) {
    (void)input;
    leftSwordAttackDamage_ = kBaseSwordAttackDamage;
    rightSwordAttackDamage_ = kBaseSwordAttackDamage;
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

    const bool singlePointerControl =
        !hasLeftJoyCon && !hasRightJoyCon && !useDualUdpControls;
    if (singlePointerControl && rightPose.isSlashMode) {
        leftPose = MakeMirroredSwordPose(rightPose);
        rightPose.isSlashMode = false;
    }

    (void)deltaTime;
    applyJoyConSwingDamage();
}

void Player::ToggleGamepadControlMode() {
    useGamepadCameraLook_ = !useGamepadCameraLook_;
}
