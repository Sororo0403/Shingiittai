#pragma once
#include "BaseScene.h"
#include "HandCameraPreviewReceiver.h"
#include "Sprite.h"
#include "SwordUdpController.h"
#include <DirectXMath.h>
#include <array>
#include <cstdint>
#include <vector>

class Input;

class CameraTestScene : public BaseScene {
  public:
    void Initialize(const SceneContext &ctx) override;
    void Update() override;
    void Draw() override;
    void DrawOverlay() override {}

  private:
    struct Rect {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
    };

    struct TrailSample {
        DirectX::XMFLOAT2 point{0.5f, 0.5f};
        float age = 0.0f;
    };

    void UpdateInput(Input *input);
    void UpdateTrails();
    void UpdatePreviewTexture();
    void Layout(float screenWidth, float screenHeight);
    void ReturnToTitle();
    DirectX::XMFLOAT2 PreviewPoint(const DirectX::XMFLOAT2 &point) const;
    DirectX::XMFLOAT4 HandColor(size_t handIndex, float alpha = 1.0f) const;
    void DrawBackground(float screenWidth, float screenHeight);
    void DrawPreview();
    void DrawTrackingOverlay();
    void DrawHandMarker(size_t handIndex, const DirectX::XMFLOAT2 &point,
                        float speed, bool active);
    void DrawStatusBars(float screenWidth, float screenHeight);
    void DrawRect(float x, float y, float w, float h,
                  const DirectX::XMFLOAT4 &color);
    void DrawImage(uint32_t textureId, float x, float y, float w, float h,
                   const DirectX::XMFLOAT4 &color);

  private:
    static constexpr size_t kHandCount = 2;
    static constexpr uint32_t kPreviewWidth = 320;
    static constexpr uint32_t kPreviewHeight = 240;
    static constexpr float kTrailSeconds = 1.6f;
    static constexpr size_t kMaxTrailSamples = 72;

    SwordUdpController handController_;
    HandCameraPreviewReceiver cameraPreviewReceiver_;
    uint32_t previewTextureId_ = 0;
    std::vector<uint8_t> previewPixels_;
    bool hasPreviewFrame_ = false;
    Rect previewRect_{};
    std::array<DirectX::XMFLOAT2, kHandCount> handCenters_{};
    std::array<bool, kHandCount> handActive_{};
    std::array<float, kHandCount> handSpeeds_{};
    std::array<std::vector<TrailSample>, kHandCount> trails_{};
    float sceneTime_ = 0.0f;
};
