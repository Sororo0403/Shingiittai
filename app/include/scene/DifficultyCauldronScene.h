#pragma once
#include "BaseScene.h"
#include "CameraPreviewReceiver.h"
#include "Sprite.h"
#include "SwordInputCalibration.h"
#include "SwordUdpController.h"
#include <DirectXMath.h>
#include <array>
#include <cstdint>
#include <memory>
#include <string>

class GameScene;

class DifficultyCauldronScene : public BaseScene {
  public:
    explicit DifficultyCauldronScene(
        const SwordInputCalibration &inputCalibration);
    ~DifficultyCauldronScene() override;

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
    bool RequestHandTrackingStartOnce();
    void UpdateCameraPreview(float deltaTime);
    void UpdateSelection();
    float SelectedDifficultyValue() const;
    void BeginReturnToWeaponSelect();
    void BeginStartGame();
    void ApplyHeatPostProcess();
    void DrawHeatEffects(float screenWidth, float screenHeight);
    void DrawDifficultyGauge(float screenWidth, float screenHeight);
    void DrawDigit(int digit, float centerX, float y, float scale,
                   float alpha = 1.0f);
    void DrawDifficultyValue(float value, float centerX, float y, float scale,
                             float alpha = 1.0f);
    void DrawCameraPreview();
    void DrawRect(float x, float y, float w, float h,
                  const DirectX::XMFLOAT4 &color);
    void DrawFrame(float x, float y, float w, float h, float thickness,
                   const DirectX::XMFLOAT4 &color);
    void DrawImage(const Image &image, float x, float y, float scale = 1.0f,
                   float alpha = 1.0f);
    void DrawTextureRect(uint32_t textureId, float x, float y, float w,
                         float h, const DirectX::XMFLOAT4 &color,
                         float uvWidth = 1.0f,
                         SpriteBlendMode blendMode = SpriteBlendMode::Alpha,
                         float uvLeft = 0.0f);

    SwordInputCalibration inputCalibration_{};
    std::unique_ptr<GameScene> backgroundScene_{};
    SwordUdpController handController_;
    CameraPreviewReceiver previewReceiver_{};
    std::array<Image, 10> digitImages_{};
    Image triangleMaskImage_{};
    Image triangleGradientImage_{};
    float sceneTime_ = 0.0f;
    float transitionTimer_ = 0.0f;
    float selectionPulse_ = 0.0f;
    float handSwingCooldown_ = 0.0f;
    float selectionHoldTimer_ = 0.0f;
    float selectionRepeatTimer_ = 0.0f;
    int selectionHoldDirection_ = 0;
    int selectedDifficultyTenths_ = 20;
    bool handSwingArmed_ = true;
    bool handTrackingStartRequested_ = false;
    bool returnToSelectRequested_ = false;
    bool startGameRequested_ = false;
    Image dotImage_{};
};
