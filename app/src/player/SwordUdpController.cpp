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
constexpr float kHandControlGainX = 2.35f;
constexpr float kHandControlGainY = 2.65f;
constexpr float kHandSlashThreshold = 1.65f;
constexpr float kHandSlashResetThreshold = 0.55f;
constexpr float kHandSlashCooldownSeconds = 0.32f;
constexpr float kHandControlSmoothing = 18.0f;
constexpr float kHandFastControlSmoothing = 46.0f;
constexpr float kHandFastMotionDistance = 0.045f;
constexpr float kHandReacquireSuppressSeconds = 0.24f;
constexpr float kHandReacquireSlashThreshold = 0.12f;
constexpr float kHandTrackingJumpThreshold = 0.30f;
constexpr float kHandPreLossDirectionThreshold = 0.38f;
constexpr float kHandPreLossNetDistanceThreshold = 0.035f;
constexpr float kHandPreLossDirectionMaxAgeSeconds = 0.14f;
constexpr float kSyntheticLostSlashSeconds = 0.16f;
constexpr float kHandMotionWindowSeconds = 0.12f;
constexpr float kHandStableNetDistanceThreshold = 0.035f;
constexpr float kHandSlashNetDistanceThreshold = 0.065f;
constexpr float kHandStableConsistencyThreshold = 0.58f;
constexpr std::array<DirectX::XMFLOAT2, 2> kDefaultHandNeutral = {
    DirectX::XMFLOAT2{0.34f, 0.58f}, DirectX::XMFLOAT2{0.66f, 0.58f}};
constexpr size_t kHandLandmarkCount = 21;
constexpr size_t kHandWrist = 0;
constexpr size_t kHandMiddleMcp = 9;

struct HandSample {
    DirectX::XMFLOAT2 palm{0.5f, 0.5f};
    DirectX::XMFLOAT2 controlPalm{0.5f, 0.5f};
    std::array<DirectX::XMFLOAT2, kHandLandmarkCount> landmarks{};
    bool hasLandmarks = false;
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

bool ReadLandmarks01(const nlohmann::json &object,
                     std::array<DirectX::XMFLOAT2, kHandLandmarkCount> &out) {
    const auto it = object.find("landmarks01");
    if (it == object.end() || !it->is_array() ||
        it->size() < kHandLandmarkCount) {
        return false;
    }

    for (size_t i = 0; i < kHandLandmarkCount; ++i) {
        const auto &point = (*it)[i];
        if (!point.is_array() || point.size() < 2) {
            return false;
        }
        out[i].x = std::clamp(point[0].get<float>(), 0.0f, 1.0f);
        out[i].y = std::clamp(point[1].get<float>(), 0.0f, 1.0f);
    }
    return true;
}

float Length(const DirectX::XMFLOAT2 &value) {
    return std::sqrt(value.x * value.x + value.y * value.y);
}

DirectX::XMFLOAT2 CameraPalmToPlayerView(const DirectX::XMFLOAT2 &palm) {
    return {1.0f - palm.x, palm.y};
}

DirectX::XMFLOAT2 PlayerViewPalmToCamera(const DirectX::XMFLOAT2 &palm) {
    return {1.0f - palm.x, palm.y};
}

DirectX::XMFLOAT2 Add(const DirectX::XMFLOAT2 &a,
                      const DirectX::XMFLOAT2 &b) {
    return {a.x + b.x, a.y + b.y};
}

DirectX::XMFLOAT2 Scale(const DirectX::XMFLOAT2 &value, float scale) {
    return {value.x * scale, value.y * scale};
}

DirectX::XMFLOAT2 Subtract(const DirectX::XMFLOAT2 &a,
                           const DirectX::XMFLOAT2 &b) {
    return {a.x - b.x, a.y - b.y};
}

DirectX::XMFLOAT2 ClampPoint01(const DirectX::XMFLOAT2 &value) {
    return {std::clamp(value.x, 0.0f, 1.0f),
            std::clamp(value.y, 0.0f, 1.0f)};
}

DirectX::XMFLOAT2 EstimateControlPalm(const HandSample &sample) {
    if (!sample.hasLandmarks) {
        return sample.palm;
    }

    const DirectX::XMFLOAT2 wrist = sample.landmarks[kHandWrist];
    const DirectX::XMFLOAT2 middleMcp = sample.landmarks[kHandMiddleMcp];
    const DirectX::XMFLOAT2 palmAxis = Subtract(middleMcp, wrist);
    const float palmLength = Length(palmAxis);
    if (palmLength <= 0.0001f) {
        return sample.palm;
    }

    // Use a wrist-to-middle-knuckle anchor instead of averaging all joints.
    // Finger joints bend and jitter enough to create false slash velocity.
    return ClampPoint01(Add(wrist, Scale(palmAxis, 0.82f)));
}

}

