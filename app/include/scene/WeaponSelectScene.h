#pragma once
#include "BaseScene.h"
#include "Camera.h"
#include "PlayerWeaponType.h"
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
    void DrawOverlay() override;

  private:
    void StartGame(PlayerWeaponType weaponType);
    void UpdateSelection(Input *input);
    void UpdateCamera();
    void UpdateLighting();
    void DrawModels();
    void DrawUiBase();
    void DrawUiOverlay();
    void DrawRect(float x, float y, float w, float h,
                  const DirectX::XMFLOAT4 &color);
    struct TextImage {
        uint32_t textureId = 0;
        float width = 0.0f;
        float height = 0.0f;
    };
    TextImage LoadTextImage(const std::wstring &path);
    void DrawImage(const TextImage &image, float x, float y,
                   float scale = 1.0f, float alpha = 1.0f);
    void DrawStretchImage(const TextImage &image, float x, float y, float w,
                          float h, float alpha = 1.0f);
    Transform MakeSwordTransform(int weaponIndex, int swordIndex) const;
    DirectX::XMFLOAT4 WeaponColor(int index, float alpha = 1.0f) const;

  private:
    static constexpr int kWeaponCount = 3;

    int selectedIndex_ = 0;
    bool startRequested_ = false;
    PlayerWeaponType requestedWeaponType_ = PlayerWeaponType::Standard;
    float sceneTime_ = 0.0f;
    float selectionFlashTimer_ = 0.0f;
    float readyTimer_ = 0.0f;
    uint32_t swordModelId_ = 0;
    TextImage backgroundImage_;
    TextImage titleImage_;
    std::array<TextImage, kWeaponCount> weaponNameImages_{};
    std::array<TextImage, kWeaponCount> weaponDescImages_{};
    std::array<TextImage, kWeaponCount> weaponBottomImages_{};
    TextImage controlsImage_;
    TextImage readyImage_;
    Camera camera_;
    std::array<float, kWeaponCount> tilePulse_{};
};
