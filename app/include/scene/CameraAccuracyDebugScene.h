#pragma once
#include "BaseScene.h"
#include "Camera.h"
#include "CameraPreviewReceiver.h"
#include "SwordInputCalibration.h"
#include "SwordPose.h"
#include "SwordUdpController.h"
#include "Transform.h"
#include <DirectXMath.h>
#include <cstdint>
#include <string>

class CameraAccuracyDebugScene : public BaseScene {
  public:
    ~CameraAccuracyDebugScene() override;

    void Initialize(const SceneContext &ctx) override;
    void Update() override;
    void Draw() override;
    void DrawTransparent() override;

  private:
    bool RequestHandTrackingStartOnce();
    void UpdateCamera();
    void CaptureNeutral();
    void ResetNeutral();
    SwordPose MakePoseFromPalm(const DirectX::XMFLOAT2 &palm) const;
    Transform BuildSwordTransform(const SwordPose &pose,
                                  const DirectX::XMFLOAT3 &anchor,
                                  bool isLeft) const;
    void DrawDebugSwords();
    void DrawOverlay(float screenWidth, float screenHeight);
    void DrawHandPanel(const char *title, const char *subtitle, size_t handIndex,
                       float x, float y, float w, float h);
    void DrawHandStats(size_t handIndex, float x, float y);
    void DrawRect(float x, float y, float w, float h,
                  const DirectX::XMFLOAT4 &color);
    void DrawFrame(float x, float y, float w, float h, float thickness,
                   const DirectX::XMFLOAT4 &color);
    void DrawPoint(float x, float y, float radius,
                   const DirectX::XMFLOAT4 &color);
    void DrawText(const std::string &text, float x, float y, float scale,
                  const DirectX::XMFLOAT4 &color);

    SwordInputCalibration calibration_{};
    SwordUdpController controller_{};
    CameraPreviewReceiver previewReceiver_{};
    Camera camera_{};
    uint32_t swordModelId_ = 0;
    bool handTrackingStartRequested_ = false;
    bool neutralCapturedThisScene_ = false;
    float sceneTime_ = 0.0f;
};
