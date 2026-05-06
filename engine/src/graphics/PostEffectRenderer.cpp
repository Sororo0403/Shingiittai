#include "PostEffectRenderer.h"
#include "DirectXCommon.h"
#include "DxHelpers.h"
#include "DxUtils.h"
#include "ShaderCompiler.h"
#include "SrvManager.h"
#include <algorithm>

using namespace DxUtils;

void PostEffectRenderer::Initialize(DirectXCommon *dxCommon,
                                    SrvManager *srvManager, int width,
                                    int height) {
    dxCommon_ = dxCommon;
    srvManager_ = srvManager;

    CreateRootSignature();
    CreatePipelineState();
    CreateConstantBuffer();
    Resize(width, height);
}

void PostEffectRenderer::Resize(int width, int height) {
    width_ = width > 0 ? width : 1;
    height_ = height > 0 ? height : 1;

    viewport_.TopLeftX = 0.0f;
    viewport_.TopLeftY = 0.0f;
    viewport_.Width = static_cast<float>(width_);
    viewport_.Height = static_cast<float>(height_);
    viewport_.MinDepth = 0.0f;
    viewport_.MaxDepth = 1.0f;

    scissorRect_.left = 0;
    scissorRect_.top = 0;
    scissorRect_.right = width_;
    scissorRect_.bottom = height_;

    UpdateConstantBuffer();
}

void PostEffectRenderer::Draw(D3D12_GPU_DESCRIPTOR_HANDLE textureHandle,
                              D3D12_GPU_DESCRIPTOR_HANDLE depthHandle) {
    auto commandList = dxCommon_->GetCommandList();

    ID3D12DescriptorHeap *heaps[] = {srvManager_->GetHeap()};
    commandList->SetDescriptorHeaps(1, heaps);

    commandList->RSSetViewports(1, &viewport_);
    commandList->RSSetScissorRects(1, &scissorRect_);
    commandList->SetPipelineState(pipelineState_.Get());
    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetGraphicsRootDescriptorTable(0, textureHandle);
    commandList->SetGraphicsRootDescriptorTable(1, depthHandle);
    commandList->SetGraphicsRootConstantBufferView(
        2, constBuffer_->GetGPUVirtualAddress());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->DrawInstanced(3, 1, 0, 0);
}

void PostEffectRenderer::SetColorMode(ColorMode mode) {
    colorMode_ = mode;
    UpdateConstantBuffer();
}

void PostEffectRenderer::SetFilterMode(FilterMode mode) {
    filterMode_ = mode;
    UpdateConstantBuffer();
}

void PostEffectRenderer::SetEdgeMode(EdgeMode mode) {
    edgeMode_ = mode;
    UpdateConstantBuffer();
}

void PostEffectRenderer::SetLuminanceEdgeThreshold(float threshold) {
    luminanceEdgeThreshold_ = threshold;
    UpdateConstantBuffer();
}

void PostEffectRenderer::SetDepthEdgeThreshold(float threshold) {
    depthEdgeThreshold_ = threshold;
    UpdateConstantBuffer();
}

void PostEffectRenderer::SetDepthParameters(float nearZ, float farZ) {
    nearZ_ = nearZ;
    farZ_ = farZ;
    UpdateConstantBuffer();
}

void PostEffectRenderer::SetVignettingEnabled(bool enabled) {
    enableVignetting_ = enabled;
    UpdateConstantBuffer();
}

void PostEffectRenderer::SetRadialBlurCenter(float x, float y) {
    radialBlurCenter_[0] = std::clamp(x, 0.0f, 1.0f);
    radialBlurCenter_[1] = std::clamp(y, 0.0f, 1.0f);
    UpdateConstantBuffer();
}

void PostEffectRenderer::SetRadialBlurStrength(float strength) {
    radialBlurStrength_ = std::clamp(strength, 0.0f, 1.0f);
    UpdateConstantBuffer();
}

void PostEffectRenderer::SetRadialBlurSampleCount(int32_t sampleCount) {
    radialBlurSampleCount_ = std::clamp(sampleCount, 2, 32);
    UpdateConstantBuffer();
}

void PostEffectRenderer::SetRandomMode(RandomMode mode) {
    randomMode_ = mode;
    UpdateConstantBuffer();
}

void PostEffectRenderer::SetRandomStrength(float strength) {
    randomStrength_ = std::clamp(strength, 0.0f, 1.0f);
    UpdateConstantBuffer();
}

void PostEffectRenderer::SetRandomScale(float scale) {
    randomScale_ = std::clamp(scale, 1.0f, 4096.0f);
    UpdateConstantBuffer();
}

