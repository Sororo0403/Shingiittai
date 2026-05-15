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
constexpr float kHandSpeedDeadzone = 0.34f;
constexpr float kHandSpeedToSwordSpeed = 2300.0f;
constexpr float kHandMinConfidence = 0.28f;
constexpr float kHandNoiseFrameDelta = 0.32f;
constexpr float kHandPositionFollow = 0.62f;
constexpr float kHandNoisyPositionFollow = 0.16f;
constexpr float kHandVelocityFollow = 0.58f;
constexpr float kHandNoisyVelocityDecay = 0.72f;
constexpr float kHandPositionLead = 1.35f;
constexpr float kHandMaxLead = 0.075f;
constexpr float kSlashDirMinFrameDelta = 0.0055f;
constexpr float kSlashDirStartRatio = 0.90f;
constexpr size_t kSlashDirectionMinSamples = 3;
constexpr float kSlashDirectionMinStability = 0.74f;
constexpr float kSlashDirectionResetRatio = 0.35f;
constexpr float kHandRestSpeedMultiplier = 2.2f;
constexpr float kHandRestSpeedPadding = 0.16f;
constexpr float kReturnRecoverySeconds = 0.22f;
constexpr float kReturnRecoveryOppositeDot = -0.62f;
constexpr float kLostSlashGraceSeconds = 0.16f;
constexpr float kLostSlashSpeedDecay = 0.82f;

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
    return HasFreshTracking(handIndex) ||
           (hand != nullptr && hand->lostSlashGraceTimer > 0.0f &&
            hand->state.isSlashMode);
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
    return hand->speed;
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
        hand.motionDirectionCount = 0;
        hand.motionDirectionCursor = 0;
        hand.directionStability = 0.0f;
        hand.returnRecoveryTimer = 0.0f;
        hand.lostSlashGraceTimer = 0.0f;
        hand.lastActiveMotionSpeed = 0.0f;
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

