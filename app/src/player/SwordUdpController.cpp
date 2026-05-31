#include "SwordUdpController.h"
#include <DirectXMath.h>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include "AppSceneServices.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace DirectX;

namespace {
constexpr char kRawMagic[] = "HAND_RAW ";
constexpr std::array<DirectX::XMFLOAT2, 2> kDefaultHandNeutral = {
    DirectX::XMFLOAT2{0.34f, 0.58f}, DirectX::XMFLOAT2{0.66f, 0.58f}};
constexpr size_t kHandLandmarkCount = 21;
constexpr size_t kHandWrist = 0;
constexpr size_t kHandIndexMcp = 5;
constexpr size_t kHandMiddleMcp = 9;
constexpr size_t kHandPinkyMcp = 17;

struct HandSample {
    DirectX::XMFLOAT2 palm{0.5f, 0.5f};
    DirectX::XMFLOAT2 controlPalm{0.5f, 0.5f};
    std::array<DirectX::XMFLOAT2, kHandLandmarkCount> landmarks{};
    bool hasLandmarks = false;
    float visualScale = 0.095f;
    bool hasTilt = false;
    float tiltRadians = 0.0f;
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

const AppSceneServices::CameraAdvancedSettings &CameraSettings() {
    return AppSceneServices::GetCameraAdvancedSettingsConst();
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

float ReachCompensatedDelta(float delta, float neutral, float defaultNeutral,
                            bool positiveTowardMax) {
    if (std::fabs(delta) <= 0.0001f) {
        return 0.0f;
    }

    const bool towardMax = delta > 0.0f;
    const float reach = towardMax == positiveTowardMax ? 1.0f - neutral : neutral;
    const float defaultReach = towardMax == positiveTowardMax
                                   ? 1.0f - defaultNeutral
                                   : defaultNeutral;
    const auto &settings = CameraSettings();
    if (reach < settings.handMinReachForCompensation ||
        defaultReach < settings.handMinReachForCompensation) {
        return delta;
    }

    const float scale = std::clamp(defaultReach / reach,
                                   settings.handReachCompensationMin,
                                   settings.handReachCompensationMax);
    return delta * scale;
}

bool IsNearNeutral(const DirectX::XMFLOAT2 &palm) {
    return Length(Subtract(palm, DirectX::XMFLOAT2{0.5f, 0.5f})) <=
           CameraSettings().handSlashRearmNeutralRadius;
}

bool IsNearControlEdge(const DirectX::XMFLOAT2 &palm) {
    return palm.x < 0.06f || palm.x > 0.94f || palm.y < 0.06f ||
           palm.y > 0.94f;
}

bool IsMovingAwayFromNeutral(const DirectX::XMFLOAT2 &delta,
                             const DirectX::XMFLOAT2 &currentPalm) {
    const DirectX::XMFLOAT2 fromNeutral =
        Subtract(currentPalm, DirectX::XMFLOAT2{0.5f, 0.5f});
    return delta.x * fromNeutral.x + delta.y * fromNeutral.y > 0.0f;
}

float EstimateHandVisualScale(const HandSample &sample) {
    if (!sample.hasLandmarks) {
        return CameraSettings().handReferenceVisualScale;
    }

    const DirectX::XMFLOAT2 wrist = sample.landmarks[kHandWrist];
    const DirectX::XMFLOAT2 middleMcp = sample.landmarks[kHandMiddleMcp];
    const auto &settings = CameraSettings();
    return std::clamp(Length(Subtract(middleMcp, wrist)),
                      settings.handMinVisualScale, settings.handMaxVisualScale);
}

float HandDistanceThresholdScale(float visualScale) {
    const auto &settings = CameraSettings();
    const float clampedScale =
        std::clamp(visualScale, settings.handMinVisualScale,
                   settings.handMaxVisualScale);
    return std::clamp(clampedScale / settings.handReferenceVisualScale,
                      settings.handFarThresholdScale,
                      settings.handNearThresholdScale);
}

float HandSensitivityGainScale(float sensitivity) {
    const auto &settings = CameraSettings();
    return std::lerp(settings.handHardSensitivityGainScale,
                     settings.handEasySensitivityGainScale,
                     std::clamp(sensitivity, 0.0f, 1.0f));
}

float HandSensitivityThresholdScale(float sensitivity) {
    const auto &settings = CameraSettings();
    return std::lerp(settings.handHardSensitivityThresholdScale,
                     settings.handEasySensitivityThresholdScale,
                     std::clamp(sensitivity, 0.0f, 1.0f));
}

float DirectionalCameraSensitivity(const DirectX::XMFLOAT2 &direction,
                                   float verticalSensitivity,
                                   float horizontalSensitivity) {
    const float horizontalWeight = std::fabs(direction.x);
    const float verticalWeight = std::fabs(direction.y);
    const float totalWeight = horizontalWeight + verticalWeight;
    if (totalWeight <= 0.0001f) {
        return (verticalSensitivity + horizontalSensitivity) * 0.5f;
    }
    return (horizontalSensitivity * horizontalWeight +
            verticalSensitivity * verticalWeight) /
           totalWeight;
}

float DirectionalRootThresholdScale(const DirectX::XMFLOAT2 &direction) {
    const float horizontalWeight = std::fabs(direction.x);
    const float verticalWeight = std::fabs(direction.y);
    const float totalWeight = horizontalWeight + verticalWeight;
    if (totalWeight <= 0.0001f) {
        return 1.0f;
    }
    return (horizontalWeight +
            verticalWeight * CameraSettings().handVerticalSlashThresholdScale) /
           totalWeight;
}

bool EstimateTiltFromPlayerPoints(const DirectX::XMFLOAT2 &a,
                                  const DirectX::XMFLOAT2 &b,
                                  float &outRadians) {
    DirectX::XMFLOAT2 left = a;
    DirectX::XMFLOAT2 right = b;
    if (right.x < left.x) {
        std::swap(left, right);
    }

    const DirectX::XMFLOAT2 delta = Subtract(right, left);
    if (std::fabs(delta.x) < CameraSettings().handTiltMinWidth) {
        return false;
    }

    const float maxTilt = CameraSettings().handTiltMaxRadians;
    outRadians = std::clamp(std::atan2(delta.y, delta.x), -maxTilt, maxTilt);
    return true;
}

bool ReadBodyTilt(const nlohmann::json &packet, float &outTiltRadians,
                  float &outVisibility) {
    const auto it = packet.find("body");
    if (it == packet.end() || !it->is_object()) {
        return false;
    }
    const nlohmann::json &body = *it;
    if (!body.value("tracked", true) || !body.contains("tiltRadians")) {
        return false;
    }
    const float maxTilt = CameraSettings().handTiltMaxRadians;
    outTiltRadians =
        std::clamp(body.value("tiltRadians", 0.0f), -maxTilt, maxTilt);
    outVisibility = std::clamp(body.value("visibility", 0.0f), 0.0f, 1.0f);
    return true;
}

bool EstimateTiltFromHandLandmarks(const HandSample &sample,
                                   float &outRadians) {
    if (!sample.hasLandmarks) {
        return false;
    }

    return EstimateTiltFromPlayerPoints(
        CameraPalmToPlayerView(sample.landmarks[kHandIndexMcp]),
        CameraPalmToPlayerView(sample.landmarks[kHandPinkyMcp]), outRadians);
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
    handSlashNeutralTimer_ = {0.0f, 0.0f};
    handSlashCooldown_ = {0.0f, 0.0f};
    smoothedPalm_ = {DirectX::XMFLOAT2{0.5f, 0.5f},
                     DirectX::XMFLOAT2{0.5f, 0.5f}};
    hasSmoothedPalm_ = {false, false};
    wasHandActive_ = {false, false};
    reacquireSuppressTimer_ = {0.0f, 0.0f};
    edgeExitSuppressTimer_ = {0.0f, 0.0f};
    lostPalm_ = {DirectX::XMFLOAT2{0.5f, 0.5f},
                 DirectX::XMFLOAT2{0.5f, 0.5f}};
    hasLostPalm_ = {false, false};
    lastMotionDir_ = {DirectX::XMFLOAT2{0.0f, 0.0f},
                      DirectX::XMFLOAT2{0.0f, 0.0f}};
    lastMotionSpeed_ = {0.0f, 0.0f};
    lastMotionAge_ = {999.0f, 999.0f};
    syntheticLostSlashTimer_ = {0.0f, 0.0f};
    smoothedTiltRadians_ = 0.0f;
    hasSmoothedTilt_ = false;
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
        handSlashNeutralTimer_ = {0.0f, 0.0f};
        handSlashCooldown_ = {0.0f, 0.0f};
        hasSmoothedPalm_ = {false, false};
        wasHandActive_ = {false, false};
        reacquireSuppressTimer_ = {0.0f, 0.0f};
        edgeExitSuppressTimer_ = {0.0f, 0.0f};
        hasLostPalm_ = {false, false};
        lastMotionDir_ = {DirectX::XMFLOAT2{0.0f, 0.0f},
                          DirectX::XMFLOAT2{0.0f, 0.0f}};
        lastMotionSpeed_ = {0.0f, 0.0f};
        lastMotionAge_ = {999.0f, 999.0f};
        syntheticLostSlashTimer_ = {0.0f, 0.0f};
        smoothedTiltRadians_ = 0.0f;
        hasSmoothedTilt_ = false;
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
    debug.bodyCorrected = rawInput_.bodyTracked && hasSmoothedTilt_;
    debug.bodyTiltRadians = rawInput_.bodyTiltRadians;
    debug.handTiltRadians =
        handIndex < rawInput_.handTiltRadians.size()
            ? rawInput_.handTiltRadians[handIndex]
            : 0.0f;
    debug.appliedTiltRadians = hasSmoothedTilt_ ? smoothedTiltRadians_ : 0.0f;
    debug.packetChanged = packetChangedThisUpdate_;
    debug.packetDeltaMs = rawInput_.packetDeltaMs;
    debug.packetDeltaPalm = packetDeltaPalm_[handIndex];
    debug.packetMotionSpeed = debug.active ? packetMotionSpeed_[handIndex] : 0.0f;
    debug.gameNetDelta = debugGameNetDelta_[handIndex];
    debug.gameNetDirection = debugGameNetDirection_[handIndex];
    debug.gameNetDistance = debugGameNetDistance_[handIndex];
    debug.gameStableSpeed = debugGameStableSpeed_[handIndex];
    debug.gameSlashThreshold = debugGameSlashThreshold_[handIndex];
    debug.gameSlashNetDistanceThreshold =
        debugGameSlashNetDistanceThreshold_[handIndex];
    debug.gameGatedSlashSpeed = debugGameGatedSlashSpeed_[handIndex];
    debug.slashTriggeredThisFrame = debugSlashTriggeredThisFrame_[handIndex];
    debug.visualScale = rawInput_.handScale[handIndex];
    debug.tiltRadians = hasSmoothedTilt_ ? smoothedTiltRadians_ : 0.0f;
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
                    sample.visualScale = EstimateHandVisualScale(sample);
                    sample.controlPalm = EstimateControlPalm(sample);
                    sample.hasTilt =
                        EstimateTiltFromHandLandmarks(sample, sample.tiltRadians);
                    sample.label = (*handsIt)[i].value("label", "");
                    sample.score = (*handsIt)[i].value("score", 0.0f);
                    hands.push_back(sample);
                }
            }

