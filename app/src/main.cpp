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
#include <mfapi.h>
#include <mfidl.h>
#include <memory>
#include <objbase.h>
#include <string>

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")

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

bool DetectVideoCaptureDevice() {
    const HRESULT coResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool shouldUninitializeCom = SUCCEEDED(coResult);

    bool found = false;
    if (SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) {
        IMFAttributes *attributes = nullptr;
        if (SUCCEEDED(MFCreateAttributes(&attributes, 1))) {
            HRESULT hr = attributes->SetGUID(
                MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
            IMFActivate **devices = nullptr;
            UINT32 count = 0;
            if (SUCCEEDED(hr)) {
                hr = MFEnumDeviceSources(attributes, &devices, &count);
            }
            if (SUCCEEDED(hr)) {
                found = count > 0;
            }

            for (UINT32 i = 0; i < count; ++i) {
                if (devices[i] != nullptr) {
                    devices[i]->Release();
                }
            }
            CoTaskMemFree(devices);
            attributes->Release();
        }
        MFShutdown();
    }

    if (shouldUninitializeCom) {
        CoUninitialize();
    }

    return found;
}

class HandUdpSenderProcess {
  public:
    ~HandUdpSenderProcess() { Stop(); }

    bool Prepare() { return Start(true); }

    bool IsPreparationReady() {
        PollStatus();
        return preparationReady_;
    }

    void ActivateCamera() {
        if (!isRunning_) {
            Start(false);
            cameraActivationRequested_ = false;
            return;
        }
        cameraActivationRequested_ = true;
        cameraActivationStartTick_ = GetTickCount();
        lastCameraStartCommandTick_ = 0;
        SendCameraStartCommand();
    }

    void Update() {
        PollStatus();
        if (!cameraActivationRequested_) {
            return;
        }

        const DWORD now = GetTickCount();
        if (now - cameraActivationStartTick_ > 5000) {
            cameraActivationRequested_ = false;
            return;
        }
        if (now - lastCameraStartCommandTick_ < 150) {
            return;
        }

        SendCameraStartCommand();
        lastCameraStartCommandTick_ = now;
    }

    bool Start(bool startPaused) {
        if (isRunning_) {
            return true;
        }
        if (IsDisabled()) {
            return false;
        }

        const std::filesystem::path runtimeRoot = ResolveRuntimeRoot();
        const std::filesystem::path modelPath =
            runtimeRoot / L"tools" / L"hand_tracking" / L"models" /
            L"hand_landmarker.task";

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
        if (!scriptCommand.empty() && startPaused) {
            scriptCommand += L" --start-paused --status-port 5008";
        }

        std::wstring packagedCommand;
        if (std::filesystem::exists(packagedExe)) {
            packagedCommand = L"\"" + packagedExe.wstring() + L"\" --model \"" +
                              modelPath.wstring() + L"\"";
        }
        if (!packagedCommand.empty() && startPaused) {
            packagedCommand += L" --start-paused --status-port 5008";
        }

        std::wstring command;
        if (startPaused && !scriptCommand.empty()) {
            command = scriptCommand;
        } else {
#ifdef _DEBUG
            if (!scriptCommand.empty()) {
                command = scriptCommand;
            } else {
                command = packagedCommand;
            }
#else
            if (!packagedCommand.empty()) {
                command = packagedCommand;
            } else {
                command = scriptCommand;
            }
#endif
        }

        if (command.empty()) {
            return false;
        }

        if (startPaused && !OpenStatusSocket()) {
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
            CloseStatusSocket();
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
        preparationReady_ = !startPaused;
        return true;
    }

  private:
    bool OpenStatusSocket() {
        CloseStatusSocket();

        WSADATA wsaData{};
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            return false;
        }

        statusSocket_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (statusSocket_ == INVALID_SOCKET) {
            statusSocket_ = INVALID_SOCKET;
            WSACleanup();
            return false;
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(5008);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        if (bind(statusSocket_, reinterpret_cast<sockaddr *>(&address),
                 sizeof(address)) == SOCKET_ERROR) {
            CloseStatusSocket();
            return false;
        }

        u_long nonBlocking = 1;
        if (ioctlsocket(statusSocket_, FIONBIO, &nonBlocking) == SOCKET_ERROR) {
            CloseStatusSocket();
            return false;
        }

        return true;
    }

    void CloseStatusSocket() {
        if (statusSocket_ != INVALID_SOCKET) {
            closesocket(statusSocket_);
            statusSocket_ = INVALID_SOCKET;
            WSACleanup();
        }
    }

    void PollStatus() {
        if (statusSocket_ == INVALID_SOCKET || preparationReady_) {
            return;
        }

        char buffer[128]{};
        for (;;) {
            sockaddr_in from{};
            int fromLength = sizeof(from);
            const int bytes =
                recvfrom(statusSocket_, buffer,
                         static_cast<int>(sizeof(buffer) - 1), 0,
                         reinterpret_cast<sockaddr *>(&from), &fromLength);
            if (bytes == SOCKET_ERROR) {
                return;
            }

            buffer[bytes] = '\0';
            if (std::string(buffer) == "SGCAMERA_READY") {
                preparationReady_ = true;
                CloseStatusSocket();
                return;
            }
        }
    }

    static void SendCameraStartCommand() {
        WSADATA wsaData{};
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            return;
        }

        SOCKET udpSocket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (udpSocket == INVALID_SOCKET) {
            WSACleanup();
            return;
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(5007);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        constexpr char kCommand[] = "SGCAMERA_START";
        sendto(udpSocket, kCommand, static_cast<int>(sizeof(kCommand) - 1), 0,
               reinterpret_cast<sockaddr *>(&address), sizeof(address));
        closesocket(udpSocket);
        WSACleanup();
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

    static std::filesystem::path ResolveRuntimeRoot() {
        const std::filesystem::path executableDir = ResolveExecutableDirectory();
        if (std::filesystem::exists(executableDir / L"tools" /
                                    L"hand_tracking" /
                                    L"hand_udp_sender.py")) {
            return executableDir;
        }
        return ResolveRepoRoot();
    }

    void Stop() {
        CloseStatusSocket();
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
    SOCKET statusSocket_ = INVALID_SOCKET;
    bool isRunning_ = false;
    bool preparationReady_ = false;
    bool cameraActivationRequested_ = false;
    DWORD cameraActivationStartTick_ = 0;
    DWORD lastCameraStartCommandTick_ = 0;
};
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    SetCurrentDirectoryW(ResolveExecutableDirectory().wstring().c_str());

    HandUdpSenderProcess handUdpSenderProcess;
    const bool cameraDeviceAvailable = DetectVideoCaptureDevice();
    bool cameraPreparationStarted = false;
    if (cameraDeviceAvailable) {
        cameraPreparationStarted = handUdpSenderProcess.Prepare();
    }

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
    sceneCtx.isCameraDeviceAvailable = [cameraDeviceAvailable]() {
        return cameraDeviceAvailable;
    };
    sceneCtx.isHandTrackingReady =
        [&handUdpSenderProcess, cameraPreparationStarted]() {
            return !cameraPreparationStarted ||
                   handUdpSenderProcess.IsPreparationReady();
        };
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
        handUdpSenderProcess.Update();

        // 入力更新
        input.Update(deltaTime);
        if (input.IsKeyTrigger(DIK_ESCAPE)) {
            break;
        }
        if (input.IsKeyTrigger(DIK_F11)) {
            winApp.ToggleFullscreen();
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
