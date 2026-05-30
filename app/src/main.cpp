#include "DirectXCommon.h"
#include "GameScene.h"
#include "Input.h"
#include "AppSceneServices.h"
#include "core/AssetManager.h"
#include "Lighting.h"
#include "ModelManager.h"
#include "RenderPassController.h"
#include "PostEffectManager.h"
#include "PostProcessSystem.h"
#include "SceneContext.h"
#include "SceneManager.h"
#include "SoundManager.h"
#include "SpriteManager.h"
#include "SrvManager.h"
#include "TextureManager.h"
#include "TitleScene.h"
#include "WinApp.h"
#include <Windows.h>
#include <filesystem>
#include <memory>
#include <string>

namespace {
std::filesystem::path ResolveExecutableDirectory() {
    std::wstring path(MAX_PATH, L'\0');
    DWORD length =
        GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    while (length == path.size()) {
        path.resize(path.size() * 2, L'\0');
        length =
            GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    }
    path.resize(length);
    return std::filesystem::path(path).parent_path();
}

class HandUdpSenderProcess {
  public:
    ~HandUdpSenderProcess() { Stop(); }

    static bool IsRuntimeAvailable() {
        if (IsDisabled()) {
            return false;
        }

        const std::filesystem::path runtimeRoot = ResolveRuntimeRoot();
        const std::filesystem::path sourceDir = HandTrackingSourceDir(runtimeRoot);
        const std::filesystem::path modelPath =
            sourceDir / L"models" / L"hand_landmarker.task";
        const std::filesystem::path packagedExe = PackagedExePath(runtimeRoot);
        const std::filesystem::path scriptPath =
            sourceDir / L"src" / L"hand_udp_sender.py";
        return std::filesystem::exists(modelPath) &&
               (std::filesystem::exists(packagedExe) ||
                std::filesystem::exists(scriptPath));
    }

    bool ActivateCamera() {
        shouldKeepRunning_ = true;
        RefreshProcessState();
        if (!isRunning_) {
            return Start();
        }
        return true;
    }

    void DeactivateCamera() {
        shouldKeepRunning_ = false;
        Stop();
    }

    void Update() {
        RefreshProcessState();
        if (shouldKeepRunning_ && !isRunning_ && !IsDisabled()) {
            const DWORD now = GetTickCount();
            if (now - lastStartAttemptTick_ >= 1500) {
                Start();
            }
        }
    }

