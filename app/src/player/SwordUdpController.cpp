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
constexpr float kHandSpeedDeadzone = 0.35f;
constexpr float kHandSpeedToSwordSpeed = 2300.0f;
constexpr float kHandMinConfidence = 0.28f;
constexpr float kHandNoiseFrameDelta = 0.16f;
constexpr float kHandPositionFollow = 0.38f;
constexpr float kHandNoisyPositionFollow = 0.08f;
constexpr float kHandVelocityFollow = 0.32f;
constexpr float kHandNoisyVelocityDecay = 0.55f;
constexpr float kSlashDirMinFrameDelta = 0.0025f;
constexpr float kSlashDirStartRatio = 0.72f;

SOCKET ToSocket(uintptr_t value) {
    return static_cast<SOCKET>(value);
}

float Lerp(float from, float to, float amount) {
    return from + (to - from) * amount;
}
}

SwordUdpController::~SwordUdpController() {
    CloseSocket();
}

bool SwordUdpController::IsActive(size_t handIndex) const {
    const HandState *hand = GetHand(handIndex);
    return hand != nullptr && hand->hasPacket &&
           hand->staleTimer < kStaleSeconds && hand->valid != 0;
}

SwordPose SwordUdpController::GetPose(size_t handIndex) const {
    const HandState *hand = GetHand(handIndex);
    return hand != nullptr ? hand->state.ToPose() : SwordPose{};
}

float SwordUdpController::GetMotionSpeed(size_t handIndex) const {
    const HandState *hand = GetHand(handIndex);
    return hand != nullptr ? hand->motionSpeed : 0.0f;
}

float SwordUdpController::GetRawMotionSpeed(size_t handIndex) const {
    const HandState *hand = GetHand(handIndex);
    if (hand == nullptr || !IsActive(handIndex)) {
        return 0.0f;
    }
    return (std::max)(hand->speed, hand->wristSpeed);
}

bool SwordUdpController::GetHandCenter(size_t handIndex, float &x,
                                       float &y) const {
    const HandState *hand = GetHand(handIndex);
    if (hand == nullptr || !IsActive(handIndex)) {
        return false;
    }

    x = hand->x;
    y = hand->y;
    return true;
}

void SwordUdpController::SetCalibration(
    const SwordInputCalibration &calibration) {
    calibration_ = calibration;
    for (HandState &hand : hands_) {
        hand.filterReady = false;
        hand.motionSpeed = 0.0f;
        hand.state.UpdateSlash(0.0f, 1.0f);
    }
}

void SwordUdpController::Update(float dt) {
    if (!EnsureSocket()) {
        return;
    }

    ReceivePackets();
    for (size_t i = 0; i < hands_.size(); ++i) {
        hands_[i].staleTimer += dt;
        ApplyHand(i, dt);
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
        float angle = 0.0f;
        float grip = 1.0f;
        float wristSpeed = speed;

        std::istringstream stream(buffer);
        if (stream >> tag >> valid >> x >> y >> dx >> dy >> speed >>
            confidence) {
            if (!(stream >> angle)) {
                angle = 0.0f;
            }
            if (!(stream >> grip)) {
                grip = 1.0f;
            }
            if (!(stream >> wristSpeed)) {
                wristSpeed = speed;
            }

            if (HandState *hand = FindHand(tag)) {
                hand->valid = valid;
                hand->x = std::clamp(x, 0.0f, 1.0f);
                hand->y = std::clamp(y, 0.0f, 1.0f);
                hand->dx = dx;
                hand->dy = dy;
                hand->speed = speed;
                hand->confidence = std::clamp(confidence, 0.0f, 1.0f);
                hand->angle = angle;
                hand->grip = std::clamp(grip, 0.0f, 1.0f);
                hand->wristSpeed = wristSpeed;
                hand->hasPacket = true;
                hand->staleTimer = 0.0f;
            }
        }
    }
}

