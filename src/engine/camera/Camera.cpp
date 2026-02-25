#include "Camera.h"
using namespace DirectX;

void Camera::Initialize(float aspect) {
    aspect_ = aspect;
    Update();
}

void Camera::Update() {
    XMMATRIX world =
        XMMatrixRotationRollPitchYaw(rotation_.x, rotation_.y, rotation_.z) *
        XMMatrixTranslation(position_.x, position_.y, position_.z);

    view_ = XMMatrixInverse(nullptr, world);

    proj_ = XMMatrixPerspectiveFovLH(fovY_, aspect_, nearZ_, farZ_);
}