SwordUdpController::~SwordUdpController() {
    CloseSocket();
}

bool SwordUdpController::IsActive(size_t handIndex) const {
    if (handIndex >= rawInput_.active.size() || !HasFreshRawInput()) {
        return false;
    }
    return rawInput_.active[handIndex] ||
           syntheticLostSlashTimer_[handIndex] > 0.0f;
}

SwordPose SwordUdpController::GetPose(size_t handIndex) const {
    if (handIndex < swordStates_.size() && HasFreshRawInput() &&
        (rawInput_.active[handIndex] ||
         syntheticLostSlashTimer_[handIndex] > 0.0f)) {
        return swordStates_[handIndex].ToPose();
    }
    return SwordPose{};
}

float SwordUdpController::GetMotionSpeed(size_t handIndex) const {
    if (handIndex >= motionSpeed_.size() || !HasFreshRawInput() ||
        (!rawInput_.active[handIndex] &&
         syntheticLostSlashTimer_[handIndex] <= 0.0f)) {
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
    hasPreviousPacketPalm_ = {false, false};
    packetDeltaPalm_ = {DirectX::XMFLOAT2{0.0f, 0.0f},
                        DirectX::XMFLOAT2{0.0f, 0.0f}};
    packetMotionSpeed_ = {0.0f, 0.0f};
    lastAppliedPacketSequence_ = 0;
    packetChangedThisUpdate_ = false;
    motionSpeed_ = {0.0f, 0.0f};
    handSlashArmed_ = {true, true};
    handSlashCooldown_ = {0.0f, 0.0f};
    smoothedPalm_ = {DirectX::XMFLOAT2{0.5f, 0.5f},
                     DirectX::XMFLOAT2{0.5f, 0.5f}};
    hasSmoothedPalm_ = {false, false};
    wasHandActive_ = {false, false};
    reacquireSuppressTimer_ = {0.0f, 0.0f};
    lostPalm_ = {DirectX::XMFLOAT2{0.5f, 0.5f},
                 DirectX::XMFLOAT2{0.5f, 0.5f}};
    hasLostPalm_ = {false, false};
    lastMotionDir_ = {DirectX::XMFLOAT2{0.0f, 0.0f},
                      DirectX::XMFLOAT2{0.0f, 0.0f}};
    lastMotionSpeed_ = {0.0f, 0.0f};
    lastMotionAge_ = {999.0f, 999.0f};
    syntheticLostSlashTimer_ = {0.0f, 0.0f};
    for (size_t i = 0; i < swordStates_.size(); ++i) {
        ResetMotionHistory(i);
    }
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
        packetChangedThisUpdate_ = false;
        motionSpeed_ = {0.0f, 0.0f};
        packetDeltaPalm_ = {DirectX::XMFLOAT2{0.0f, 0.0f},
                            DirectX::XMFLOAT2{0.0f, 0.0f}};
        packetMotionSpeed_ = {0.0f, 0.0f};
        handSlashArmed_ = {true, true};
        handSlashCooldown_ = {0.0f, 0.0f};
        hasSmoothedPalm_ = {false, false};
        wasHandActive_ = {false, false};
        reacquireSuppressTimer_ = {0.0f, 0.0f};
        hasLostPalm_ = {false, false};
        lastMotionDir_ = {DirectX::XMFLOAT2{0.0f, 0.0f},
                          DirectX::XMFLOAT2{0.0f, 0.0f}};
        lastMotionSpeed_ = {0.0f, 0.0f};
        lastMotionAge_ = {999.0f, 999.0f};
        syntheticLostSlashTimer_ = {0.0f, 0.0f};
        for (size_t i = 0; i < swordStates_.size(); ++i) {
            ResetMotionHistory(i);
        }
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
                        : PlayerViewPalmToCamera(kDefaultHandNeutral[handIndex]);
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

        const nlohmann::json packet =
            nlohmann::json::parse(buffer + rawMagicSize, nullptr, false);
        if (packet.is_discarded()) {
            continue;
        }
            const auto handsIt = packet.find("hands");
            if (handsIt == packet.end() || !handsIt->is_array()) {
                continue;
            }

            std::vector<HandSample> hands;
            hands.reserve(2);
            for (size_t i = 0; i < handsIt->size() && i < rawInput_.palm.size();
                 ++i) {
                DirectX::XMFLOAT2 palm{};
                if (ReadPoint01((*handsIt)[i], "grip01", palm) ||
                    ReadPoint01((*handsIt)[i], "palm01", palm)) {
                    HandSample sample{};
                    sample.palm = palm;
                    sample.hasLandmarks =
                        ReadLandmarks01((*handsIt)[i], sample.landmarks);
                    sample.controlPalm = EstimateControlPalm(sample);
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
                              return CameraPalmToPlayerView(a.controlPalm).x <
                                     CameraPalmToPlayerView(b.controlPalm).x;
                          });
                rawInput_.palm[0] = hands[0].controlPalm;
                rawInput_.palm[1] = hands[1].controlPalm;
                rawInput_.sourceLabel[0] = hands[0].label;
                rawInput_.sourceLabel[1] = hands[1].label;
                rawInput_.sourceScore[0] = hands[0].score;
                rawInput_.sourceScore[1] = hands[1].score;
                rawInput_.active = {true, true};
            } else if (hands.size() == 1) {
                const size_t slot = ChooseSingleHandSlot(
                    CameraPalmToPlayerView(hands[0].controlPalm));
                rawInput_.palm[slot] = hands[0].controlPalm;
                rawInput_.active[slot] = true;
                rawInput_.sourceLabel[slot] = hands[0].label;
                rawInput_.sourceScore[slot] = hands[0].score;
            }
            rawInput_.hasPacket = true;
            ++rawInput_.sequence;
            rawInput_.staleTimer = 0.0f;
    }
}

