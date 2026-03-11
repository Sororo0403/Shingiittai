#pragma once
#include "Animator.h"
#include "AssimpLoader.h"
#include "MeshManager.h"
#include "Model.h"
#include "ModelRenderer.h"
#include <cstdint>
#include <string>
#include <vector>

class DirectXCommon;
class TextureManager;
class SrvManager;
class Camera;

class ModelManager {
  public:
    /// <summary>
    /// 初期化処理
    /// </summary>
    /// <param name="dxCommon">DirectXCommonインスタンス</param>
    /// <param name="srvManager">SrvManagerインスタンス</param>
    /// <param name="textureManager">TextureManagerインスタンス</param>
    void Initialize(DirectXCommon *dxCommon, SrvManager *srvManager,
                    TextureManager *textureManager);

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
    /// <param name="transform">描画するモデルのトランスフォーム</param>
    /// <param name="camera">描画に使用するカメラ</param>
    void Draw(uint32_t modelId, const Transform &transform,
              const Camera &camera);

    /// <summary>
    /// 描画前処理
    /// </summary>
    void PreDraw();

    /// <summary>
    /// 描画後処理
    /// </summary>
    void PostDraw();

    /// <summary>
    /// アニメーションの更新
    /// </summary>
    /// <param name="modelId">アニメーションを更新するモデルのID</param>
    /// <param name="deltaTime">前フレームからの経過時間(秒)</param>
    void UpdateAnimation(uint32_t modelId, float deltaTime);

  private:
    DirectXCommon *dxCommon_ = nullptr;
    TextureManager *textureManager_ = nullptr;

    MeshManager meshManager_;
    AssimpLoader assimpLoader_;
    ModelRenderer modelRenderer_;
    Animator animator_;

    std::vector<Model> models_;
};
