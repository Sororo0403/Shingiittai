#pragma once
#include <DirectXMath.h>
#include <vector>

struct EnemyElectricRingSpawnRequest {
    DirectX::XMFLOAT3 worldPos = {0.0f, 0.0f, 0.0f};
    bool isWarpEnd = false; // false = warp start, true = warp end
};