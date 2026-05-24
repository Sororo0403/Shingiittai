#pragma once
#include "BaseScene.h"
#include "CameraPreviewReceiver.h"
#include "JoyCon.h"
#include "Sprite.h"
#include "SwordInputCalibration.h"
#include "SwordUdpController.h"
#include <cstdint>
#include <string>

class TipScene : public BaseScene {
  public:
    explicit TipScene(const SwordInputCalibration &inputCalibration);
    ~TipScene() override;

    void Initialize(const SceneContext &ctx) override;
    void Update() override;
    void Draw() override;
    void DrawTransparent() override;

  private:
    struct Image {
        uint32_t textureId = 0;
        float width = 0.0f;
        float height = 0.0f;
    };

    Image LoadTextureImage(const std::wstring &path);
    bool ShouldStart();
    void RequestHandTrackingStartOnce();
    void UpdateCameraPreview(float deltaTime);
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
    CameraPreviewReceiver previewReceiver_{};
};
