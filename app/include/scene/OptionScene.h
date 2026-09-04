#pragma once
#include "BaseScene.h"
#include "GameScene.h"
#include <DirectXMath.h>
#include <array>
#include <cstdint>
#include <memory>
#include <string>

class Input;

/// <summary>
/// 音量やカメラなどのゲーム設定を編集する画面を管理する
/// </summary>
class OptionScene : public BaseScene {
  public:
    enum class ReturnTarget {
        WeaponSelect,
        TutorialSelect,
    };

    /// <summary>
    /// OptionSceneに対応する公開処理を実行する
    /// </summary>
    explicit OptionScene(ReturnTarget returnTarget);
    /// <summary>
    /// ~OptionSceneに対応する公開処理を実行する
    /// </summary>
    ~OptionScene() override;

    /// <summary>
    /// 使用するリソースと初期状態を準備する
    /// </summary>
    void Initialize(const SceneContext &ctx) override;
    /// <summary>
    /// 入力と状態を1フレーム進める
    /// </summary>
    void Update() override;
    /// <summary>
    /// 現在の状態を描画する
    /// </summary>
    void Draw() override;
    /// <summary>
    /// 透明描画パスへ必要な要素を描画する
    /// </summary>
    void DrawTransparent() override {}

  private:
    struct Image {
        uint32_t textureId = 0;
        float width = 0.0f;
        float height = 0.0f;
    };
    struct PanelRowLayout {
        float intro = 0.0f;
        float rowX = 0.0f;
        float barX = 0.0f;
        float barWidth = 0.0f;
        float barHeight = 0.0f;
        float labelAreaWidth = 0.0f;
    };

    Image LoadTextureImage(const std::wstring &path);
    void BeginReturn();
    void AdjustSelectedOption(int direction);
    bool UpdateNavigationInput(Input &input);
    void UpdateAdjustmentInput(Input &input);
    void ResetAdjustmentRepeat();
    int GetAdjustmentTriggerDirection(Input &input) const;
    int GetHeldAdjustmentDirection(Input &input) const;
    void BeginAdjustment(int direction);
    void DrawOverlay(float screenWidth, float screenHeight);
    void DrawPanel(float screenWidth, float screenHeight);
    void DrawPanelRow(int index, const Image &label, float value,
                      float rowCenterY, const PanelRowLayout &layout);
    void DrawControlsPrompt(float screenWidth, float screenHeight);
    void DrawTransition(float screenWidth, float screenHeight);
    void DrawRect(float x, float y, float w, float h,
                  const DirectX::XMFLOAT4 &color);
    void DrawFrame(float x, float y, float w, float h, float thickness,
                   const DirectX::XMFLOAT4 &color);
    void DrawImage(const Image &image, float x, float y, float scale = 1.0f,
                   float alpha = 1.0f);
    std::unique_ptr<BaseScene> CreateReturnScene() const;

    static constexpr int kOptionCount = 8;

    ReturnTarget returnTarget_ = ReturnTarget::WeaponSelect;
    std::unique_ptr<GameScene> backgroundScene_;
    Image titleImage_{};
    Image displayLabelImage_{};
    Image bgmLabelImage_{};
    Image seLabelImage_{};
    Image cameraSensitivityLabelImage_{};
    Image cameraSlashSensitivityLabelImage_{};
    Image cameraVerticalSensitivityLabelImage_{};
    Image cameraHorizontalSensitivityLabelImage_{};
    Image mouseSlashLabelImage_{};
    Image fullscreenValueImage_{};
    Image windowValueImage_{};
    Image keyAImage_{};
    Image keyDImage_{};
    Image tabBackPromptImage_{};
    int selectedIndex_ = 0;
    int adjustHoldDirection_ = 0;
    float adjustHoldTimer_ = 0.0f;
    float adjustRepeatTimer_ = 0.0f;
    float introTimer_ = 0.0f;
    float transitionTimer_ = 0.0f;
    bool returnRequested_ = false;
    bool preserveMenuBgmOnExit_ = false;
};
