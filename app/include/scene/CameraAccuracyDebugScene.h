#pragma once
#include "BaseScene.h"
#include "Camera.h"
#include "CameraPreviewReceiver.h"
#include "Player.h"
#include "SwordInputCalibration.h"
#include "SwordPose.h"
#include "SwordUdpController.h"
#include "Transform.h"
#include <DirectXMath.h>
#include <array>
#include <cstdint>
#include <string>

class CameraAccuracyDebugScene : public BaseScene {
  public:
    enum class ReturnTarget {
        Title,
        WeaponSelect,
        TutorialSelect,
        WeaponOption,
        TutorialOption,
    };

    explicit CameraAccuracyDebugScene(ReturnTarget returnTarget =
                                          ReturnTarget::Title);
    ~CameraAccuracyDebugScene() override;

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

    bool RequestHandTrackingStartOnce();
    Image LoadTextureImage(const std::wstring &path);
    void UpdateCamera();
    void AdjustSelectedSensitivity(int direction);
    void CaptureNeutral();
    void ResetNeutral();
    void UpdateGamePreview(float deltaTime);
    SwordPose MakePoseFromPalm(const DirectX::XMFLOAT2 &palm) const;
    Transform BuildSwordTransform(const SwordPose &pose,
                                  const DirectX::XMFLOAT3 &anchor,
                                  bool isLeft) const;
    void DrawDebugSwords();
    void DrawPreviewBackground(float screenWidth, float screenHeight);
    void DrawOverlay(float screenWidth, float screenHeight);
    void DrawSensitivityPanel(float screenWidth);
    void DrawGaugeRow(size_t index, const Image &label, float value,
                      const DirectX::XMFLOAT4 &barColor, float panelX,
                      float rowY, float panelW);
    void DrawImage(const Image &image, float x, float y, float scale = 1.0f,
                   float alpha = 1.0f);
    void DrawImageCentered(const Image &image, float centerX, float centerY,
                           float maxWidth, float maxHeight, float alpha = 1.0f);
    void DrawSensitivityValue(float value, float x, float y, float scale,
                              const DirectX::XMFLOAT4 &color);
    float RequiredTravelForSensitivity(float axisSensitivity) const;
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
    Player gamePreviewPlayer_{};
    uint32_t playerModelId_ = 0;
    uint32_t swordModelId_ = 0;
    Image titleImage_{};
    Image controlsImage_{};
    std::array<Image, 4> sensitivityLabelImages_{};
    std::array<Image, 10> digitImages_{};
    Image dotImage_{};
    ReturnTarget returnTarget_ = ReturnTarget::Title;
    int selectedSensitivityIndex_ = 0;
    bool handTrackingStartRequested_ = false;
    bool neutralCapturedThisScene_ = false;
    float sceneTime_ = 0.0f;
};
