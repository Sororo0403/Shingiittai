#pragma once
#include "BaseScene.h"
#include "CameraPreviewReceiver.h"
#include "GameScene.h"
#include "SwordInputCalibration.h"
#include <DirectXMath.h>

class HandLoadingScene : public BaseScene {
  public:
    HandLoadingScene(const SwordInputCalibration &inputCalibration,
                     float difficulty);
    HandLoadingScene(const SwordInputCalibration &inputCalibration,
                     GameScene::Mode destinationMode);

    void Initialize(const SceneContext &ctx) override;
    void Update() override;
    void Draw() override;
    void DrawTransparent() override {}
    void DrawPostProcessOverlay() override;

  private:
    bool RequestHandTrackingStartOnce();
    bool IsHandTrackingReady() const;
    void DrawRect(float x, float y, float w, float h,
                  const DirectX::XMFLOAT4 &color);
    void DrawLoadingText(float screenWidth, float screenHeight);
    void DrawFacingInstruction(float screenWidth, float screenHeight);
    void DrawBlockText(const char *text, float x, float y, float scale,
                       const DirectX::XMFLOAT4 &color);
    void DrawBlockGlyph(char glyph, float x, float y, float scale,
                        const DirectX::XMFLOAT4 &color);
    float MeasureBlockText(const char *text, float scale) const;

    SwordInputCalibration inputCalibration_{};
    float difficulty_ = 2.0f;
    GameScene::Mode destinationMode_ = GameScene::Mode::Gameplay;
    bool handTrackingStartRequested_ = false;
    CameraPreviewReceiver previewReceiver_{};
};
