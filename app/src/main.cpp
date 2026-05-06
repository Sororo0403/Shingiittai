#include "DirectXCommon.h"
#include "ElectricRingEffectRenderer.h"
#include "GameScene.h"
#include "GpuSlashParticleSystem.h"
#include "Input.h"
#include "ModelManager.h"
#include "SceneContext.h"
#include "SceneManager.h"
#include "SlashEffectRenderer.h"
#include "SoundManager.h"
#include "SpriteManager.h"
#include "SrvManager.h"
#include "SwordTrailRenderer.h"
#include "TextureManager.h"
#include "WarpPostEffectRenderer.h"
#include "MagnetismicRenderer.h"
#include "WinApp.h"
#include <memory>
#ifdef _DEBUG
#include "DebugDraw.h"
#endif // _DEBUG

#ifndef IMGUI_DISABLED
#include "ImguiManager.h"
#endif // IMGUI_DISABLED

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
    srvManager.Initialize(&dxCommon, 512);
    dxCommon.RegisterSceneColorSRV(&srvManager);

    WarpPostEffectRenderer warpPostEffectRenderer;
    warpPostEffectRenderer.Initialize(&dxCommon, &srvManager);
    WarpPostEffectParamGPU warpPostParam{};

    ElectricRingEffectRenderer electricRingEffectRenderer;
    electricRingEffectRenderer.Initialize(&dxCommon, &srvManager);
    ElectricRingParamGPU electricRingParam{};

    // Input
    Input input;
    input.Initialize(hInstance, winApp.GetHwnd());

    // SoundManager
    SoundManager soundManager;
    soundManager.Initialize();

    // TextureManager
    TextureManager textureManager;
    textureManager.Initialize(&dxCommon, &srvManager);

    SlashEffectRenderer slashEffectRenderer;
    slashEffectRenderer.Initialize(&dxCommon, &srvManager, &textureManager);

    GpuSlashParticleSystem gpuSlashParticleSystem;
    gpuSlashParticleSystem.Initialize(&dxCommon, &srvManager, &textureManager,
                                      4096);

    uint32_t electricNoiseTex0 =
        textureManager.Load(L"engine/resources/texture/effect/warp_smoke.png");
    uint32_t electricNoiseTex1 =
        textureManager.Load(L"engine/resources/texture/effect/warp_smoke_dark.png");

    SwordTrailRenderer swordTrailRenderer;
    swordTrailRenderer.Initialize(&dxCommon, &srvManager, &textureManager, 24);
    swordTrailRenderer.SetLifeTime(0.24f);

        MagnetismicRenderer magnetismicRenderer;
    magnetismicRenderer.Initialize(
        dxCommon.GetDevice(), DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_FORMAT_D24_UNORM_S8_UINT,
        L"engine/resources/shaders/warp/Magnetismic.VS.hlsl",
        L"engine/resources/shaders/warp/Magnetismic.PS.hlsl");

    // ModelManager
    ModelManager modelManager;
    modelManager.Initialize(&dxCommon, &srvManager, &textureManager);

    // SpriteManager
    SpriteManager spriteManager;
    spriteManager.Initialize(&dxCommon, &textureManager, &srvManager, width,
                             height);

    dxCommon.BeginUpload();

#ifdef _DEBUG
    // DebugDraw
    DebugDraw debugDraw;
    uint32_t boxModelId = modelManager.Load(L"engine/resources/model/debug/box.glb");
#endif // _DEBUG

    dxCommon.EndUpload();

    textureManager.ReleaseUploadBuffers();

#ifdef _DEBUG
    debugDraw.Initialize(boxModelId);
#endif // _DEBUG

#ifndef IMGUI_DISABLED
    // ImguiManager
    ImguiManager imguiManager;
    imguiManager.Initialize(&winApp, &dxCommon, &srvManager);
#endif // IMGUI_DISABLED

    SceneContext sceneCtx{};
    sceneCtx.input = &input;
    sceneCtx.winApp = &winApp;
    sceneCtx.sound = &soundManager;
    sceneCtx.model = &modelManager;
    sceneCtx.sprite = &spriteManager;
    sceneCtx.texture = &textureManager;
    sceneCtx.dxCommon = &dxCommon;
    sceneCtx.warpPostEffectParam = &warpPostParam;
    sceneCtx.electricRingParam = &electricRingParam;
    sceneCtx.slashEffectRenderer = &slashEffectRenderer;
    sceneCtx.gpuSlashParticleSystem = &gpuSlashParticleSystem;
    sceneCtx.swordTrailRenderer = &swordTrailRenderer;
    sceneCtx.magnetismicRenderer = &magnetismicRenderer;
    sceneCtx.deltaTime = 0.0f;

#ifdef _DEBUG
    sceneCtx.debugDraw = &debugDraw;
#endif // _DEBUG

#ifndef IMGUI_DISABLED
    sceneCtx.imgui = &imguiManager;
#endif // IMGUI_DISABLED

    // SceneManager
    SceneManager sceneManager;
    sceneManager.Initialize(sceneCtx);
    sceneManager.ChangeScene(std::make_unique<GameScene>());

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

        // Scene 更新
        sceneManager.Update();

        // 描画
        dxCommon.BeginFrame();

#ifndef IMGUI_DISABLED
        ID3D12GraphicsCommandList *cmdList = dxCommon.GetCommandList();
        imguiManager.Begin(cmdList);
#endif // IMGUI_DISABLED

        dxCommon.BeginScenePass();
        sceneManager.Draw();
        dxCommon.EndScenePass();

        dxCommon.BeginBackBufferPass();

        /* warpPostEffectRenderer.Draw(warpPostParam,
                                     dxCommon.GetSceneSrvGpuHandle(&srvManager));*/
        D3D12_GPU_DESCRIPTOR_HANDLE sceneSrv =
            dxCommon.GetSceneSrvGpuHandle(&srvManager);

        warpPostEffectRenderer.Draw(warpPostParam, sceneSrv);

        if (electricRingParam.enabled > 0.5f) {
            D3D12_GPU_DESCRIPTOR_HANDLE noise0 =
                textureManager.GetGpuHandle(electricNoiseTex0);
            D3D12_GPU_DESCRIPTOR_HANDLE noise1 =
                textureManager.GetGpuHandle(electricNoiseTex1);

            electricRingEffectRenderer.DrawDistortion(electricRingParam,
                                                      sceneSrv, noise0, noise1);

            electricRingEffectRenderer.DrawPlasma(electricRingParam, noise0,
                                                  noise1);
        }

#ifndef IMGUI_DISABLED
        imguiManager.End(cmdList);
#endif // IMGUI_DISABLED

        dxCommon.EndFrame();
    }

    return 0;
}
