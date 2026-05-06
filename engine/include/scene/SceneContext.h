#pragma once

class Input;
class WinApp;
class SoundManager;
class ModelManager;
class SpriteManager;
class ModelRenderer;
class SpriteRenderer;
class TextureManager;
class DirectXCommon;
class SrvManager;
class PostEffectRenderer;
class RenderTexture;
class SkyboxRenderer;

#ifdef _DEBUG
class ImguiManager;
#endif // _DEBUG

/// <summary>
/// シーンが参照する各種システムへのアクセスポイントをまとめる
/// </summary>
struct SceneContext {
    Input *input = nullptr;
    WinApp *winApp = nullptr;
    SoundManager *sound = nullptr;
    ModelManager *model = nullptr;
    SpriteManager *sprite = nullptr;
    ModelRenderer *modelRenderer = nullptr;
    SpriteRenderer *spriteRenderer = nullptr;
    TextureManager *texture = nullptr;
    DirectXCommon *dxCommon = nullptr;
    SrvManager *srv = nullptr;
    RenderTexture *renderTexture = nullptr;
    PostEffectRenderer *postEffectRenderer = nullptr;
    SkyboxRenderer *skyboxRenderer = nullptr;
    float deltaTime = 0.0f;

#ifdef _DEBUG
    ImguiManager *imgui = nullptr;
#endif // _DEBUG
};
