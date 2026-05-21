#pragma once
#include "BaseScene.h"
#include "Sprite.h"
#include "SwordUdpController.h"
#include <DirectXMath.h>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class GameScene;
class Input;

class TitleScene : public BaseScene {
  public:
    ~TitleScene() override;
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

    struct CameraPreviewFrame {
        bool valid = false;
        bool dirty = false;
        uint32_t textureId = 0;
        uint32_t width = 320;
        uint32_t height = 240;
        float staleTimer = 999.0f;
        std::vector<uint8_t> rgbaPixels;
    };

    struct HandReadyState {
        bool active = false;
        bool hasCenter = false;
        bool insideReadyZone = false;
        float x = 0.5f;
        float y = 0.5f;
        float rawSpeed = 0.0f;
        float pulse = 0.0f;
    };

    Image LoadTitleImage(const std::wstring &path);
    void DrawRect(float x, float y, float w, float h,
                  const DirectX::XMFLOAT4 &color);
    void DrawImage(const Image &image, float x, float y, float alpha = 1.0f);
    void DrawImage(const Image &image, float x, float y, float alpha,
                   float scale);
    void DrawCameraModeBadge(float screenWidth, float screenHeight);
    void DrawCameraPreparation(float screenWidth, float screenHeight);
    void DrawCameraPreview(float fieldX, float fieldY, float fieldW,
                           float fieldH);
    void DrawReadyGuide(float fieldX, float fieldY, float fieldW,
                        float fieldH);
    void DrawHandMarker(const HandReadyState &hand, size_t handIndex,
                        float fieldX, float fieldY, float fieldW,
                        float fieldH);
    bool IsAnyButtonTriggered(const Input &input) const;
    bool IsCameraShortcutTriggered(const Input &input) const;
    bool IsHandTestShortcutTriggered(const Input &input) const;
    bool IsCameraReady() const;
    bool HasFreshPreview() const;
    bool HasHandsInReadyZone() const;
    bool HasWaveGesture() const;
    void BeginCameraMode();
    void RequestHandTrackingStartOnce();
    void UpdateCameraPreparation();
    void UpdateHandReadyState(size_t handIndex);
    bool EnsurePreviewSocket();
    void ClosePreviewSocket();
    void ReceivePreviewPackets();
    void HandlePreviewPacket(const uint8_t *data, int bytes);
    void DecodePreviewJpeg(const std::vector<uint8_t> &jpegData);
    void UploadPreviewTextureIfNeeded();
    DirectX::XMFLOAT4 HandColor(size_t handIndex, float alpha = 1.0f) const;

  private:
    std::unique_ptr<GameScene> demoScene_;
    SwordUdpController handWarmupController_;
    CameraPreviewFrame previewFrame_;
    std::array<HandReadyState, 2> handReadyStates_{};
    std::vector<uint8_t> previewJpegBuffer_;
    std::vector<bool> previewChunkReceived_;
    Image logoImage_;
    uintptr_t previewSocket_ = UINTPTR_MAX;
    uint32_t previewFrameId_ = 0;
    size_t previewReceivedChunks_ = 0;
    float sceneTime_ = 0.0f;
    float fadeTimer_ = 0.0f;
    float cameraRequestTimer_ = 999.0f;
    float handsReadyTimer_ = 0.0f;
    bool startRequested_ = false;
    bool cameraStartRequested_ = false;
    bool waitingForCameraReady_ = false;
    bool handTrackingStartRequested_ = false;
    bool previewSocketReady_ = false;
    bool waveArmed_ = true;
};