void PostEffectRenderer::SetRandomTime(float time) {
    randomTime_ = time;
    UpdateConstantBuffer();
}

void PostEffectRenderer::CreateRootSignature() {
    CD3DX12_DESCRIPTOR_RANGE textureRange{};
    textureRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    CD3DX12_DESCRIPTOR_RANGE depthRange{};
    depthRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);

    CD3DX12_ROOT_PARAMETER params[3]{};
    params[0].InitAsDescriptorTable(1, &textureRange,
                                    D3D12_SHADER_VISIBILITY_PIXEL);
    params[1].InitAsDescriptorTable(1, &depthRange,
                                    D3D12_SHADER_VISIBILITY_PIXEL);
    params[2].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    CD3DX12_STATIC_SAMPLER_DESC sampler{};
    sampler.Init(0);
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;

    CD3DX12_ROOT_SIGNATURE_DESC desc{};
    desc.Init(_countof(params), params, 1, &sampler,
              D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    Microsoft::WRL::ComPtr<ID3DBlob> blob;
    Microsoft::WRL::ComPtr<ID3DBlob> error;
    ThrowIfFailed(D3D12SerializeRootSignature(
                      &desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error),
                  "Serialize PostEffect root signature failed");

    ThrowIfFailed(dxCommon_->GetDevice()->CreateRootSignature(
                      0, blob->GetBufferPointer(), blob->GetBufferSize(),
                      IID_PPV_ARGS(&rootSignature_)),
                  "Create PostEffect root signature failed");
}

void PostEffectRenderer::CreatePipelineState() {
    auto vs = ShaderCompiler::Compile(
        L"engine/resources/shaders/posteffect/PostEffectVS.hlsl", "main",
        "vs_5_0");
    auto ps = ShaderCompiler::Compile(
        L"engine/resources/shaders/posteffect/PostEffectPS.hlsl", "main",
        "ps_5_0");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = rootSignature_.Get();
    desc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    desc.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    desc.SampleDesc.Count = 1;
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

    D3D12_DEPTH_STENCIL_DESC depth =
        CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    depth.DepthEnable = FALSE;
    depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    desc.DepthStencilState = depth;
    desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

    ThrowIfFailed(dxCommon_->GetDevice()->CreateGraphicsPipelineState(
                      &desc, IID_PPV_ARGS(&pipelineState_)),
                  "Create PostEffect pipeline state failed");
}

void PostEffectRenderer::CreateConstantBuffer() {
    const UINT size = Align256(sizeof(EffectConstBuffer));

    CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);
    auto desc = CD3DX12_RESOURCE_DESC::Buffer(size);

    ThrowIfFailed(dxCommon_->GetDevice()->CreateCommittedResource(
                      &heap, D3D12_HEAP_FLAG_NONE, &desc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&constBuffer_)),
                  "Create PostEffect constant buffer failed");

    ThrowIfFailed(constBuffer_->Map(
                      0, nullptr,
                      reinterpret_cast<void **>(&mappedConstBuffer_)),
                  "Map PostEffect constant buffer failed");

    UpdateConstantBuffer();
}

void PostEffectRenderer::UpdateConstantBuffer() {
    if (!mappedConstBuffer_) {
        return;
    }

    mappedConstBuffer_->colorMode = static_cast<int32_t>(colorMode_);
    mappedConstBuffer_->enableVignetting = enableVignetting_ ? 1 : 0;
    mappedConstBuffer_->filterMode = static_cast<int32_t>(filterMode_);
    mappedConstBuffer_->texelSize[0] = 1.0f / static_cast<float>(width_);
    mappedConstBuffer_->texelSize[1] = 1.0f / static_cast<float>(height_);
    mappedConstBuffer_->edgeMode = static_cast<int32_t>(edgeMode_);
    mappedConstBuffer_->luminanceEdgeThreshold = luminanceEdgeThreshold_;
    mappedConstBuffer_->depthEdgeThreshold = depthEdgeThreshold_;
    mappedConstBuffer_->nearZ = nearZ_;
    mappedConstBuffer_->farZ = farZ_;
    mappedConstBuffer_->radialBlurCenter[0] = radialBlurCenter_[0];
    mappedConstBuffer_->radialBlurCenter[1] = radialBlurCenter_[1];
    mappedConstBuffer_->radialBlurStrength = radialBlurStrength_;
    mappedConstBuffer_->radialBlurSampleCount = radialBlurSampleCount_;
    mappedConstBuffer_->randomMode = static_cast<int32_t>(randomMode_);
    mappedConstBuffer_->randomStrength = randomStrength_;
    mappedConstBuffer_->randomScale = randomScale_;
    mappedConstBuffer_->randomTime = randomTime_;
}
