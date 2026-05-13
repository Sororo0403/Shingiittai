#pragma once
#include "SwordControllerState.h"
#include "SwordPose.h"
#include <cstdint>

class SwordUdpController {
  public:
    SwordUdpController() = default;
    ~SwordUdpController();

    SwordUdpController(const SwordUdpController &) = delete;
    SwordUdpController &operator=(const SwordUdpController &) = delete;

    void Update(float dt);

    bool IsActive() const { return hasPacket_ && staleTimer_ < kStaleSeconds; }
    SwordPose GetPose() const;
    float GetMotionSpeed() const { return motionSpeed_; }

  private:
    bool EnsureSocket();
    void ReceivePackets();
    void ApplyHand(float dt);
    void CloseSocket();

    static constexpr uint16_t kPort = 5005;
    static constexpr float kStaleSeconds = 0.25f;

    SwordControllerState state_{};
    uintptr_t socket_ = UINTPTR_MAX;
    bool socketReady_ = false;
    bool hasPacket_ = false;
    int valid_ = 0;
    float handX_ = 0.5f;
    float handY_ = 0.5f;
    float handDx_ = 0.0f;
    float handDy_ = 0.0f;
    float handSpeed_ = 0.0f;
    float confidence_ = 0.0f;
    float staleTimer_ = kStaleSeconds;
    float motionSpeed_ = 0.0f;
};