void SwordUdpController::ApplyHand(size_t handIndex, float dt) {
    HandState *hand = handIndex < hands_.size() ? &hands_[handIndex] : nullptr;
    if (hand == nullptr) {
        return;
    }

    hand->state.isGuard = false;
    hand->state.isCounter = false;
    hand->state.counterTimer = SwordControllerState::kCounterFrames;

    if (!IsActive(handIndex)) {
        hand->motionSpeed = 0.0f;
        hand->filterReady = false;
        hand->state.UpdateSlash(0.0f, dt);
        return;
    }

    const float rawDeltaLength =
        std::sqrt(hand->dx * hand->dx + hand->dy * hand->dy);
    const float rawSpeed = (std::max)(hand->speed, hand->wristSpeed);
    const bool noisyPacket =
        hand->confidence < kHandMinConfidence ||
        rawDeltaLength > kHandNoiseFrameDelta;

    if (!hand->filterReady) {
        hand->filteredX = hand->x;
        hand->filteredY = hand->y;
        hand->filteredDx = 0.0f;
        hand->filteredDy = 0.0f;
        hand->filteredSpeed = 0.0f;
        hand->filterReady = true;
    }

    const float positionFollow =
        noisyPacket ? kHandNoisyPositionFollow : kHandPositionFollow;
    hand->filteredX = Lerp(hand->filteredX, hand->x, positionFollow);
    hand->filteredY = Lerp(hand->filteredY, hand->y, positionFollow);

    if (noisyPacket) {
        hand->filteredDx *= kHandNoisyVelocityDecay;
        hand->filteredDy *= kHandNoisyVelocityDecay;
        hand->filteredSpeed *= kHandNoisyVelocityDecay;
    } else {
        hand->filteredDx =
            Lerp(hand->filteredDx, hand->dx, kHandVelocityFollow);
        hand->filteredDy =
            Lerp(hand->filteredDy, hand->dy, kHandVelocityFollow);
        hand->filteredSpeed =
            Lerp(hand->filteredSpeed, rawSpeed, kHandVelocityFollow);
    }

    const DirectX::XMFLOAT2 neutral =
        calibration_.hasHandNeutral
            ? calibration_.handNeutral[handIndex]
            : DirectX::XMFLOAT2{0.5f, 0.5f};
    const float calibratedX =
        std::clamp(0.5f + hand->filteredX - neutral.x, 0.0f, 1.0f);
    const float calibratedY =
        std::clamp(0.5f + hand->filteredY - neutral.y, 0.0f, 1.0f);

    const float yaw = (calibratedX - 0.5f) * 2.0f * kHandYawRange;
    const float pitch = (calibratedY - 0.5f) * 2.0f * kHandPitchRange;
    XMVECTOR qYaw = XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 0), yaw);
    XMVECTOR qPitch =
        XMQuaternionRotationAxis(XMVectorSet(1, 0, 0, 0), pitch);
    XMVECTOR q = XMQuaternionNormalize(XMQuaternionMultiply(qPitch, qYaw));
    XMStoreFloat4(&hand->state.orientation, q);

    const float filteredDeltaLength = std::sqrt(
        hand->filteredDx * hand->filteredDx + hand->filteredDy * hand->filteredDy);
    const float effectiveSpeed =
        (std::max)(hand->filteredSpeed - kHandSpeedDeadzone, 0.0f);
    hand->motionSpeed = effectiveSpeed * kHandSpeedToSwordSpeed;

    const bool stableSlashCandidate =
        !noisyPacket && filteredDeltaLength > kSlashDirMinFrameDelta &&
        hand->motionSpeed >
            SwordControllerState::kSlashThreshold * kSlashDirStartRatio;
    if (stableSlashCandidate && !hand->state.isSlashMode) {
        const float invLength = 1.0f / filteredDeltaLength;
        hand->stableSlashDirX = hand->filteredDx * invLength;
        hand->stableSlashDirY = hand->filteredDy * invLength;
    }

    hand->state.slashDir = {hand->stableSlashDirX, -hand->stableSlashDirY};
    hand->state.UpdateSlash(hand->motionSpeed, dt);
    (void)hand->confidence;
}

void SwordUdpController::CloseSocket() {
    if (socketReady_) {
        closesocket(ToSocket(socket_));
        WSACleanup();
    }

    socket_ = UINTPTR_MAX;
    socketReady_ = false;
}

SwordUdpController::HandState *SwordUdpController::FindHand(
    const std::string &tag) {
    if (tag == "HAND1") {
        return &hands_[0];
    }
    if (tag == "HAND2") {
        return &hands_[1];
    }
    return nullptr;
}

const SwordUdpController::HandState *SwordUdpController::GetHand(
    size_t handIndex) const {
    if (handIndex >= hands_.size()) {
        return nullptr;
    }
    return &hands_[handIndex];
}
