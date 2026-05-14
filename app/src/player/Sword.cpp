#include "Sword.h"
#include "Camera.h"
#include "ModelManager.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace {
constexpr float kSwordVisualScaleMultiplier = 3.0f;
constexpr float kSlashFollowThroughDuration = 0.16f;
constexpr float kSlashFollowThroughPixelsPerSecond = 3000.0f;
constexpr float kSlashFollowThroughMouseSensitivity = 0.003f;
constexpr float kSlashFollowThroughPivotDistance = 1.25f;
constexpr float kSlashFollowThroughReturnSpeed = 10.0f;
constexpr float kSlashFollowThroughRollScale = 0.48f;
constexpr float kSlashFollowThroughSurgePerRadian = 0.20f;
constexpr float kSlashFollowThroughMaxSurge = 0.26f;
constexpr float kSlashFollowThroughMaxStretch = 0.14f;
constexpr float kSlashFollowThroughMinDirLengthSq = 0.01f;
constexpr float kSlashFollowThroughMinAngle = 0.001f;

XMFLOAT3 GetBladePointWorld(const Transform &tf, float forwardDistance) {
    XMVECTOR pos = XMLoadFloat3(&tf.position);
    XMVECTOR rot = XMLoadFloat4(&tf.rotation);
    rot = XMQuaternionNormalize(rot);

    XMVECTOR forward = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), rot);
    XMVECTOR point = pos + forward * forwardDistance;

    XMFLOAT3 result{};
    XMStoreFloat3(&result, point);
    return result;
}
}

void Sword::Initialize(uint32_t modelId) {
    modelId_ = modelId;

    tf_.position = {0, 0, 0};
    tf_.scale = {1, 1, 1};
    tf_.rotation = {0, 0, 0, 1};

    prevIsCounter_ = false;
    isCounterStance_ = false;
    justCountered_ = false;
    justCounterFailed_ = false;
    justCounterEarly_ = false;
    justCounterLate_ = false;
    counterStateTimer_ = 0.0f;
    counterAxis_ = SwordCounterAxis::None;
    slashFollowThroughTimer_ = 0.0f;
    slashFollowThroughStarted_ = false;
    slashFollowThroughDir_ = {};
    slashFollowThroughAngles_ = {};
    slashFollowThroughRoll_ = 0.0f;
    slashFollowThroughSurge_ = 0.0f;
}

void Sword::Update(const Transform &transform, const SwordPose &pose,
                   float deltaTime) {
    tf_ = transform;
    isSlashMode_ = pose.isSlashMode;
    isGuard_ = pose.isGuard;
    isCounter_ = pose.isCounter;
    isMouse = pose.isMouse;
    isJoyCon = pose.isJoyCon;
    slashDir_ = pose.slashDir;
    orientation_ = pose.orientation;
    UpdateSlashFollowThrough(deltaTime);
    UpdateCounterObservation(deltaTime);
}

void Sword::SetRecoveryReaction(float reaction) {
    recoveryReaction_ = std::clamp(reaction, 0.0f, 1.0f);
}

OBB Sword::GetOBB() const {
    OBB box;

    float hitBoxDepth = size_.z;
    float forwardOffset = kSwordLength * 0.5f;
    if (isSlashMode_) {
        hitBoxDepth += kSlashHitDepthExtension;
        forwardOffset += kSlashHitDepthExtension * 0.5f;
    }

    XMVECTOR pos = XMLoadFloat3(&tf_.position);
    XMVECTOR rot = XMLoadFloat4(&tf_.rotation);
    XMVECTOR forward = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), rot);
    XMVECTOR center = pos + forward * forwardOffset;

    XMStoreFloat3(&box.center, center);
    box.size = size_;
    box.size.z = hitBoxDepth;
    box.rotation = tf_.rotation;
    return box;
}

