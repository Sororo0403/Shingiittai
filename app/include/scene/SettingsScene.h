#pragma once
#include "BaseScene.h"
#include "Sprite.h"
#include <DirectXMath.h>
#include <cstdint>
#include <string>

class Input;

class SettingsScene : public BaseScene {
  public:
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

    struct Rect {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
    };

    Image LoadSettingsImage(const std::wstring &path);
    void UpdateInput(Input *input);
    void SetVolume(float volume);
    void ReturnToModeSelect();
    void Layout(float screenWidth, float screenHeight);
    bool IsMouseOver(const Rect &rect) const;
    float MouseX() const;
    void DrawBackground(float screenWidth, float screenHeight);
    void DrawVolumeControl();
    void DrawBackButton();
    void DrawRect(float x, float y, float w, float h,
                  const DirectX::XMFLOAT4 &color);
    void DrawImage(const Image &image, float x, float y, float scale = 1.0f,
                   float alpha = 1.0f);

  private:
    Image titleImage_;
    Image volumeImage_;
    Image backImage_;
    Image controlsImage_;
    Rect volumeRect_;
    Rect backRect_;
    int selectedIndex_ = 0;
    float volume_ = 1.0f;
    float sceneTime_ = 0.0f;
};