    bool Start() {
        if (isRunning_) {
            return true;
        }
        lastStartAttemptTick_ = GetTickCount();
        if (IsDisabled()) {
            return false;
        }

        const std::filesystem::path runtimeRoot = ResolveRuntimeRoot();
        const std::filesystem::path sourceDir = HandTrackingSourceDir(runtimeRoot);
        const std::filesystem::path modelPath =
            sourceDir / L"models" / L"hand_landmarker.task";
        if (!std::filesystem::exists(modelPath)) {
            return false;
        }

        const std::filesystem::path packagedExe = PackagedExePath(runtimeRoot);
        const std::filesystem::path scriptPath =
            sourceDir / L"src" / L"hand_udp_sender.py";
        const std::filesystem::path venvPython = VenvPythonPath(runtimeRoot);
        const auto appendCameraArg = [](std::wstring& command,
                                        const wchar_t* envName,
                                        const wchar_t* argName) {
            wchar_t cameraSource[1024]{};
            constexpr DWORD kCameraSourceCapacity =
                static_cast<DWORD>(sizeof(cameraSource) /
                                   sizeof(cameraSource[0]));
            const DWORD length =
                GetEnvironmentVariableW(envName, cameraSource,
                                        kCameraSourceCapacity);
            if (!command.empty() && length > 0 &&
                length < kCameraSourceCapacity) {
                command += L" ";
                command += argName;
                command += L" \"";
                command += std::wstring(cameraSource, length);
                command += L"\"";
                return true;
            }
            return false;
        };

        std::wstring scriptCommand;
        if (std::filesystem::exists(scriptPath) &&
            std::filesystem::exists(venvPython)) {
            scriptCommand = L"\"" + venvPython.wstring() + L"\" \"" +
                            scriptPath.wstring() + L"\" --model \"" +
                            modelPath.wstring() + L"\"";
        } else if (std::filesystem::exists(scriptPath)) {
            scriptCommand = L"py -3.11 \"" + scriptPath.wstring() +
                            L"\" --model \"" + modelPath.wstring() + L"\"";
        }
        appendCameraArg(scriptCommand, L"SHINGIITTAI_MAIN_CAMERA", L"--camera");
        std::wstring packagedCommand;
        if (std::filesystem::exists(packagedExe)) {
            packagedCommand = L"\"" + packagedExe.wstring() + L"\" --model \"" +
                              modelPath.wstring() + L"\"";
        }
        appendCameraArg(packagedCommand, L"SHINGIITTAI_MAIN_CAMERA", L"--camera");

        std::wstring command;
        if (!scriptCommand.empty()) {
            command = scriptCommand;
        } else {
            command = packagedCommand;
        }

        if (command.empty()) {
            return false;
        }

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
        startupInfo.dwFlags = STARTF_USESHOWWINDOW;
        startupInfo.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION processInfo{};
        const BOOL started = CreateProcessW(
            nullptr, command.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
            runtimeRoot.wstring().c_str(),
            &startupInfo, &processInfo);
        if (!started) {
            if (jobHandle != nullptr) {
                CloseHandle(jobHandle);
            }
            return false;
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
        return true;
    }

    bool IsRunning() {
        RefreshProcessState();
        return isRunning_;
    }

  private:
    void RefreshProcessState() {
        if (!isRunning_) {
            return;
        }

        DWORD exitCode = 0;
        if (GetExitCodeProcess(processInfo_.hProcess, &exitCode) &&
            exitCode == STILL_ACTIVE) {
            return;
        }

        if (jobHandle_ != nullptr) {
            CloseHandle(jobHandle_);
            jobHandle_ = nullptr;
        }
        if (processInfo_.hThread != nullptr) {
            CloseHandle(processInfo_.hThread);
        }
        if (processInfo_.hProcess != nullptr) {
            CloseHandle(processInfo_.hProcess);
        }
        processInfo_ = {};
        isRunning_ = false;
    }

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

    static std::filesystem::path HandTrackingSourceDir(
        const std::filesystem::path &root) {
        return root / L"hand_tracking";
    }

    static std::filesystem::path VenvPythonPath(
        const std::filesystem::path &root) {
        return root / L"generated" / L"intermediate" / L"HandUdpSender" /
               L".venv" / L"Scripts" / L"python.exe";
    }

    static std::filesystem::path PackagedExePath(
        const std::filesystem::path &root) {
        const std::filesystem::path runtimePackagedExe =
            HandTrackingSourceDir(root) / L"hand_udp_sender" /
            L"hand_udp_sender.exe";
        if (std::filesystem::exists(runtimePackagedExe)) {
            return runtimePackagedExe;
        }

        return root / L"generated" / L"outputs" / L"x64" / L"Release" /
               L"HandUdpSender" / L"hand_udp_sender" / L"hand_udp_sender.exe";
    }

    static std::filesystem::path ResolveRuntimeRoot() {
        const std::filesystem::path executableDir = ResolveExecutableDirectory();
        const std::filesystem::path repoRoot = ResolveRepoRoot();
        const auto hasSender = [](const std::filesystem::path &root) {
            const std::filesystem::path sourceDir = HandTrackingSourceDir(root);
            return std::filesystem::exists(sourceDir / L"src" /
                                           L"hand_udp_sender.py") &&
                   std::filesystem::exists(sourceDir / L"models" /
                                           L"hand_landmarker.task");
        };
        const auto hasVenv = [](const std::filesystem::path &root) {
            return std::filesystem::exists(VenvPythonPath(root));
        };
        const std::filesystem::path siblingRepo =
            executableDir.parent_path()
                .parent_path()
                .parent_path()
                .parent_path() /
            L"Shingiittai";

        if (hasSender(repoRoot) && hasVenv(repoRoot)) {
            return repoRoot;
        }
        if (hasSender(siblingRepo) && hasVenv(siblingRepo)) {
            return siblingRepo;
        }
        if (hasSender(executableDir) && hasVenv(executableDir)) {
            return executableDir;
        }
        if (hasSender(repoRoot)) {
            return repoRoot;
        }
        if (hasSender(siblingRepo)) {
            return siblingRepo;
        }
        if (hasSender(executableDir)) {
            return executableDir;
        }
        return repoRoot;
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
    bool shouldKeepRunning_ = false;
    DWORD lastStartAttemptTick_ = 0;
};

}

int RunApp(HINSTANCE hInstance, int nCmdShow) {
    const std::filesystem::path executableDirectory = ResolveExecutableDirectory();
    SetCurrentDirectoryW(executableDirectory.wstring().c_str());
    AssetManager::SetAssetRoot(executableDirectory);

    HandUdpSenderProcess handUdpSenderProcess;
    const bool handTrackingRuntimeAvailable =
        HandUdpSenderProcess::IsRuntimeAvailable();

    // WinApp初期化
    WinApp winApp;
    winApp.Initialize(hInstance, nCmdShow, 1280, 720, L"3145_身技一体", true);
    winApp.SetCursorVisible(false);

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

    PostProcessSystem postProcessSystem;
    postProcessSystem.Initialize(&dxCommon, &srvManager, width, height);
    PostEffectManager postEffectManager;
    postEffectManager.Initialize(&postProcessSystem);
    postEffectManager.SetBaseProfile(PostProcessProfile{});

    // Input
    Input input;
    input.Initialize(hInstance, winApp.GetHwnd());

    // SoundManager
    SoundManager soundManager;
    soundManager.Initialize();

    // TextureManager
    TextureManager textureManager;
    textureManager.Initialize(&dxCommon, &srvManager);
    const float dummyShadowDepth = 1.0f;
    const uint32_t dummyShadowTextureId = textureManager.CreateTexture2D(
        1, 1, DXGI_FORMAT_R32_FLOAT,
        reinterpret_cast<const uint8_t *>(&dummyShadowDepth),
        sizeof(dummyShadowDepth));

    // ModelManager
    ModelManager modelManager;
    modelManager.Initialize(&dxCommon, &srvManager, &textureManager);
    DirectX::XMFLOAT4X4 identityLightViewProjection{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};
    SceneShadowSettings noShadow{};
    noShadow.strength = 0.0f;
    modelManager.GetRenderer()->SetShadowMap(
        textureManager.GetGpuHandle(dummyShadowTextureId),
        identityLightViewProjection, noShadow);
    modelManager.GetRenderer()->SetEnvironmentTexture(
        textureManager.GetWhiteCubeTextureId());

    // SpriteManager
    SpriteManager &spriteManager = SpriteManager::GetInstance();
    spriteManager.Initialize(&dxCommon, &textureManager, &srvManager, width,
                             height);

    SceneContext sceneCtx{};
    sceneCtx.systems.input = &input;
    sceneCtx.systems.winApp = &winApp;
    sceneCtx.systems.sound = &soundManager;
    sceneCtx.systems.texture = &textureManager;
    sceneCtx.rendering.model = &modelManager;
    sceneCtx.rendering.sprite = &spriteManager;
    sceneCtx.rendering.srv = &srvManager;
    sceneCtx.rendering.texture = &textureManager;
    sceneCtx.rendering.dxCommon = &dxCommon;
    sceneCtx.rendering.postEffectManager = &postEffectManager;
    sceneCtx.frame.deltaTime = 0.0f;
    AppSceneServices::ConfigureHandTracking(
        [&handUdpSenderProcess, &winApp]() {
            if (handUdpSenderProcess.IsRunning()) {
                return true;
            }

            HWND hwnd = winApp.GetHwnd();
            SetForegroundWindow(hwnd);
            const bool started = handUdpSenderProcess.ActivateCamera();
            SetForegroundWindow(hwnd);
            return started;
        },
        [&handUdpSenderProcess]() { handUdpSenderProcess.DeactivateCamera(); },
        [handTrackingRuntimeAvailable]() { return handTrackingRuntimeAvailable; },
        [&handUdpSenderProcess]() {
            return handUdpSenderProcess.IsRunning();
        });

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

        sceneCtx.frame.deltaTime = deltaTime;
        handUdpSenderProcess.Update();

        // 入力更新
        input.Update(deltaTime);

        const int currentWidth = winApp.GetWidth();
        const int currentHeight = winApp.GetHeight();
        if (currentWidth > 0 && currentHeight > 0 &&
            (currentWidth != width || currentHeight != height)) {
            width = currentWidth;
            height = currentHeight;
            dxCommon.Resize(width, height);
            postProcessSystem.Resize(width, height);
            spriteManager.Resize(width, height);
        }

        // Scene 更新
        sceneManager.Update();

        // 描画
        dxCommon.BeginFrame();
        modelManager.BeginFrame();
        spriteManager.BeginFrame();

        dxCommon.BeginScenePass();
        sceneManager.Draw();
        if (sceneManager.UsesForeground3DPass()) {
            dxCommon.ClearDepth();
            sceneManager.DrawForeground3D();
        }
        sceneManager.DrawTransparent();
        dxCommon.EndScenePass();

        dxCommon.BeginBackBufferPass(false);
        dxCommon.TransitionDepthToShaderResource();
        postProcessSystem.Draw(dxCommon.GetSceneSrvGpuHandle(&srvManager),
                               dxCommon.GetDepthStencilGpuHandle());
        dxCommon.TransitionDepthToWrite();
        sceneManager.DrawPostProcessOverlay();

        dxCommon.EndFrame();
    }

    winApp.SetCursorVisible(true);
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    return RunApp(hInstance, nCmdShow);
}