OBB Sword::GetCounterOBB() const {
    OBB box;

    XMVECTOR pos = XMLoadFloat3(&tf_.position);
    XMVECTOR rot = XMLoadFloat4(&tf_.rotation);

    XMVECTOR forward = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), rot);
    XMVECTOR center = pos + forward * 0.9f;

    XMStoreFloat3(&box.center, center);

    box.size = counterSize_;
    box.rotation = tf_.rotation;

    return box;
}

DirectX::XMFLOAT3 Sword::GetBladeRootWorld() const {
    DirectX::XMVECTOR pos = DirectX::XMLoadFloat3(&tf_.position);
    DirectX::XMVECTOR rot = DirectX::XMLoadFloat4(&tf_.rotation);
    rot = DirectX::XMQuaternionNormalize(rot);

    DirectX::XMVECTOR forward =
        DirectX::XMVector3Rotate(DirectX::XMVectorSet(0, 0, 1, 0), rot);

    // 根元をそのまま使うと扇形が大きくなりすぎるため、
    // 少し剣先側に寄せる
    DirectX::XMVECTOR root = pos + forward * 0.25f;

    DirectX::XMFLOAT3 result{};
    DirectX::XMStoreFloat3(&result, root);
    return result;
}

DirectX::XMFLOAT3 Sword::GetBladeTipWorld() const {
    DirectX::XMVECTOR pos = DirectX::XMLoadFloat3(&tf_.position);
    DirectX::XMVECTOR rot = DirectX::XMLoadFloat4(&tf_.rotation);
    rot = DirectX::XMQuaternionNormalize(rot);

    DirectX::XMVECTOR forward =
        DirectX::XMVector3Rotate(DirectX::XMVectorSet(0, 0, 1, 0), rot);

    // 以前の 1.75f は長すぎる可能性が高い。
    // まずは見た目確認用に短めへ。
    DirectX::XMVECTOR tip = pos + forward * 1.05f;

    DirectX::XMFLOAT3 result{};
    DirectX::XMStoreFloat3(&result, tip);
    return result;
}

DirectX::XMFLOAT3 Sword::GetBladeCenterWorld() const {
    const XMFLOAT3 rootPos = GetBladeRootWorld();
    const XMFLOAT3 tipPos = GetBladeTipWorld();

    XMVECTOR root = XMLoadFloat3(&rootPos);
    XMVECTOR tip = XMLoadFloat3(&tipPos);

    XMFLOAT3 result{};
    XMStoreFloat3(&result, (root + tip) * 0.5f);
    return result;
}

DirectX::XMFLOAT3 Sword::GetVisualBladeRootWorld() const {
    return GetBladePointWorld(BuildVisualTransform(), 0.25f);
}

DirectX::XMFLOAT3 Sword::GetVisualBladeTipWorld() const {
    return GetBladePointWorld(BuildVisualTransform(), 1.05f);
}

Transform Sword::BuildVisualTransform() const {
    Transform drawTransform = tf_;
    drawTransform.scale.x *= kSwordVisualScaleMultiplier;
    drawTransform.scale.y *= kSwordVisualScaleMultiplier;
    drawTransform.scale.z *= kSwordVisualScaleMultiplier;
    if (recoveryReaction_ > 0.0f) {
        const float phase = (1.0f - recoveryReaction_) * 36.0f;
        const float pulse = std::sinf(phase);
        const float scaleBoost = 1.0f + 0.08f * recoveryReaction_ * pulse;
        drawTransform.scale.x *= scaleBoost;
        drawTransform.scale.y *= scaleBoost;
        drawTransform.scale.z *= 1.0f + 0.12f * recoveryReaction_;
    }
    ApplySlashFollowThrough(drawTransform);
    return drawTransform;
}

void Sword::Draw(ModelManager *modelManager, const Camera &camera) {
    Transform drawTransform = BuildVisualTransform();

    if (const Model *model = modelManager->GetModel(modelId_)) {
        modelManager->GetRenderer()->Draw(*model, drawTransform, camera);
    }
}

