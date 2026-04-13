#pragma once
#include <DirectXMath.h>

struct SwordPose {
    DirectX::XMFLOAT2 slashDir{};
    DirectX::XMFLOAT4 orientation{0, 0, 0, 1};
    bool isSlashMode = false;
    bool isGuard = false;
    bool isCounter = false;
    bool isMouse = false;
    bool isJoyCon = false;
};
