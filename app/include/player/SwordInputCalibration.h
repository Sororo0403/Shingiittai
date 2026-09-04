#pragma once
#include "InputControlType.h"
#include <DirectXMath.h>
#include <array>

/// <summary>
/// ハンド入力を剣操作へ変換する際の補正値を保持する
/// </summary>
struct SwordInputCalibration {
    InputControlType controlType = InputControlType::KeyboardMouse;
    bool hasHandNeutral = false;
    bool hasHandRestSpeed = false;
    std::array<DirectX::XMFLOAT2, 2> handNeutral = {
        DirectX::XMFLOAT2{0.5f, 0.5f}, DirectX::XMFLOAT2{0.5f, 0.5f}};
    std::array<float, 2> handRestSpeed = {0.0f, 0.0f};
};
