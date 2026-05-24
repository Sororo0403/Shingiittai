#pragma once
#include "SwordInputCalibration.h"
#include "SwordControllerState.h"
#include "SwordPose.h"
#include <array>
#include <cstdint>
#include <cstddef>

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

  private:
    static constexpr uint16_t kPort = 5005;
    static constexpr float kStaleSeconds = 0.45f;
    struct PlayerActionInput {
        bool hasPacket = false;
        float staleTimer = kStaleSeconds;
        std::array<float, 2> slashSpeed = {0.0f, 0.0f};
        std::array<float, 2> slashDirX = {1.0f, -1.0f};
        std::array<float, 2> slashDirY = {0.0f, 0.0f};
        std::array<float, 2> slashConfidence = {0.0f, 0.0f};
        uint32_t debugFlags = 0;
    };

    bool EnsureSocket();
    bool HasFreshActionInput() const;
    void ReceivePackets();
    void ApplyActionInput(float dt);
    void CloseSocket();

    uintptr_t socket_ = UINTPTR_MAX;
    bool socketReady_ = false;
    SwordInputCalibration calibration_{};
    PlayerActionInput actionInput_{};
    std::array<SwordControllerState, 2> actionSwordStates_{};
};
