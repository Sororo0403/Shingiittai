#include "camera/Camera.h"
#include <algorithm>

using namespace DirectX;

namespace {

constexpr float kMinAspect = 0.0001f;
constexpr float kMinFovY = XMConvertToRadians(1.0f);
constexpr float kMaxFovY = XMConvertToRadians(179.0f);
constexpr float kMinOrthoHeight = 0.001f;
constexpr float kMinNearZ = 0.001f;
constexpr float kMinDepthRange = 0.001f;

} // namespace

Camera::Camera() { UpdateMatrices(); }

void Camera::Initialize(float aspect) {
    aspect_ = aspect;
    UpdateMatrices();
}

void Camera::UpdateMatrices() {
    SanitizeProjection();

    const XMMATRIX world =
        XMMatrixRotationRollPitchYaw(rotation_.x, rotation_.y, rotation_.z) *
        XMMatrixTranslation(position_.x, position_.y, position_.z);
    view_ = XMMatrixInverse(nullptr, world);

    if (projectionMode_ == ProjectionMode::Orthographic) {
        proj_ = XMMatrixOrthographicLH(orthographicHeight_ * aspect_,
                                       orthographicHeight_, nearZ_, farZ_);
    } else {
        proj_ = XMMatrixPerspectiveFovLH(fovY_, aspect_, nearZ_, farZ_);
    }
    viewProjection_ = view_ * proj_;
}

void Camera::SetPosition(const XMFLOAT3 &position) {
    position_ = position;
    UpdateMatrices();
}

void Camera::SetRotation(const XMFLOAT3 &rotation) {
    rotation_ = rotation;
    UpdateMatrices();
}

void Camera::SetAspect(float aspect) {
    aspect_ = aspect;
    UpdateMatrices();
}

void Camera::SetPerspectiveFovDeg(float fovDeg) {
    SetPerspectiveFovRad(XMConvertToRadians(fovDeg));
}

void Camera::SetPerspectiveFovRad(float fovRad) {
    projectionMode_ = ProjectionMode::Perspective;
    fovY_ = fovRad;
    UpdateMatrices();
}

void Camera::SetOrthographicHeight(float height) {
    projectionMode_ = ProjectionMode::Orthographic;
    orthographicHeight_ = height;
    UpdateMatrices();
}

void Camera::SetClipRange(float nearZ, float farZ) {
    nearZ_ = nearZ;
    farZ_ = farZ;
    UpdateMatrices();
}

void Camera::SanitizeProjection() {
    aspect_ = (std::max)(aspect_, kMinAspect);
    fovY_ = std::clamp(fovY_, kMinFovY, kMaxFovY);
    orthographicHeight_ = (std::max)(orthographicHeight_, kMinOrthoHeight);
    nearZ_ = (std::max)(nearZ_, kMinNearZ);
    farZ_ = (std::max)(farZ_, nearZ_ + kMinDepthRange);
}
