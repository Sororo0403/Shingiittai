#include "Player.h"
#include "Input.h"
#include "ModelManager.h"
#include "SwordPose.h"
#include "imgui.h"
#include <cmath>

using namespace DirectX;

void Player::Initialize(uint32_t playerModelId, uint32_t swordModelId) {
    modelId_ = playerModelId;

    tf_.position = {0, 0, 0};
    tf_.scale = {1, 1, 1};
    tf_.rotation = {0, 0, 0, 1};

    leftJoyCon_.Initialize(true);
    rightJoyCon_.Initialize(false);

    leftSword_.Initialize(swordModelId);
    rightSword_.Initialize(swordModelId);
    leftSword_.Update(BuildSwordTransform(MakeIdleSwordPose(true), true));
    rightSword_.Update(BuildSwordTransform(MakeIdleSwordPose(false), false));
}

void Player::Update(Input *input, float deltaTime, const XMFLOAT3 &lookTarget) {
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

    UpdateMovement(input, deltaTime);
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

    leftSword_.Update(BuildSwordTransform(leftPose, true));
    rightSword_.Update(BuildSwordTransform(rightPose, false));

    leftSwordSlashMode_ = leftPose.isSlashMode;
    rightSwordSlashMode_ = rightPose.isSlashMode;
    leftSwordSlashDir_ = leftPose.slashDir;
    rightSwordSlashDir_ = rightPose.slashDir;
    leftSwordVisible_ = hasLeftJoyCon;
    rightSwordVisible_ = hasRightJoyCon || useMouseRightSword;
    isGuarding_ = leftPose.isGuard || rightPose.isGuard;

}

void Player::Draw(ModelManager *modelManager, const Camera &camera) {
    modelManager->Draw(modelId_, tf_, camera);
    if (leftSwordVisible_) {
        leftSword_.Draw(modelManager, camera);
    }
    if (rightSwordVisible_) {
        rightSword_.Draw(modelManager, camera);
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

void Player::UpdateMovement(Input *input, float deltaTime) {
    float moveX = 0.0f;
    float moveZ = 0.0f;

    if (input->IsKeyPress(DIK_W))
        moveZ += 1.0f;
    if (input->IsKeyPress(DIK_S))
        moveZ -= 1.0f;
    if (input->IsKeyPress(DIK_A))
        moveX -= 1.0f;
    if (input->IsKeyPress(DIK_D))
        moveX += 1.0f;

    tf_.position.x += moveX * moveSpeed_ * deltaTime;
    tf_.position.z += moveZ * moveSpeed_ * deltaTime;

    tf_.position.x += knockbackVelocity_.x * deltaTime;
    tf_.position.y += knockbackVelocity_.y * deltaTime;
    tf_.position.z += knockbackVelocity_.z * deltaTime;

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
    hp_ -= damage;
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
