#pragma once
#include "BaseScene.h"
#include "Camera.h"
#include "InputControlType.h"
#include "Sprite.h"
#include "Transform.h"
#include <DirectXMath.h>
#include <array>
#include <cstdint>
#include <string>

class Input;

class WeaponSelectScene : public BaseScene {
  public:
    void Initialize(const SceneContext &ctx) override;
    void Update() override;
    void Draw() override;
    void DrawOverlay() override {}

  private:
    struct Image {
        uint32_t textureId = 0;
        float width = 0.0f;
        float height = 0.0f;
    };

    struct ButtonRect {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
    };

    static constexpr int kWeaponCount = 3;

    Image LoadTextureImage(const std::wstring &path);
    void UpdateSelection(Input *input);
    void BeginStart();
    void Layout(float screenWidth, float screenHeight);
    bool IsMouseOver(const ButtonRect &rect) const;
    InputControlType SelectedControlType() const;
    void UpdateCamera();
    void UpdateLighting();

    void DrawBackground(float screenWidth, float screenHeight);
    void DrawCards(float screenWidth, float screenHeight);
    void DrawModelPreviews();
    void DrawLabels(float screenWidth, float screenHeight);
    void DrawStartTransition(float screenWidth, float screenHeight);
    void DrawRect(float x, float y, float w, float h,
                  const DirectX::XMFLOAT4 &color);
    void DrawImage(const Image &image, float x, float y, float scale = 1.0f,
                   float alpha = 1.0f);
    Transform MakeSwordTransform(int weaponIndex, int swordIndex) const;
    DirectX::XMFLOAT4 WeaponColor(int index, float alpha = 1.0f) const;

    int selectedIndex_ = 0;
    float sceneTime_ = 0.0f;
    float transitionTimer_ = 0.0f;
    bool startRequested_ = false;

    uint32_t swordModelId_ = 0;
    Camera camera_;
    Image backgroundImage_{};
    Image titleImage_{};
    Image controlsImage_{};
    Image readyImage_{};
    std::array<Image, kWeaponCount> weaponNameImages_{};
    std::array<Image, kWeaponCount> weaponDescImages_{};
    std::array<Image, kWeaponCount> weaponBottomImages_{};
    std::array<ButtonRect, kWeaponCount> cardRects_{};
    std::array<float, kWeaponCount> pulseTimers_{};
};
