#pragma once
#include <DirectXMath.h>

/// <summary>
/// 回転を考慮した境界ボックスを表す
/// </summary>
struct OBB {
    DirectX::XMFLOAT3 center;
    /// <summary>各ローカル軸方向の全体サイズ</summary>
    DirectX::XMFLOAT3 size;
    DirectX::XMFLOAT4 rotation;
};
