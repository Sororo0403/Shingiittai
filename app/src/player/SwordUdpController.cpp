#include "SwordUdpController.h"
#include <DirectXMath.h>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace DirectX;

namespace {
constexpr char kRawMagic[] = "HAND_RAW ";
constexpr float kSingleHandLeftThreshold = 0.45f;
constexpr float kSingleHandRightThreshold = 0.55f;
constexpr float kSlowPalmSmoothingAlpha = 0.24f;
constexpr float kFastPalmSmoothingAlpha = 0.72f;
constexpr float kFastPalmSpeed = 2.0f;

struct HandSample {
    DirectX::XMFLOAT2 palm{0.5f, 0.5f};
    bool bodyCorrected = false;
    std::string label{};
    float score = 0.0f;
};

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

float Length(const DirectX::XMFLOAT2 &value) {
    return std::sqrt(value.x * value.x + value.y * value.y);
}

DirectX::XMFLOAT2 LerpPoint(const DirectX::XMFLOAT2 &from,
                            const DirectX::XMFLOAT2 &to, float alpha) {
    return {from.x + (to.x - from.x) * alpha,
            from.y + (to.y - from.y) * alpha};
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
    filteredCalibratedPalm_ = {DirectX::XMFLOAT2{0.5f, 0.5f},
                               DirectX::XMFLOAT2{0.5f, 0.5f}};
    hasFilteredCalibratedPalm_ = {false, false};
    hasPreviousPacketPalm_ = {false, false};
    packetDeltaPalm_ = {DirectX::XMFLOAT2{0.0f, 0.0f},
                        DirectX::XMFLOAT2{0.0f, 0.0f}};
    packetMotionSpeed_ = {0.0f, 0.0f};
    lastAppliedPacketSequence_ = 0;
    packetChangedThisUpdate_ = false;
    motionSpeed_ = {0.0f, 0.0f};
}

void SwordUdpController::Update(float dt) {
    if (!EnsureSocket()) {
        return;
    }

    ReceivePackets();
    rawInput_.staleTimer += dt;
    packetChangedThisUpdate_ =
        rawInput_.hasPacket && rawInput_.sequence != lastAppliedPacketSequence_;
    if (HasFreshRawInput()) {
        ApplyRawInput(dt);
        lastAppliedPacketSequence_ = rawInput_.sequence;
    } else {
        rawInput_.active = {false, false};
        swordStates_ = {};
        hasPreviousCalibratedPalm_ = {false, false};
        hasFilteredCalibratedPalm_ = {false, false};
        packetChangedThisUpdate_ = false;
        motionSpeed_ = {0.0f, 0.0f};
        packetDeltaPalm_ = {DirectX::XMFLOAT2{0.0f, 0.0f},
                            DirectX::XMFLOAT2{0.0f, 0.0f}};
        packetMotionSpeed_ = {0.0f, 0.0f};
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
    debug.packetFrame = rawInput_.frame;
    debug.packetTimestampMs = rawInput_.timestampMs;
    debug.packetSequence = rawInput_.sequence;
    debug.handCount = rawInput_.handCount;
    debug.bodyTracked = rawInput_.bodyTracked;
    debug.bodyCorrected = rawInput_.bodyCorrected[handIndex];
    debug.packetChanged = packetChangedThisUpdate_;
    debug.packetDeltaMs = rawInput_.packetDeltaMs;
    debug.packetDeltaPalm = packetDeltaPalm_[handIndex];
    debug.packetMotionSpeed = debug.active ? packetMotionSpeed_[handIndex] : 0.0f;
    debug.nearEdge =
        debug.active &&
        (debug.rawPalm.x < 0.05f || debug.rawPalm.x > 0.95f ||
         debug.rawPalm.y < 0.05f || debug.rawPalm.y > 0.95f);
    debug.sourceLabel = rawInput_.sourceLabel[handIndex];
    debug.sourceScore = rawInput_.sourceScore[handIndex];
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

            std::vector<HandSample> hands;
            hands.reserve(2);
            for (size_t i = 0; i < handsIt->size() && i < rawInput_.palm.size();
                 ++i) {
                DirectX::XMFLOAT2 palm{};
                const bool hasBodyCorrected =
                    ReadPoint01((*handsIt)[i], "bodyCorrectedMirroredGrip01",
                                palm) ||
                    ReadPoint01((*handsIt)[i], "bodyCorrectedMirroredPalm01",
                                palm);
                if (hasBodyCorrected ||
                    ReadPoint01((*handsIt)[i], "mirroredGrip01", palm) ||
                    ReadPoint01((*handsIt)[i], "mirroredPalm01", palm) ||
                    ReadPoint01((*handsIt)[i], "grip01", palm) ||
                    ReadPoint01((*handsIt)[i], "palm01", palm)) {
                    HandSample sample{};
                    sample.palm = palm;
                    sample.bodyCorrected = hasBodyCorrected;
                    sample.label = (*handsIt)[i].value("label", "");
                    sample.score = (*handsIt)[i].value("score", 0.0f);
                    hands.push_back(sample);
                }
            }

            rawInput_.active = {false, false};
            rawInput_.bodyCorrected = {false, false};
            rawInput_.sourceLabel = {"", ""};
            rawInput_.sourceScore = {0.0f, 0.0f};
            rawInput_.handCount = static_cast<uint32_t>(hands.size());
            const auto bodyIt = packet.find("body");
            rawInput_.bodyTracked =
                bodyIt != packet.end() && !bodyIt->is_null();
            rawInput_.frame = packet.value("frame", 0ull);
            const uint64_t previousTimestampMs = rawInput_.timestampMs;
            rawInput_.timestampMs = packet.value("timestampMs", 0ull);
            rawInput_.packetDeltaMs =
                previousTimestampMs > 0 &&
                        rawInput_.timestampMs >= previousTimestampMs
                    ? rawInput_.timestampMs - previousTimestampMs
                    : 0;
            if (hands.size() >= 2) {
                std::sort(hands.begin(), hands.end(),
                          [](const HandSample &a, const HandSample &b) {
                              return a.palm.x < b.palm.x;
                          });
                rawInput_.palm[0] = hands[0].palm;
                rawInput_.palm[1] = hands[1].palm;
                rawInput_.bodyCorrected[0] = hands[0].bodyCorrected;
                rawInput_.bodyCorrected[1] = hands[1].bodyCorrected;
                rawInput_.sourceLabel[0] = hands[0].label;
                rawInput_.sourceLabel[1] = hands[1].label;
                rawInput_.sourceScore[0] = hands[0].score;
                rawInput_.sourceScore[1] = hands[1].score;
                rawInput_.active = {true, true};
            } else if (hands.size() == 1) {
                const size_t slot = ChooseSingleHandSlot(hands[0].palm);
                rawInput_.palm[slot] = hands[0].palm;
                rawInput_.active[slot] = true;
                rawInput_.bodyCorrected[slot] = hands[0].bodyCorrected;
                rawInput_.sourceLabel[slot] = hands[0].label;
                rawInput_.sourceScore[slot] = hands[0].score;
            }
            rawInput_.hasPacket = true;
            ++rawInput_.sequence;
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
            hasFilteredCalibratedPalm_[i] = false;
            hasPreviousPacketPalm_[i] = false;
            motionSpeed_[i] = 0.0f;
            packetDeltaPalm_[i] = {0.0f, 0.0f};
            packetMotionSpeed_[i] = 0.0f;
            continue;
        }

        const DirectX::XMFLOAT2 &palm = rawInput_.palm[i];
        const DirectX::XMFLOAT2 neutral =
            calibration_.hasHandNeutral ? calibration_.handNeutral[i]
                                        : DirectX::XMFLOAT2{0.5f, 0.5f};
        DirectX::XMFLOAT2 corrected{
            std::clamp(palm.x - neutral.x + 0.5f, 0.0f, 1.0f),
            std::clamp(palm.y - neutral.y + 0.5f, 0.0f, 1.0f)};
        if (hasFilteredCalibratedPalm_[i]) {
            const DirectX::XMFLOAT2 delta{
                corrected.x - filteredCalibratedPalm_[i].x,
                corrected.y - filteredCalibratedPalm_[i].y};
            const float speed = dt > 0.0001f ? Length(delta) / dt : 0.0f;
            const float speedRatio =
                std::clamp(speed / kFastPalmSpeed, 0.0f, 1.0f);
            const float alpha =
                kSlowPalmSmoothingAlpha +
                (kFastPalmSmoothingAlpha - kSlowPalmSmoothingAlpha) *
                    speedRatio;
            corrected = LerpPoint(filteredCalibratedPalm_[i], corrected, alpha);
        } else {
            hasFilteredCalibratedPalm_[i] = true;
        }
        filteredCalibratedPalm_[i] = corrected;
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

        if (packetChangedThisUpdate_) {
            if (hasPreviousPacketPalm_[i]) {
                const float dx = corrected.x - previousPacketPalm_[i].x;
                const float dy = corrected.y - previousPacketPalm_[i].y;
                packetDeltaPalm_[i] = {dx, dy};
                const float packetDt =
                    static_cast<float>(rawInput_.packetDeltaMs) * 0.001f;
                packetMotionSpeed_[i] =
                    packetDt > 0.0001f ? std::sqrt(dx * dx + dy * dy) / packetDt
                                        : 0.0f;
            } else {
                packetDeltaPalm_[i] = {0.0f, 0.0f};
                packetMotionSpeed_[i] = 0.0f;
            }
            previousPacketPalm_[i] = corrected;
            hasPreviousPacketPalm_[i] = true;
        } else {
            packetDeltaPalm_[i] = {0.0f, 0.0f};
            packetMotionSpeed_[i] = 0.0f;
        }

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

size_t SwordUdpController::ChooseSingleHandSlot(
    const DirectX::XMFLOAT2 &palm) const {
    if (palm.x < kSingleHandLeftThreshold) {
        return 0u;
    }
    if (palm.x > kSingleHandRightThreshold) {
        return 1u;
    }

    if (rawInput_.active[0] != rawInput_.active[1]) {
        return rawInput_.active[0] ? 0u : 1u;
    }
    if (hasPreviousCalibratedPalm_[0] != hasPreviousCalibratedPalm_[1]) {
        return hasPreviousCalibratedPalm_[0] ? 0u : 1u;
    }
    return palm.x < 0.5f ? 0u : 1u;
}

void SwordUdpController::CloseSocket() {
    if (socketReady_) {
        closesocket(ToSocket(socket_));
        WSACleanup();
    }

    socket_ = UINTPTR_MAX;
    socketReady_ = false;
}
