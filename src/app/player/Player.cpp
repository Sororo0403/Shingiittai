#include "Player.h"
#include "Input.h"
#include "ModelManager.h"
#include "SwordPose.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace {
constexpr float kPlayerVisualScaleMultiplier = 3.0f;
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
    hp_ = maxHp_;
    velocity_ = {0.0f, 0.0f, 0.0f};
    postSlashRecoveryTimer_ = 0.0f;
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
    const bool useMouseRightSword = !hasLeftJoyCon && !hasRightJoyCon;

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
    } else if (useMouseRightSword) {
        swordMouseController_.Update(input, deltaTime, rightSword_.GetTransform());
        rightPose = swordMouseController_.GetPose();
    }

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
    leftSwordSlashDir_ = leftPose.slashDir;
    rightSwordSlashDir_ = rightPose.slashDir;
    leftSwordVisible_ = hasLeftJoyCon;
    rightSwordVisible_ = hasRightJoyCon || useMouseRightSword;
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

#ifndef IMGUI_DISABLED
    ImGui::Begin("Player Combat");
    ImGui::Text("Guarding: %s", isGuarding_ ? "true" : "false");
    ImGui::Text("Left Slash : %s", leftSwordSlashMode_ ? "true" : "false");
    ImGui::Text("Left Dir   : %.2f, %.2f", leftSwordSlashDir_.x,
                leftSwordSlashDir_.y);
    ImGui::Text("Left Conn  : %s", leftJoyCon_.IsConnected() ? "true" : "false");
    ImGui::Text("Left Calib : %s", leftJoyCon_.IsCalibrating() ? "true" : "false");
    ImGui::Text("Left Still : %.2f", leftJoyCon_.GetStillTimer());
    ImGui::Text("Left CalTm : %.2f", leftJoyCon_.GetCalibrationTimer());
    ImGui::Text("Right Slash: %s", rightSwordSlashMode_ ? "true" : "false");
    ImGui::Text("Right Dir  : %.2f, %.2f", rightSwordSlashDir_.x,
                rightSwordSlashDir_.y);
    ImGui::Text("Right Conn : %s", rightJoyCon_.IsConnected() ? "true" : "false");
    ImGui::Text("Right Calib: %s",
                rightJoyCon_.IsCalibrating() ? "true" : "false");
    ImGui::Text("Right Still: %.2f", rightJoyCon_.GetStillTimer());
    ImGui::Text("Right CalTm: %.2f", rightJoyCon_.GetCalibrationTimer());
    ImGui::Text("Recovery : %.2f", postSlashRecoveryTimer_);
    ImGui::End();
#endif
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

PlayerTuningPreset Player::CreateTuningPreset() const {
    PlayerTuningPreset p{};
    p.maxHp = maxHp_;
    p.initialHp = hp_;
    p.moveSpeed = moveSpeed_;
    p.damageTakenScale = damageTakenScale_;
    return p;
}

void Player::ApplyTuningPreset(const PlayerTuningPreset &preset) {
    maxHp_ = preset.maxHp;
    if (maxHp_ < 1.0f) {
        maxHp_ = 1.0f;
    }

    moveSpeed_ = preset.moveSpeed;
    if (moveSpeed_ < 0.0f) {
        moveSpeed_ = 0.0f;
    }

    damageTakenScale_ = preset.damageTakenScale;
    if (damageTakenScale_ < 0.0f) {
        damageTakenScale_ = 0.0f;
    }

    hp_ = std::clamp(preset.initialHp, 0.0f, maxHp_);
}

void Player::ResetTuningPreset() { ApplyTuningPreset(PlayerTuningPreset{}); }

void Player::AddKnockback(const DirectX::XMFLOAT3 &velocity) {
    knockbackVelocity_.x += velocity.x;
    knockbackVelocity_.y += velocity.y;
    knockbackVelocity_.z += velocity.z;
}

bool Player::IsGuarding() const {
    return isGuarding_;
}

void Player::NotifyCounterSuccess() {
    if (leftSword_.IsCounterStance()) {
        leftSword_.NotifyCounterSuccess();
    }
    if (rightSword_.IsCounterStance()) {
        rightSword_.NotifyCounterSuccess();
    }
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
