#pragma once
#include "SwordInputCalibration.h"
#include "SwordControllerState.h"
#include "SwordPose.h"
#include <DirectXMath.h>
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
    };

    DebugHandState GetDebugHandState(size_t handIndex = 0) const;

  private:
    static constexpr uint16_t kPort = 5005;
    static constexpr float kStaleSeconds = 0.45f;
    struct RawHandInput {
        bool hasPacket = false;
        float staleTimer = kStaleSeconds;
        std::array<bool, 2> active = {false, false};
        std::array<DirectX::XMFLOAT2, 2> palm = {
            DirectX::XMFLOAT2{0.5f, 0.5f},
            DirectX::XMFLOAT2{0.5f, 0.5f}};
    };

    bool EnsureSocket();
    bool HasFreshRawInput() const;
    void ReceivePackets();
    void ApplyRawInput(float dt);
    void CloseSocket();

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
    std::array<float, 2> motionSpeed_ = {0.0f, 0.0f};
};
