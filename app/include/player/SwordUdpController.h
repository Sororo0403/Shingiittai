#pragma once
#include "SwordInputCalibration.h"
#include "SwordControllerState.h"
#include "SwordPose.h"
#include <DirectXMath.h>
#include <array>
#include <cstdint>
#include <cstddef>
#include <string>

class SwordUdpController {
  public:
    SwordUdpController() = default;
    ~SwordUdpController();

    SwordUdpController(const SwordUdpController &) = delete;
    SwordUdpController &operator=(const SwordUdpController &) = delete;

    void Update(float dt);

    bool IsActive(size_t handIndex = 0) const;
    SwordPose GetPose(size_t handIndex = 0) const;
    float GetMotionSpeed(size_t handIndex = 0) const;
    void SetCalibration(const SwordInputCalibration &calibration);
    void SetPostSlashCooldownEnabled(bool enabled) {
        postSlashCooldownEnabled_ = enabled;
    }
    bool HasFreshInput() const;

    struct DebugHandState {
        bool fresh = false;
        bool active = false;
        DirectX::XMFLOAT2 rawPalm{0.5f, 0.5f};
        DirectX::XMFLOAT2 neutral{0.5f, 0.5f};
        DirectX::XMFLOAT2 calibratedPalm{0.5f, 0.5f};
        DirectX::XMFLOAT2 slashDir{};
        DirectX::XMFLOAT4 orientation{0.0f, 0.0f, 0.0f, 1.0f};
        bool isSlashMode = false;
        float motionSpeed = 0.0f;
        float staleTimer = 0.0f;
        uint64_t packetFrame = 0;
        uint64_t packetTimestampMs = 0;
        uint32_t packetSequence = 0;
        uint32_t handCount = 0;
        bool bodyTracked = false;
        bool bodyCorrected = false;
        float bodyTiltRadians = 0.0f;
        float handTiltRadians = 0.0f;
        float appliedTiltRadians = 0.0f;
        bool packetChanged = false;
        uint64_t packetDeltaMs = 0;
        DirectX::XMFLOAT2 packetDeltaPalm{};
        float packetMotionSpeed = 0.0f;
        DirectX::XMFLOAT2 gameNetDelta{};
        DirectX::XMFLOAT2 gameNetDirection{};
        float gameNetDistance = 0.0f;
        float gameStableSpeed = 0.0f;
        float gameSlashThreshold = 0.0f;
        float gameSlashNetDistanceThreshold = 0.0f;
        float gameGatedSlashSpeed = 0.0f;
        bool slashTriggeredThisFrame = false;
        float visualScale = 0.095f;
        bool nearEdge = false;
        std::string sourceLabel;
        float sourceScore = 0.0f;
        float tiltRadians = 0.0f;
    };

    DebugHandState GetDebugHandState(size_t handIndex = 0) const;

  private:
    static constexpr uint16_t kPort = 5005;
    static constexpr float kStaleSeconds = 0.45f;
    struct RawHandInput {
        bool hasPacket = false;
        float staleTimer = kStaleSeconds;
        uint64_t frame = 0;
        uint64_t timestampMs = 0;
        uint32_t sequence = 0;
        uint64_t packetDeltaMs = 0;
        uint32_t handCount = 0;
        std::array<bool, 2> active = {false, false};
        std::array<std::string, 2> sourceLabel = {"", ""};
        std::array<float, 2> sourceScore = {0.0f, 0.0f};
        std::array<float, 2> handScale = {0.095f, 0.095f};
        std::array<bool, 2> hasHandTilt = {false, false};
        std::array<float, 2> handTiltRadians = {0.0f, 0.0f};
        bool bodyTracked = false;
        float bodyTiltRadians = 0.0f;
        float bodyVisibility = 0.0f;
        bool hasTilt = false;
        float tiltRadians = 0.0f;
        std::array<DirectX::XMFLOAT2, 2> palm = {
            DirectX::XMFLOAT2{0.5f, 0.5f},
            DirectX::XMFLOAT2{0.5f, 0.5f}};
    };

    struct StableMotion {
        bool valid = false;
        DirectX::XMFLOAT2 direction{0.0f, 0.0f};
        float speed = 0.0f;
        float netDistance = 0.0f;
    };