bool SwordUdpController::HasFreshTracking(size_t handIndex) const {
    const HandState *hand = GetHand(handIndex);
    return hand != nullptr && hand->hasPacket &&
           hand->staleTimer < kStaleSeconds && hand->valid != 0;
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

    if (!HasFreshTracking(handIndex)) {
        if (hand->lostSlashGraceTimer > 0.0f && hand->state.isSlashMode) {
            hand->lostSlashGraceTimer =
                (std::max)(0.0f, hand->lostSlashGraceTimer - dt);
            hand->lastActiveMotionSpeed *= kLostSlashSpeedDecay;
            hand->motionSpeed = (std::max)(
                hand->lastActiveMotionSpeed,
                SwordControllerState::kSlashThreshold * 0.55f);
            hand->state.UpdateSlash(hand->motionSpeed, dt);
            return;
        }

        hand->motionSpeed = 0.0f;
        hand->filterReady = false;
        hand->motionDirectionCount = 0;
        hand->motionDirectionCursor = 0;
        hand->directionStability = 0.0f;
        hand->returnRecoveryTimer = 0.0f;
        hand->lostSlashGraceTimer = 0.0f;
        hand->lastActiveMotionSpeed = 0.0f;
        hand->state.UpdateSlash(0.0f, dt);
        return;
    }

    const float rawDeltaLength =
        std::sqrt(hand->dx * hand->dx + hand->dy * hand->dy);
    const float rawSpeed = hand->speed;
    const bool noisyPacket =
        hand->confidence < kHandMinConfidence ||
        rawDeltaLength > kHandNoiseFrameDelta;

    if (!hand->filterReady) {
        hand->filteredX = hand->x;
        hand->filteredY = hand->y;
        hand->filteredDx = 0.0f;
        hand->filteredDy = 0.0f;
        hand->filteredSpeed = 0.0f;
        hand->motionDirectionCount = 0;
        hand->motionDirectionCursor = 0;
        hand->directionStability = 0.0f;
        hand->returnRecoveryTimer = 0.0f;
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
    const float leadX = std::clamp(hand->filteredDx * kHandPositionLead,
                                   -kHandMaxLead, kHandMaxLead);
    const float leadY = std::clamp(hand->filteredDy * kHandPositionLead,
                                   -kHandMaxLead, kHandMaxLead);
    const float ledX = noisyPacket ? hand->filteredX : hand->filteredX + leadX;
    const float ledY = noisyPacket ? hand->filteredY : hand->filteredY + leadY;
    const float calibratedX =
        std::clamp(0.5f + ledX - neutral.x, 0.0f, 1.0f);
    const float calibratedY =
        std::clamp(0.5f + ledY - neutral.y, 0.0f, 1.0f);

    const float yaw = (calibratedX - 0.5f) * 2.0f * kHandYawRange;
    const float pitch = (calibratedY - 0.5f) * 2.0f * kHandPitchRange;
    XMVECTOR qYaw = XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 0), yaw);
    XMVECTOR qPitch =
        XMQuaternionRotationAxis(XMVectorSet(1, 0, 0, 0), pitch);
    XMVECTOR q = XMQuaternionNormalize(XMQuaternionMultiply(qPitch, qYaw));
    XMStoreFloat4(&hand->state.orientation, q);

    const float filteredDeltaLength = std::sqrt(
        hand->filteredDx * hand->filteredDx + hand->filteredDy * hand->filteredDy);
    const float calibratedDeadzone =
        calibration_.hasHandRestSpeed
            ? calibration_.handRestSpeed[handIndex] * kHandRestSpeedMultiplier +
                  kHandRestSpeedPadding
            : kHandSpeedDeadzone;
    const float speedDeadzone = (std::max)(kHandSpeedDeadzone, calibratedDeadzone);
    const float effectiveSpeed =
        (std::max)(hand->filteredSpeed - speedDeadzone, 0.0f);
    hand->motionSpeed = effectiveSpeed * kHandSpeedToSwordSpeed;

    if (!noisyPacket && filteredDeltaLength > kSlashDirMinFrameDelta) {
        const float invLength = 1.0f / filteredDeltaLength;
        const float currentDirX = hand->filteredDx * invLength;
        const float currentDirY = hand->filteredDy * invLength;
        hand->motionDirX[hand->motionDirectionCursor] = currentDirX;
        hand->motionDirY[hand->motionDirectionCursor] = currentDirY;
        hand->motionDirectionCursor =
            (hand->motionDirectionCursor + 1) % kMotionDirectionHistorySize;
        if (hand->motionDirectionCount < kMotionDirectionHistorySize) {
            ++hand->motionDirectionCount;
        }

        float dotTotal = 0.0f;
        for (size_t i = 0; i < hand->motionDirectionCount; ++i) {
            dotTotal += currentDirX * hand->motionDirX[i] +
                        currentDirY * hand->motionDirY[i];
        }
        hand->directionStability = std::clamp(
            dotTotal / static_cast<float>(hand->motionDirectionCount), -1.0f,
            1.0f);
    } else if (hand->motionSpeed <
               SwordControllerState::kSlashThreshold *
                   kSlashDirectionResetRatio) {
        hand->motionDirectionCount = 0;
        hand->motionDirectionCursor = 0;
        hand->directionStability = 0.0f;
    }

    const bool stableSlashCandidate =
        !noisyPacket && filteredDeltaLength > kSlashDirMinFrameDelta &&
        hand->motionDirectionCount >= kSlashDirectionMinSamples &&
        hand->directionStability >= kSlashDirectionMinStability &&
        hand->motionSpeed >
            SwordControllerState::kSlashThreshold * kSlashDirStartRatio;
    float candidateDirX = hand->stableSlashDirX;
    float candidateDirY = hand->stableSlashDirY;
    if (filteredDeltaLength > kSlashDirMinFrameDelta) {
        const float invLength = 1.0f / filteredDeltaLength;
        candidateDirX = hand->filteredDx * invLength;
        candidateDirY = hand->filteredDy * invLength;
    }
    const float returnDot = candidateDirX * hand->lastSlashDirX +
                            candidateDirY * hand->lastSlashDirY;
    const bool isReturnMotion =
        hand->returnRecoveryTimer > 0.0f &&
        returnDot <= kReturnRecoveryOppositeDot;
    const bool canStartSlash = stableSlashCandidate && !isReturnMotion;
    if (canStartSlash && !hand->state.isSlashMode) {
        hand->stableSlashDirX = candidateDirX;
        hand->stableSlashDirY = candidateDirY;
    }

    hand->state.slashDir = {hand->stableSlashDirX, -hand->stableSlashDirY};
    const bool wasSlashing = hand->state.isSlashMode;
    const float slashMotionSpeed =
        (hand->state.isSlashMode || canStartSlash) ? hand->motionSpeed : 0.0f;
    hand->state.UpdateSlash(slashMotionSpeed, dt);
    if (!wasSlashing && hand->state.isSlashMode) {
        hand->lastSlashDirX = hand->stableSlashDirX;
        hand->lastSlashDirY = hand->stableSlashDirY;
        hand->returnRecoveryTimer = 0.0f;
    } else if (wasSlashing && !hand->state.isSlashMode) {
        hand->returnRecoveryTimer = kReturnRecoverySeconds;
    } else if (hand->returnRecoveryTimer > 0.0f) {
        hand->returnRecoveryTimer =
            (std::max)(0.0f, hand->returnRecoveryTimer - dt);
    }
    if (hand->state.isSlashMode) {
        hand->lostSlashGraceTimer = kLostSlashGraceSeconds;
        hand->lastActiveMotionSpeed = hand->motionSpeed;
    } else {
        hand->lostSlashGraceTimer = 0.0f;
        hand->lastActiveMotionSpeed = 0.0f;
    }
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
