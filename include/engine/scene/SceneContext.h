#pragma once

class Input;
class WinApp;
class SoundManager;
class ModelManager;
class SpriteManager;

#ifdef _DEBUG
class DebugDraw;
#endif // _DEBUG

#ifndef IMGUI_DISABLED
class ImguiManager;
#endif // IMGUI_DISABLED

struct SceneContext {
    Input *input = nullptr;
    WinApp *winApp = nullptr;
    SoundManager *sound = nullptr;
    ModelManager *model = nullptr;
    SpriteManager *sprite = nullptr;

#ifdef _DEBUG
    DebugDraw *debugDraw = nullptr;
#endif // _DEBUG

    float deltaTime = 0.0f;

#ifndef IMGUI_DISABLED
    ImguiManager *imgui = nullptr;
#endif // IMGUI_DISABLED
};
