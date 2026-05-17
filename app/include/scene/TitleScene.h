#pragma once
#include "BaseScene.h"
#include "Sprite.h"
#include "SwordUdpController.h"
#include <DirectXMath.h>
#include <cstdint>
#include <memory>
#include <string>

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

    Image LoadTitleImage(const std::wstring &path);
    void DrawRect(float x, float y, float w, float h,
                  const DirectX::XMFLOAT4 &color);
    void DrawImage(const Image &image, float x, float y, float alpha = 1.0f);
    void DrawImage(const Image &image, float x, float y, float alpha,
                   float scale);
    void DrawCameraModeBadge(float screenWidth, float screenHeight);
    bool IsAnyButtonTriggered(const Input &input) const;
    bool IsCameraShortcutTriggered(const Input &input) const;
    bool IsCameraReady() const;

  private:
    std::unique_ptr<GameScene> demoScene_;
    SwordUdpController handWarmupController_;
    Image logoImage_;
    float sceneTime_ = 0.0f;
    float fadeTimer_ = 0.0f;
    bool startRequested_ = false;
    bool cameraStartRequested_ = false;
    bool waitingForCameraReady_ = false;
};
