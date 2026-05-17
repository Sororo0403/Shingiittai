#pragma once
#include "BaseScene.h"
#include "HandCameraPreviewReceiver.h"
#include "Sprite.h"
#include "SwordInputCalibration.h"
#include "SwordUdpController.h"
#include <DirectXMath.h>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

class HandRegistrationScene : public BaseScene {
  public:
    void Initialize(const SceneContext &ctx) override;
    void Update() override;
    void Draw() override;
    void DrawOverlay() override;

  private:
    struct Image {
        uint32_t textureId = 0;
        float width = 0.0f;
        float height = 0.0f;
    };

    Image LoadTextureImage(const std::wstring &path);
    enum class RegistrationStage {
        RegisterLeft,
        WaitForClear,
        RegisterRight,
        Done,
    };

    void SendHandRegistrationReset();
    void SendHandRegistrationSlot(size_t handIndex);
    void FinishRegistration();
    bool IsOnlyHandVisible(size_t handIndex) const;
    bool AreHandsClear() const;
    void DrawBackground(float screenWidth, float screenHeight);
    void DrawCameraPreview(float screenWidth, float screenHeight);
    void DrawProgress(float screenWidth, float screenHeight);
    void DrawHandStatus(float screenWidth, float screenHeight);
    void DrawCameraBadge();
    void DrawRect(float x, float y, float w, float h,
                  const DirectX::XMFLOAT4 &color);
    void DrawImage(const Image &image, float x, float y, float scale = 1.0f,
                   float alpha = 1.0f);

    SwordUdpController handController_;
    HandCameraPreviewReceiver cameraPreviewReceiver_;
    SwordInputCalibration inputCalibration_{};

    Image backgroundImage_{};
    uint32_t cameraPreviewTextureId_ = 0;
    std::vector<uint8_t> cameraPreviewPixels_{};
    bool hasCameraPreviewFrame_ = false;

    float sceneTime_ = 0.0f;
    float registrationTimer_ = 0.0f;
    float clearTimer_ = 0.0f;
    float commandRetryTimer_ = 0.0f;
    std::array<DirectX::XMFLOAT2, 2> handPreviewCenters_{};
    std::array<bool, 2> handPreviewVisible_{};
    std::array<DirectX::XMFLOAT2, 2> registeredCenters_{};
    std::array<bool, 2> registeredVisible_{};
    RegistrationStage stage_ = RegistrationStage::RegisterLeft;
    bool finished_ = false;
};