            rawInput_.active = {false, false};
            rawInput_.sourceLabel = {"", ""};
            rawInput_.sourceScore = {0.0f, 0.0f};
            rawInput_.hasHandTilt = {false, false};
            rawInput_.handTiltRadians = {0.0f, 0.0f};
            rawInput_.bodyTracked = false;
            rawInput_.bodyTiltRadians = 0.0f;
            rawInput_.bodyVisibility = 0.0f;
            rawInput_.hasTilt = false;
            rawInput_.tiltRadians = 0.0f;
            rawInput_.handCount = static_cast<uint32_t>(hands.size());
            rawInput_.bodyTracked = ReadBodyTilt(
                packet, rawInput_.bodyTiltRadians, rawInput_.bodyVisibility);
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
                rawInput_.handScale[0] = hands[0].visualScale;
                rawInput_.handScale[1] = hands[1].visualScale;
                rawInput_.hasHandTilt[0] = hands[0].hasTilt;
                rawInput_.hasHandTilt[1] = hands[1].hasTilt;
                rawInput_.handTiltRadians[0] = hands[0].tiltRadians;
                rawInput_.handTiltRadians[1] = hands[1].tiltRadians;
                rawInput_.active = {true, true};
                rawInput_.hasTilt = EstimateTiltFromPlayerPoints(
                    CameraPalmToPlayerView(rawInput_.palm[0]),
                    CameraPalmToPlayerView(rawInput_.palm[1]),
                    rawInput_.tiltRadians);
            } else if (hands.size() == 1) {
                const size_t slot = ChooseSingleHandSlot(
                    CameraPalmToPlayerView(hands[0].controlPalm));
                rawInput_.palm[slot] = hands[0].controlPalm;
                rawInput_.active[slot] = true;
                rawInput_.sourceLabel[slot] = hands[0].label;
                rawInput_.sourceScore[slot] = hands[0].score;
                rawInput_.handScale[slot] = hands[0].visualScale;
                rawInput_.hasHandTilt[slot] = hands[0].hasTilt;
                rawInput_.handTiltRadians[slot] = hands[0].tiltRadians;
                rawInput_.hasTilt = hands[0].hasTilt;
                rawInput_.tiltRadians = hands[0].tiltRadians;
            }
            rawInput_.hasPacket = true;
            ++rawInput_.sequence;
            rawInput_.staleTimer = 0.0f;
    }
}

