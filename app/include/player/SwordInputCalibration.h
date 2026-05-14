#pragma once
#include <DirectXMath.h>
#include <array>
#include "InputControlType.h"

struct SwordInputCalibration {
    InputControlType controlType = InputControlType::KeyboardMouse;
    bool hasHandNeutral = false;
    bool hasHandRestSpeed = false;
    bool resetJoyConBaseOnStart = false;
    std::array<DirectX::XMFLOAT2, 2> handNeutral = {
        DirectX::XMFLOAT2{0.5f, 0.5f}, DirectX::XMFLOAT2{0.5f, 0.5f}};
    std::array<float, 2> handRestSpeed = {0.0f, 0.0f};
};
