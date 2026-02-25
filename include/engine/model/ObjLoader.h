#pragma once
#include "Model.h"
#include <string>

class TextureManager;
class MeshManager;

struct ObjIndex {
    uint32_t pos;
    uint32_t uv;
};

class ObjLoader {
  public:
    /// <summary>
    /// 初期化処理
    /// </summary>
    /// <param name="textureManager">TextureManagerインスタンス</param>
    /// <param name="meshManager">MeshManagerインスタンス</param>
    void Initialize(TextureManager *textureManager, MeshManager *meshManager);

    /// <summary>
    /// モデルをOBJファイルから読み込む
    /// </summary>
    /// <param name="path">読み込むOBJのファイルパス</param>
    /// <returns>Model</returns>
    Model Load(const std::wstring &path);

  private:
    // キャッシュ
    ObjIndex ParseFaceToken(const std::string &token);

  private:
    TextureManager *textureManager_ = nullptr;
    MeshManager *meshManager_ = nullptr;
};