void SwordUdpController::ApplyRawInput(float dt) {
    UpdateTiltEstimate(dt);

    for (size_t i = 0; i < swordStates_.size(); ++i) {
        const auto &settings = CameraSettings();
        SwordControllerState &state = swordStates_[i];
        debugGameNetDelta_[i] = {0.0f, 0.0f};
        debugGameNetDirection_[i] = {0.0f, 0.0f};
        debugGameNetDistance_[i] = 0.0f;
        debugGameStableSpeed_[i] = 0.0f;
        debugGameSlashThreshold_[i] = 0.0f;
        debugGameSlashNetDistanceThreshold_[i] = 0.0f;
        debugGameGatedSlashSpeed_[i] = 0.0f;
        debugSlashTriggeredThisFrame_[i] = false;
        handSlashCooldown_[i] =
            (std::max)(0.0f, handSlashCooldown_[i] - dt);
        reacquireSuppressTimer_[i] =
            (std::max)(0.0f, reacquireSuppressTimer_[i] - dt);
        edgeExitSuppressTimer_[i] =
            (std::max)(0.0f, edgeExitSuppressTimer_[i] - dt);
        syntheticLostSlashTimer_[i] =
            (std::max)(0.0f, syntheticLostSlashTimer_[i] - dt);
        lastMotionAge_[i] += dt;
        const float distanceThresholdScale =
            HandDistanceThresholdScale(rawInput_.handScale[i]);
        const float slashSensitivity =
            AppSceneServices::GetCameraSlashSensitivity(i);
        const float verticalSensitivity =
            AppSceneServices::GetCameraVerticalSensitivity(i);
        const float horizontalSensitivity =
            AppSceneServices::GetCameraHorizontalSensitivity(i);
        const float overallThresholdScale =
            HandSensitivityThresholdScale(slashSensitivity);
        const float defaultAxisThresholdScale = HandSensitivityThresholdScale(
            (verticalSensitivity + horizontalSensitivity) * 0.5f);
        const float baseSlashThreshold =
            settings.handSlashThreshold * distanceThresholdScale;
        const float baseSlashResetThreshold =
            settings.handSlashResetThreshold * distanceThresholdScale;
        float slashThreshold =
            baseSlashThreshold * overallThresholdScale *
            defaultAxisThresholdScale;
        float slashResetThreshold =
            baseSlashResetThreshold * overallThresholdScale *
            defaultAxisThresholdScale;
        const float reacquireSlashThreshold =
            settings.handReacquireSlashThreshold * distanceThresholdScale;
        const float jumpSlashDistanceThreshold =
            settings.handJumpSlashDistanceThreshold * distanceThresholdScale;
        const float preLossDirectionThreshold =
            settings.handPreLossDirectionThreshold * distanceThresholdScale;
        const float preLossNetDistanceThreshold =
            settings.handPreLossNetDistanceThreshold * distanceThresholdScale;
        const float stableNetDistanceThreshold =
            settings.handStableNetDistanceThreshold * distanceThresholdScale;
        const float baseSlashNetDistanceThreshold =
            settings.handSlashNetDistanceThreshold * distanceThresholdScale;

        if (!rawInput_.active[i]) {
            if (hasSmoothedPalm_[i]) {
                lostPalm_[i] = smoothedPalm_[i];
                hasLostPalm_[i] = true;
            } else if (hasPreviousCalibratedPalm_[i]) {
                lostPalm_[i] = previousCalibratedPalm_[i];
                hasLostPalm_[i] = true;
            }
            if (wasHandActive_[i] && hasLostPalm_[i] &&
                IsNearControlEdge(lostPalm_[i])) {
                edgeExitSuppressTimer_[i] = settings.handEdgeExitSuppressSeconds;
                handSlashArmed_[i] = false;
                handSlashNeutralTimer_[i] = 0.0f;
            }

            const bool canUsePreLossMotion =
                wasHandActive_[i] &&
                edgeExitSuppressTimer_[i] <= 0.0f &&
                lastMotionAge_[i] <= settings.handPreLossDirectionMaxAgeSeconds &&
                lastMotionSpeed_[i] >= preLossDirectionThreshold &&
                handSlashCooldown_[i] <= 0.0f;
            if (canUsePreLossMotion) {
                state.slashDir = lastMotionDir_[i];
                state.UpdateSlash(
                    (std::max)(lastMotionSpeed_[i], slashThreshold + 0.01f),
                    dt, slashThreshold);
                syntheticLostSlashTimer_[i] = settings.syntheticLostSlashSeconds;
                handSlashArmed_[i] = false;
                handSlashNeutralTimer_[i] = 0.0f;
                handSlashCooldown_[i] = settings.handSlashCooldownSeconds;
            } else if (syntheticLostSlashTimer_[i] > 0.0f) {
                state.UpdateSlash(0.0f, dt, settings.handSlashThreshold);
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
            reacquireSuppressTimer_[i] = settings.handReacquireSuppressSeconds;
            if (syntheticLostSlashTimer_[i] <= 0.0f) {
                ResetMotionHistory(i);
            }
            continue;
        }

        DirectX::XMFLOAT2 corrected =
            TransformCameraPalmForSword(i, rawInput_.palm[i]);
        const bool reacquired = !wasHandActive_[i] || !hasSmoothedPalm_[i];
        const DirectX::XMFLOAT2 reacquireDelta =
            reacquired && hasLostPalm_[i]
                ? Subtract(corrected, lostPalm_[i])
                : DirectX::XMFLOAT2{0.0f, 0.0f};
        const float reacquireDistance = Length(reacquireDelta);
        const bool synthesizeReacquireSlash =
            reacquired && hasLostPalm_[i] &&
            edgeExitSuppressTimer_[i] <= 0.0f &&
            reacquireDistance >= reacquireSlashThreshold &&
            handSlashArmed_[i] && handSlashCooldown_[i] <= 0.0f &&
            IsMovingAwayFromNeutral(reacquireDelta, corrected);
        const DirectX::XMFLOAT2 jumpDelta =
            hasSmoothedPalm_[i] ? Subtract(corrected, smoothedPalm_[i])
                                : DirectX::XMFLOAT2{0.0f, 0.0f};
        const float jumpDistance = Length(jumpDelta);
        const bool jumped =
            hasSmoothedPalm_[i] &&
            jumpDistance > settings.handTrackingJumpThreshold;
        const bool teleported =
            jumped && jumpDistance > settings.handTrackingTeleportThreshold;
        const bool synthesizeJumpSlash =
            jumped && !teleported &&
            jumpDistance >= jumpSlashDistanceThreshold &&
            edgeExitSuppressTimer_[i] <= 0.0f &&
            handSlashArmed_[i] && handSlashCooldown_[i] <= 0.0f &&
            IsMovingAwayFromNeutral(jumpDelta, corrected);
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
            handSlashArmed_[i] =
                synthesizeReacquireSlash || synthesizeJumpSlash;
            handSlashNeutralTimer_[i] = 0.0f;
            reacquireSuppressTimer_[i] =
                (synthesizeReacquireSlash || synthesizeJumpSlash)
                    ? 0.0f
                    : settings.handReacquireSuppressSeconds;
        } else {
            const float rawDistance = Length(Subtract(corrected, smoothedPalm_[i]));
            const float fastBlend =
                std::clamp(rawDistance / settings.handFastMotionDistance, 0.0f,
                           1.0f);
            const float smoothingRate =
                std::lerp(settings.handControlSmoothing,
                          settings.handFastControlSmoothing,
                          fastBlend);
            const float smoothing =
                std::clamp(1.0f - std::exp(-smoothingRate * dt), 0.0f, 1.0f);
            smoothedPalm_[i].x += (corrected.x - smoothedPalm_[i].x) * smoothing;
            smoothedPalm_[i].y += (corrected.y - smoothedPalm_[i].y) * smoothing;
        }
        corrected = smoothedPalm_[i];
        AddMotionSample(i, corrected, dt);
        const StableMotion stableMotion =
            ComputeStableMotion(i, stableNetDistanceThreshold);
        calibratedPalm_[i] = corrected;
        debugGameNetDirection_[i] = stableMotion.direction;
        debugGameNetDistance_[i] = stableMotion.netDistance;
        debugGameStableSpeed_[i] = stableMotion.speed;
        if (stableMotion.valid) {
            debugGameNetDelta_[i] = {stableMotion.direction.x *
                                         stableMotion.netDistance,
                                     -stableMotion.direction.y *
                                         stableMotion.netDistance};
        }

        DirectX::XMFLOAT2 frameDeltaPalm = {0.0f, 0.0f};
        if (!reacquired && !jumped && hasPreviousCalibratedPalm_[i] &&
            dt > 0.0001f) {
            const float dx = corrected.x - previousCalibratedPalm_[i].x;
            const float dy = corrected.y - previousCalibratedPalm_[i].y;
            frameDeltaPalm = {dx, dy};
            motionSpeed_[i] = std::sqrt(dx * dx + dy * dy) / dt;
            if (stableMotion.valid &&
                stableMotion.netDistance >= preLossNetDistanceThreshold &&
                stableMotion.speed >= preLossDirectionThreshold) {
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
        if (synthesizeJumpSlash && jumpDistance > 0.0001f) {
            slashDir = {jumpDelta.x / jumpDistance,
                        -jumpDelta.y / jumpDistance};
        } else if (synthesizeReacquireSlash && reacquireDistance > 0.0001f) {
            slashDir = {reacquireDelta.x / reacquireDistance,
                        -reacquireDelta.y / reacquireDistance};
        } else if (stableMotion.valid) {
            slashDir = stableMotion.direction;
        } else {
            const float frameDistance = Length(frameDeltaPalm);
            if (frameDistance >= settings.handVelocitySlashNetDistanceThreshold) {
                slashDir = {frameDeltaPalm.x / frameDistance,
                            -frameDeltaPalm.y / frameDistance};
            }
        }

        const float directionalSensitivity = DirectionalCameraSensitivity(
            slashDir, verticalSensitivity, horizontalSensitivity);
        const float directionalThresholdScale =
            HandSensitivityThresholdScale(directionalSensitivity) *
            DirectionalRootThresholdScale(slashDir);
        slashThreshold =
            baseSlashThreshold * overallThresholdScale *
            directionalThresholdScale;
        slashResetThreshold =
            baseSlashResetThreshold * overallThresholdScale *
            directionalThresholdScale;
        const float slashNetDistanceThreshold =
            baseSlashNetDistanceThreshold * overallThresholdScale *
            directionalThresholdScale;
        debugGameSlashThreshold_[i] = slashThreshold;
        debugGameSlashNetDistanceThreshold_[i] = slashNetDistanceThreshold;

        const float slashSpeed =
            (std::max)(motionSpeed_[i], packetMotionSpeed_[i]);
        const bool frameVelocitySlashMotion =
            slashSpeed >= slashThreshold &&
            Length(frameDeltaPalm) >=
                settings.handVelocitySlashNetDistanceThreshold &&
            IsMovingAwayFromNeutral(frameDeltaPalm, corrected);
        const bool stableSlashMotion =
            stableMotion.valid &&
            stableMotion.netDistance >= slashNetDistanceThreshold &&
            IsMovingAwayFromNeutral(
                DirectX::XMFLOAT2{stableMotion.direction.x,
                                  -stableMotion.direction.y},
                corrected);
        const bool movingAwaySlash =
            frameVelocitySlashMotion ||
            IsMovingAwayFromNeutral(packetDeltaPalm_[i], corrected) ||
            IsMovingAwayFromNeutral(
                DirectX::XMFLOAT2{stableMotion.direction.x,
                                  -stableMotion.direction.y},
                corrected);
        const float stableSlashSpeed =
            stableSlashMotion || frameVelocitySlashMotion
                ? (std::max)(slashSpeed, slashThreshold + 0.01f)
                : (movingAwaySlash ? slashSpeed : 0.0f);
        if (slashSpeed <= slashResetThreshold &&
            reacquireSuppressTimer_[i] <= 0.0f &&
            edgeExitSuppressTimer_[i] <= 0.0f && IsNearNeutral(corrected)) {
            handSlashNeutralTimer_[i] += dt;
            if (handSlashNeutralTimer_[i] >=
                settings.handSlashNeutralRearmSeconds) {
                handSlashArmed_[i] = true;
            }
        } else {
            handSlashNeutralTimer_[i] = 0.0f;
        }

        state.slashDir = slashDir;
        const bool wasSlashMode = state.isSlashMode;
        const float gatedSlashSpeed =
            synthesizeReacquireSlash || synthesizeJumpSlash
                ? (std::max)(slashSpeed, slashThreshold + 0.01f)
                : (handSlashArmed_[i] && handSlashCooldown_[i] <= 0.0f &&
                           reacquireSuppressTimer_[i] <= 0.0f &&
                           edgeExitSuppressTimer_[i] <= 0.0f
                       ? stableSlashSpeed
                       : 0.0f);
        state.UpdateSlash(gatedSlashSpeed, dt, slashThreshold);
        debugGameGatedSlashSpeed_[i] = gatedSlashSpeed;
        if (!wasSlashMode && state.isSlashMode) {
            handSlashArmed_[i] = false;
            handSlashNeutralTimer_[i] = 0.0f;
            handSlashCooldown_[i] = settings.handSlashCooldownSeconds;
            debugSlashTriggeredThisFrame_[i] = true;
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

void SwordUdpController::UpdateTiltEstimate(float dt) {
    float targetTilt = 0.0f;
    bool hasTargetTilt = rawInput_.bodyTracked;
    if (hasTargetTilt) {
        targetTilt = rawInput_.bodyTiltRadians;
    } else if (rawInput_.hasTilt) {
        hasTargetTilt = true;
        targetTilt = rawInput_.tiltRadians;
    }

    if (!hasTargetTilt) {
        float sum = 0.0f;
        uint32_t count = 0;
        for (size_t i = 0; i < rawInput_.active.size(); ++i) {
            if (rawInput_.active[i] && rawInput_.hasHandTilt[i]) {
                sum += rawInput_.handTiltRadians[i];
                ++count;
            }
        }
        if (count > 0) {
            targetTilt = sum / static_cast<float>(count);
            hasTargetTilt = true;
        }
    }

    if (!hasTargetTilt) {
        targetTilt = 0.0f;
    }

    if (!hasSmoothedTilt_) {
        smoothedTiltRadians_ = targetTilt;
        hasSmoothedTilt_ = hasTargetTilt;
        return;
    }

    const float smoothing =
        std::clamp(1.0f - std::exp(-CameraSettings().handTiltSmoothing * dt),
                   0.0f, 1.0f);
    smoothedTiltRadians_ += (targetTilt - smoothedTiltRadians_) * smoothing;
    hasSmoothedTilt_ =
        hasTargetTilt || std::fabs(smoothedTiltRadians_) > 0.001f;
}

size_t SwordUdpController::ChooseSingleHandSlot(
    const DirectX::XMFLOAT2 &palm) const {
    const auto &settings = CameraSettings();
    if (palm.x < settings.singleHandLeftThreshold) {
        return 0u;
    }
    if (palm.x > settings.singleHandRightThreshold) {
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
    const DirectX::XMFLOAT2 defaultNeutral = kDefaultHandNeutral[handIndex];
    DirectX::XMFLOAT2 neutral = defaultNeutral;
    if (calibration_.hasHandNeutral) {
        neutral = CameraPalmToPlayerView(calibration_.handNeutral[handIndex]);
    }
    DirectX::XMFLOAT2 delta = Subtract(playerView, neutral);
    if (hasSmoothedTilt_) {
        const float c = std::cos(smoothedTiltRadians_);
        const float s = std::sin(smoothedTiltRadians_);
        delta = {delta.x * c + delta.y * s, -delta.x * s + delta.y * c};
    }
    const float compensatedX = ReachCompensatedDelta(
        delta.x, neutral.x, defaultNeutral.x, true);
    const float compensatedY = ReachCompensatedDelta(
        delta.y, neutral.y, defaultNeutral.y, true);
    const float overallGainScale =
        HandSensitivityGainScale(AppSceneServices::GetCameraSensitivity(handIndex));
    const float horizontalGainScale =
        HandSensitivityGainScale(
            AppSceneServices::GetCameraHorizontalSensitivity(handIndex));
    const float verticalGainScale =
        HandSensitivityGainScale(
            AppSceneServices::GetCameraVerticalSensitivity(handIndex));
    const auto &settings = CameraSettings();
    return {std::clamp(0.5f + compensatedX * settings.handControlGainX *
                                  overallGainScale * horizontalGainScale,
                       0.0f, 1.0f),
            std::clamp(0.5f + compensatedY * settings.handControlGainY *
                                  overallGainScale * verticalGainScale,
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
            CameraSettings().handMotionWindowSeconds) {
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
SwordUdpController::ComputeStableMotion(size_t handIndex,
                                        float minNetDistance) const {
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
    if (duration <= 0.025f || netDistance < minNetDistance) {
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
        netDistance / pathDistance <
            CameraSettings().handStableConsistencyThreshold) {
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
