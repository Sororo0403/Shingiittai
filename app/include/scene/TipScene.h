#pragma once
#include "BaseScene.h"
#include "JoyCon.h"
#include "Sprite.h"
#include "SwordInputCalibration.h"
#include "SwordUdpController.h"
#include <cstdint>
#include <string>
#include <vector>

class TipScene : public BaseScene {
  public:
    explicit TipScene(const SwordInputCalibration &inputCalibration);
    ~TipScene() override;

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

    Image LoadTextureImage(const std::wstring &path);
    bool ShouldStart();
    void RequestHandTrackingStartOnce();
    void UpdateCameraPreview(float deltaTime);
    bool EnsurePreviewSocket();
    void ClosePreviewSocket();
    void ReceivePreviewPackets();
    void HandlePreviewPacket(const uint8_t *data, int bytes);
    void DecodePreviewJpeg(const std::vector<uint8_t> &jpegData);
    void UploadPreviewTextureIfNeeded();
    void DrawCameraPreview();
    void DrawRect(float x, float y, float w, float h,
                  const DirectX::XMFLOAT4 &color);
    void DrawImage(const Image &image, float x, float y, float scale = 1.0f,
                   float alpha = 1.0f);

    SwordInputCalibration inputCalibration_{};
    SwordUdpController handController_;
    JoyCon leftJoyCon_;
    JoyCon rightJoyCon_;
    Image backgroundImage_{};
    Image titleImage_{};
    Image bodyImage_{};
    Image promptImage_{};
    float sceneTime_ = 0.0f;
    int handSwingCount_ = 0;
    bool handSwingArmed_ = true;
    bool handTrackingStartRequested_ = false;
    CameraPreviewFrame previewFrame_{};
    std::vector<uint8_t> previewJpegBuffer_{};
    std::vector<bool> previewChunkReceived_{};
    uintptr_t previewSocket_ = UINTPTR_MAX;
    uint32_t previewFrameId_ = 0;
    size_t previewReceivedChunks_ = 0;
    bool previewSocketReady_ = false;
};
