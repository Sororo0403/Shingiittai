#include "SwordUdpController.h"
#include <DirectXMath.h>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

using namespace DirectX;

namespace {
SOCKET ToSocket(uintptr_t value) {
    return static_cast<SOCKET>(value);
}
}

SwordUdpController::~SwordUdpController() {
    CloseSocket();
}

bool SwordUdpController::IsActive(size_t handIndex) const {
    if (handIndex >= actionSwordStates_.size() || !HasFreshActionInput()) {
        return false;
    }
    return handIndex == 0 ||
           actionInput_.slashConfidence[handIndex] > 0.0f ||
           actionSwordStates_[handIndex].isSlashMode;
}

SwordPose SwordUdpController::GetPose(size_t handIndex) const {
    if (handIndex < actionSwordStates_.size() && HasFreshActionInput()) {
        return actionSwordStates_[handIndex].ToPose();
    }
    return SwordPose{};
}

float SwordUdpController::GetMotionSpeed(size_t handIndex) const {
    return handIndex < actionInput_.slashSpeed.size() && HasFreshActionInput()
               ? actionInput_.slashSpeed[handIndex]
               : 0.0f;
}

float SwordUdpController::GetRawMotionSpeed(size_t handIndex) const {
    return GetMotionSpeed(handIndex);
}

bool SwordUdpController::GetHandCenter(size_t handIndex, float &x,
                                       float &y) const {
    if (handIndex >= actionSwordStates_.size() || !HasFreshActionInput()) {
        return false;
    }
    x = 0.5f;
    y = 0.5f;
    return true;
}

bool SwordUdpController::HasRecentPacket() const {
    return HasFreshActionInput();
}

void SwordUdpController::SetCalibration(
    const SwordInputCalibration &calibration) {
    calibration_ = calibration;
    actionInput_ = {};
    actionInput_.staleTimer = kStaleSeconds;
    actionSwordStates_ = {};
}

void SwordUdpController::Update(float dt) {
    if (!EnsureSocket()) {
        return;
    }

    ReceivePackets();
    actionInput_.staleTimer += dt;
    if (HasFreshActionInput()) {
        ApplyActionInput(dt);
    } else {
        for (SwordControllerState &state : actionSwordStates_) {
            state.UpdateSlash(0.0f, dt);
        }
    }
}

bool SwordUdpController::EnsureSocket() {
    if (socketReady_) {
        return true;
    }

    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return false;
    }

    SOCKET udpSocket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udpSocket == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(kPort);

    if (bind(udpSocket, reinterpret_cast<sockaddr *>(&address),
             sizeof(address)) == SOCKET_ERROR) {
        closesocket(udpSocket);
        WSACleanup();
        return false;
    }

    u_long nonBlocking = 1;
    if (ioctlsocket(udpSocket, FIONBIO, &nonBlocking) == SOCKET_ERROR) {
        closesocket(udpSocket);
        WSACleanup();
        return false;
    }

    socket_ = static_cast<uintptr_t>(udpSocket);
    socketReady_ = true;
    return true;
}

bool SwordUdpController::HasFreshActionInput() const {
    return actionInput_.hasPacket && actionInput_.staleTimer < kStaleSeconds;
}

void SwordUdpController::ReceivePackets() {
    char buffer[512]{};
    for (;;) {
        sockaddr_in from{};
        int fromLength = sizeof(from);
        const int bytes = recvfrom(ToSocket(socket_), buffer,
                                   static_cast<int>(sizeof(buffer) - 1), 0,
                                   reinterpret_cast<sockaddr *>(&from),
                                   &fromLength);
        if (bytes == SOCKET_ERROR) {
            const int error = WSAGetLastError();
            if (error == WSAEWOULDBLOCK) {
                return;
            }
            return;
        }

        buffer[bytes] = '\0';

        std::string tag;
        std::istringstream stream(buffer);
        stream >> tag;
        if (tag != "PLAYER_INPUT") {
            continue;
        }

        std::vector<double> values;
        double value = 0.0;
        while (stream >> value) {
            values.push_back(value);
        }
        if (values.size() >= 9) {
            const double timestamp = values[0];
            (void)timestamp;
            std::array<float, 2> slashSpeed = {0.0f, 0.0f};
            std::array<float, 2> slashDirX = {1.0f, -1.0f};
            std::array<float, 2> slashDirY = {0.0f, 0.0f};
            std::array<float, 2> slashConfidence = {0.0f, 0.0f};
            slashSpeed[0] = static_cast<float>(values[1]);
            slashDirX[0] = static_cast<float>(values[2]);
            slashDirY[0] = static_cast<float>(values[3]);
            slashConfidence[0] = static_cast<float>(values[4]);
            slashSpeed[1] = static_cast<float>(values[5]);
            slashDirX[1] = static_cast<float>(values[6]);
            slashDirY[1] = static_cast<float>(values[7]);
            slashConfidence[1] = static_cast<float>(values[8]);
            uint32_t debugFlags = 0;
            if (values.size() >= 10) {
                debugFlags = static_cast<uint32_t>(values[9]);
            }
            actionInput_.hasPacket = true;
            actionInput_.staleTimer = 0.0f;
            for (size_t i = 0; i < actionInput_.slashSpeed.size(); ++i) {
                actionInput_.slashSpeed[i] = (std::max)(0.0f, slashSpeed[i]);
                actionInput_.slashDirX[i] = slashDirX[i];
                actionInput_.slashDirY[i] = slashDirY[i];
                actionInput_.slashConfidence[i] =
                    std::clamp(slashConfidence[i], 0.0f, 1.0f);
            }
            actionInput_.debugFlags = debugFlags;
        }
    }
}

void SwordUdpController::ApplyActionInput(float dt) {
    for (size_t i = 0; i < actionSwordStates_.size(); ++i) {
        const float dirLen =
            std::sqrt(actionInput_.slashDirX[i] * actionInput_.slashDirX[i] +
                      actionInput_.slashDirY[i] * actionInput_.slashDirY[i]);
        float dirX = i == 0 ? 1.0f : -1.0f;
        float dirY = 0.0f;
        if (dirLen > 0.001f) {
            dirX = actionInput_.slashDirX[i] / dirLen;
            dirY = actionInput_.slashDirY[i] / dirLen;
        }

        SwordControllerState &state = actionSwordStates_[i];
        state.slashDir = {dirX, dirY};
        const float yaw = dirX * 0.52f;
        const float pitch = -dirY * 0.46f;
        XMVECTOR qYaw =
            XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 0), yaw);
        XMVECTOR qPitch =
            XMQuaternionRotationAxis(XMVectorSet(1, 0, 0, 0), pitch);
        XMStoreFloat4(
            &state.orientation,
            XMQuaternionNormalize(XMQuaternionMultiply(qPitch, qYaw)));

        const float confidence =
            std::clamp(actionInput_.slashConfidence[i], 0.0f, 1.0f);
        const float confidenceGate = confidence >= 0.22f ? 1.0f : 0.0f;
        const float confidenceScaledSpeed =
            actionInput_.slashSpeed[i] * (0.78f + confidence * 0.22f) *
            confidenceGate;
        state.UpdateSlash(confidenceScaledSpeed, dt);
        state.isGuard = false;
        state.isCounter = false;
    }
}

void SwordUdpController::CloseSocket() {
    if (socketReady_) {
        closesocket(ToSocket(socket_));
        WSACleanup();
    }

    socket_ = UINTPTR_MAX;
    socketReady_ = false;
}
