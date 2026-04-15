#include "WarpPostEffectRenderer.h"
#include "DirectXCommon.h"
#include "DxHelpers.h"
#include "DxUtils.h"
#include "ShaderCompiler.h"
#include "SrvManager.h"

using namespace DxUtils;
using Microsoft::WRL::ComPtr;

void WarpPostEffectRenderer::Initialize(DirectXCommon *dxCommon,
                                        SrvManager *srvManager) {
    dxCommon_ = dxCommon;
    srvManager_ = srvManager;

    CreateRootSignature();
    CreatePipelineState();
    CreateConstantBuffer();
}

void WarpPostEffectRenderer::Draw(const WarpPostEffectParamGPU &param,
                                  D3D12_GPU_DESCRIPTOR_HANDLE sceneSrvHandle) {

    auto cmd = dxCommon_->GetCommandList();

    ID3D12DescriptorHeap *heaps[] = {srvManager_->GetHeap()};
    cmd->SetDescriptorHeaps(1, heaps);

    mappedCB_->center = param.center;
    mappedCB_->radius = param.radius;
    mappedCB_->strength = param.strength;
    mappedCB_->time = param.time;

    mappedCB_->center2 = param.center2;
    mappedCB_->radius2 = param.radius2;
    mappedCB_->strength2 = param.strength2;
    mappedCB_->enabled = param.enabled;

    cmd->SetPipelineState(pipelineState_.Get());
    cmd->SetGraphicsRootSignature(rootSignature_.Get());

    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    cmd->SetGraphicsRootConstantBufferView(
        0, constBuffer_->GetGPUVirtualAddress());
    cmd->SetGraphicsRootDescriptorTable(1, sceneSrvHandle);

    cmd->DrawInstanced(3, 1, 0, 0);
}

void WarpPostEffectRenderer::CreateConstantBuffer() {
    UINT size = Align256(sizeof(ConstantBufferData));

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
    auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(size);

    ThrowIfFailed(dxCommon_->GetDevice()->CreateCommittedResource(
                      &heapProps, D3D12_HEAP_FLAG_NONE, &resourceDesc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&constBuffer_)),
                  "CreateCommittedResource(WarpPost CB) failed");

    ThrowIfFailed(
        constBuffer_->Map(0, nullptr, reinterpret_cast<void **>(&mappedCB_)),
        "WarpPost CB Map failed");

    mappedCB_->center = {0.5f, 0.5f};
    mappedCB_->radius = 0.0f;
    mappedCB_->strength = 0.0f;
    mappedCB_->time = 0.0f;
    mappedCB_->center2 = {0.5f, 0.5f};
    mappedCB_->radius2 = 0.0f;
    mappedCB_->strength2 = 0.0f;
    mappedCB_->enabled = 0.0f;
}

void WarpPostEffectRenderer::CreateRootSignature() {
    CD3DX12_ROOT_PARAMETER params[2]{};

    params[0].InitAsConstantBufferView(0);

    CD3DX12_DESCRIPTOR_RANGE range{};
    range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    params[1].InitAsDescriptorTable(1, &range);

    CD3DX12_STATIC_SAMPLER_DESC sampler(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);

    CD3DX12_ROOT_SIGNATURE_DESC desc{};
    desc.Init(_countof(params), params, 1, &sampler,
              D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> error;

    ThrowIfFailed(D3D12SerializeRootSignature(
                      &desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error),
                  "D3D12SerializeRootSignature(WarpPost) failed");

    ThrowIfFailed(dxCommon_->GetDevice()->CreateRootSignature(
                      0, blob->GetBufferPointer(), blob->GetBufferSize(),
                      IID_PPV_ARGS(&rootSignature_)),
                  "CreateRootSignature(WarpPost) failed");
}

void WarpPostEffectRenderer::CreatePipelineState() {
    auto device = dxCommon_->GetDevice();

    auto vs = ShaderCompiler::Compile(
        L"resources/shaders/warp/Fullscreen.VS.hlsl", "main", "vs_5_0");

    auto ps = ShaderCompiler::Compile(
        L"resources/shaders/warp/WarpPostEffect.PS.hlsl", "main", "ps_5_0");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = rootSignature_.Get();
    desc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    desc.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    desc.InputLayout = {nullptr, 0};
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

    D3D12_BLEND_DESC blend = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    blend.RenderTarget[0].BlendEnable = FALSE;
    desc.BlendState = blend;

    D3D12_DEPTH_STENCIL_DESC depth = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    depth.DepthEnable = FALSE;
    depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    desc.DepthStencilState = depth;

    ThrowIfFailed(device->CreateGraphicsPipelineState(
                      &desc, IID_PPV_ARGS(&pipelineState_)),
                  "CreateGraphicsPipelineState(WarpPost) failed");
}