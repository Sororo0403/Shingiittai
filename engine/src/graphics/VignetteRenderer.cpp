#include "VignetteRenderer.h"
#include "DirectXCommon.h"
#include "DxHelpers.h"
#include "DxUtils.h"
#include "ShaderCompiler.h"

using namespace DxUtils;

void VignetteRenderer::Initialize(DirectXCommon *dxCommon) {
    dxCommon_ = dxCommon;

    CreateRootSignature();
    CreatePipelineState();
    CreateConstantBuffer();
}

void VignetteRenderer::Draw(const VignetteParams &params) {
    if (dxCommon_ == nullptr || params.intensity <= 0.001f) {
        return;
    }

    UpdateConstants(params);

    ID3D12GraphicsCommandList *cmd = dxCommon_->GetCommandList();
    cmd->SetPipelineState(pipelineState_.Get());
    cmd->SetGraphicsRootSignature(rootSignature_.Get());
    cmd->SetGraphicsRootConstantBufferView(0, constBuffer_->GetGPUVirtualAddress());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->DrawInstanced(3, 1, 0, 0);
}

void VignetteRenderer::CreateRootSignature() {
    CD3DX12_ROOT_PARAMETER params[1]{};
    params[0].InitAsConstantBufferView(0);

    CD3DX12_ROOT_SIGNATURE_DESC desc{};
    desc.Init(_countof(params), params, 0, nullptr,
              D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    Microsoft::WRL::ComPtr<ID3DBlob> blob;
    Microsoft::WRL::ComPtr<ID3DBlob> error;
    ThrowIfFailed(D3D12SerializeRootSignature(
                      &desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error),
                  "SerializeRootSignature failed");

    ThrowIfFailed(dxCommon_->GetDevice()->CreateRootSignature(
                      0, blob->GetBufferPointer(), blob->GetBufferSize(),
                      IID_PPV_ARGS(&rootSignature_)),
                  "CreateRootSignature failed");
}

void VignetteRenderer::CreatePipelineState() {
    auto vs = ShaderCompiler::Compile(
        L"engine/resources/shaders/postprocess/VignetteVS.hlsl", "main", "vs_5_0");
    auto ps = ShaderCompiler::Compile(
        L"engine/resources/shaders/postprocess/VignettePS.hlsl", "main", "ps_5_0");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = rootSignature_.Get();
    desc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    desc.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    desc.SampleDesc.Count = 1;
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

    D3D12_DEPTH_STENCIL_DESC depth =
        CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    depth.DepthEnable = FALSE;
    depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    desc.DepthStencilState = depth;

    D3D12_BLEND_DESC blend = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    auto &rt = blend.RenderTarget[0];
    rt.BlendEnable = TRUE;
    rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    rt.BlendOp = D3D12_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D12_BLEND_ONE;
    rt.DestBlendAlpha = D3D12_BLEND_ZERO;
    rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.BlendState = blend;

    ThrowIfFailed(dxCommon_->GetDevice()->CreateGraphicsPipelineState(
                      &desc, IID_PPV_ARGS(&pipelineState_)),
                  "CreateGraphicsPipelineState failed");
}

void VignetteRenderer::CreateConstantBuffer() {
    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
    auto desc = CD3DX12_RESOURCE_DESC::Buffer(Align256(sizeof(VignetteConstBuffer)));

    ThrowIfFailed(dxCommon_->GetDevice()->CreateCommittedResource(
                      &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&constBuffer_)),
                  "CreateCommittedResource failed");
}

void VignetteRenderer::UpdateConstants(const VignetteParams &params) {
    VignetteConstBuffer *mapped = nullptr;
    constBuffer_->Map(0, nullptr, reinterpret_cast<void **>(&mapped));
    mapped->colorIntensity =
        {params.color.x, params.color.y, params.color.z, params.intensity};
    mapped->settings = {params.innerRadius, params.power, params.roundness, 0.0f};
    constBuffer_->Unmap(0, nullptr);
}
