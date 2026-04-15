#pragma once
#include "Camera.h"
#include "MaterialManager.h"
#include "Model.h"
#include "Transform.h"
#include <DirectXMath.h>
#include <d3d12.h>
#include <wrl.h>

class DirectXCommon;
class SrvManager;
class MeshManager;
class TextureManager;

struct ModelDrawEffect {
    bool enabled = false;
    bool additiveBlend = false;
    DirectX::XMFLOAT4 color = {1.0f, 0.2f, 0.7f, 0.65f};
    float intensity = 0.0f;
    float fresnelPower = 3.5f;
    float noiseAmount = 0.0f;
    float time = 0.0f;
};

class ModelRenderer {
  public:
    /// <summary>
    /// 初期化処理
    /// </summary>
    /// <param name="dxCommon"></param>
    /// <param name="srvManager"></param>
    /// <param name="meshManager"></param>
    /// <param name="textureManager"></param>
    /// <param name="materialManager"></param>
    void Initialize(DirectXCommon *dxCommon, SrvManager *srvManager,
                    MeshManager *meshManager, TextureManager *textureManager,
                    MaterialManager *materialManager);

    /// <summary>
    /// 描画処理
    /// </summary>
    /// <param name="model">描画するモデル</param>
    /// <param name="transform">描画するモデルのTransform</param>
    /// <param name="camera">描画に使用するカメラ</param>
    void Draw(const Model &model, const Transform &transform,
              const Camera &camera);

    void SetDrawEffect(const ModelDrawEffect &effect) { currentEffect_ = effect; }
    void ClearDrawEffect() { currentEffect_ = ModelDrawEffect{}; }
    void CreateSkinClusters(Model &model);
    void UpdateSkinClusters(Model &model);

    /// <summary>
    /// 描画前処理
    /// </summary>
    void PreDraw();

    /// <summary>
    /// 描画後処理
    /// </summary>
    void PostDraw();

  private:
    // Create
    void CreateRootSignature();
    void CreatePipelineState();
    void CreateConstantBuffer();

  private:
    static constexpr uint32_t kMaxDraws = 4096;

    DirectXCommon *dxCommon_ = nullptr;
    SrvManager *srvManager_ = nullptr;
    MeshManager *meshManager_ = nullptr;
    TextureManager *textureManager_ = nullptr;
    MaterialManager *materialManager_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> opaquePSO_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> transparentPSO_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> additivePSO_;
    Microsoft::WRL::ComPtr<ID3D12Resource> constBuffer_;

    uint32_t drawIndex_ = 0;
    uint32_t cbStride_ = 0;
    uint8_t *mappedCB_ = nullptr;
    ModelDrawEffect currentEffect_{};
};
