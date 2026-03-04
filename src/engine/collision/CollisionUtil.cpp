#include "CollisionUtil.h"
#include <cmath>

bool CollisionUtil::CheckSphere(const DirectX::XMFLOAT3 &a,
                                const DirectX::XMFLOAT3 &b, float radius) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    float dz = a.z - b.z;

    float distSq = dx * dx + dy * dy + dz * dz;

    return distSq <= radius * radius;
}