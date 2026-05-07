#pragma once
#include "BaseScene.h"
#include "Sprite.h"
#include <DirectXMath.h>
#include <array>
#include <cstdint>
#include <string>

class Input;

class ModeSelectScene : public BaseScene {
  public:
    void Initialize(const SceneContext &ctx) override;
    void Update() override;
    void Draw() override;
    void DrawOverlay() override;

  private:
    enum class NextScene {
        None,
        Game,
        Settings,
    };

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

    Image LoadMenuImage(const std::wstring &path);
    void UpdateSelection(Input *input);
    void ActivateSelection();
    void BeginTransition(NextScene nextScene);
    void LayoutButtons(float screenWidth, float screenHeight);
    bool IsMouseOver(const ButtonRect &rect) const;
    void DrawBackground(float screenWidth, float screenHeight);
    void DrawHelp(float screenWidth, float screenHeight);
    void DrawButton(const ButtonRect &rect, const Image &label, bool selected);
    void DrawRect(float x, float y, float w, float h,
                  const DirectX::XMFLOAT4 &color);
    void DrawImage(const Image &image, float x, float y, float scale = 1.0f,
                   float alpha = 1.0f);

  private:
    static constexpr int kButtonCount = 2;

    std::array<Image, kButtonCount> buttonImages_{};
    std::array<Image, kButtonCount> helpImages_{};
    std::array<ButtonRect, kButtonCount> buttons_{};
    int selectedIndex_ = 0;
    float sceneTime_ = 0.0f;
    float transitionTimer_ = 0.0f;
    NextScene nextScene_ = NextScene::None;
};
