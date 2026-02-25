#pragma once
#include "Model.h"
#include <cstdint>
#include <string>
#include <vector>
#include "MeshManager.h"
#include "ModelRenderer.h"
#include "ObjLoader.h"
#include "TextureManager.h"

class DirectXCommon;
class SrvManager;
class Camera;

class ModelManager {
  public:
    /// <summary>
    /// 初期化処理
    /// </summary>
    /// <param name="dxCommon">DirectXCommonインスタンス</param>
    /// <param name="srvManager">SrvManagerインスタンス</param>
    void Initialize(DirectXCommon *dxCommon, SrvManager *srvManager);

    /// <summary>
    /// OBJファイルからモデルをロードする
    /// </summary>
    /// <param name="path">OBJファイルパス</param>
    /// <returns>モデルID</returns>
    uint32_t Load(const std::wstring &path);

    /// <summary>
    /// モデルを描画する
    /// </summary>
    /// <param name="modelId">描画するモデルのモデルID</param>
    /// <param name="camera">描画に使用するカメラ</param>
    void Draw(uint32_t modelId, const Camera &camera);

    /// <summary>
    /// 描画前処理
    /// </summary>
    void PreDraw();

    /// <summary>
    /// 描画後処理
    /// </summary>
    void PostDraw();

  private:
    DirectXCommon *dxCommon_ = nullptr;

    MeshManager meshManager_;
    TextureManager textureManager_;
    ObjLoader objLoader_;
    ModelRenderer modelRenderer_;

    std::vector<Model> models_;
};