void Sword::UpdateSlashFollowThrough(float deltaTime) {
    const float dirLenSq =
        slashDir_.x * slashDir_.x + slashDir_.y * slashDir_.y;
    if (isSlashMode_ && !slashFollowThroughStarted_ &&
        dirLenSq > kSlashFollowThroughMinDirLengthSq) {
        const float invLen = 1.0f / std::sqrt(dirLenSq);
        slashFollowThroughDir_ = {slashDir_.x * invLen,
                                  -slashDir_.y * invLen};
        slashFollowThroughTimer_ = kSlashFollowThroughDuration;
        slashFollowThroughStarted_ = true;
        slashFollowThroughAngles_ = {};
        slashFollowThroughRoll_ = 0.0f;
        slashFollowThroughSurge_ = 0.0f;
    }

    auto returnFollowThrough = [&]() {
        const float decay =
            std::clamp(1.0f - kSlashFollowThroughReturnSpeed * deltaTime,
                       0.0f, 1.0f);
        slashFollowThroughAngles_.x *= decay;
        slashFollowThroughAngles_.y *= decay;
        slashFollowThroughRoll_ *= decay;
        slashFollowThroughSurge_ *= decay;
        if (std::fabs(slashFollowThroughAngles_.x) <
            kSlashFollowThroughMinAngle) {
            slashFollowThroughAngles_.x = 0.0f;
        }
        if (std::fabs(slashFollowThroughAngles_.y) <
            kSlashFollowThroughMinAngle) {
            slashFollowThroughAngles_.y = 0.0f;
        }
        if (std::fabs(slashFollowThroughRoll_) < kSlashFollowThroughMinAngle) {
            slashFollowThroughRoll_ = 0.0f;
        }
        if (slashFollowThroughSurge_ < kSlashFollowThroughMinAngle) {
            slashFollowThroughSurge_ = 0.0f;
        }
    };

    if (!isSlashMode_) {
        slashFollowThroughStarted_ = false;
        returnFollowThrough();
        slashFollowThroughTimer_ = 0.0f;
        return;
    }

    if (slashFollowThroughTimer_ <= 0.0f) {
        returnFollowThrough();
        return;
    }

    const float ratio =
        std::clamp(slashFollowThroughTimer_ / kSlashFollowThroughDuration,
                   0.0f, 1.0f);
    const float angleStep = kSlashFollowThroughPixelsPerSecond *
                            kSlashFollowThroughMouseSensitivity * ratio *
                            deltaTime;
    slashFollowThroughAngles_.x += slashFollowThroughDir_.x * angleStep;
    slashFollowThroughAngles_.y += slashFollowThroughDir_.y * angleStep;
    slashFollowThroughRoll_ +=
        (-slashFollowThroughDir_.x + slashFollowThroughDir_.y * 0.25f) *
        angleStep * kSlashFollowThroughRollScale;
    slashFollowThroughSurge_ =
        (std::min)(slashFollowThroughSurge_ +
                       angleStep * kSlashFollowThroughSurgePerRadian,
                   kSlashFollowThroughMaxSurge);

    slashFollowThroughTimer_ -= deltaTime;
    if (slashFollowThroughTimer_ < 0.0f) {
        slashFollowThroughTimer_ = 0.0f;
    }
}

