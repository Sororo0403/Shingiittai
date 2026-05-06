#pragma once
#include "SceneContext.h"
#include <memory>

class BaseScene;

/// <summary>
/// シーンの切り替えと更新を管理する
/// </summary>
class SceneManager {
  public:
    /// <summary>
    /// 初期化処理
    /// </summary>
    /// <param name="ctx">シーンコンテキスト</param>
    void Initialize(const SceneContext &ctx);

    /// <summary>
    /// シーンを変更する
    /// </summary>
    /// <param name="nextScene">変更後のシーン</param>
    void ChangeScene(std::unique_ptr<BaseScene> nextScene);

    /// <summary>
    /// 更新処理
    /// </summary>
    void Update();

    /// <summary>
    /// 描画処理
    /// </summary>
    void Draw();
    void DrawOverlay();

  private:
    void ApplySceneChange(std::unique_ptr<BaseScene> nextScene);

  private:
    std::unique_ptr<BaseScene> currentScene_;
    std::unique_ptr<BaseScene> pendingScene_;
    const SceneContext *ctx_ = nullptr;
    bool isUpdating_ = false;
    bool isDrawing_ = false;
};
