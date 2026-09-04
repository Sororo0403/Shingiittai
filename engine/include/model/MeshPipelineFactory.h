#pragma once

#include "model/Material.h"

#include <array>
#include <cstddef>
#include <d3d12.h>
#include <string>
#include <wrl.h>

/// <summary>メッシュ描画時のブレンド方式</summary>
enum class MeshBlendMode {
    Opaque,
    Alpha,
    Additive,
};

/// <summary>メッシュ描画時の深度処理方式</summary>
enum class MeshDepthMode {
    TestWrite,
    TestOnly,
    None,
};

/// <summary>メッシュ描画時のカリング方式</summary>
enum class MeshCullMode {
    Back,
    Front,
    None,
};

/// <summary>生成するパイプライン派生の構成</summary>
enum class MeshPipelineVariantMode {
    MaterialDriven,
    Fixed,
};

/// <summary>メッシュパイプライン生成に必要な状態を指定する</summary>
struct MeshPipelineDesc {
    std::wstring vertexShader;
    std::wstring pixelShader;
    MeshBlendMode blend = MeshBlendMode::Opaque;
    MeshDepthMode depth = MeshDepthMode::TestWrite;
    MeshCullMode cull = MeshCullMode::Back;
    bool instanced = false;
    MeshPipelineVariantMode variantMode =
        MeshPipelineVariantMode::MaterialDriven;
};

static constexpr size_t kMeshPipelineVariantCount = 12;

using MeshPipelineStateArray =
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>,
               kMeshPipelineVariantCount>;

/// <summary>通常描画と影描画用のパイプラインをまとめて保持する</summary>
struct MeshPipelineSet {
    MeshPipelineStateArray pipelineStates;
};

/// <summary>指定された描画状態からメッシュ用パイプラインを生成する</summary>
class MeshPipelineFactory {
  public:
    static MeshPipelineSet CreatePipelineSet(
        ID3D12Device *device, ID3D12RootSignature *rootSignature,
        const MeshPipelineDesc &desc, D3D12_INPUT_LAYOUT_DESC inputLayout,
        DXGI_FORMAT renderTargetFormat, DXGI_FORMAT depthStencilFormat);
};
