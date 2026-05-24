#include "DirectXCommon.h"
#include "GameScene.h"
#include "Input.h"
#include "ModelManager.h"
#include "PostEffectRenderer.h"
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
#include <fstream>
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

std::filesystem::path ResolveHandTrackingLogPath() {
    std::wstring tempPath(MAX_PATH, L'\0');
    const DWORD length =
        GetTempPathW(static_cast<DWORD>(tempPath.size()), tempPath.data());
    if (length > 0 && length < tempPath.size()) {
        tempPath.resize(length);
        return std::filesystem::path(tempPath) /
               L"shingiittai_hand_udp_sender.log";
    }
    return ResolveExecutableDirectory() / L"shingiittai_hand_udp_sender.log";
}

class HandUdpSenderProcess {
  public:
    ~HandUdpSenderProcess() { Stop(); }

    static bool IsRuntimeAvailable() {
        if (IsDisabled()) {
            return false;
        }

        const std::filesystem::path runtimeRoot = ResolveRuntimeRoot();
        const std::filesystem::path modelPath =
            runtimeRoot / L"tools" / L"hand_tracking" / L"models" /
            L"hand_landmarker.task";
        const std::filesystem::path packagedExe =
            runtimeRoot / L"tools" / L"hand_tracking" / L"hand_udp_sender" /
            L"hand_udp_sender.exe";
        const std::filesystem::path scriptPath =
            runtimeRoot / L"tools" / L"hand_tracking" / L"hand_udp_sender.py";
        return std::filesystem::exists(modelPath) &&
               (std::filesystem::exists(packagedExe) ||
                std::filesystem::exists(scriptPath));
    }

    void ActivateCamera() {
        shouldKeepRunning_ = true;
        RefreshProcessState();
        if (!isRunning_) {
            Start();
        }
    }

