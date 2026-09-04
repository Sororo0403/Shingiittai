#pragma once
#include "BaseScene.h"
#include "CameraPreviewReceiver.h"
#include "GameScene.h"
#include "SwordInputCalibration.h"
#include "SwordUdpController.h"
#include <DirectXMath.h>
#include <array>
#include <cstdint>
#include <memory>
#include <string>

/// <summary>
/// 戦闘操作を段階的に練習するチュートリアル進行を管理する
/// </summary>
class TutorialScene : public BaseScene {
  public:
    /// <summary>
    /// TutorialSceneに対応する公開処理を実行する
    /// </summary>
    explicit TutorialScene(const SwordInputCalibration &inputCalibration);
    /// <summary>
    /// ~TutorialSceneに対応する公開処理を実行する
    /// </summary>
    ~TutorialScene() override;

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

    Image LoadTextureImage(const std::wstring &path);
    bool IsHandTutorial() const;
    bool RequestHandTrackingStartOnce();
    void DrawRect(float x, float y, float w, float h,
                  const DirectX::XMFLOAT4 &color);
    void DrawImage(const Image &image, float x, float y, float scale = 1.0f,
                   float alpha = 1.0f);

    SwordInputCalibration inputCalibration_{};
    std::unique_ptr<GameScene> backgroundScene_;
    SwordUdpController handController_;
    CameraPreviewReceiver previewReceiver_{};
    Image titleImage_{};
    std::array<Image, 2> bodyImages_{};
    Image promptImage_{};
    float sceneTime_ = 0.0f;
    float transitionTimer_ = 0.0f;
    bool returnRequested_ = false;
    bool startGameRequested_ = false;
    bool handTrackingStartRequested_ = false;
};
