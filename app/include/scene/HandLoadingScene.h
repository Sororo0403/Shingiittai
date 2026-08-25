#pragma once
#include "BaseScene.h"
#include "CameraAccuracyDebugScene.h"
#include "CameraPreviewReceiver.h"
#include "GameScene.h"
#include "SwordInputCalibration.h"
#include "SwordUdpController.h"
#include <DirectXMath.h>
#include <array>
#include <string>

class HandLoadingScene : public BaseScene {
  public:
    HandLoadingScene(const SwordInputCalibration &inputCalibration,
                     float difficulty);
    HandLoadingScene(const SwordInputCalibration &inputCalibration,
                     GameScene::Mode destinationMode);
    explicit HandLoadingScene(const SwordInputCalibration &inputCalibration);
    explicit HandLoadingScene(CameraAccuracyDebugScene::ReturnTarget returnTarget);

    void Initialize(const SceneContext &ctx) override;
    void Update() override;
    void Draw() override;
    void DrawTransparent() override {}
    void DrawPostProcessOverlay() override;

  private:
    struct Image {
        uint32_t textureId = 0;
        float width = 0.0f;
        float height = 0.0f;
    };

    bool RequestHandTrackingStartOnce();
    bool IsHandTrackingReady() const;
    bool TryAdvancePreviewDestination(bool previewReady);
    Image LoadTextureImage(const std::wstring &path);
    void DrawRect(float x, float y, float w, float h,
                  const DirectX::XMFLOAT4 &color);
    void DrawLoadingMark(float screenWidth, float screenHeight);
    void DrawLoadingRing(float centerX, float centerY, float radius);
    void DrawImageCentered(const Image &image, float centerX, float centerY,
                           float maxWidth, float maxHeight,
                           const DirectX::XMFLOAT4 &color);
    void DrawFacingInstruction(float screenWidth, float screenHeight);
    void DrawCalibrationOverlay(float screenWidth, float screenHeight);
    void DrawHandGuide(const SwordUdpController::DebugHandState &hand,
                       float screenWidth, float screenHeight,
                       const DirectX::XMFLOAT4 &color);
    void DrawFrame(float x, float y, float w, float h, float thickness,
                   const DirectX::XMFLOAT4 &color);
    void DrawCountdownNumber(int value, float centerX, float centerY,
                             float scale, const DirectX::XMFLOAT4 &color);
    void DrawSevenSegmentDigit(int value, float x, float y, float scale,
                               const DirectX::XMFLOAT4 &color);
    void DrawBlockText(const char *text, float x, float y, float scale,
                       const DirectX::XMFLOAT4 &color);
    void DrawBlockGlyph(char glyph, float x, float y, float scale,
                        const DirectX::XMFLOAT4 &color);
    float MeasureBlockText(const char *text, float scale) const;

    SwordInputCalibration inputCalibration_{};
    float difficulty_ = 2.0f;
    GameScene::Mode destinationMode_ = GameScene::Mode::Gameplay;
    CameraAccuracyDebugScene::ReturnTarget sensitivityReturnTarget_ =
        CameraAccuracyDebugScene::ReturnTarget::WeaponOption;
    bool destinationDifficultySelect_ = false;
    bool destinationSensitivityAdjust_ = false;
    bool handTrackingStartRequested_ = false;
    CameraPreviewReceiver previewReceiver_{};
    SwordUdpController handController_;
    Image gameLogoImage_{};
    Image faceCameraMessageImage_{};
    float stillTimer_ = 0.0f;
    float sceneTimer_ = 0.0f;
    std::array<DirectX::XMFLOAT2, 2> neutralSum_ = {
        DirectX::XMFLOAT2{0.0f, 0.0f}, DirectX::XMFLOAT2{0.0f, 0.0f}};
    std::array<float, 2> restSpeedSum_ = {0.0f, 0.0f};
    uint32_t neutralSampleCount_ = 0;
};
