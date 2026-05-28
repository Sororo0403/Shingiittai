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
constexpr float kCounterAxisMinComponent = 0.24f;
constexpr float kCounterAxisDominanceRatio = 1.8f;

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
    previousTf_ = tf_;
    hasPreviousTransform_ = false;

    slashFollowThroughTimer_ = 0.0f;
    slashFollowThroughStarted_ = false;
    slashFollowThroughDir_ = {};
    slashFollowThroughAngles_ = {};
    slashFollowThroughRoll_ = 0.0f;
    slashFollowThroughSurge_ = 0.0f;
}

void Sword::Update(const Transform &transform, const SwordPose &pose,
                   float deltaTime) {
    previousTf_ = hasPreviousTransform_ ? tf_ : transform;
    hasPreviousTransform_ = true;
    tf_ = transform;
    isSlashMode_ = pose.isSlashMode;
    slashDir_ = pose.slashDir;
    orientation_ = pose.orientation;
    UpdateSlashFollowThrough(deltaTime);
}

OBB Sword::BuildOBB(const Transform &transform) const {
    OBB box;

    float hitBoxDepth = size_.z;
    float forwardOffset = kSwordLength * 0.5f;
    if (isSlashMode_) {
        hitBoxDepth += kSlashHitDepthExtension;
        forwardOffset += kSlashHitDepthExtension * 0.5f;
    }

    XMVECTOR pos = XMLoadFloat3(&transform.position);
    XMVECTOR rot = XMLoadFloat4(&transform.rotation);
    XMVECTOR forward = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), rot);
    XMVECTOR center = pos + forward * forwardOffset;

    XMStoreFloat3(&box.center, center);
    box.size = size_;
    box.size.z = hitBoxDepth;
    box.rotation = transform.rotation;
    return box;
}

OBB Sword::GetOBB() const { return BuildOBB(tf_); }

Transform Sword::InterpolateTransform(float alpha) const {
    Transform result = tf_;
    if (!hasPreviousTransform_) {
        return result;
    }

    alpha = std::clamp(alpha, 0.0f, 1.0f);
    result.position = {
        previousTf_.position.x +
            (tf_.position.x - previousTf_.position.x) * alpha,
        previousTf_.position.y +
            (tf_.position.y - previousTf_.position.y) * alpha,
        previousTf_.position.z +
            (tf_.position.z - previousTf_.position.z) * alpha};
    result.scale = {
        previousTf_.scale.x + (tf_.scale.x - previousTf_.scale.x) * alpha,
        previousTf_.scale.y + (tf_.scale.y - previousTf_.scale.y) * alpha,
        previousTf_.scale.z + (tf_.scale.z - previousTf_.scale.z) * alpha};

    XMVECTOR from = XMQuaternionNormalize(XMLoadFloat4(&previousTf_.rotation));
    XMVECTOR to = XMQuaternionNormalize(XMLoadFloat4(&tf_.rotation));
    XMStoreFloat4(&result.rotation, XMQuaternionSlerp(from, to, alpha));
    return result;
}

std::array<OBB, 3> Sword::GetOBBSamples() const {
    return {BuildOBB(InterpolateTransform(0.0f)),
            BuildOBB(InterpolateTransform(0.5f)), BuildOBB(tf_)};
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
    ApplySlashFollowThrough(drawTransform);
    return drawTransform;
}

void Sword::Draw(ModelManager *modelManager, const Camera &camera,
                 float visualScale) {
    Transform drawTransform = BuildVisualTransform();
    drawTransform.scale.x *= visualScale;
    drawTransform.scale.y *= visualScale;
    drawTransform.scale.z *= visualScale;

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

SwordCounterAxis Sword::ComputeSlashAxis() const {
    const float absX = std::fabs(slashDir_.x);
    const float absY = std::fabs(slashDir_.y);

    if (absY >= kCounterAxisMinComponent &&
        absY >= absX * kCounterAxisDominanceRatio) {
        return SwordCounterAxis::Vertical;
    }

    if (absX >= kCounterAxisMinComponent &&
        absX >= absY * kCounterAxisDominanceRatio) {
        return SwordCounterAxis::Horizontal;
    }

    return SwordCounterAxis::None;
}

SwordCounterAxis Sword::GetSlashAxis() const {
    if (!CanSlashCounter()) {
        return SwordCounterAxis::None;
    }

    return ComputeSlashAxis();
}