    void RestartCamera() {
        shouldKeepRunning_ = true;
        Stop();
        Start();
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
        const std::filesystem::path modelPath =
            runtimeRoot / L"tools" / L"hand_tracking" / L"models" /
            L"hand_landmarker.task";
        const std::filesystem::path poseModelPath =
            ResolvePoseModelPath(runtimeRoot);

        if (!std::filesystem::exists(modelPath)) {
            return false;
        }

        const std::filesystem::path packagedExe =
            runtimeRoot / L"tools" / L"hand_tracking" / L"hand_udp_sender" /
            L"hand_udp_sender.exe";
        const std::filesystem::path scriptPath =
            runtimeRoot / L"tools" / L"hand_tracking" / L"hand_udp_sender.py";
        const std::filesystem::path venvPython =
            runtimeRoot / L"tools" / L"hand_tracking" / L".venv" / L"Scripts" /
            L"python.exe";
        const auto appendPoseModel = [&](std::wstring& command) {
            if (!command.empty() && std::filesystem::exists(poseModelPath)) {
                command += L" --pose-model \"" + poseModelPath.wstring() + L"\"";
            }
        };
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
        appendPoseModel(scriptCommand);
        appendCameraArg(scriptCommand, L"SHINGIITTAI_MAIN_CAMERA", L"--camera");
        std::wstring packagedCommand;
        if (std::filesystem::exists(packagedExe)) {
            packagedCommand = L"\"" + packagedExe.wstring() + L"\" --model \"" +
                              modelPath.wstring() + L"\"";
        }
        appendPoseModel(packagedCommand);
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
        startupInfo.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
        startupInfo.wShowWindow = SW_HIDE;
        const std::filesystem::path logPath = ResolveHandTrackingLogPath();
        SECURITY_ATTRIBUTES securityAttributes{};
        securityAttributes.nLength = sizeof(securityAttributes);
        securityAttributes.bInheritHandle = TRUE;
        HANDLE logHandle = CreateFileW(
            logPath.wstring().c_str(), FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE, &securityAttributes, OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (logHandle != INVALID_HANDLE_VALUE) {
            startupInfo.hStdOutput = logHandle;
            startupInfo.hStdError = logHandle;
            startupInfo.hStdInput = nullptr;
            std::ofstream log(logPath, std::ios::app);
            log << "\n=== hand_udp_sender start ===\n";
        } else {
            startupInfo.dwFlags = STARTF_USESHOWWINDOW;
        }
        const BOOL inheritHandles =
            logHandle != INVALID_HANDLE_VALUE ? TRUE : FALSE;
        PROCESS_INFORMATION processInfo{};
        const BOOL started = CreateProcessW(
            nullptr, command.data(), nullptr, nullptr, inheritHandles,
            CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
            runtimeRoot.wstring().c_str(),
            &startupInfo, &processInfo);
        if (!started) {
            if (jobHandle != nullptr) {
                CloseHandle(jobHandle);
            }
            if (logHandle != INVALID_HANDLE_VALUE) {
                CloseHandle(logHandle);
            }
            return false;
        }
        if (logHandle != INVALID_HANDLE_VALUE) {
            CloseHandle(logHandle);
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

    static std::filesystem::path
    ResolvePoseModelPath(const std::filesystem::path &runtimeRoot) {
        const std::filesystem::path modelDir =
            runtimeRoot / L"tools" / L"hand_tracking" / L"models";
        const std::filesystem::path full =
            modelDir / L"pose_landmarker_full.task";
        const std::filesystem::path lite =
            modelDir / L"pose_landmarker_lite.task";

        if (std::filesystem::exists(full)) {
            return full;
        }
        return lite;
    }

    static std::filesystem::path ResolveRepoRoot() {
        std::filesystem::path sourcePath = std::filesystem::path(__FILE__);
        if (!sourcePath.is_absolute()) {
            sourcePath = std::filesystem::current_path() / sourcePath;
        }

        return sourcePath.parent_path().parent_path().parent_path();
    }

    static std::filesystem::path ResolveRuntimeRoot() {
        const std::filesystem::path executableDir = ResolveExecutableDirectory();
        const std::filesystem::path repoRoot = ResolveRepoRoot();
        const auto hasSender = [](const std::filesystem::path &root) {
            return std::filesystem::exists(root / L"tools" / L"hand_tracking" /
                                           L"hand_udp_sender.py") &&
                   std::filesystem::exists(root / L"tools" / L"hand_tracking" /
                                           L"models" /
                                           L"hand_landmarker.task");
        };
        const auto hasVenv = [](const std::filesystem::path &root) {
            return std::filesystem::exists(root / L"tools" / L"hand_tracking" /
                                           L".venv" / L"Scripts" /
                                           L"python.exe");
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

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    SetCurrentDirectoryW(ResolveExecutableDirectory().wstring().c_str());

    HandUdpSenderProcess handUdpSenderProcess;
    const bool handTrackingRuntimeAvailable =
        HandUdpSenderProcess::IsRuntimeAvailable();

    // WinApp初期化
    WinApp winApp;
    winApp.Initialize(hInstance, nCmdShow, 1280, 720, L"3145_身技一体", true);

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
    sceneCtx.requestHandTrackingStart = [&handUdpSenderProcess, &winApp]() {
        handUdpSenderProcess.ActivateCamera();
        winApp.BringToFront();
    };
    sceneCtx.requestHandTrackingRestart = [&handUdpSenderProcess, &winApp]() {
        handUdpSenderProcess.RestartCamera();
        winApp.BringToFront();
    };
    sceneCtx.isCameraDeviceAvailable = [handTrackingRuntimeAvailable]() {
        return handTrackingRuntimeAvailable;
    };
    sceneCtx.isHandTrackingReady = [&handUdpSenderProcess]() {
        (void)handUdpSenderProcess;
        return true;
    };
    sceneCtx.deltaTime = 0.0f;
    if (handTrackingRuntimeAvailable) {
        handUdpSenderProcess.ActivateCamera();
    }

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
        handUdpSenderProcess.Update();

        // 入力更新
        input.Update(deltaTime);
        if (input.IsKeyTrigger(DIK_ESCAPE)) {
            break;
        }

        const int currentWidth = winApp.GetWidth();
        const int currentHeight = winApp.GetHeight();
        if (currentWidth > 0 && currentHeight > 0 &&
            (currentWidth != width || currentHeight != height)) {
            width = currentWidth;
            height = currentHeight;
            dxCommon.Resize(width, height);
            postEffectRenderer.Resize(width, height);
            spriteManager.Resize(width, height);
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
