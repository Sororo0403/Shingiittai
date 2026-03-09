#pragma once
#include "Model.h"
#include <string>

class TextureManager;
class MeshManager;

class AssimpLoader {
  public:
    /// <summary>
    /// 初期化処理
    /// </summary>
    /// <param name="textureManager">TextureManagerインスタンス</param>
    /// <param name="meshManager">MeshManagerインスタンス</param>
    void Initialize(TextureManager *textureManager, MeshManager *meshManager);

    /// <summary>
    /// モデルをファイルから読み込む
    /// </summary>
    /// <param name="path">読み込むモデルのファイルパス</param>
    /// <returns>モデル構造体</returns>
    Model Load(const std::string &path);

  private:
    TextureManager *textureManager_ = nullptr;
    MeshManager *meshManager_ = nullptr;
};