#pragma once
#include <DirectXMath.h>

namespace CollisionUtil {

bool CheckSphere(const DirectX::XMFLOAT3 &a, const DirectX::XMFLOAT3 &b,
                 float radius);

} // namespace CollisionUtil