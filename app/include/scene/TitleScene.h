#pragma once
#include "BaseScene.h"
#include "GameScene.h"
#include <DirectXMath.h>
#include <cstdint>
#include <memory>
#include <string>

class Input;

/// <summary>
/// タイトル表示、メニュー遷移、終了確認を管理する
/// </summary>
class TitleScene : public BaseScene {
  public:
    /// <summary>
    /// ~TitleSceneに対応する公開処理を実行する
    /// </summary>
    ~TitleScene() override;

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
    void DrawTransparent() override;

  private:
    struct Image {
        uint32_t textureId = 0;
        float width = 0.0f;
        float height = 0.0f;
    };

    Image LoadTitleImage(const std::wstring &path);
    void UpdateExitConfirm(Input &input);
    void DrawTitleOverlay(float screenWidth, float screenHeight);
    void DrawExitConfirmWindow(float screenWidth, float screenHeight);
    void DrawStartupFrame(float screenWidth, float screenHeight);
    void DrawRect(float x, float y, float w, float h,
                  const DirectX::XMFLOAT4 &color);
    void DrawImage(const Image &image, float x, float y, float alpha,
                   float scale);
    bool IsAnyButtonTriggered(const Input &input) const;
    void StartTitleBgm();
    void StopTitleBgm();

    Image logoImage_;
    Image pressAnyButtonImage_;
    Image exitPromptImage_;
    Image exitConfirmMessageImage_;
    Image exitConfirmYesImage_;
    Image exitConfirmNoImage_;
    std::unique_ptr<GameScene> backgroundScene_;
    float sceneTime_ = 0.0f;
    float frameIntroTimer_ = 0.0f;
    float fadeTimer_ = 0.0f;
    uint32_t titleBgmSoundId_ = UINT32_MAX;
    uint32_t titleBgmVoiceHandle_ = UINT32_MAX;
    bool startRequested_ = false;
    bool exitConfirmVisible_ = false;
    int exitConfirmIndex_ = 1;
};
