#include "ElectricRingEffectRenderer.h"
#include "DirectXCommon.h"
#include "DxHelpers.h"
#include "DxUtils.h"
#include "ShaderCompiler.h"
#include "SrvManager.h"

using namespace DxUtils;
using Microsoft::WRL::ComPtr;

void ElectricRingEffectRenderer::Initialize(DirectXCommon *dxCommon,
                                            SrvManager *srvManager) {
    dxCommon_ = dxCommon;
    srvManager_ = srvManager;

    CreateRootSignature();
    CreateDistortionPipelineState();
    CreatePlasmaPipelineState();
    CreateConstantBuffer();
}

void ElectricRingEffectRenderer::DrawDistortion(
    const ElectricRingParamGPU &param,
    D3D12_GPU_DESCRIPTOR_HANDLE sceneSrvHandle,
    D3D12_GPU_DESCRIPTOR_HANDLE noise0SrvHandle,
    D3D12_GPU_DESCRIPTOR_HANDLE noise1SrvHandle) {

    auto cmd = dxCommon_->GetCommandList();

    ID3D12DescriptorHeap *heaps[] = {srvManager_->GetHeap()};
    cmd->SetDescriptorHeaps(1, heaps);

    mappedCB_->center = param.center;
    mappedCB_->radius = param.radius;
    mappedCB_->time = param.time;

    mappedCB_->ringWidth = param.ringWidth;
    mappedCB_->distortionWidth = param.distortionWidth;
    mappedCB_->distortionStrength = param.distortionStrength;
    mappedCB_->swirlStrength = param.swirlStrength;

    mappedCB_->cloudScale = param.cloudScale;
    mappedCB_->cloudIntensity = param.cloudIntensity;
    mappedCB_->brightness = param.brightness;
    mappedCB_->haloIntensity = param.haloIntensity;

    mappedCB_->aspectInvAspect = param.aspectInvAspect;
    mappedCB_->innerFade = param.innerFade;
    mappedCB_->outerFade = param.outerFade;
    mappedCB_->enabled = param.enabled;

    cmd->SetPipelineState(distortionPSO_.Get());
    cmd->SetGraphicsRootSignature(rootSignature_.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    cmd->SetGraphicsRootConstantBufferView(
        0, constBuffer_->GetGPUVirtualAddress());

    // t0 = scene, t1 = noise0, t2 = noise1
    cmd->SetGraphicsRootDescriptorTable(1, sceneSrvHandle);
    cmd->SetGraphicsRootDescriptorTable(2, noise0SrvHandle);
    cmd->SetGraphicsRootDescriptorTable(3, noise1SrvHandle);

    cmd->DrawInstanced(3, 1, 0, 0);
}

void ElectricRingEffectRenderer::DrawPlasma(
    const ElectricRingParamGPU &param,
    D3D12_GPU_DESCRIPTOR_HANDLE noise0SrvHandle,
    D3D12_GPU_DESCRIPTOR_HANDLE noise1SrvHandle) {

    auto cmd = dxCommon_->GetCommandList();

    ID3D12DescriptorHeap *heaps[] = {srvManager_->GetHeap()};
    cmd->SetDescriptorHeaps(1, heaps);

    mappedCB_->center = param.center;
    mappedCB_->radius = param.radius;
    mappedCB_->time = param.time;

    mappedCB_->ringWidth = param.ringWidth;
    mappedCB_->distortionWidth = param.distortionWidth;
    mappedCB_->distortionStrength = param.distortionStrength;
    mappedCB_->swirlStrength = param.swirlStrength;

    mappedCB_->cloudScale = param.cloudScale;
    mappedCB_->cloudIntensity = param.cloudIntensity;
    mappedCB_->brightness = param.brightness;
    mappedCB_->haloIntensity = param.haloIntensity;

    mappedCB_->aspectInvAspect = param.aspectInvAspect;
    mappedCB_->innerFade = param.innerFade;
    mappedCB_->outerFade = param.outerFade;
    mappedCB_->enabled = param.enabled;

    cmd->SetPipelineState(plasmaPSO_.Get());
    cmd->SetGraphicsRootSignature(rootSignature_.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    cmd->SetGraphicsRootConstantBufferView(
        0, constBuffer_->GetGPUVirtualAddress());

    // Plasma では scene は読まない
    // ただし RootSignature を共通にするため、未使用でも t0
    // を埋めるならダミーを置いても良い
    cmd->SetGraphicsRootDescriptorTable(2, noise0SrvHandle);
    cmd->SetGraphicsRootDescriptorTable(3, noise1SrvHandle);

    cmd->DrawInstanced(3, 1, 0, 0);
}

void ElectricRingEffectRenderer::CreateConstantBuffer() {
    UINT size = Align256(sizeof(ConstantBufferData));

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
    auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(size);

    ThrowIfFailed(dxCommon_->GetDevice()->CreateCommittedResource(
                      &heapProps, D3D12_HEAP_FLAG_NONE, &resourceDesc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&constBuffer_)),
                  "CreateCommittedResource(ElectricRing CB) failed");

    ThrowIfFailed(
        constBuffer_->Map(0, nullptr, reinterpret_cast<void **>(&mappedCB_)),
        "ElectricRing CB Map failed");

    mappedCB_->center = {0.5f, 0.5f};
    mappedCB_->radius = 0.0f;
    mappedCB_->time = 0.0f;

    mappedCB_->ringWidth = 0.015f;
    mappedCB_->distortionWidth = 0.045f;
    mappedCB_->distortionStrength = 0.018f;
    mappedCB_->swirlStrength = 0.006f;

    mappedCB_->cloudScale = 3.5f;
    mappedCB_->cloudIntensity = 1.4f;
    mappedCB_->brightness = 2.4f;
    mappedCB_->haloIntensity = 1.0f;

    mappedCB_->aspectInvAspect = {1.0f, 1.0f};
    mappedCB_->innerFade = 0.85f;
    mappedCB_->outerFade = 1.0f;
    mappedCB_->enabled = 0.0f;
}

void ElectricRingEffectRenderer::CreateRootSignature() {
    CD3DX12_ROOT_PARAMETER params[4]{};

    // b0
    params[0].InitAsConstantBufferView(0);

    // t0 = scene
    CD3DX12_DESCRIPTOR_RANGE range0{};
    range0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    params[1].InitAsDescriptorTable(1, &range0);

    // t1 = noise0
    CD3DX12_DESCRIPTOR_RANGE range1{};
    range1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
    params[2].InitAsDescriptorTable(1, &range1);

    // t2 = noise1
    CD3DX12_DESCRIPTOR_RANGE range2{};
    range2.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);
    params[3].InitAsDescriptorTable(1, &range2);

    CD3DX12_STATIC_SAMPLER_DESC sampler(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);

    CD3DX12_ROOT_SIGNATURE_DESC desc{};
    desc.Init(_countof(params), params, 1, &sampler,
              D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> error;

    ThrowIfFailed(D3D12SerializeRootSignature(
                      &desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error),
                  "D3D12SerializeRootSignature(ElectricRing) failed");

    ThrowIfFailed(dxCommon_->GetDevice()->CreateRootSignature(
                      0, blob->GetBufferPointer(), blob->GetBufferSize(),
                      IID_PPV_ARGS(&rootSignature_)),
                  "CreateRootSignature(ElectricRing) failed");
}

void ElectricRingEffectRenderer::CreateDistortionPipelineState() {
    auto device = dxCommon_->GetDevice();

    auto vs = ShaderCompiler::Compile(
        L"resources/shaders/warp/Fullscreen.VS.hlsl", "main", "vs_5_0");

    auto ps = ShaderCompiler::Compile(
        L"resources/shaders/warp/DistortionPS.hlsl", "main", "ps_5_0");

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

    ThrowIfFailed(
        device->CreateGraphicsPipelineState(&desc,
                                            IID_PPV_ARGS(&distortionPSO_)),
        "CreateGraphicsPipelineState(ElectricRing Distortion) failed");
}

void ElectricRingEffectRenderer::CreatePlasmaPipelineState() {
    auto device = dxCommon_->GetDevice();

    auto vs = ShaderCompiler::Compile(
        L"resources/shaders/warp/Fullscreen.VS.hlsl", "main", "vs_5_0");

    auto ps = ShaderCompiler::Compile(
        L"resources/shaders/warp/PlasmaRingPS.hlsl", "main", "ps_5_0");

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
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    blend.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
    blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
    blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.BlendState = blend;

    D3D12_DEPTH_STENCIL_DESC depth = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    depth.DepthEnable = FALSE;
    depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    desc.DepthStencilState = depth;

    ThrowIfFailed(
        device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&plasmaPSO_)),
        "CreateGraphicsPipelineState(ElectricRing Plasma) failed");
}