void SwordUdpController::ApplyRawInput(float dt) {
    for (size_t i = 0; i < swordStates_.size(); ++i) {
        SwordControllerState &state = swordStates_[i];
        handSlashCooldown_[i] =
            (std::max)(0.0f, handSlashCooldown_[i] - dt);
        reacquireSuppressTimer_[i] =
            (std::max)(0.0f, reacquireSuppressTimer_[i] - dt);
        syntheticLostSlashTimer_[i] =
            (std::max)(0.0f, syntheticLostSlashTimer_[i] - dt);
        lastMotionAge_[i] += dt;

        if (!rawInput_.active[i]) {
            if (hasSmoothedPalm_[i]) {
                lostPalm_[i] = smoothedPalm_[i];
                hasLostPalm_[i] = true;
            } else if (hasPreviousCalibratedPalm_[i]) {
                lostPalm_[i] = previousCalibratedPalm_[i];
                hasLostPalm_[i] = true;
            }

            const bool canUsePreLossMotion =
                wasHandActive_[i] &&
                lastMotionAge_[i] <= kHandPreLossDirectionMaxAgeSeconds &&
                lastMotionSpeed_[i] >= kHandPreLossDirectionThreshold &&
                handSlashCooldown_[i] <= 0.0f;
            if (canUsePreLossMotion) {
                state.slashDir = lastMotionDir_[i];
                state.UpdateSlash(kHandSlashThreshold + 0.01f, dt,
                                  kHandSlashThreshold);
                syntheticLostSlashTimer_[i] = kSyntheticLostSlashSeconds;
                handSlashArmed_[i] = false;
                handSlashCooldown_[i] = kHandSlashCooldownSeconds;
            } else if (syntheticLostSlashTimer_[i] > 0.0f) {
                state.UpdateSlash(0.0f, dt, kHandSlashThreshold);
            } else {
                state = {};
            }

            hasPreviousCalibratedPalm_[i] = false;
            hasPreviousPacketPalm_[i] = false;
            motionSpeed_[i] = 0.0f;
            packetDeltaPalm_[i] = {0.0f, 0.0f};
            packetMotionSpeed_[i] = 0.0f;
            hasSmoothedPalm_[i] = false;
            wasHandActive_[i] = false;
            reacquireSuppressTimer_[i] = kHandReacquireSuppressSeconds;
            if (syntheticLostSlashTimer_[i] <= 0.0f) {
                ResetMotionHistory(i);
            }
            continue;
        }

        DirectX::XMFLOAT2 corrected = TransformCameraPalmForSword(
            i, rawInput_.palm[i]);
        const bool reacquired = !wasHandActive_[i] || !hasSmoothedPalm_[i];
        const DirectX::XMFLOAT2 reacquireDelta =
            reacquired && hasLostPalm_[i]
                ? Subtract(corrected, lostPalm_[i])
                : DirectX::XMFLOAT2{0.0f, 0.0f};
        const float reacquireDistance = Length(reacquireDelta);
        const bool synthesizeReacquireSlash =
            reacquired && hasLostPalm_[i] &&
            reacquireDistance >= kHandReacquireSlashThreshold &&
            handSlashCooldown_[i] <= 0.0f;
        const bool jumped =
            hasSmoothedPalm_[i] &&
            Length(Subtract(corrected, smoothedPalm_[i])) >
                kHandTrackingJumpThreshold;
        if (reacquired || jumped) {
            ResetMotionHistory(i);
            smoothedPalm_[i] = corrected;
            hasSmoothedPalm_[i] = true;
            previousCalibratedPalm_[i] = corrected;
            previousPacketPalm_[i] = corrected;
            hasPreviousCalibratedPalm_[i] = true;
            hasPreviousPacketPalm_[i] = true;
            motionSpeed_[i] = 0.0f;
            packetDeltaPalm_[i] = {0.0f, 0.0f};
            packetMotionSpeed_[i] = 0.0f;
            handSlashArmed_[i] = synthesizeReacquireSlash;
            reacquireSuppressTimer_[i] =
                synthesizeReacquireSlash ? 0.0f : kHandReacquireSuppressSeconds;
        } else {
            const float rawDistance = Length(Subtract(corrected, smoothedPalm_[i]));
            const float fastBlend =
                std::clamp(rawDistance / kHandFastMotionDistance, 0.0f, 1.0f);
            const float smoothingRate =
                std::lerp(kHandControlSmoothing, kHandFastControlSmoothing,
                          fastBlend);
            const float smoothing =
                std::clamp(1.0f - std::exp(-smoothingRate * dt), 0.0f, 1.0f);
            smoothedPalm_[i].x += (corrected.x - smoothedPalm_[i].x) * smoothing;
            smoothedPalm_[i].y += (corrected.y - smoothedPalm_[i].y) * smoothing;
        }
        corrected = smoothedPalm_[i];
        AddMotionSample(i, corrected, dt);
        const StableMotion stableMotion = ComputeStableMotion(i);
        calibratedPalm_[i] = corrected;

        if (!reacquired && !jumped && hasPreviousCalibratedPalm_[i] &&
            dt > 0.0001f) {
            const float dx = corrected.x - previousCalibratedPalm_[i].x;
            const float dy = corrected.y - previousCalibratedPalm_[i].y;
            motionSpeed_[i] = std::sqrt(dx * dx + dy * dy) / dt;
            if (stableMotion.valid &&
                stableMotion.netDistance >= kHandPreLossNetDistanceThreshold &&
                stableMotion.speed >= kHandPreLossDirectionThreshold) {
                lastMotionDir_[i] = stableMotion.direction;
                lastMotionSpeed_[i] = stableMotion.speed;
                lastMotionAge_[i] = 0.0f;
            }
        } else {
            motionSpeed_[i] = 0.0f;
        }
        previousCalibratedPalm_[i] = corrected;
        hasPreviousCalibratedPalm_[i] = true;

        if (!reacquired && !jumped && packetChangedThisUpdate_) {
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
        DirectX::XMFLOAT2 slashDir = {dirX, dirY};
        if (synthesizeReacquireSlash && reacquireDistance > 0.0001f) {
            slashDir = {reacquireDelta.x / reacquireDistance,
                        -reacquireDelta.y / reacquireDistance};
        } else if (stableMotion.valid) {
            slashDir = stableMotion.direction;
        }

        const float slashSpeed =
            (std::max)(motionSpeed_[i], packetMotionSpeed_[i]);
        const bool stableSlashMotion =
            stableMotion.valid &&
            stableMotion.netDistance >= kHandSlashNetDistanceThreshold;
        const float stableSlashSpeed =
            stableSlashMotion ? kHandSlashThreshold + 0.01f : slashSpeed;
        if (slashSpeed <= kHandSlashResetThreshold &&
            reacquireSuppressTimer_[i] <= 0.0f) {
            handSlashArmed_[i] = true;
        }

        state.slashDir = slashDir;
        const bool wasSlashMode = state.isSlashMode;
        const float gatedSlashSpeed =
            synthesizeReacquireSlash
                ? kHandSlashThreshold + 0.01f
                : (handSlashArmed_[i] && handSlashCooldown_[i] <= 0.0f &&
                           reacquireSuppressTimer_[i] <= 0.0f
                       ? stableSlashSpeed
                       : 0.0f);
        state.UpdateSlash(gatedSlashSpeed, dt, kHandSlashThreshold);
        if (!wasSlashMode && state.isSlashMode) {
            handSlashArmed_[i] = false;
            handSlashCooldown_[i] = kHandSlashCooldownSeconds;
        }
        wasHandActive_[i] = true;
        hasLostPalm_[i] = false;

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

DirectX::XMFLOAT2 SwordUdpController::TransformCameraPalmForSword(
    size_t handIndex, const DirectX::XMFLOAT2 &palm) const {
    const DirectX::XMFLOAT2 playerView = CameraPalmToPlayerView(palm);
    const DirectX::XMFLOAT2 neutral =
        calibration_.hasHandNeutral
            ? CameraPalmToPlayerView(calibration_.handNeutral[handIndex])
            : kDefaultHandNeutral[handIndex];

    return {std::clamp(0.5f + (playerView.x - neutral.x) * kHandControlGainX,
                       0.0f, 1.0f),
            std::clamp(0.5f + (playerView.y - neutral.y) * kHandControlGainY,
                       0.0f, 1.0f)};
}

void SwordUdpController::ResetMotionHistory(size_t handIndex) {
    if (handIndex >= motionHistoryCount_.size()) {
        return;
    }
    motionHistoryStart_[handIndex] = 0;
    motionHistoryCount_[handIndex] = 0;
}

void SwordUdpController::AddMotionSample(size_t handIndex,
                                         const DirectX::XMFLOAT2 &palm,
                                         float dt) {
    if (handIndex >= motionHistoryCount_.size()) {
        return;
    }

    const size_t count = motionHistoryCount_[handIndex];
    for (size_t j = 0; j < count; ++j) {
        const size_t index =
            (motionHistoryStart_[handIndex] + j) % kMotionHistorySize;
        motionHistoryAge_[handIndex][index] += dt;
    }

    while (motionHistoryCount_[handIndex] > 0) {
        const size_t oldest = motionHistoryStart_[handIndex];
        if (motionHistoryAge_[handIndex][oldest] <=
            kHandMotionWindowSeconds) {
            break;
        }
        motionHistoryStart_[handIndex] =
            (motionHistoryStart_[handIndex] + 1) % kMotionHistorySize;
        --motionHistoryCount_[handIndex];
    }

    const size_t writeIndex =
        (motionHistoryStart_[handIndex] + motionHistoryCount_[handIndex]) %
        kMotionHistorySize;
    motionHistoryPalm_[handIndex][writeIndex] = palm;
    motionHistoryAge_[handIndex][writeIndex] = 0.0f;
    if (motionHistoryCount_[handIndex] < kMotionHistorySize) {
        ++motionHistoryCount_[handIndex];
    } else {
        motionHistoryStart_[handIndex] =
            (motionHistoryStart_[handIndex] + 1) % kMotionHistorySize;
    }
}

SwordUdpController::StableMotion
SwordUdpController::ComputeStableMotion(size_t handIndex) const {
    StableMotion result{};
    if (handIndex >= motionHistoryCount_.size() ||
        motionHistoryCount_[handIndex] < 2) {
        return result;
    }

    const size_t count = motionHistoryCount_[handIndex];
    const size_t first = motionHistoryStart_[handIndex];
    const size_t last = (motionHistoryStart_[handIndex] + count - 1) %
                        kMotionHistorySize;
    const DirectX::XMFLOAT2 oldest = motionHistoryPalm_[handIndex][first];
    const DirectX::XMFLOAT2 newest = motionHistoryPalm_[handIndex][last];
    const DirectX::XMFLOAT2 netDelta = Subtract(newest, oldest);
    const float netDistance = Length(netDelta);
    const float duration =
        motionHistoryAge_[handIndex][first] - motionHistoryAge_[handIndex][last];
    if (duration <= 0.025f || netDistance < kHandStableNetDistanceThreshold) {
        return result;
    }

    float pathDistance = 0.0f;
    DirectX::XMFLOAT2 previous = oldest;
    for (size_t j = 1; j < count; ++j) {
        const size_t index = (motionHistoryStart_[handIndex] + j) %
                             kMotionHistorySize;
        const DirectX::XMFLOAT2 current = motionHistoryPalm_[handIndex][index];
        pathDistance += Length(Subtract(current, previous));
        previous = current;
    }
    if (pathDistance <= 0.0001f ||
        netDistance / pathDistance < kHandStableConsistencyThreshold) {
        return result;
    }

    const float invDistance = 1.0f / netDistance;
    result.valid = true;
    result.direction = {netDelta.x * invDistance, -netDelta.y * invDistance};
    result.speed = netDistance / duration;
    result.netDistance = netDistance;
    return result;
}

void SwordUdpController::CloseSocket() {
    if (socketReady_) {
        closesocket(ToSocket(socket_));
        WSACleanup();
    }

    socket_ = UINTPTR_MAX;
    socketReady_ = false;
}
