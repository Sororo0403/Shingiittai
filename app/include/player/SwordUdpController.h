#pragma once
#include "SwordInputCalibration.h"
#include "SwordControllerState.h"
#include "SwordPose.h"
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
    float GetRawMotionSpeed(size_t handIndex = 0) const;
    bool GetHandCenter(size_t handIndex, float &x, float &y) const;
    bool HasRecentPacket() const;
    void SetCalibration(const SwordInputCalibration &calibration);

  private:
    static constexpr uint16_t kPort = 5005;
    static constexpr float kStaleSeconds = 0.45f;
    static constexpr size_t kMaxHands = 2;
    static constexpr size_t kMotionDirectionHistorySize = 4;

    struct HandState {
        SwordControllerState state{};
        bool hasPacket = false;
        int valid = 0;
        float x = 0.5f;
        float y = 0.5f;
        float dx = 0.0f;
        float dy = 0.0f;
        float speed = 0.0f;
        float confidence = 0.0f;
        float angle = 0.0f;
        float grip = 1.0f;
        float wristSpeed = 0.0f;
        float staleTimer = kStaleSeconds;
        float motionSpeed = 0.0f;
        bool filterReady = false;
        float filteredX = 0.5f;
        float filteredY = 0.5f;
        float filteredDx = 0.0f;
        float filteredDy = 0.0f;
        float filteredSpeed = 0.0f;
        float stableSlashDirX = 1.0f;
        float stableSlashDirY = 0.0f;
        std::array<float, kMotionDirectionHistorySize> motionDirX{};
        std::array<float, kMotionDirectionHistorySize> motionDirY{};
        size_t motionDirectionCount = 0;
        size_t motionDirectionCursor = 0;
        float directionStability = 0.0f;
        float lastSlashDirX = 1.0f;
        float lastSlashDirY = 0.0f;
        float returnRecoveryTimer = 0.0f;
        float lostSlashGraceTimer = 0.0f;
        float lastActiveMotionSpeed = 0.0f;
    };

    bool EnsureSocket();
    bool HasFreshTracking(size_t handIndex) const;
    void ReceivePackets();
    void ApplyHand(size_t handIndex, float dt);
    void CloseSocket();
    HandState *FindHand(const std::string &tag);
    const HandState *GetHand(size_t handIndex) const;

    uintptr_t socket_ = UINTPTR_MAX;
    bool socketReady_ = false;
    SwordInputCalibration calibration_{};
    std::array<HandState, kMaxHands> hands_{};
};
