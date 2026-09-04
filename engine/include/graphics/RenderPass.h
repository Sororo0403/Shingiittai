#pragma once
#include <cstdint>

/// <summary>フレーム内で実行する描画パスの種類</summary>
enum class RenderPass : uint8_t {
    None,
    Shadow,
    SceneColor,
    Foreground3D,
    Transparent,
    PostProcess,
    Debug,
    UI,
    BackBuffer,

    Scene = SceneColor,
    DebugUi = UI,
};