    bool EnsureSocket();
    bool HasFreshRawInput() const;
    void ReceivePackets();
    void ApplyRawInput(float dt);
    void ApplyRawHand(size_t i, float dt);
    void UpdateActiveHandTracking(
        size_t i, float dt, float effectiveSlashCooldown,
        float reacquireSlashThreshold, float jumpSlashDistanceThreshold,
        DirectX::XMFLOAT2 &corrected, bool &reacquired, bool &jumped,
        bool &synthesizeReacquireSlash, bool &synthesizeJumpSlash,
        DirectX::XMFLOAT2 &reacquireDelta, float &reacquireDistance,
        DirectX::XMFLOAT2 &jumpDelta, float &jumpDistance);
    bool ShouldSynthesizeReacquireSlash(
        size_t i, float effectiveSlashCooldown,
        const DirectX::XMFLOAT2 &corrected,
        const DirectX::XMFLOAT2 &reacquireDelta,
        float reacquireDistance, float threshold,
        bool reacquired) const;
    bool ShouldSynthesizeJumpSlash(
        size_t i, float effectiveSlashCooldown,
        const DirectX::XMFLOAT2 &corrected,
        const DirectX::XMFLOAT2 &jumpDelta, float jumpDistance,
        float threshold, bool jumped, bool teleported) const;
    StableMotion UpdateActiveHandMotion(
        size_t i, float dt, const DirectX::XMFLOAT2 &corrected,
        bool reacquired, bool jumped, float stableNetDistanceThreshold,
        float preLossNetDistanceThreshold,
        float preLossDirectionThreshold,
        DirectX::XMFLOAT2 &frameDeltaPalm);
    DirectX::XMFLOAT2 ResolveHandSlashDirection(
        const DirectX::XMFLOAT2 &corrected,
        const StableMotion &stableMotion,
        const DirectX::XMFLOAT2 &frameDeltaPalm,
        bool synthesizeJumpSlash,
        const DirectX::XMFLOAT2 &jumpDelta, float jumpDistance,
        bool synthesizeReacquireSlash,
        const DirectX::XMFLOAT2 &reacquireDelta,
        float reacquireDistance) const;
    void UpdateActiveHandSlash(
        size_t i, float dt, float effectiveSlashCooldown,
        const DirectX::XMFLOAT2 &corrected,
        const StableMotion &stableMotion,
        const DirectX::XMFLOAT2 &frameDeltaPalm,
        const DirectX::XMFLOAT2 &slashDir,
        bool synthesizeReacquireSlash, bool synthesizeJumpSlash,
        float baseSlashThreshold, float baseSlashResetThreshold,
        float baseSlashNetDistanceThreshold, float overallThresholdScale,
        float stableNetDistanceThreshold, float verticalSensitivity,
        float horizontalSensitivity);
    float ComputeStableSlashSpeed(
        size_t i, float slashThreshold,
        float slashNetDistanceThreshold,
        const StableMotion &stableMotion,
        const DirectX::XMFLOAT2 &frameDeltaPalm) const;
    void UpdateHandSlashRearm(size_t i, float dt, float slashSpeed,
                              float slashResetThreshold,
                              float stableNetDistanceThreshold,
                              const StableMotion &stableMotion,
                              const DirectX::XMFLOAT2 &corrected);
    float ComputeGatedHandSlashSpeed(
        size_t i, float effectiveSlashCooldown, float slashSpeed,
        float slashThreshold, float stableSlashSpeed,
        bool synthesizeReacquireSlash,
        bool synthesizeJumpSlash) const;
    void UpdateHandOrientation(size_t i,
                               const DirectX::XMFLOAT2 &corrected);
    void ResetHandFrameState(size_t i, float dt);
    bool HandleInactiveHand(size_t i, float dt,
                            float effectiveSlashCooldown,
                            float slashThreshold,
                            float preLossDirectionThreshold);
    void CloseSocket();
    void UpdateTiltEstimate(float dt);
    size_t ChooseSingleHandSlot(const DirectX::XMFLOAT2 &palm) const;
    DirectX::XMFLOAT2 TransformCameraPalmForSword(
        size_t handIndex, const DirectX::XMFLOAT2 &palm) const;
    void ResetMotionHistory(size_t handIndex);
    void AddMotionSample(size_t handIndex, const DirectX::XMFLOAT2 &palm,
                         float dt);
    StableMotion ComputeStableMotion(size_t handIndex,
                                     float minNetDistance) const;