void Sword::ApplySlashFollowThrough(Transform &drawTransform) const {
    if (slashFollowThroughAngles_.x == 0.0f &&
        slashFollowThroughAngles_.y == 0.0f &&
        slashFollowThroughRoll_ == 0.0f &&
        slashFollowThroughSurge_ == 0.0f) {
        return;
    }

    const float yaw = slashFollowThroughAngles_.x;
    const float pitch = slashFollowThroughAngles_.y;
    const float roll = slashFollowThroughRoll_;

    XMVECTOR qYaw = XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 0), yaw);
    XMVECTOR qPitch =
        XMQuaternionRotationAxis(XMVectorSet(1, 0, 0, 0), pitch);
    XMVECTOR qRoll = XMQuaternionRotationAxis(XMVectorSet(0, 0, 1, 0), roll);
    XMVECTOR followRot = XMQuaternionNormalize(
        XMQuaternionMultiply(XMQuaternionMultiply(qRoll, qPitch), qYaw));
    XMVECTOR baseRot = XMLoadFloat4(&drawTransform.rotation);
    XMVECTOR finalRot =
        XMQuaternionNormalize(XMQuaternionMultiply(followRot, baseRot));

    XMVECTOR basePos = XMLoadFloat3(&drawTransform.position);
    XMVECTOR baseForward =
        XMVector3Rotate(XMVectorSet(0, 0, 1, 0), baseRot);
    XMVECTOR finalForward =
        XMVector3Rotate(XMVectorSet(0, 0, 1, 0), finalRot);
    XMVECTOR pivot =
        XMVectorSubtract(basePos, baseForward * kSlashFollowThroughPivotDistance);
    XMVECTOR finalPos =
        XMVectorAdd(pivot, finalForward * kSlashFollowThroughPivotDistance);
    finalPos = XMVectorAdd(finalPos, finalForward * slashFollowThroughSurge_);

    const float stretch =
        std::clamp((std::fabs(yaw) + std::fabs(pitch) + std::fabs(roll)) *
                       0.10f,
                   0.0f, kSlashFollowThroughMaxStretch);
    drawTransform.scale.z *= 1.0f + stretch;
    drawTransform.scale.x *= 1.0f - stretch * 0.18f;
    drawTransform.scale.y *= 1.0f - stretch * 0.18f;

    XMStoreFloat4(&drawTransform.rotation, finalRot);
    XMStoreFloat3(&drawTransform.position, finalPos);
}

void Sword::UpdateCounterObservation(float deltaTime) {
    justCountered_ = false;
    justCounterFailed_ = false;
    justCounterEarly_ = false;
    justCounterLate_ = false;

    isCounterStance_ = isCounter_;

    if (isCounterStance_) {
        if (std::fabs(slashDir_.y) >= std::fabs(slashDir_.x)) {
            counterAxis_ = std::fabs(slashDir_.y) > 0.1f
                               ? SwordCounterAxis::Vertical
                               : SwordCounterAxis::None;
        } else {
            counterAxis_ = std::fabs(slashDir_.x) > 0.1f
                               ? SwordCounterAxis::Horizontal
                               : SwordCounterAxis::None;
        }
    } else {
        counterAxis_ = SwordCounterAxis::None;
    }

    if (isCounterStance_) {
        if (!prevIsCounter_) {
            counterStateTimer_ = 0.0f;
        } else {
            counterStateTimer_ += deltaTime;
        }
    } else {
        if (prevIsCounter_) {
            justCounterFailed_ = true;

            if (counterStateTimer_ < counterEarlyThreshold_) {
                justCounterEarly_ = true;
            } else if (counterStateTimer_ > counterLateThreshold_) {
                justCounterLate_ = true;
            }
        }

        counterStateTimer_ = 0.0f;
    }

    prevIsCounter_ = isCounterStance_;
}

SwordCounterAxis Sword::ComputeSlashAxis() const {
    if (std::fabs(slashDir_.y) >= std::fabs(slashDir_.x)) {
        return std::fabs(slashDir_.y) > 0.1f ? SwordCounterAxis::Vertical
                                             : SwordCounterAxis::None;
    }

    return std::fabs(slashDir_.x) > 0.1f ? SwordCounterAxis::Horizontal
                                         : SwordCounterAxis::None;
}

SwordCounterAxis Sword::GetSlashCounterAxis() const {
    return GetSlashAxis();
}

SwordCounterAxis Sword::GetSlashAxis() const {
    if (!CanSlashCounter()) {
        return SwordCounterAxis::None;
    }

    return ComputeSlashAxis();
}

void Sword::NotifyCounterSuccess() {
    justCountered_ = true;
    justCounterFailed_ = false;
    justCounterEarly_ = false;
    justCounterLate_ = false;
    isCounterStance_ = false;
    prevIsCounter_ = false;
    counterStateTimer_ = 0.0f;
    counterAxis_ = SwordCounterAxis::None;
}
