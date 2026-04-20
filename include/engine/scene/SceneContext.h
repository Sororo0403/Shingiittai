#pragma once
struct WarpPostEffectParamGPU;
struct ElectricRingParamGPU;
class SlashEffectRenderer;
class GpuSlashParticleSystem;
class SwordTrailRenderer;
class MagnetismicRenderer;
class Input;
class WinApp;
class SoundManager;
class ModelManager;
class SpriteManager;
class TextureManager;
class DirectXCommon;
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
    TextureManager *texture = nullptr;
    DirectXCommon *dxCommon = nullptr;
#ifdef _DEBUG
    DebugDraw *debugDraw = nullptr;
#endif // _DEBUG

    float deltaTime = 0.0f;

#ifndef IMGUI_DISABLED
    ImguiManager *imgui = nullptr;
#endif // IMGUI_DISABLED

    WarpPostEffectParamGPU *warpPostEffectParam = nullptr;
    ElectricRingParamGPU *electricRingParam = nullptr;
    SlashEffectRenderer *slashEffectRenderer = nullptr;
    GpuSlashParticleSystem *gpuSlashParticleSystem = nullptr;
    SwordTrailRenderer *swordTrailRenderer = nullptr;
    MagnetismicRenderer *magnetismicRenderer = nullptr;
};
