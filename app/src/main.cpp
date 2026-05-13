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
#include <Windows.h>
#include <filesystem>
#include <memory>
#include <string>

namespace {
class HandUdpSenderProcess {
  public:
    ~HandUdpSenderProcess() { Stop(); }

    void Start() {
        if (IsDisabled()) {
            return;
        }

        const std::filesystem::path repoRoot = ResolveRepoRoot();
        const std::filesystem::path scriptPath =
            repoRoot / L"tools" / L"hand_tracking" / L"hand_udp_sender.py";
        const std::filesystem::path modelPath =
            L"C:\\models\\hand_landmarker.task";

        if (!std::filesystem::exists(scriptPath) ||
            !std::filesystem::exists(modelPath)) {
            return;
        }

        std::wstring command = L"py -3.11 \"" + scriptPath.wstring() +
                               L"\" --model \"" + modelPath.wstring() + L"\"";

        HANDLE jobHandle = CreateJobObjectW(nullptr, nullptr);
        if (jobHandle != nullptr) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limitInfo{};
            limitInfo.BasicLimitInformation.LimitFlags =
                JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (!SetInformationJobObject(jobHandle,
                                         JobObjectExtendedLimitInformation,
                                         &limitInfo, sizeof(limitInfo))) {
                CloseHandle(jobHandle);
                jobHandle = nullptr;
            }
        }

        STARTUPINFOW startupInfo{};
        startupInfo.cb = sizeof(startupInfo);
        PROCESS_INFORMATION processInfo{};
        const BOOL started = CreateProcessW(
            nullptr, command.data(), nullptr, nullptr, FALSE,
            CREATE_NEW_CONSOLE | CREATE_SUSPENDED, nullptr,
            repoRoot.wstring().c_str(),
            &startupInfo, &processInfo);
        if (!started) {
            if (jobHandle != nullptr) {
                CloseHandle(jobHandle);
            }
            return;
        }

        if (jobHandle != nullptr &&
            !AssignProcessToJobObject(jobHandle, processInfo.hProcess)) {
            CloseHandle(jobHandle);
            jobHandle = nullptr;
        }

        ResumeThread(processInfo.hThread);

        processInfo_ = processInfo;
        jobHandle_ = jobHandle;
        isRunning_ = true;
    }

  private:
    static bool IsDisabled() {
        wchar_t value[8]{};
        const DWORD length = GetEnvironmentVariableW(
            L"SHINGIITTAI_DISABLE_HAND_CAMERA", value,
            static_cast<DWORD>(std::size(value)));
        return length > 0 && value[0] == L'1';
    }

    static std::filesystem::path ResolveRepoRoot() {
        std::filesystem::path sourcePath = std::filesystem::path(__FILE__);
        if (!sourcePath.is_absolute()) {
            sourcePath = std::filesystem::current_path() / sourcePath;
        }

        return sourcePath.parent_path().parent_path().parent_path();
    }

    void Stop() {
        if (!isRunning_) {
            return;
        }

        if (jobHandle_ != nullptr) {
            CloseHandle(jobHandle_);
            jobHandle_ = nullptr;
            WaitForSingleObject(processInfo_.hProcess, 1000);
        } else {
            DWORD exitCode = 0;
            if (GetExitCodeProcess(processInfo_.hProcess, &exitCode) &&
                exitCode == STILL_ACTIVE) {
                TerminateProcess(processInfo_.hProcess, 0);
                WaitForSingleObject(processInfo_.hProcess, 1000);
            }
        }

        CloseHandle(processInfo_.hThread);
        CloseHandle(processInfo_.hProcess);
        processInfo_ = {};
        isRunning_ = false;
    }

    PROCESS_INFORMATION processInfo_{};
    HANDLE jobHandle_ = nullptr;
    bool isRunning_ = false;
};
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    HandUdpSenderProcess handUdpSenderProcess;
    handUdpSenderProcess.Start();

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