    uintptr_t socket_ = UINTPTR_MAX;
    bool socketReady_ = false;
    SwordInputCalibration calibration_{};
    RawHandInput rawInput_{};
    std::array<SwordControllerState, 2> swordStates_{};
    std::array<DirectX::XMFLOAT2, 2> calibratedPalm_ = {
        DirectX::XMFLOAT2{0.5f, 0.5f}, DirectX::XMFLOAT2{0.5f, 0.5f}};
    std::array<DirectX::XMFLOAT2, 2> previousCalibratedPalm_ = {
        DirectX::XMFLOAT2{0.5f, 0.5f}, DirectX::XMFLOAT2{0.5f, 0.5f}};
    std::array<bool, 2> hasPreviousCalibratedPalm_ = {false, false};
    std::array<DirectX::XMFLOAT2, 2> previousPacketPalm_ = {
        DirectX::XMFLOAT2{0.5f, 0.5f}, DirectX::XMFLOAT2{0.5f, 0.5f}};
    std::array<bool, 2> hasPreviousPacketPalm_ = {false, false};
    std::array<DirectX::XMFLOAT2, 2> packetDeltaPalm_ = {
        DirectX::XMFLOAT2{0.0f, 0.0f}, DirectX::XMFLOAT2{0.0f, 0.0f}};
    std::array<float, 2> packetMotionSpeed_ = {0.0f, 0.0f};
    std::array<DirectX::XMFLOAT2, 2> debugGameNetDelta_ = {
        DirectX::XMFLOAT2{0.0f, 0.0f}, DirectX::XMFLOAT2{0.0f, 0.0f}};
    std::array<DirectX::XMFLOAT2, 2> debugGameNetDirection_ = {
        DirectX::XMFLOAT2{0.0f, 0.0f}, DirectX::XMFLOAT2{0.0f, 0.0f}};
    std::array<float, 2> debugGameNetDistance_ = {0.0f, 0.0f};
    std::array<float, 2> debugGameStableSpeed_ = {0.0f, 0.0f};
    std::array<float, 2> debugGameSlashThreshold_ = {0.0f, 0.0f};
    std::array<float, 2> debugGameSlashNetDistanceThreshold_ = {0.0f, 0.0f};
    std::array<float, 2> debugGameGatedSlashSpeed_ = {0.0f, 0.0f};
    std::array<bool, 2> debugSlashTriggeredThisFrame_ = {false, false};
    uint32_t lastAppliedPacketSequence_ = 0;
    bool packetChangedThisUpdate_ = false;
    std::array<float, 2> motionSpeed_ = {0.0f, 0.0f};
    std::array<bool, 2> handSlashArmed_ = {true, true};
    std::array<float, 2> handSlashNeutralTimer_ = {0.0f, 0.0f};
    std::array<float, 2> handSlashCooldown_ = {0.0f, 0.0f};
    bool postSlashCooldownEnabled_ = true;
    std::array<DirectX::XMFLOAT2, 2> smoothedPalm_ = {
        DirectX::XMFLOAT2{0.5f, 0.5f}, DirectX::XMFLOAT2{0.5f, 0.5f}};
    std::array<bool, 2> hasSmoothedPalm_ = {false, false};
    std::array<bool, 2> wasHandActive_ = {false, false};
    std::array<float, 2> reacquireSuppressTimer_ = {0.0f, 0.0f};
    std::array<float, 2> edgeExitSuppressTimer_ = {0.0f, 0.0f};
    std::array<DirectX::XMFLOAT2, 2> lostPalm_ = {
        DirectX::XMFLOAT2{0.5f, 0.5f}, DirectX::XMFLOAT2{0.5f, 0.5f}};
    std::array<bool, 2> hasLostPalm_ = {false, false};
    std::array<DirectX::XMFLOAT2, 2> lastMotionDir_ = {
        DirectX::XMFLOAT2{0.0f, 0.0f}, DirectX::XMFLOAT2{0.0f, 0.0f}};
    std::array<float, 2> lastMotionSpeed_ = {0.0f, 0.0f};
    std::array<float, 2> lastMotionAge_ = {999.0f, 999.0f};
    std::array<float, 2> syntheticLostSlashTimer_ = {0.0f, 0.0f};
    float smoothedTiltRadians_ = 0.0f;
    bool hasSmoothedTilt_ = false;
    static constexpr size_t kMotionHistorySize = 8;
    std::array<std::array<DirectX::XMFLOAT2, kMotionHistorySize>, 2>
        motionHistoryPalm_{};
    std::array<std::array<float, kMotionHistorySize>, 2> motionHistoryAge_{};
    std::array<size_t, 2> motionHistoryStart_ = {0, 0};
    std::array<size_t, 2> motionHistoryCount_ = {0, 0};
};
