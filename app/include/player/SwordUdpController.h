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
        bool packetChanged = false;
        uint64_t packetDeltaMs = 0;
        DirectX::XMFLOAT2 packetDeltaPalm{};
        float packetMotionSpeed = 0.0f;
        bool nearEdge = false;
        std::string sourceLabel{};
        float sourceScore = 0.0f;
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
        bool bodyTracked = false;
        std::array<bool, 2> bodyCorrected = {false, false};
        std::array<bool, 2> active = {false, false};
        std::array<std::string, 2> sourceLabel = {"", ""};
        std::array<float, 2> sourceScore = {0.0f, 0.0f};
        std::array<DirectX::XMFLOAT2, 2> palm = {
            DirectX::XMFLOAT2{0.5f, 0.5f},
            DirectX::XMFLOAT2{0.5f, 0.5f}};
    };

    bool EnsureSocket();
    bool HasFreshRawInput() const;
    void ReceivePackets();
    void ApplyRawInput(float dt);
    void CloseSocket();
    size_t ChooseSingleHandSlot(const DirectX::XMFLOAT2 &palm) const;

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
    std::array<DirectX::XMFLOAT2, 2> filteredCalibratedPalm_ = {
        DirectX::XMFLOAT2{0.5f, 0.5f}, DirectX::XMFLOAT2{0.5f, 0.5f}};
    std::array<bool, 2> hasFilteredCalibratedPalm_ = {false, false};
    std::array<DirectX::XMFLOAT2, 2> previousPacketPalm_ = {
        DirectX::XMFLOAT2{0.5f, 0.5f}, DirectX::XMFLOAT2{0.5f, 0.5f}};
    std::array<bool, 2> hasPreviousPacketPalm_ = {false, false};
    std::array<DirectX::XMFLOAT2, 2> packetDeltaPalm_ = {
        DirectX::XMFLOAT2{0.0f, 0.0f}, DirectX::XMFLOAT2{0.0f, 0.0f}};
    std::array<float, 2> packetMotionSpeed_ = {0.0f, 0.0f};
    uint32_t lastAppliedPacketSequence_ = 0;
    bool packetChangedThisUpdate_ = false;
    std::array<float, 2> motionSpeed_ = {0.0f, 0.0f};
};
