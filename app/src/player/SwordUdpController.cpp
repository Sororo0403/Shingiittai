#include "SwordUdpController.h"
#include <DirectXMath.h>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

using namespace DirectX;

namespace {
constexpr char kRawMagic[] = "HAND_RAW ";

SOCKET ToSocket(uintptr_t value) {
    return static_cast<SOCKET>(value);
}

bool ReadPoint01(const nlohmann::json &object, const char *key,
                 DirectX::XMFLOAT2 &out) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_array() || it->size() < 2) {
        return false;
    }

    out.x = std::clamp((*it)[0].get<float>(), 0.0f, 1.0f);
    out.y = std::clamp((*it)[1].get<float>(), 0.0f, 1.0f);
    return true;
}
}

SwordUdpController::~SwordUdpController() {
    CloseSocket();
}

bool SwordUdpController::IsActive(size_t handIndex) const {
    if (handIndex >= rawInput_.active.size() || !HasFreshRawInput()) {
        return false;
    }
    return rawInput_.active[handIndex];
}

SwordPose SwordUdpController::GetPose(size_t handIndex) const {
    if (handIndex < swordStates_.size() && HasFreshRawInput() &&
        rawInput_.active[handIndex]) {
        return swordStates_[handIndex].ToPose();
    }
    return SwordPose{};
}

float SwordUdpController::GetMotionSpeed(size_t handIndex) const {
    if (handIndex >= motionSpeed_.size() || !HasFreshRawInput() ||
        !rawInput_.active[handIndex]) {
        return 0.0f;
    }
    return motionSpeed_[handIndex];
}

void SwordUdpController::SetCalibration(
    const SwordInputCalibration &calibration) {
    calibration_ = calibration;
    rawInput_ = {};
    rawInput_.staleTimer = kStaleSeconds;
    swordStates_ = {};
    calibratedPalm_ = {DirectX::XMFLOAT2{0.5f, 0.5f},
                       DirectX::XMFLOAT2{0.5f, 0.5f}};
    previousCalibratedPalm_ = {DirectX::XMFLOAT2{0.5f, 0.5f},
                               DirectX::XMFLOAT2{0.5f, 0.5f}};
    hasPreviousCalibratedPalm_ = {false, false};
    motionSpeed_ = {0.0f, 0.0f};
}

void SwordUdpController::Update(float dt) {
    if (!EnsureSocket()) {
        return;
    }

    ReceivePackets();
    rawInput_.staleTimer += dt;
    if (HasFreshRawInput()) {
        ApplyRawInput(dt);
    } else {
        rawInput_.active = {false, false};
        swordStates_ = {};
        hasPreviousCalibratedPalm_ = {false, false};
        motionSpeed_ = {0.0f, 0.0f};
    }
}

bool SwordUdpController::HasFreshInput() const { return HasFreshRawInput(); }

SwordUdpController::DebugHandState
SwordUdpController::GetDebugHandState(size_t handIndex) const {
    DebugHandState debug{};
    debug.fresh = HasFreshRawInput();
    debug.staleTimer = rawInput_.staleTimer;
    if (handIndex >= rawInput_.active.size()) {
        return debug;
    }

    debug.active = debug.fresh && rawInput_.active[handIndex];
    debug.rawPalm = rawInput_.palm[handIndex];
    debug.neutral = calibration_.hasHandNeutral
                        ? calibration_.handNeutral[handIndex]
                        : DirectX::XMFLOAT2{0.5f, 0.5f};
    debug.calibratedPalm = calibratedPalm_[handIndex];
    debug.slashDir = swordStates_[handIndex].slashDir;
    debug.orientation = swordStates_[handIndex].orientation;
    debug.isSlashMode = debug.active && swordStates_[handIndex].isSlashMode;
    debug.motionSpeed = debug.active ? motionSpeed_[handIndex] : 0.0f;
    return debug;
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

bool SwordUdpController::HasFreshRawInput() const {
    return rawInput_.hasPacket && rawInput_.staleTimer < kStaleSeconds;
}

void SwordUdpController::ReceivePackets() {
    char buffer[16384]{};
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
        const size_t rawMagicSize = std::strlen(kRawMagic);
        if (static_cast<size_t>(bytes) < rawMagicSize ||
            std::strncmp(buffer, kRawMagic, rawMagicSize) != 0) {
            continue;
        }

        try {
            const nlohmann::json packet =
                nlohmann::json::parse(buffer + rawMagicSize);
            const auto handsIt = packet.find("hands");
            if (handsIt == packet.end() || !handsIt->is_array()) {
                continue;
            }

            rawInput_.active = {false, false};
            for (size_t i = 0; i < handsIt->size() && i < rawInput_.palm.size();
                 ++i) {
                DirectX::XMFLOAT2 palm{};
                if (ReadPoint01((*handsIt)[i], "mirroredPalm01", palm) ||
                    ReadPoint01((*handsIt)[i], "palm01", palm)) {
                    rawInput_.palm[i] = palm;
                    rawInput_.active[i] = true;
                }
            }
            rawInput_.hasPacket = true;
            rawInput_.staleTimer = 0.0f;
        } catch (...) {
            continue;
        }
    }
}

void SwordUdpController::ApplyRawInput(float dt) {
    for (size_t i = 0; i < swordStates_.size(); ++i) {
        SwordControllerState &state = swordStates_[i];
        if (!rawInput_.active[i]) {
            state = {};
            hasPreviousCalibratedPalm_[i] = false;
            motionSpeed_[i] = 0.0f;
            continue;
        }

        const DirectX::XMFLOAT2 &palm = rawInput_.palm[i];
        const DirectX::XMFLOAT2 neutral =
            calibration_.hasHandNeutral ? calibration_.handNeutral[i]
                                        : DirectX::XMFLOAT2{0.5f, 0.5f};
        DirectX::XMFLOAT2 corrected{
            std::clamp(palm.x - neutral.x + 0.5f, 0.0f, 1.0f),
            std::clamp(palm.y - neutral.y + 0.5f, 0.0f, 1.0f)};
        calibratedPalm_[i] = corrected;

        if (hasPreviousCalibratedPalm_[i] && dt > 0.0001f) {
            const float dx = corrected.x - previousCalibratedPalm_[i].x;
            const float dy = corrected.y - previousCalibratedPalm_[i].y;
            motionSpeed_[i] = std::sqrt(dx * dx + dy * dy) / dt;
        } else {
            motionSpeed_[i] = 0.0f;
        }
        previousCalibratedPalm_[i] = corrected;
        hasPreviousCalibratedPalm_[i] = true;

        const float dirX =
            std::clamp((corrected.x - 0.5f) * 2.0f, -1.0f, 1.0f);
        const float dirY =
            std::clamp((0.5f - corrected.y) * 2.0f, -1.0f, 1.0f);

        state.slashDir = {dirX, dirY};
        state.isSlashMode = true;
        state.slashTimer = 0.0f;

        const float yaw = dirX * 0.82f;
        const float pitch = -dirY * 0.72f;
        XMVECTOR qYaw =
            XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 0), yaw);
        XMVECTOR qPitch =
            XMQuaternionRotationAxis(XMVectorSet(1, 0, 0, 0), pitch);
        XMStoreFloat4(
            &state.orientation,
            XMQuaternionNormalize(XMQuaternionMultiply(qPitch, qYaw)));
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
