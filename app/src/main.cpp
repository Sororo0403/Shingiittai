#include "DirectXCommon.h"
#include "GameScene.h"
#include "Input.h"
#include "ModelManager.h"
#include "ModeSelectScene.h"
#include "PostEffectRenderer.h"
#include "SceneContext.h"
#include "SceneManager.h"
#include "SoundManager.h"
#include "SpriteManager.h"
#include "SrvManager.h"
#include "TextureManager.h"
#include "TitleScene.h"
#include "WeaponSelectScene.h"
#include "WinApp.h"
#include <memory>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    // WinApp初期化
    WinApp winApp;
    winApp.Initialize(hInstance, nCmdShow, 1280, 720, L"3145_身技一体");

    // クライアント領域の幅と高さ
    int width = winApp.GetWidth();
    int height = winApp.GetHeight();

    // DirectX
    DirectXCommon dxCommon;
    dxCommon.Initialize(winApp.GetHwnd(), width, height);

    // SrvManager
    SrvManager srvManager;
    srvManager.Initialize(&dxCommon, 4096);
    dxCommon.RegisterSceneColorSRV(&srvManager);
    dxCommon.CreateDepthStencilSrv(&srvManager);

    PostEffectRenderer postEffectRenderer;
    postEffectRenderer.Initialize(&dxCommon, &srvManager, width, height);
    postEffectRenderer.SetVignettingEnabled(false);

    // Input
    Input input;
    input.Initialize(hInstance, winApp.GetHwnd());

    // SoundManager
    SoundManager soundManager;
    soundManager.Initialize();

    // TextureManager
    TextureManager textureManager;
    dxCommon.BeginUpload();
    textureManager.Initialize(&dxCommon, &srvManager);
    dxCommon.EndUpload();
    textureManager.ReleaseUploadBuffers();

    // ModelManager
    ModelManager modelManager;
    modelManager.Initialize(&dxCommon, &srvManager, &textureManager);

    // SpriteManager
    SpriteManager spriteManager;
    spriteManager.Initialize(&dxCommon, &textureManager, &srvManager, width,
                             height);

    SceneContext sceneCtx{};
    sceneCtx.input = &input;
    sceneCtx.winApp = &winApp;
    sceneCtx.sound = &soundManager;
    sceneCtx.model = &modelManager;
    sceneCtx.sprite = &spriteManager;
    sceneCtx.srv = &srvManager;
    sceneCtx.texture = &textureManager;
    sceneCtx.dxCommon = &dxCommon;
    sceneCtx.postEffectRenderer = &postEffectRenderer;
    sceneCtx.deltaTime = 0.0f;

    // SceneManager
    SceneManager sceneManager;
    sceneManager.Initialize(sceneCtx);
    sceneManager.ChangeScene(std::make_unique<TitleScene>());

    // 高精細タイマの周波数を取得
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);

    LARGE_INTEGER prevTime;
    QueryPerformanceCounter(&prevTime);

    // メインループ
    while (winApp.ProcessMessage()) {
        // deltaTime計算
        LARGE_INTEGER currentTime;
        QueryPerformanceCounter(&currentTime);

        float deltaTime =
            static_cast<float>(currentTime.QuadPart - prevTime.QuadPart) /
            static_cast<float>(freq.QuadPart);

        prevTime = currentTime;

        sceneCtx.deltaTime = deltaTime;

        // 入力更新
        input.Update(deltaTime);
        if (input.IsKeyTrigger(DIK_ESCAPE)) {
            break;
        }

        // Scene 更新
        sceneManager.Update();

        // 描画
        dxCommon.BeginFrame();
        spriteManager.BeginFrame();

        dxCommon.BeginScenePass();
        sceneManager.Draw();
        dxCommon.EndScenePass();

        dxCommon.BeginBackBufferPass();
        dxCommon.TransitionDepthToShaderResource();
        postEffectRenderer.Draw(dxCommon.GetSceneSrvGpuHandle(&srvManager),
                                dxCommon.GetDepthStencilGpuHandle());
        dxCommon.TransitionDepthToWrite();

        sceneManager.DrawOverlay();

        dxCommon.EndFrame();
    }

    return 0;
}
