#include "SwordUdpController.h"
#include <DirectXMath.h>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>

using namespace DirectX;

namespace {
constexpr float kHandYawRange = 1.15f;
constexpr float kHandPitchRange = 0.95f;
constexpr float kHandSpeedToSwordSpeed = 3000.0f;

SOCKET ToSocket(uintptr_t value) {
    return static_cast<SOCKET>(value);
}
}

SwordUdpController::~SwordUdpController() {
    CloseSocket();
}

SwordPose SwordUdpController::GetPose() const {
    return state_.ToPose();
}

void SwordUdpController::Update(float dt) {
    if (!EnsureSocket()) {
        return;
    }

    ReceivePackets();
    staleTimer_ += dt;
    ApplyHand(dt);
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

void SwordUdpController::ReceivePackets() {
    char buffer[256]{};
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
        int valid = 0;
        float x = 0.5f;
        float y = 0.5f;
        float dx = 0.0f;
        float dy = 0.0f;
        float speed = 0.0f;
        float confidence = 0.0f;

        std::istringstream stream(buffer);
        if (stream >> tag >> valid >> x >> y >> dx >> dy >> speed >>
            confidence) {
            if (tag == "HAND1") {
                valid_ = valid;
                handX_ = std::clamp(x, 0.0f, 1.0f);
                handY_ = std::clamp(y, 0.0f, 1.0f);
                handDx_ = dx;
                handDy_ = dy;
                handSpeed_ = speed;
                confidence_ = std::clamp(confidence, 0.0f, 1.0f);
                hasPacket_ = true;
                staleTimer_ = 0.0f;
            }
        }
    }
}

void SwordUdpController::ApplyHand(float dt) {
    state_.isGuard = false;
    state_.isCounter = false;
    state_.counterTimer = SwordControllerState::kCounterFrames;

    if (!IsActive() || valid_ == 0) {
        motionSpeed_ = 0.0f;
        state_.UpdateSlash(0.0f, dt);
        return;
    }

    const float yaw = (handX_ - 0.5f) * 2.0f * kHandYawRange;
    const float pitch = (handY_ - 0.5f) * 2.0f * kHandPitchRange;
    XMVECTOR qYaw = XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 0), yaw);
    XMVECTOR qPitch =
        XMQuaternionRotationAxis(XMVectorSet(1, 0, 0, 0), pitch);
    XMVECTOR q = XMQuaternionNormalize(XMQuaternionMultiply(qPitch, qYaw));
    XMStoreFloat4(&state_.orientation, q);

    const float deltaLength =
        std::sqrt(handDx_ * handDx_ + handDy_ * handDy_);
    if (deltaLength > 0.00001f) {
        const float invLength = 1.0f / deltaLength;
        state_.slashDir = {handDx_ * invLength, -handDy_ * invLength};
    }

    motionSpeed_ = handSpeed_ * kHandSpeedToSwordSpeed;
    state_.UpdateSlash(motionSpeed_, dt);
    (void)confidence_;
}

void SwordUdpController::CloseSocket() {
    if (socketReady_) {
        closesocket(ToSocket(socket_));
        WSACleanup();
    }

    socket_ = UINTPTR_MAX;
    socketReady_ = false;
}
