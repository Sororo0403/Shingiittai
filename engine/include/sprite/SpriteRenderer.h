#pragma once
#include "Sprite.h"
#include <DirectXMath.h>
#include <cstdint>
#include <d3d12.h>
#include <wrl.h>

class DirectXCommon;
class TextureManager;
class SrvManager;

class SpriteRenderer {
  public:
    /// <summary>
    /// 初期化処理
    /// </summary>
    /// <param name="dxCommon">DirectXCommonインスタンス</param>
    /// <param name="textureManager">TextureManagerインスタンス</param>
    /// <param name="srvManager">SrvManagerインスタンス</param>
    /// <param name="width">クライアント領域の幅</param>
    /// <param name="height">クライアント領域の高さ</param>
    void Initialize(DirectXCommon *dxCommon, TextureManager *textureManager,
                    SrvManager *srvManager, int width, int height);

    /// <summary>
    /// 描画処理
    /// </summary>
    /// <param name="sprite">描画するスプライト</param>
    void Draw(const Sprite &sprite);

    /// <summary>
    /// フレーム開始時に一時描画領域を先頭へ戻す
    /// </summary>
    void BeginFrame();

    /// <summary>
    /// 描画前処理
    /// </summary>
    void PreDraw();

    /// <summary>
    /// 描画後処理
    /// </summary>
    void PostDraw();

    /// <summary>
    /// 投影行列を更新する
    /// </summary>
    void UpdateProjection(int width, int height);

  private:
    enum class PipelineKind : uint32_t {
        Alpha = 0,
        Modulate = 1,
        DarkSmoke = 2,
        Count,
    };

    // Create
    void CreateRootSignature();
    void CreatePipelineState();
    void CreateVertexBuffer();
    void CreateConstantBuffer();

  private:
    DirectXCommon *dxCommon_ = nullptr;
    TextureManager *textureManager_ = nullptr;
    SrvManager *srvManager_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState>
        pipelineStates_[static_cast<uint32_t>(PipelineKind::Count)];

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
    D3D12_VERTEX_BUFFER_VIEW vbView_{};
    uint32_t drawCursor_ = 0;
    static constexpr uint32_t kVerticesPerSprite = 6;
    static constexpr uint32_t kMaxSpriteDraws = 4096;

    Microsoft::WRL::ComPtr<ID3D12Resource> constBuffer_;

    DirectX::XMFLOAT4X4 matProjection_{};
    PipelineKind activePipelineKind_ = PipelineKind::Alpha;
};
