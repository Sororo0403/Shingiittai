#pragma once
#include <DirectXMath.h>

/// <summary>
/// 入力装置から得た剣の位置、回転、操作状態を表す
/// </summary>
struct SwordPose {
    DirectX::XMFLOAT2 slashDir{};
    DirectX::XMFLOAT4 orientation{0, 0, 0, 1};
    bool isSlashMode = false;
};
