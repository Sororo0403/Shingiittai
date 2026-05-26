#pragma once
#include <cstdint>

enum class RenderPass : uint8_t {
    None,
    Shadow,
    SceneColor,
    Transparent,
    PostProcess,
    Debug,
    UI,
    BackBuffer,

    Scene = SceneColor,
    DebugUi = UI,
};
