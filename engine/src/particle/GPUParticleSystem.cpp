#include "particle/GPUParticleSystem.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/DxUtils.h"
#include "graphics/ShaderCompiler.h"
#include "graphics/ShaderPaths.h"
#include "graphics/SrvManager.h"
#include "texture/TextureManager.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <random>
#include <stdexcept>

using namespace DirectX;
using namespace DxUtils;
using Microsoft::WRL::ComPtr;

namespace {

constexpr uint32_t kParticleThreadCount = 256u;
constexpr uint32_t kMaxParticleArgsJobs = 16u;
constexpr uint32_t kParticleArgsDescriptorFrameCount = 3u;
constexpr uint32_t kMaxParticleBatchJobs = 16u;
constexpr uint32_t kMaxParticleBatchItems = 16384u;
constexpr uint32_t kParticleBatchDescriptorFrameCount = 3u;

struct ParticleBatchEmitterForGPU {
    XMFLOAT4 position{};
    XMFLOAT4 spawnOffsetScale{};
    XMFLOAT4 spawnShapeParams{};
    XMFLOAT4 basisRight{};
    XMFLOAT4 basisUp{};
    XMFLOAT4 basisForward{};
    XMFLOAT4 directionAndDirectionalVelocity{};
    XMFLOAT4 velocityBiasAndRadialVelocity{};
    XMFLOAT4 lifeAndFade{};
    XMFLOAT4 scale{};
    XMFLOAT4 accelerationAndTurbulence{};
    XMFLOAT4 motion{};
    XMFLOAT4 atlasAndRotation{};
    XMFLOAT4 tintColor{};
    XMUINT4 config{};
};

struct ParticleBatchJobForGPU {
    ParticleBatchEmitterForGPU emitter{};
    XMFLOAT4 time{};
    uint32_t particleCount = 0;
    uint32_t updateStart = 0;
    uint32_t emitStart = 0;
    uint32_t emitCount = 0;
};

ID3D12Device *gCachedParticleDrawDevice = nullptr;
ComPtr<ID3D12RootSignature> gCachedParticleDrawRootSignature;
ComPtr<ID3D12CommandSignature> gCachedParticleDrawCommandSignature;
std::map<std::wstring, ComPtr<ID3D12PipelineState>> gParticleDrawPsoCache;
ID3D12Device *gCachedParticleArgsDevice = nullptr;
SrvManager *gCachedParticleArgsSrvManager = nullptr;
uint32_t gParticleArgsCountUavBase[kParticleArgsDescriptorFrameCount] = {};
uint32_t gParticleArgsDrawUavBase[kParticleArgsDescriptorFrameCount] = {};
bool gParticleArgsDescriptorsInitialized = false;
ID3D12Device *gCachedParticleBatchDevice = nullptr;
SrvManager *gCachedParticleBatchSrvManager = nullptr;
ComPtr<ID3D12RootSignature> gParticleBatchUpdateRootSignature;
ComPtr<ID3D12PipelineState> gParticleBatchUpdatePSO;
ComPtr<ID3D12RootSignature> gParticleBatchDrawRootSignature;
std::map<std::wstring, ComPtr<ID3D12PipelineState>> gParticleBatchDrawPsoCache;
ComPtr<ID3D12Resource> gParticleBatchJobsResource[kParticleBatchDescriptorFrameCount];
ParticleBatchJobForGPU
    *gMappedParticleBatchJobs[kParticleBatchDescriptorFrameCount] = {};
UINT gParticleBatchJobSrvIndex[kParticleBatchDescriptorFrameCount] = {};
UINT gParticleBatchParticleSrvBase[kParticleBatchDescriptorFrameCount] = {};
UINT gParticleBatchParticleUavBase[kParticleBatchDescriptorFrameCount] = {};
UINT gParticleBatchFreeListUavBase[kParticleBatchDescriptorFrameCount] = {};
UINT gParticleBatchFreeListIndexUavBase[kParticleBatchDescriptorFrameCount] =
    {};
ComPtr<ID3D12Resource> gParticleBatchActiveItemsResource;
D3D12_CPU_DESCRIPTOR_HANDLE gParticleBatchActiveItemsSrvCpuHandle{};
D3D12_GPU_DESCRIPTOR_HANDLE gParticleBatchActiveItemsSrvGpuHandle{};
UINT gParticleBatchActiveItemsSrvIndex = UINT_MAX;
D3D12_CPU_DESCRIPTOR_HANDLE gParticleBatchActiveItemsUavCpuHandle{};
D3D12_GPU_DESCRIPTOR_HANDLE gParticleBatchActiveItemsUavGpuHandle{};
UINT gParticleBatchActiveItemsUavIndex = UINT_MAX;
D3D12_RESOURCE_STATES gParticleBatchActiveItemsState =
    D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
ComPtr<ID3D12Resource> gParticleBatchActiveCountResource;
D3D12_CPU_DESCRIPTOR_HANDLE gParticleBatchActiveCountUavCpuHandle{};
D3D12_GPU_DESCRIPTOR_HANDLE gParticleBatchActiveCountUavGpuHandle{};
UINT gParticleBatchActiveCountUavIndex = UINT_MAX;
ComPtr<ID3D12Resource> gParticleBatchDrawArgsResource;
D3D12_CPU_DESCRIPTOR_HANDLE gParticleBatchDrawArgsUavCpuHandle{};
D3D12_GPU_DESCRIPTOR_HANDLE gParticleBatchDrawArgsUavGpuHandle{};
UINT gParticleBatchDrawArgsUavIndex = UINT_MAX;
D3D12_RESOURCE_STATES gParticleBatchDrawArgsState =
    D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
bool gParticleBatchInitialized = false;
bool gParticleBatchReadyToDraw = false;
uint64_t gParticleBatchDrawFrameId = UINT64_MAX;
std::vector<GPUParticleSystem *> gParticleBatchLastJobs;

float EstimateParticleActiveDuration(const ParticleEmitterSettings &settings) {
    return (std::max)(0.0f, settings.baseLifeTime + settings.lifeTimeRandom +
                                settings.fadeOutTime);
}

bool IsContinuousEmitter(const ParticleEmitterSettings &settings) {
    return settings.emissionType == ParticleEmissionType::Continuous &&
           settings.emitRate > 0.0f;
}

ParticleEmitterSettings
NormalizeParticleEmitterSettings(ParticleEmitterSettings settings) {
    settings.maxParticles = (std::max)(1u, settings.maxParticles);
    settings.emitRate = (std::max)(0.0f, settings.emitRate);
    settings.burstCount = (std::max)(1u, settings.burstCount);
    settings.spawnOffsetScale.x = (std::max)(0.0f, settings.spawnOffsetScale.x);
    settings.spawnOffsetScale.y = (std::max)(0.0f, settings.spawnOffsetScale.y);
    settings.spawnOffsetScale.z = (std::max)(0.0f, settings.spawnOffsetScale.z);
    settings.spawnShapeParams.x = (std::max)(0.0f, settings.spawnShapeParams.x);
    settings.radialVelocity = (std::max)(0.0f, settings.radialVelocity);
    settings.directionalVelocity =
        (std::max)(0.0f, settings.directionalVelocity);
    settings.baseLifeTime = (std::max)(0.01f, settings.baseLifeTime);
    settings.lifeTimeRandom = (std::max)(0.0f, settings.lifeTimeRandom);
    settings.startScale = (std::max)(0.001f, settings.startScale);
    settings.endScale = (std::max)(0.0f, settings.endScale);
    settings.scaleRandom = (std::max)(0.0f, settings.scaleRandom);
    settings.stretch = (std::max)(0.0f, settings.stretch);
    settings.atlasColumns = (std::max)(1u, settings.atlasColumns);
    settings.atlasRows = (std::max)(1u, settings.atlasRows);
    const uint32_t atlasFrameCapacity =
        settings.atlasColumns * settings.atlasRows;
    settings.atlasFrameStart =
        (std::min)(settings.atlasFrameStart, atlasFrameCapacity - 1u);
    settings.atlasFrameCount =
        (std::clamp)(settings.atlasFrameCount, 1u,
                     atlasFrameCapacity - settings.atlasFrameStart);
    settings.turbulence = (std::max)(0.0f, settings.turbulence);
    settings.damping = (std::clamp)(settings.damping, 0.0f, 1.0f);
    settings.fadeInTime = (std::max)(0.0f, settings.fadeInTime);
    settings.fadeOutTime = (std::max)(0.0f, settings.fadeOutTime);
    settings.fadeOutPower = (std::max)(0.01f, settings.fadeOutPower);
    settings.tintColor.w = (std::clamp)(settings.tintColor.w, 0.0f, 1.0f);
    return settings;
}

void ResetParticleDrawCacheIfDeviceChanged(ID3D12Device *device) {
    if (gCachedParticleDrawDevice == device) {
        return;
    }

    gCachedParticleDrawDevice = device;
    gCachedParticleDrawRootSignature.Reset();
    gCachedParticleDrawCommandSignature.Reset();
    gParticleDrawPsoCache.clear();
}

ID3D12RootSignature *GetSharedParticleDrawRootSignature(ID3D12Device *device) {
    ResetParticleDrawCacheIfDeviceChanged(device);
    if (gCachedParticleDrawRootSignature) {
        return gCachedParticleDrawRootSignature.Get();
    }

    CD3DX12_ROOT_PARAMETER params[5];
    params[0].InitAsConstantBufferView(0);

    CD3DX12_DESCRIPTOR_RANGE particleRange;
    particleRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    params[1].InitAsDescriptorTable(1, &particleRange);

    CD3DX12_DESCRIPTOR_RANGE textureRange;
    textureRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
    params[2].InitAsDescriptorTable(1, &textureRange);

    CD3DX12_DESCRIPTOR_RANGE noiseTextureRange;
    noiseTextureRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);
    params[3].InitAsDescriptorTable(1, &noiseTextureRange);

    CD3DX12_DESCRIPTOR_RANGE activeIndexRange;
    activeIndexRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3);
    params[4].InitAsDescriptorTable(1, &activeIndexRange);

    CD3DX12_STATIC_SAMPLER_DESC sampler(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;

    CD3DX12_ROOT_SIGNATURE_DESC desc;
    desc.Init(_countof(params), params, 1, &sampler,
              D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> blob, error;
    ThrowIfFailed(D3D12SerializeRootSignature(
                      &desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error),
                  "D3D12SerializeRootSignature(GPUParticleDraw) failed");
    ThrowIfFailed(device->CreateRootSignature(
                      0, blob->GetBufferPointer(), blob->GetBufferSize(),
                      IID_PPV_ARGS(&gCachedParticleDrawRootSignature)),
                  "CreateRootSignature(GPUParticleDraw) failed");
    return gCachedParticleDrawRootSignature.Get();
}

ID3D12PipelineState *GetOrCreateParticleDrawPso(
    ID3D12Device *device, ID3D12RootSignature *rootSignature,
    const std::wstring &pixelShaderPath) {
    ResetParticleDrawCacheIfDeviceChanged(device);

    auto found = gParticleDrawPsoCache.find(pixelShaderPath);
    if (found != gParticleDrawPsoCache.end()) {
        return found->second.Get();
    }

    auto vs =
        ShaderCompiler::Compile(ShaderPaths::ParticleVS, "main", "vs_6_6");
    auto ps = ShaderCompiler::Compile(pixelShaderPath, "main", "ps_6_6");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC drawPso{};
    drawPso.pRootSignature = rootSignature;
    drawPso.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    drawPso.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    drawPso.InputLayout = {nullptr, 0};
    drawPso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    drawPso.NumRenderTargets = 1;
    drawPso.RTVFormats[0] = DirectXCommon::kSceneColorFormat;
    drawPso.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    drawPso.SampleDesc.Count = 1;
    drawPso.SampleMask = UINT_MAX;

    D3D12_RASTERIZER_DESC rasterizer = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    rasterizer.CullMode = D3D12_CULL_MODE_NONE;
    drawPso.RasterizerState = rasterizer;

    D3D12_BLEND_DESC blend = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blend.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
    blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    drawPso.BlendState = blend;

    D3D12_DEPTH_STENCIL_DESC depth = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    depth.DepthEnable = TRUE;
    depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    drawPso.DepthStencilState = depth;

    ComPtr<ID3D12PipelineState> pso;
    ThrowIfFailed(device->CreateGraphicsPipelineState(
                      &drawPso, IID_PPV_ARGS(&pso)),
                  "CreateGraphicsPipelineState(GPUParticleDraw) failed");

    ID3D12PipelineState *result = pso.Get();
    gParticleDrawPsoCache[pixelShaderPath] = std::move(pso);
    return result;
}

ID3D12CommandSignature *GetSharedParticleDrawCommandSignature(
    ID3D12Device *device) {
    ResetParticleDrawCacheIfDeviceChanged(device);
    if (gCachedParticleDrawCommandSignature) {
        return gCachedParticleDrawCommandSignature.Get();
    }

    D3D12_INDIRECT_ARGUMENT_DESC indirectArgument{};
    indirectArgument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;

    D3D12_COMMAND_SIGNATURE_DESC commandSignatureDesc{};
    commandSignatureDesc.ByteStride = sizeof(D3D12_DRAW_ARGUMENTS);
    commandSignatureDesc.NumArgumentDescs = 1;
    commandSignatureDesc.pArgumentDescs = &indirectArgument;
    ThrowIfFailed(device->CreateCommandSignature(
                      &commandSignatureDesc, nullptr,
                      IID_PPV_ARGS(&gCachedParticleDrawCommandSignature)),
                  "CreateCommandSignature(GPUParticleDraw) failed");
    return gCachedParticleDrawCommandSignature.Get();
}

void EnsureParticleArgsDescriptorRanges(ID3D12Device *device,
                                        SrvManager *srvManager) {
    if (gParticleArgsDescriptorsInitialized &&
        gCachedParticleArgsDevice == device &&
        gCachedParticleArgsSrvManager == srvManager) {
        return;
    }

    gCachedParticleArgsDevice = device;
    gCachedParticleArgsSrvManager = srvManager;
    for (uint32_t frameIndex = 0;
         frameIndex < kParticleArgsDescriptorFrameCount; ++frameIndex) {
        gParticleArgsCountUavBase[frameIndex] =
            srvManager->AllocateRange(kMaxParticleArgsJobs);
        gParticleArgsDrawUavBase[frameIndex] =
            srvManager->AllocateRange(kMaxParticleArgsJobs);
    }
    gParticleArgsDescriptorsInitialized = true;
}

void EnsureParticleBatchResources(ID3D12Device *device,
                                  SrvManager *srvManager) {
    if (gParticleBatchInitialized && gCachedParticleBatchDevice == device &&
        gCachedParticleBatchSrvManager == srvManager) {
        return;
    }

    gCachedParticleBatchDevice = device;
    gCachedParticleBatchSrvManager = srvManager;
    gParticleBatchDrawPsoCache.clear();
    {
        CD3DX12_ROOT_PARAMETER params[7];
        params[0].InitAsConstants(6, 0);

        CD3DX12_DESCRIPTOR_RANGE jobRange;
        jobRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
        params[1].InitAsDescriptorTable(1, &jobRange);

        CD3DX12_DESCRIPTOR_RANGE particleRange;
        particleRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
                           kMaxParticleBatchJobs, 0);
        params[2].InitAsDescriptorTable(1, &particleRange);

        CD3DX12_DESCRIPTOR_RANGE freeListRange;
        freeListRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
                           kMaxParticleBatchJobs, 32);
        params[3].InitAsDescriptorTable(1, &freeListRange);

        CD3DX12_DESCRIPTOR_RANGE freeListIndexRange;
        freeListIndexRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
                                kMaxParticleBatchJobs, 64);
        params[4].InitAsDescriptorTable(1, &freeListIndexRange);

        CD3DX12_DESCRIPTOR_RANGE activeCountRange;
        activeCountRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 96);
        params[5].InitAsDescriptorTable(1, &activeCountRange);

        CD3DX12_DESCRIPTOR_RANGE activeItemsRange;
        activeItemsRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 97);
        params[6].InitAsDescriptorTable(1, &activeItemsRange);

        CD3DX12_ROOT_SIGNATURE_DESC desc;
        desc.Init(_countof(params), params, 0, nullptr);

        ComPtr<ID3DBlob> blob, error;
        ThrowIfFailed(D3D12SerializeRootSignature(
                          &desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error),
                      "D3D12SerializeRootSignature(GPUParticleBatchUpdate) "
                      "failed");
        ThrowIfFailed(device->CreateRootSignature(
                          0, blob->GetBufferPointer(), blob->GetBufferSize(),
                          IID_PPV_ARGS(&gParticleBatchUpdateRootSignature)),
                      "CreateRootSignature(GPUParticleBatchUpdate) failed");
    }

    {
        CD3DX12_ROOT_PARAMETER params[5];
        params[0].InitAsConstantBufferView(0);

        CD3DX12_DESCRIPTOR_RANGE particleRange;
        particleRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
                           kMaxParticleBatchJobs, 0);
        params[1].InitAsDescriptorTable(1, &particleRange);

        CD3DX12_DESCRIPTOR_RANGE textureRange;
        textureRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 16);
        params[2].InitAsDescriptorTable(1, &textureRange);

        CD3DX12_DESCRIPTOR_RANGE noiseTextureRange;
        noiseTextureRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 17);
        params[3].InitAsDescriptorTable(1, &noiseTextureRange);

        CD3DX12_DESCRIPTOR_RANGE activeItemsRange;
        activeItemsRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 32);
        params[4].InitAsDescriptorTable(1, &activeItemsRange);

        CD3DX12_STATIC_SAMPLER_DESC sampler(0,
                                            D3D12_FILTER_MIN_MAG_MIP_LINEAR);
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;

        CD3DX12_ROOT_SIGNATURE_DESC desc;
        desc.Init(_countof(params), params, 1, &sampler,
                  D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> blob, error;
        ThrowIfFailed(D3D12SerializeRootSignature(
                          &desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error),
                      "D3D12SerializeRootSignature(GPUParticleBatchDraw) "
                      "failed");
        ThrowIfFailed(device->CreateRootSignature(
                          0, blob->GetBufferPointer(), blob->GetBufferSize(),
                          IID_PPV_ARGS(&gParticleBatchDrawRootSignature)),
                      "CreateRootSignature(GPUParticleBatchDraw) failed");
    }

    auto updateCs = ShaderCompiler::Compile(
        ShaderPaths::ParticleBatchUpdateCS, "main", "cs_6_6");
    D3D12_COMPUTE_PIPELINE_STATE_DESC updatePso{};
    updatePso.pRootSignature = gParticleBatchUpdateRootSignature.Get();
    updatePso.CS = {updateCs->GetBufferPointer(), updateCs->GetBufferSize()};
    ThrowIfFailed(device->CreateComputePipelineState(
                      &updatePso, IID_PPV_ARGS(&gParticleBatchUpdatePSO)),
                  "CreateComputePipelineState(GPUParticleBatchUpdate) failed");

    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    const UINT jobBufferSize =
        static_cast<UINT>(sizeof(ParticleBatchJobForGPU) *
                          kMaxParticleBatchJobs);
    for (uint32_t frameIndex = 0;
         frameIndex < kParticleBatchDescriptorFrameCount; ++frameIndex) {
        gParticleBatchJobSrvIndex[frameIndex] = srvManager->Allocate();
        gParticleBatchParticleSrvBase[frameIndex] =
            srvManager->AllocateRange(kMaxParticleBatchJobs);
        gParticleBatchParticleUavBase[frameIndex] =
            srvManager->AllocateRange(kMaxParticleBatchJobs);
        gParticleBatchFreeListUavBase[frameIndex] =
            srvManager->AllocateRange(kMaxParticleBatchJobs);
        gParticleBatchFreeListIndexUavBase[frameIndex] =
            srvManager->AllocateRange(kMaxParticleBatchJobs);

        auto jobDesc = CD3DX12_RESOURCE_DESC::Buffer(jobBufferSize);
        ThrowIfFailed(device->CreateCommittedResource(
                          &uploadHeap, D3D12_HEAP_FLAG_NONE, &jobDesc,
                          D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                          IID_PPV_ARGS(&gParticleBatchJobsResource[frameIndex])),
                      "CreateCommittedResource(GPUParticleBatchJobs) failed");
        ThrowIfFailed(gParticleBatchJobsResource[frameIndex]->Map(
                          0, nullptr,
                          reinterpret_cast<void **>(
                              &gMappedParticleBatchJobs[frameIndex])),
                      "Map GPUParticleBatchJobs failed");

        D3D12_SHADER_RESOURCE_VIEW_DESC jobSrvDesc{};
        jobSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
        jobSrvDesc.Shader4ComponentMapping =
            D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        jobSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        jobSrvDesc.Buffer.NumElements = kMaxParticleBatchJobs;
        jobSrvDesc.Buffer.StructureByteStride =
            sizeof(ParticleBatchJobForGPU);
        device->CreateShaderResourceView(
            gParticleBatchJobsResource[frameIndex].Get(), &jobSrvDesc,
            srvManager->GetCpuHandle(gParticleBatchJobSrvIndex[frameIndex]));
    }

    CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
    const UINT activeItemsBufferSize =
        static_cast<UINT>(sizeof(uint32_t) * 2u * kMaxParticleBatchItems);
    auto activeItemsDesc = CD3DX12_RESOURCE_DESC::Buffer(
        activeItemsBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ThrowIfFailed(device->CreateCommittedResource(
                      &defaultHeap, D3D12_HEAP_FLAG_NONE, &activeItemsDesc,
                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                      IID_PPV_ARGS(&gParticleBatchActiveItemsResource)),
                  "CreateCommittedResource(GPUParticleBatchActiveItems) "
                  "failed");
    gParticleBatchActiveItemsState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    gParticleBatchActiveItemsSrvIndex = srvManager->Allocate();
    gParticleBatchActiveItemsSrvCpuHandle =
        srvManager->GetCpuHandle(gParticleBatchActiveItemsSrvIndex);
    gParticleBatchActiveItemsSrvGpuHandle =
        srvManager->GetGpuHandle(gParticleBatchActiveItemsSrvIndex);
    D3D12_SHADER_RESOURCE_VIEW_DESC activeItemsSrvDesc{};
    activeItemsSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
    activeItemsSrvDesc.Shader4ComponentMapping =
        D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    activeItemsSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    activeItemsSrvDesc.Buffer.NumElements = kMaxParticleBatchItems;
    activeItemsSrvDesc.Buffer.StructureByteStride = sizeof(uint32_t) * 2u;
    device->CreateShaderResourceView(gParticleBatchActiveItemsResource.Get(),
                                     &activeItemsSrvDesc,
                                     gParticleBatchActiveItemsSrvCpuHandle);

    gParticleBatchActiveItemsUavIndex = srvManager->Allocate();
    gParticleBatchActiveItemsUavCpuHandle =
        srvManager->GetCpuHandle(gParticleBatchActiveItemsUavIndex);
    gParticleBatchActiveItemsUavGpuHandle =
        srvManager->GetGpuHandle(gParticleBatchActiveItemsUavIndex);
    D3D12_UNORDERED_ACCESS_VIEW_DESC activeItemsUavDesc{};
    activeItemsUavDesc.Format = DXGI_FORMAT_UNKNOWN;
    activeItemsUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    activeItemsUavDesc.Buffer.NumElements = kMaxParticleBatchItems;
    activeItemsUavDesc.Buffer.StructureByteStride = sizeof(uint32_t) * 2u;
    device->CreateUnorderedAccessView(gParticleBatchActiveItemsResource.Get(),
                                      nullptr, &activeItemsUavDesc,
                                      gParticleBatchActiveItemsUavCpuHandle);

    constexpr UINT counterBufferSize = 16;
    auto counterDesc = CD3DX12_RESOURCE_DESC::Buffer(
        counterBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ThrowIfFailed(device->CreateCommittedResource(
                      &defaultHeap, D3D12_HEAP_FLAG_NONE, &counterDesc,
                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                      IID_PPV_ARGS(&gParticleBatchActiveCountResource)),
                  "CreateCommittedResource(GPUParticleBatchActiveCount) "
                  "failed");
    gParticleBatchActiveCountUavIndex = srvManager->Allocate();
    gParticleBatchActiveCountUavCpuHandle =
        srvManager->GetCpuHandle(gParticleBatchActiveCountUavIndex);
    gParticleBatchActiveCountUavGpuHandle =
        srvManager->GetGpuHandle(gParticleBatchActiveCountUavIndex);

    D3D12_UNORDERED_ACCESS_VIEW_DESC rawUavDesc{};
    rawUavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    rawUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    rawUavDesc.Buffer.NumElements = counterBufferSize / sizeof(uint32_t);
    rawUavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    device->CreateUnorderedAccessView(gParticleBatchActiveCountResource.Get(),
                                      nullptr, &rawUavDesc,
                                      gParticleBatchActiveCountUavCpuHandle);

    const UINT drawArgsBufferSize = sizeof(D3D12_DRAW_ARGUMENTS);
    auto drawArgsDesc = CD3DX12_RESOURCE_DESC::Buffer(
        drawArgsBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ThrowIfFailed(device->CreateCommittedResource(
                      &defaultHeap, D3D12_HEAP_FLAG_NONE, &drawArgsDesc,
                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                      IID_PPV_ARGS(&gParticleBatchDrawArgsResource)),
                  "CreateCommittedResource(GPUParticleBatchDrawArgs) failed");
    gParticleBatchDrawArgsState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    gParticleBatchDrawArgsUavIndex = srvManager->Allocate();
    gParticleBatchDrawArgsUavCpuHandle =
        srvManager->GetCpuHandle(gParticleBatchDrawArgsUavIndex);
    gParticleBatchDrawArgsUavGpuHandle =
        srvManager->GetGpuHandle(gParticleBatchDrawArgsUavIndex);
    rawUavDesc.Buffer.NumElements = drawArgsBufferSize / sizeof(uint32_t);
    device->CreateUnorderedAccessView(gParticleBatchDrawArgsResource.Get(),
                                      nullptr, &rawUavDesc,
                                      gParticleBatchDrawArgsUavCpuHandle);

    gParticleBatchInitialized = true;
}

ID3D12PipelineState *GetOrCreateParticleBatchDrawPso(
    ID3D12Device *device, const std::wstring &pixelShaderPath) {
    auto found = gParticleBatchDrawPsoCache.find(pixelShaderPath);
    if (found != gParticleBatchDrawPsoCache.end()) {
        return found->second.Get();
    }

    auto vs = ShaderCompiler::Compile(ShaderPaths::ParticleBatchVS, "main",
                                      "vs_6_6");
    auto ps = ShaderCompiler::Compile(pixelShaderPath, "main", "ps_6_6");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC drawPso{};
    drawPso.pRootSignature = gParticleBatchDrawRootSignature.Get();
    drawPso.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    drawPso.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    drawPso.InputLayout = {nullptr, 0};
    drawPso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    drawPso.NumRenderTargets = 1;
    drawPso.RTVFormats[0] = DirectXCommon::kSceneColorFormat;
    drawPso.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    drawPso.SampleDesc.Count = 1;
    drawPso.SampleMask = UINT_MAX;

    D3D12_RASTERIZER_DESC rasterizer =
        CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    rasterizer.CullMode = D3D12_CULL_MODE_NONE;
    drawPso.RasterizerState = rasterizer;

    D3D12_BLEND_DESC blend = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blend.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
    blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;
    drawPso.BlendState = blend;

    D3D12_DEPTH_STENCIL_DESC depth =
        CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    depth.DepthEnable = TRUE;
    depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    drawPso.DepthStencilState = depth;

    ComPtr<ID3D12PipelineState> pso;
    ThrowIfFailed(device->CreateGraphicsPipelineState(
                      &drawPso, IID_PPV_ARGS(&pso)),
                  "CreateGraphicsPipelineState(GPUParticleBatchDraw) failed");

    ID3D12PipelineState *result = pso.Get();
    gParticleBatchDrawPsoCache[pixelShaderPath] = std::move(pso);
    return result;
}

}

GPUParticleSystem::~GPUParticleSystem() { ReleaseResources(); }

void GPUParticleSystem::Initialize(DirectXCommon *dxCommon,
                                   SrvManager *srvManager,
                                   TextureManager *textureManager,
                                   uint32_t textureId, uint32_t maxParticles) {
    ReleaseResources();

    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
    textureManager_ = textureManager;
    textureId_ = textureId;
    maxParticles_ = (std::max)(1u, maxParticles);
    totalTime_ = 0.0f;
    emitterFrequencyTime_ = 0.0f;
    activeTimeRemaining_ = 0.0f;
    emitterSettings_ = NormalizeParticleEmitterSettings(ParticleEmitterSettings{});
    emitOncePending_ = false;

    std::mt19937 randomEngine{std::random_device{}()};
    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);

    std::vector<ParticleForGPU> particles(maxParticles_);
    for (ParticleForGPU &particle : particles) {
        particle.translate = emitterSettings_.position;
        particle.velocity = {};
        particle.lifeTime = 1.0f;
        particle.currentTime = particle.lifeTime;
        particle.color = {1.0f, 1.0f, 1.0f, 0.0f};
        particle.scale = {0.0f, 0.0f};
        particle.seed = dist01(randomEngine) * 10000.0f;
        particle.isActive = 0;
        particle.params0 = {};
        particle.params1 = {};
    }

    CreateRootSignatures();
    CreatePipelineStates();
    CreateParticleBuffer(particles);
    CreateFreeListBuffers();
    CreateActiveDrawBuffers();
    CreateConstantBuffers();
}

void GPUParticleSystem::SetEmitterSettings(
    const ParticleEmitterSettings &settings) {
    emitterSettings_ = NormalizeParticleEmitterSettings(settings);
}

void GPUParticleSystem::SetTextureFromFile(const std::wstring &filePath) {
    if (!textureManager_) {
        throw std::runtime_error(
            "GPUParticleSystem::SetTextureFromFile requires TextureManager");
    }

    textureId_ = textureManager_->Load(filePath);
}

void GPUParticleSystem::SetMaterialSettings(
    const GPUParticleMaterialSettings &settings) {
    materialSettings_ = settings;
}

void GPUParticleSystem::EmitOnce(const ParticleEmitterSettings &settings) {
    SetEmitterSettings(settings);
    emitterFrequencyTime_ = 0.0f;
    activeTimeRemaining_ =
        (std::max)(activeTimeRemaining_,
                   EstimateParticleActiveDuration(emitterSettings_));
    emitOncePending_ = true;
}

void GPUParticleSystem::Update(float deltaTime) {
    totalTime_ += deltaTime;

    if (!mappedUpdateCB_ || !mappedEmitterCB_) {
        return;
    }

    const bool continuousEmitter = IsContinuousEmitter(emitterSettings_);
    const bool wasActive = activeTimeRemaining_ > 0.0f;
    if (!emitOncePending_ && !continuousEmitter && !wasActive) {
        return;
    }

    if (wasActive) {
        activeTimeRemaining_ =
            (std::max)(0.0f, activeTimeRemaining_ - deltaTime);
    }

    emitterFrequencyTime_ += deltaTime;
    uint32_t emit = 0;
    if (emitOncePending_) {
        emitOncePending_ = false;
        emit = 1;
    } else if (continuousEmitter &&
               emitterFrequencyTime_ >= 1.0f / emitterSettings_.emitRate) {
        emitterFrequencyTime_ -= 1.0f / emitterSettings_.emitRate;
        emit = 1;
    }
    if (emit != 0) {
        activeTimeRemaining_ =
            (std::max)(activeTimeRemaining_,
                       EstimateParticleActiveDuration(emitterSettings_));
    }

    mappedUpdateCB_->time = {totalTime_, deltaTime,
                             static_cast<float>(maxParticles_), 0.0f};
    *mappedEmitterCB_ = BuildEmitterForGPU(emit);

    updatePending_ = true;
    if (dxCommon_ && dxCommon_->IsCommandListRecording()) {
        DispatchUpdate();
    }
}

void GPUParticleSystem::Draw(const Camera &camera) {
    if (!dxCommon_ || !srvManager_ || !textureManager_ ||
        !particleResource_ || !activeIndexResource_ || !drawArgsResource_ ||
        !drawCommandSignature_) {
        return;
    }
    if (!updatePending_ && activeTimeRemaining_ <= 0.0f &&
        !IsContinuousEmitter(emitterSettings_)) {
        return;
    }

    auto *cmd = dxCommon_->GetCommandList();
    ID3D12DescriptorHeap *heaps[] = {srvManager_->GetHeap()};
    cmd->SetDescriptorHeaps(1, heaps);

    if (updatePending_ && dxCommon_->IsCommandListRecording()) {
        DispatchUpdate();
    }

    XMMATRIX viewProjection = camera.GetView() * camera.GetProj();
    XMStoreFloat4x4(&mappedDrawCB_->viewProjection,
                    XMMatrixTranspose(viewProjection));

    XMMATRIX billboard = camera.GetView();
    billboard.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
    billboard = XMMatrixInverse(nullptr, billboard);

    XMFLOAT3 right{};
    XMFLOAT3 up{};
    XMStoreFloat3(&right, billboard.r[0]);
    XMStoreFloat3(&up, billboard.r[1]);
    mappedDrawCB_->cameraRight = {right.x, right.y, right.z, 0.0f};
    mappedDrawCB_->cameraUp = {up.x, up.y, up.z, 0.0f};
    mappedDrawCB_->tintColor = {1.0f, 1.0f, 1.0f, 1.0f};
    mappedDrawCB_->atlasInfo = {
        static_cast<float>((std::max)(1u, emitterSettings_.atlasColumns)),
        static_cast<float>((std::max)(1u, emitterSettings_.atlasRows)),
        0.0f,
        0.0f};
    mappedDrawCB_->materialParams0 = materialSettings_.params0;
    mappedDrawCB_->materialParams1 = materialSettings_.params1;

    const uint32_t noiseTextureId =
        materialSettings_.noiseTextureId != UINT32_MAX
            ? materialSettings_.noiseTextureId
            : textureManager_->GetWhiteTextureId();

    cmd->SetGraphicsRootSignature(drawRootSignature_.Get());
    cmd->SetPipelineState(drawPSO_.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->SetGraphicsRootConstantBufferView(
        0, drawConstantBuffer_->GetGPUVirtualAddress());
    cmd->SetGraphicsRootDescriptorTable(1, particleSrvGpuHandle_);
    cmd->SetGraphicsRootDescriptorTable(
        2, textureManager_->GetGpuHandle(textureId_));
    cmd->SetGraphicsRootDescriptorTable(
        3, textureManager_->GetGpuHandle(noiseTextureId));
    cmd->SetGraphicsRootDescriptorTable(4, activeIndexSrvGpuHandle_);
    cmd->ExecuteIndirect(drawCommandSignature_.Get(), 1, drawArgsResource_.Get(),
                         0, nullptr, 0);
}

void GPUParticleSystem::DrawBatch(
    std::initializer_list<GPUParticleSystem *> systems, const Camera &camera) {
    std::vector<GPUParticleSystem *> jobs;
    jobs.reserve(systems.size());
    for (GPUParticleSystem *system : systems) {
        if (!system || !system->dxCommon_ || !system->srvManager_ ||
            !system->textureManager_ || !system->particleResource_) {
            continue;
        }
        if (!system->updatePending_ && system->activeTimeRemaining_ <= 0.0f &&
            !system->emitOncePending_) {
            continue;
        }
        jobs.push_back(system);
    }

    if (jobs.empty()) {
        return;
    }

    for (GPUParticleSystem *system : jobs) {
        if (system->updatePending_) {
            DispatchPendingUpdates(systems);
            break;
        }
    }

    auto canBatchWithFirst = [&](const GPUParticleSystem *system) {
        const GPUParticleSystem *first = jobs.front();
        return system->dxCommon_ == first->dxCommon_ &&
               system->srvManager_ == first->srvManager_ &&
               system->textureManager_ == first->textureManager_ &&
               system->textureId_ == first->textureId_ &&
               system->materialSettings_.pixelShaderPath ==
                   first->materialSettings_.pixelShaderPath &&
               system->materialSettings_.noiseTextureId ==
                   first->materialSettings_.noiseTextureId &&
               std::memcmp(&system->materialSettings_.params0,
                           &first->materialSettings_.params0,
                           sizeof(XMFLOAT4)) == 0 &&
               std::memcmp(&system->materialSettings_.params1,
                           &first->materialSettings_.params1,
                           sizeof(XMFLOAT4)) == 0;
    };

    if (jobs.size() > kMaxParticleBatchJobs || !gParticleBatchReadyToDraw ||
        gParticleBatchLastJobs.size() != jobs.size() ||
        !std::equal(jobs.begin(), jobs.end(), gParticleBatchLastJobs.begin()) ||
        !std::all_of(jobs.begin(), jobs.end(), canBatchWithFirst)) {
        for (GPUParticleSystem *system : jobs) {
            system->Draw(camera);
        }
        return;
    }

    GPUParticleSystem *first = jobs.front();
    EnsureParticleBatchResources(first->dxCommon_->GetDevice(),
                                 first->srvManager_);

    auto *cmd = first->dxCommon_->GetCommandList();
    ID3D12DescriptorHeap *heaps[] = {first->srvManager_->GetHeap()};
    cmd->SetDescriptorHeaps(1, heaps);

    XMMATRIX viewProjection = camera.GetView() * camera.GetProj();
    XMStoreFloat4x4(&first->mappedDrawCB_->viewProjection,
                    XMMatrixTranspose(viewProjection));

    XMMATRIX billboard = camera.GetView();
    billboard.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
    billboard = XMMatrixInverse(nullptr, billboard);

    XMFLOAT3 right{};
    XMFLOAT3 up{};
    XMStoreFloat3(&right, billboard.r[0]);
    XMStoreFloat3(&up, billboard.r[1]);
    first->mappedDrawCB_->cameraRight = {right.x, right.y, right.z, 0.0f};
    first->mappedDrawCB_->cameraUp = {up.x, up.y, up.z, 0.0f};
    first->mappedDrawCB_->tintColor = {1.0f, 1.0f, 1.0f, 1.0f};
    first->mappedDrawCB_->atlasInfo = {
        static_cast<float>((std::max)(1u, first->emitterSettings_.atlasColumns)),
        static_cast<float>((std::max)(1u, first->emitterSettings_.atlasRows)),
        0.0f,
        0.0f};
    first->mappedDrawCB_->materialParams0 = first->materialSettings_.params0;
    first->mappedDrawCB_->materialParams1 = first->materialSettings_.params1;

    const std::wstring pixelShaderPath =
        first->materialSettings_.pixelShaderPath.empty()
            ? std::wstring(ShaderPaths::ParticlePS)
            : first->materialSettings_.pixelShaderPath;
    cmd->SetGraphicsRootSignature(gParticleBatchDrawRootSignature.Get());
    cmd->SetPipelineState(GetOrCreateParticleBatchDrawPso(
        first->dxCommon_->GetDevice(), pixelShaderPath));
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->SetGraphicsRootConstantBufferView(
        0, first->drawConstantBuffer_->GetGPUVirtualAddress());

    const uint32_t descriptorFrame =
        first->dxCommon_->GetBackBufferIndex() % kParticleBatchDescriptorFrameCount;
    cmd->SetGraphicsRootDescriptorTable(
        1, first->srvManager_->GetGpuHandle(
               gParticleBatchParticleSrvBase[descriptorFrame]));
    cmd->SetGraphicsRootDescriptorTable(
        2, first->textureManager_->GetGpuHandle(first->textureId_));
    const uint32_t noiseTextureId =
        first->materialSettings_.noiseTextureId == UINT32_MAX
            ? first->textureId_
            : first->materialSettings_.noiseTextureId;
    cmd->SetGraphicsRootDescriptorTable(
        3, first->textureManager_->GetGpuHandle(noiseTextureId));
    cmd->SetGraphicsRootDescriptorTable(4,
                                        gParticleBatchActiveItemsSrvGpuHandle);
    cmd->ExecuteIndirect(first->drawCommandSignature_.Get(), 1,
                         gParticleBatchDrawArgsResource.Get(), 0, nullptr, 0);
}

void GPUParticleSystem::DispatchPendingUpdate() {
    if (updatePending_ && dxCommon_ && dxCommon_->IsCommandListRecording()) {
        DispatchUpdate();
    }
}

void GPUParticleSystem::DispatchPendingUpdates(
    std::initializer_list<GPUParticleSystem *> systems) {
    std::vector<GPUParticleSystem *> jobs;
    jobs.reserve(systems.size());
    for (GPUParticleSystem *system : systems) {
        if (!system || !system->updatePending_ || !system->dxCommon_ ||
            !system->srvManager_ || !system->particleResource_ ||
            !system->freeListResource_ || !system->freeListIndexResource_ ||
            !system->updateConstantBuffer_ || !system->emitterConstantBuffer_) {
            continue;
        }
        jobs.push_back(system);
    }

    if (jobs.empty()) {
        return;
    }
    if (jobs.size() == 1u || jobs.size() > kMaxParticleBatchJobs) {
        for (GPUParticleSystem *system : jobs) {
            system->DispatchPendingUpdate();
        }
        return;
    }

    GPUParticleSystem *first = jobs.front();
    if (!first->dxCommon_->IsCommandListRecording()) {
        return;
    }
    const bool compatible =
        std::all_of(jobs.begin(), jobs.end(), [&](const GPUParticleSystem *job) {
            return job->dxCommon_ == first->dxCommon_ &&
                   job->srvManager_ == first->srvManager_;
        });
    if (!compatible) {
        for (GPUParticleSystem *system : jobs) {
            system->DispatchPendingUpdate();
        }
        return;
    }

    EnsureParticleBatchResources(first->dxCommon_->GetDevice(),
                                 first->srvManager_);

    auto *cmd = first->dxCommon_->GetCommandList();
    ID3D12DescriptorHeap *heaps[] = {first->srvManager_->GetHeap()};
    cmd->SetDescriptorHeaps(1, heaps);

    const uint32_t descriptorFrame =
        first->dxCommon_->GetBackBufferIndex() % kParticleBatchDescriptorFrameCount;
    const uint32_t particleSrvBase =
        gParticleBatchParticleSrvBase[descriptorFrame];
    const uint32_t particleUavBase =
        gParticleBatchParticleUavBase[descriptorFrame];
    const uint32_t freeListUavBase =
        gParticleBatchFreeListUavBase[descriptorFrame];
    const uint32_t freeListIndexUavBase =
        gParticleBatchFreeListIndexUavBase[descriptorFrame];

    auto copyDescriptor = [&](UINT destinationIndex,
                              D3D12_CPU_DESCRIPTOR_HANDLE source) {
        first->dxCommon_->GetDevice()->CopyDescriptorsSimple(
            1, first->srvManager_->GetCpuHandle(destinationIndex), source,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    };

    uint32_t totalUpdateThreadCount = 0;
    uint32_t totalEmitThreadCount = 0;
    std::memset(gMappedParticleBatchJobs[descriptorFrame], 0,
                sizeof(ParticleBatchJobForGPU) * kMaxParticleBatchJobs);
    for (size_t index = 0; index < jobs.size(); ++index) {
        GPUParticleSystem *job = jobs[index];
        const uint32_t emitCount =
            job->mappedEmitterCB_ ? job->mappedEmitterCB_->config.w : 0u;
        ParticleBatchJobForGPU &gpuJob =
            gMappedParticleBatchJobs[descriptorFrame][index];
        const EmitterForGPU emitter = job->BuildEmitterForGPU(emitCount);
        gpuJob.emitter.position = emitter.position;
        gpuJob.emitter.spawnOffsetScale = emitter.spawnOffsetScale;
        gpuJob.emitter.spawnShapeParams = emitter.spawnShapeParams;
        gpuJob.emitter.basisRight = emitter.basisRight;
        gpuJob.emitter.basisUp = emitter.basisUp;
        gpuJob.emitter.basisForward = emitter.basisForward;
        gpuJob.emitter.directionAndDirectionalVelocity =
            emitter.directionAndDirectionalVelocity;
        gpuJob.emitter.velocityBiasAndRadialVelocity =
            emitter.velocityBiasAndRadialVelocity;
        gpuJob.emitter.lifeAndFade = emitter.lifeAndFade;
        gpuJob.emitter.scale = emitter.scale;
        gpuJob.emitter.accelerationAndTurbulence =
            emitter.accelerationAndTurbulence;
        gpuJob.emitter.motion = emitter.motion;
        gpuJob.emitter.atlasAndRotation = emitter.atlasAndRotation;
        gpuJob.emitter.tintColor = emitter.tintColor;
        gpuJob.emitter.config = emitter.config;
        gpuJob.time = job->mappedUpdateCB_ ? job->mappedUpdateCB_->time
                                           : XMFLOAT4{};
        gpuJob.particleCount = job->maxParticles_;
        gpuJob.updateStart = totalUpdateThreadCount;
        gpuJob.emitStart = totalEmitThreadCount;
        gpuJob.emitCount = emitCount;
        totalUpdateThreadCount += job->maxParticles_;
        totalEmitThreadCount += emitCount;

        copyDescriptor(particleSrvBase + static_cast<UINT>(index),
                       job->particleSrvCpuHandle_);
        copyDescriptor(particleUavBase + static_cast<UINT>(index),
                       job->particleUavCpuHandle_);
        copyDescriptor(freeListUavBase + static_cast<UINT>(index),
                       job->freeListUavCpuHandle_);
        copyDescriptor(freeListIndexUavBase + static_cast<UINT>(index),
                       job->freeListIndexUavCpuHandle_);
    }

    std::vector<D3D12_RESOURCE_BARRIER> barriers;
    barriers.reserve(jobs.size() + 3u);
    for (GPUParticleSystem *job : jobs) {
        barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
            job->particleResource_.Get(),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
    }
    if (gParticleBatchActiveItemsState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
            gParticleBatchActiveItemsResource.Get(), gParticleBatchActiveItemsState,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
        gParticleBatchActiveItemsState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    if (gParticleBatchDrawArgsState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
            gParticleBatchDrawArgsResource.Get(), gParticleBatchDrawArgsState,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
        gParticleBatchDrawArgsState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    cmd->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());

    const UINT clearValues[4] = {};
    cmd->ClearUnorderedAccessViewUint(gParticleBatchActiveCountUavGpuHandle,
                                      gParticleBatchActiveCountUavCpuHandle,
                                      gParticleBatchActiveCountResource.Get(),
                                      clearValues, 0, nullptr);
    auto clearBarrier =
        CD3DX12_RESOURCE_BARRIER::UAV(gParticleBatchActiveCountResource.Get());
    cmd->ResourceBarrier(1, &clearBarrier);

    auto recordBatchDispatch = [&](uint32_t phase, uint32_t jobIndex,
                                   uint32_t totalThreadCount) {
        if (totalThreadCount == 0u) {
            return;
        }
        const uint32_t constants[6] = {
            static_cast<uint32_t>(jobs.size()), totalUpdateThreadCount,
            totalThreadCount, kMaxParticleBatchItems, phase, jobIndex};
        cmd->SetComputeRootSignature(gParticleBatchUpdateRootSignature.Get());
        cmd->SetPipelineState(gParticleBatchUpdatePSO.Get());
        cmd->SetComputeRoot32BitConstants(0, _countof(constants), constants, 0);
        cmd->SetComputeRootDescriptorTable(
            1, first->srvManager_->GetGpuHandle(
                   gParticleBatchJobSrvIndex[descriptorFrame]));
        cmd->SetComputeRootDescriptorTable(
            2, first->srvManager_->GetGpuHandle(particleUavBase));
        cmd->SetComputeRootDescriptorTable(
            3, first->srvManager_->GetGpuHandle(freeListUavBase));
        cmd->SetComputeRootDescriptorTable(
            4, first->srvManager_->GetGpuHandle(freeListIndexUavBase));
        cmd->SetComputeRootDescriptorTable(
            5, gParticleBatchActiveCountUavGpuHandle);
        cmd->SetComputeRootDescriptorTable(
            6, gParticleBatchActiveItemsUavGpuHandle);
        cmd->Dispatch((totalThreadCount + kParticleThreadCount - 1u) /
                          kParticleThreadCount,
                      1, 1);
    };

    for (uint32_t index = 0; index < jobs.size(); ++index) {
        recordBatchDispatch(0u, index, jobs[index]->maxParticles_);
    }

    std::vector<D3D12_RESOURCE_BARRIER> uavBarriers;
    uavBarriers.reserve(jobs.size() * 3u + 2u);
    for (GPUParticleSystem *job : jobs) {
        uavBarriers.push_back(
            CD3DX12_RESOURCE_BARRIER::UAV(job->particleResource_.Get()));
        uavBarriers.push_back(
            CD3DX12_RESOURCE_BARRIER::UAV(job->freeListResource_.Get()));
        uavBarriers.push_back(
            CD3DX12_RESOURCE_BARRIER::UAV(job->freeListIndexResource_.Get()));
    }
    uavBarriers.push_back(
        CD3DX12_RESOURCE_BARRIER::UAV(gParticleBatchActiveItemsResource.Get()));
    uavBarriers.push_back(
        CD3DX12_RESOURCE_BARRIER::UAV(gParticleBatchActiveCountResource.Get()));
    cmd->ResourceBarrier(static_cast<UINT>(uavBarriers.size()),
                         uavBarriers.data());

    if (totalEmitThreadCount != 0u) {
        for (uint32_t index = 0; index < jobs.size(); ++index) {
            const uint32_t emitCount =
                gMappedParticleBatchJobs[descriptorFrame][index].emitCount;
            recordBatchDispatch(1u, index, emitCount);
        }
        cmd->ResourceBarrier(static_cast<UINT>(uavBarriers.size()),
                             uavBarriers.data());
    }

    EnsureParticleArgsDescriptorRanges(first->dxCommon_->GetDevice(),
                                       first->srvManager_);
    const uint32_t argsDescriptorFrame =
        first->dxCommon_->GetBackBufferIndex() %
        kParticleArgsDescriptorFrameCount;
    const uint32_t countUavBase = gParticleArgsCountUavBase[argsDescriptorFrame];
    const uint32_t drawUavBase = gParticleArgsDrawUavBase[argsDescriptorFrame];
    copyDescriptor(countUavBase, gParticleBatchActiveCountUavCpuHandle);
    copyDescriptor(drawUavBase, gParticleBatchDrawArgsUavCpuHandle);

    const uint32_t argsConstants[1u + kMaxParticleArgsJobs] = {
        1u, kMaxParticleBatchItems};
    cmd->SetComputeRootSignature(first->argsRootSignature_.Get());
    cmd->SetPipelineState(first->argsPSO_.Get());
    cmd->SetComputeRoot32BitConstants(
        0, static_cast<UINT>(_countof(argsConstants)), argsConstants, 0);
    cmd->SetComputeRootDescriptorTable(
        1, first->srvManager_->GetGpuHandle(countUavBase));
    cmd->SetComputeRootDescriptorTable(
        2, first->srvManager_->GetGpuHandle(drawUavBase));
    cmd->Dispatch(1, 1, 1);

    auto argsUavBarrier =
        CD3DX12_RESOURCE_BARRIER::UAV(gParticleBatchDrawArgsResource.Get());
    cmd->ResourceBarrier(1, &argsUavBarrier);

    std::vector<D3D12_RESOURCE_BARRIER> finalBarriers;
    finalBarriers.reserve(jobs.size() + 2u);
    for (GPUParticleSystem *job : jobs) {
        finalBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
            job->particleResource_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    }
    finalBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
        gParticleBatchActiveItemsResource.Get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    finalBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
        gParticleBatchDrawArgsResource.Get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT));
    cmd->ResourceBarrier(static_cast<UINT>(finalBarriers.size()),
                         finalBarriers.data());
    gParticleBatchActiveItemsState =
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    gParticleBatchDrawArgsState = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;

    for (GPUParticleSystem *job : jobs) {
        job->updatePending_ = false;
    }
    gParticleBatchLastJobs = jobs;
    gParticleBatchReadyToDraw = true;
}

void GPUParticleSystem::DispatchUpdate() {
    if (!dxCommon_ || !srvManager_ || !particleResource_ ||
        !activeIndexResource_ || !activeCountResource_ || !drawArgsResource_ ||
        !updateConstantBuffer_ || !emitterConstantBuffer_) {
        return;
    }

    auto *cmd = dxCommon_->GetCommandList();
    ID3D12DescriptorHeap *heaps[] = {srvManager_->GetHeap()};
    cmd->SetDescriptorHeaps(1, heaps);

    std::vector<D3D12_RESOURCE_BARRIER> barriers;
    barriers.reserve(3);
    barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
        particleResource_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
    if (activeIndexState_ != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
            activeIndexResource_.Get(), activeIndexState_,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
        activeIndexState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    if (drawArgsState_ != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
            drawArgsResource_.Get(), drawArgsState_,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
        drawArgsState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    cmd->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());

    const UINT clearValues[4] = {};
    cmd->ClearUnorderedAccessViewUint(activeCountUavGpuHandle_,
                                      activeCountUavCpuHandle_,
                                      activeCountResource_.Get(), clearValues,
                                      0, nullptr);
    auto clearBarrier = CD3DX12_RESOURCE_BARRIER::UAV(activeCountResource_.Get());
    cmd->ResourceBarrier(1, &clearBarrier);

    RecordUpdateDispatch(0);

    D3D12_RESOURCE_BARRIER uavBarriers[] = {
        CD3DX12_RESOURCE_BARRIER::UAV(particleResource_.Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(freeListResource_.Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(freeListIndexResource_.Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(activeIndexResource_.Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(activeCountResource_.Get()),
    };
    cmd->ResourceBarrier(_countof(uavBarriers), uavBarriers);

    if (mappedEmitterCB_->config.w != 0u) {
        RecordUpdateDispatch(1);
        cmd->ResourceBarrier(_countof(uavBarriers), uavBarriers);
    }

    std::vector<GPUParticleSystem *> jobs{this};
    RecordDrawArgsDispatches(dxCommon_, srvManager_, jobs);

    auto argsUavBarrier =
        CD3DX12_RESOURCE_BARRIER::UAV(drawArgsResource_.Get());
    cmd->ResourceBarrier(1, &argsUavBarrier);

    D3D12_RESOURCE_BARRIER finalBarriers[] = {
        CD3DX12_RESOURCE_BARRIER::Transition(
            particleResource_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        CD3DX12_RESOURCE_BARRIER::Transition(
            activeIndexResource_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        CD3DX12_RESOURCE_BARRIER::Transition(
            drawArgsResource_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT),
    };
    cmd->ResourceBarrier(_countof(finalBarriers), finalBarriers);
    activeIndexState_ = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    drawArgsState_ = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    updatePending_ = false;
}

void GPUParticleSystem::RecordUpdateDispatch(uint32_t phase) {
    auto *cmd = dxCommon_->GetCommandList();
    cmd->SetComputeRootSignature(updateRootSignature_.Get());
    cmd->SetPipelineState(updatePSO_.Get());
    cmd->SetComputeRootConstantBufferView(
        0, updateConstantBuffer_->GetGPUVirtualAddress());
    cmd->SetComputeRootConstantBufferView(
        1, emitterConstantBuffer_->GetGPUVirtualAddress());
    cmd->SetComputeRootDescriptorTable(2, particleUavGpuHandle_);
    cmd->SetComputeRootDescriptorTable(3, freeListUavGpuHandle_);
    cmd->SetComputeRootDescriptorTable(4, freeListIndexUavGpuHandle_);
    cmd->SetComputeRootDescriptorTable(5, activeIndexUavGpuHandle_);
    cmd->SetComputeRootDescriptorTable(6, activeCountUavGpuHandle_);
    cmd->SetComputeRoot32BitConstant(7, phase, 0);
    cmd->Dispatch((maxParticles_ + kParticleThreadCount - 1u) /
                      kParticleThreadCount,
                  1, 1);
}

void GPUParticleSystem::RecordDrawArgsDispatches(
    DirectXCommon *dxCommon, SrvManager *srvManager,
    const std::vector<GPUParticleSystem *> &jobs) {
    if (jobs.empty()) {
        return;
    }

    EnsureParticleArgsDescriptorRanges(dxCommon->GetDevice(), srvManager);

    auto *cmd = dxCommon->GetCommandList();
    cmd->SetComputeRootSignature(jobs.front()->argsRootSignature_.Get());
    cmd->SetPipelineState(jobs.front()->argsPSO_.Get());

    const uint32_t descriptorFrame =
        dxCommon->GetBackBufferIndex() % kParticleArgsDescriptorFrameCount;
    const uint32_t countUavBase =
        gParticleArgsCountUavBase[descriptorFrame];
    const uint32_t drawUavBase = gParticleArgsDrawUavBase[descriptorFrame];

    auto copyDescriptor = [&](UINT destinationIndex,
                              D3D12_CPU_DESCRIPTOR_HANDLE source) {
        dxCommon->GetDevice()->CopyDescriptorsSimple(
            1, srvManager->GetCpuHandle(destinationIndex), source,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    };

    for (size_t chunkStart = 0; chunkStart < jobs.size();
         chunkStart += kMaxParticleArgsJobs) {
        const size_t chunkCount = (std::min<size_t>)(
            kMaxParticleArgsJobs, jobs.size() - chunkStart);
        for (size_t localIndex = 0; localIndex < chunkCount; ++localIndex) {
            GPUParticleSystem *job = jobs[chunkStart + localIndex];
            copyDescriptor(countUavBase + static_cast<UINT>(localIndex),
                           job->activeCountUavCpuHandle_);
            copyDescriptor(drawUavBase + static_cast<UINT>(localIndex),
                           job->drawArgsUavCpuHandle_);
        }

        uint32_t constants[1u + kMaxParticleArgsJobs] = {};
        constants[0] = static_cast<uint32_t>(chunkCount);
        for (size_t localIndex = 0; localIndex < chunkCount; ++localIndex) {
            constants[1u + localIndex] =
                jobs[chunkStart + localIndex]->maxParticles_;
        }
        cmd->SetComputeRoot32BitConstants(
            0, static_cast<UINT>(_countof(constants)), constants, 0);
        cmd->SetComputeRootDescriptorTable(1,
                                           srvManager->GetGpuHandle(countUavBase));
        cmd->SetComputeRootDescriptorTable(2,
                                           srvManager->GetGpuHandle(drawUavBase));
        cmd->Dispatch(1, 1, 1);
    }
}

GPUParticleSystem::EmitterForGPU
GPUParticleSystem::BuildEmitterForGPU(uint32_t emit) const {
    EmitterForGPU emitter{};
    emitter.position = {emitterSettings_.position.x, emitterSettings_.position.y,
                        emitterSettings_.position.z, 0.0f};
    emitter.spawnOffsetScale = {
        emitterSettings_.spawnOffsetScale.x, emitterSettings_.spawnOffsetScale.y,
        emitterSettings_.spawnOffsetScale.z, 0.0f};
    emitter.spawnShapeParams = emitterSettings_.spawnShapeParams;
    emitter.basisRight = {emitterSettings_.basisRight.x,
                          emitterSettings_.basisRight.y,
                          emitterSettings_.basisRight.z, 0.0f};
    emitter.basisUp = {emitterSettings_.basisUp.x, emitterSettings_.basisUp.y,
                       emitterSettings_.basisUp.z, 0.0f};
    emitter.basisForward = {emitterSettings_.basisForward.x,
                            emitterSettings_.basisForward.y,
                            emitterSettings_.basisForward.z, 0.0f};
    emitter.directionAndDirectionalVelocity = {
        emitterSettings_.direction.x, emitterSettings_.direction.y,
        emitterSettings_.direction.z, emitterSettings_.directionalVelocity};
    emitter.velocityBiasAndRadialVelocity = {
        emitterSettings_.velocityBias.x, emitterSettings_.velocityBias.y,
        emitterSettings_.velocityBias.z, emitterSettings_.radialVelocity};
    emitter.lifeAndFade = {emitterSettings_.baseLifeTime,
                           emitterSettings_.lifeTimeRandom,
                           emitterSettings_.fadeInTime,
                           emitterSettings_.fadeOutTime};
    emitter.scale = {emitterSettings_.startScale, emitterSettings_.endScale,
                     emitterSettings_.scaleRandom, emitterSettings_.stretch};
    emitter.accelerationAndTurbulence = {
        emitterSettings_.acceleration.x, emitterSettings_.acceleration.y,
        emitterSettings_.acceleration.z, emitterSettings_.turbulence};
    emitter.motion = {emitterSettings_.damping, emitterSettings_.fadeOutPower,
                      emitterSettings_.emitRate, 0.0f};
    emitter.atlasAndRotation = {
        static_cast<float>(emitterSettings_.atlasFrameStart),
        static_cast<float>(emitterSettings_.atlasFrameCount),
        emitterSettings_.rotationSpeed,
        emitterSettings_.randomStartRotation ? 1.0f : 0.0f};
    emitter.tintColor = emitterSettings_.tintColor;
    emitter.config = {
        static_cast<uint32_t>(emitterSettings_.emissionType),
        static_cast<uint32_t>(emitterSettings_.spawnShape),
        (std::min)(emitterSettings_.burstCount, maxParticles_), emit};
    return emitter;
}

void GPUParticleSystem::CreateRootSignatures() {
    {
        CD3DX12_ROOT_PARAMETER params[8];
        params[0].InitAsConstantBufferView(0);
        params[1].InitAsConstantBufferView(1);

        CD3DX12_DESCRIPTOR_RANGE particleRange;
        particleRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
        params[2].InitAsDescriptorTable(1, &particleRange);

        CD3DX12_DESCRIPTOR_RANGE freeListRange;
        freeListRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1);
        params[3].InitAsDescriptorTable(1, &freeListRange);

        CD3DX12_DESCRIPTOR_RANGE freeListIndexRange;
        freeListIndexRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 2);
        params[4].InitAsDescriptorTable(1, &freeListIndexRange);

        CD3DX12_DESCRIPTOR_RANGE activeIndexRange;
        activeIndexRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 3);
        params[5].InitAsDescriptorTable(1, &activeIndexRange);

        CD3DX12_DESCRIPTOR_RANGE activeCountRange;
        activeCountRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 4);
        params[6].InitAsDescriptorTable(1, &activeCountRange);

        params[7].InitAsConstants(1, 2);

        CD3DX12_ROOT_SIGNATURE_DESC desc;
        desc.Init(_countof(params), params, 0, nullptr);

        ComPtr<ID3DBlob> blob, error;
        ThrowIfFailed(D3D12SerializeRootSignature(
                          &desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error),
                      "D3D12SerializeRootSignature(GPUParticleUpdate) failed");
        ThrowIfFailed(dxCommon_->GetDevice()->CreateRootSignature(
                          0, blob->GetBufferPointer(), blob->GetBufferSize(),
                          IID_PPV_ARGS(&updateRootSignature_)),
                      "CreateRootSignature(GPUParticleUpdate) failed");
    }

    {
        CD3DX12_ROOT_PARAMETER params[3];
        params[0].InitAsConstants(1u + kMaxParticleArgsJobs, 0);

        CD3DX12_DESCRIPTOR_RANGE activeCountRange;
        activeCountRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
                              kMaxParticleArgsJobs, 0);
        params[1].InitAsDescriptorTable(1, &activeCountRange);

        CD3DX12_DESCRIPTOR_RANGE drawArgsRange;
        drawArgsRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
                           kMaxParticleArgsJobs, 32);
        params[2].InitAsDescriptorTable(1, &drawArgsRange);

        CD3DX12_ROOT_SIGNATURE_DESC desc;
        desc.Init(_countof(params), params, 0, nullptr);

        ComPtr<ID3DBlob> blob, error;
        ThrowIfFailed(D3D12SerializeRootSignature(
                          &desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error),
                      "D3D12SerializeRootSignature(GPUParticleArgs) failed");
        ThrowIfFailed(dxCommon_->GetDevice()->CreateRootSignature(
                          0, blob->GetBufferPointer(), blob->GetBufferSize(),
                          IID_PPV_ARGS(&argsRootSignature_)),
                      "CreateRootSignature(GPUParticleArgs) failed");
    }

    drawRootSignature_ =
        GetSharedParticleDrawRootSignature(dxCommon_->GetDevice());
}

void GPUParticleSystem::CreatePipelineStates() {
    auto *device = dxCommon_->GetDevice();

    auto cs = ShaderCompiler::Compile(ShaderPaths::ParticleUpdateCS, "main",
                                      "cs_6_6");
    D3D12_COMPUTE_PIPELINE_STATE_DESC computePso{};
    computePso.pRootSignature = updateRootSignature_.Get();
    computePso.CS = {cs->GetBufferPointer(), cs->GetBufferSize()};
    ThrowIfFailed(device->CreateComputePipelineState(&computePso,
                                                     IID_PPV_ARGS(&updatePSO_)),
                  "CreateComputePipelineState(GPUParticleUpdate) failed");

    auto argsCs =
        ShaderCompiler::Compile(ShaderPaths::ParticleArgsCS, "main", "cs_6_6");
    D3D12_COMPUTE_PIPELINE_STATE_DESC argsPso{};
    argsPso.pRootSignature = argsRootSignature_.Get();
    argsPso.CS = {argsCs->GetBufferPointer(), argsCs->GetBufferSize()};
    ThrowIfFailed(device->CreateComputePipelineState(&argsPso,
                                                     IID_PPV_ARGS(&argsPSO_)),
                  "CreateComputePipelineState(GPUParticleArgs) failed");

    drawCommandSignature_ = GetSharedParticleDrawCommandSignature(device);

    const std::wstring pixelShaderPath =
        materialSettings_.pixelShaderPath.empty()
            ? std::wstring(ShaderPaths::ParticlePS)
            : materialSettings_.pixelShaderPath;
    drawPSO_ = GetOrCreateParticleDrawPso(device, drawRootSignature_.Get(),
                                          pixelShaderPath);
}

void GPUParticleSystem::CreateParticleBuffer(
    const std::vector<ParticleForGPU> &particles) {
    const UINT bufferSize =
        static_cast<UINT>(sizeof(ParticleForGPU) * particles.size());
    auto *device = dxCommon_->GetDevice();

    CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
    auto particleDesc = CD3DX12_RESOURCE_DESC::Buffer(
        bufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ThrowIfFailed(device->CreateCommittedResource(
                      &defaultHeap, D3D12_HEAP_FLAG_NONE, &particleDesc,
                      D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                      IID_PPV_ARGS(&particleResource_)),
                  "CreateCommittedResource(GPUParticleBuffer) failed");

    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
    ThrowIfFailed(device->CreateCommittedResource(
                      &uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&particleUploadResource_)),
                  "CreateCommittedResource(GPUParticleUpload) failed");

    uint8_t *mapped = nullptr;
    ThrowIfFailed(particleUploadResource_->Map(
                      0, nullptr, reinterpret_cast<void **>(&mapped)),
                  "GPUParticleUpload Map failed");
    std::memcpy(mapped, particles.data(), bufferSize);
    particleUploadResource_->Unmap(0, nullptr);

    dxCommon_->GetCommandList()->CopyBufferRegion(particleResource_.Get(), 0,
                                                  particleUploadResource_.Get(),
                                                  0, bufferSize);
    auto toSrv = CD3DX12_RESOURCE_BARRIER::Transition(
        particleResource_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    dxCommon_->GetCommandList()->ResourceBarrier(1, &toSrv);

    particleSrvIndex_ = srvManager_->Allocate();
    particleSrvCpuHandle_ = srvManager_->GetCpuHandle(particleSrvIndex_);
    particleSrvGpuHandle_ = srvManager_->GetGpuHandle(particleSrvIndex_);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = maxParticles_;
    srvDesc.Buffer.StructureByteStride = sizeof(ParticleForGPU);
    srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    device->CreateShaderResourceView(particleResource_.Get(), &srvDesc,
                                     particleSrvCpuHandle_);

    particleUavIndex_ = srvManager_->Allocate();
    particleUavCpuHandle_ = srvManager_->GetCpuHandle(particleUavIndex_);
    particleUavGpuHandle_ = srvManager_->GetGpuHandle(particleUavIndex_);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = maxParticles_;
    uavDesc.Buffer.StructureByteStride = sizeof(ParticleForGPU);
    uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    device->CreateUnorderedAccessView(particleResource_.Get(), nullptr,
                                      &uavDesc, particleUavCpuHandle_);
}

void GPUParticleSystem::CreateFreeListBuffers() {
    auto *device = dxCommon_->GetDevice();
    const UINT freeListBufferSize = sizeof(uint32_t) * maxParticles_;

    CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
    auto freeListDesc = CD3DX12_RESOURCE_DESC::Buffer(
        freeListBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ThrowIfFailed(device->CreateCommittedResource(
                      &defaultHeap, D3D12_HEAP_FLAG_NONE, &freeListDesc,
                      D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                      IID_PPV_ARGS(&freeListResource_)),
                  "CreateCommittedResource(GPUParticleFreeList) failed");

    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    auto freeListUploadDesc = CD3DX12_RESOURCE_DESC::Buffer(freeListBufferSize);
    ThrowIfFailed(device->CreateCommittedResource(
                      &uploadHeap, D3D12_HEAP_FLAG_NONE, &freeListUploadDesc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&freeListUploadResource_)),
                  "CreateCommittedResource(GPUParticleFreeListUpload) failed");

    std::vector<uint32_t> freeList(maxParticles_);
    for (uint32_t index = 0; index < maxParticles_; ++index) {
        freeList[index] = index;
    }

    uint8_t *mappedFreeList = nullptr;
    ThrowIfFailed(freeListUploadResource_->Map(
                      0, nullptr, reinterpret_cast<void **>(&mappedFreeList)),
                  "GPUParticleFreeListUpload Map failed");
    std::memcpy(mappedFreeList, freeList.data(), freeListBufferSize);
    freeListUploadResource_->Unmap(0, nullptr);

    dxCommon_->GetCommandList()->CopyBufferRegion(freeListResource_.Get(), 0,
                                                  freeListUploadResource_.Get(),
                                                  0, freeListBufferSize);
    auto freeListToUav = CD3DX12_RESOURCE_BARRIER::Transition(
        freeListResource_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    dxCommon_->GetCommandList()->ResourceBarrier(1, &freeListToUav);

    freeListUavIndex_ = srvManager_->Allocate();
    freeListUavCpuHandle_ = srvManager_->GetCpuHandle(freeListUavIndex_);
    freeListUavGpuHandle_ = srvManager_->GetGpuHandle(freeListUavIndex_);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = maxParticles_;
    uavDesc.Buffer.StructureByteStride = sizeof(uint32_t);
    uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    device->CreateUnorderedAccessView(freeListResource_.Get(), nullptr,
                                      &uavDesc, freeListUavCpuHandle_);

    constexpr UINT freeListIndexBufferSize = sizeof(int32_t);
    auto freeListIndexDesc = CD3DX12_RESOURCE_DESC::Buffer(
        freeListIndexBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ThrowIfFailed(device->CreateCommittedResource(
                      &defaultHeap, D3D12_HEAP_FLAG_NONE, &freeListIndexDesc,
                      D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                      IID_PPV_ARGS(&freeListIndexResource_)),
                  "CreateCommittedResource(GPUParticleFreeListIndex) failed");

    auto freeListIndexUploadDesc =
        CD3DX12_RESOURCE_DESC::Buffer(freeListIndexBufferSize);
    ThrowIfFailed(
        device->CreateCommittedResource(
            &uploadHeap, D3D12_HEAP_FLAG_NONE, &freeListIndexUploadDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&freeListIndexUploadResource_)),
        "CreateCommittedResource(GPUParticleFreeListIndexUpload) failed");

    int32_t *mappedFreeListIndex = nullptr;
    ThrowIfFailed(
        freeListIndexUploadResource_->Map(
            0, nullptr, reinterpret_cast<void **>(&mappedFreeListIndex)),
        "GPUParticleFreeListIndexUpload Map failed");
    *mappedFreeListIndex = static_cast<int32_t>(maxParticles_);
    freeListIndexUploadResource_->Unmap(0, nullptr);

    dxCommon_->GetCommandList()->CopyBufferRegion(
        freeListIndexResource_.Get(), 0, freeListIndexUploadResource_.Get(), 0,
        freeListIndexBufferSize);
    auto freeListIndexToUav = CD3DX12_RESOURCE_BARRIER::Transition(
        freeListIndexResource_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    dxCommon_->GetCommandList()->ResourceBarrier(1, &freeListIndexToUav);

    freeListIndexUavIndex_ = srvManager_->Allocate();
    freeListIndexUavCpuHandle_ =
        srvManager_->GetCpuHandle(freeListIndexUavIndex_);
    freeListIndexUavGpuHandle_ =
        srvManager_->GetGpuHandle(freeListIndexUavIndex_);

    D3D12_UNORDERED_ACCESS_VIEW_DESC indexUavDesc{};
    indexUavDesc.Format = DXGI_FORMAT_UNKNOWN;
    indexUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    indexUavDesc.Buffer.FirstElement = 0;
    indexUavDesc.Buffer.NumElements = 1;
    indexUavDesc.Buffer.StructureByteStride = sizeof(int32_t);
    indexUavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    device->CreateUnorderedAccessView(freeListIndexResource_.Get(), nullptr,
                                      &indexUavDesc,
                                      freeListIndexUavCpuHandle_);
}

void GPUParticleSystem::CreateActiveDrawBuffers() {
    auto *device = dxCommon_->GetDevice();
    CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);

    const UINT activeIndexBufferSize = sizeof(uint32_t) * maxParticles_;
    auto activeIndexDesc = CD3DX12_RESOURCE_DESC::Buffer(
        activeIndexBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ThrowIfFailed(device->CreateCommittedResource(
                      &defaultHeap, D3D12_HEAP_FLAG_NONE, &activeIndexDesc,
                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                      IID_PPV_ARGS(&activeIndexResource_)),
                  "CreateCommittedResource(GPUParticleActiveIndex) failed");
    activeIndexState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    activeIndexSrvIndex_ = srvManager_->Allocate();
    activeIndexSrvCpuHandle_ = srvManager_->GetCpuHandle(activeIndexSrvIndex_);
    activeIndexSrvGpuHandle_ = srvManager_->GetGpuHandle(activeIndexSrvIndex_);

    D3D12_SHADER_RESOURCE_VIEW_DESC activeIndexSrvDesc{};
    activeIndexSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
    activeIndexSrvDesc.Shader4ComponentMapping =
        D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    activeIndexSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    activeIndexSrvDesc.Buffer.FirstElement = 0;
    activeIndexSrvDesc.Buffer.NumElements = maxParticles_;
    activeIndexSrvDesc.Buffer.StructureByteStride = sizeof(uint32_t);
    activeIndexSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    device->CreateShaderResourceView(activeIndexResource_.Get(),
                                     &activeIndexSrvDesc,
                                     activeIndexSrvCpuHandle_);

    activeIndexUavIndex_ = srvManager_->Allocate();
    activeIndexUavCpuHandle_ = srvManager_->GetCpuHandle(activeIndexUavIndex_);
    activeIndexUavGpuHandle_ = srvManager_->GetGpuHandle(activeIndexUavIndex_);

    D3D12_UNORDERED_ACCESS_VIEW_DESC activeIndexUavDesc{};
    activeIndexUavDesc.Format = DXGI_FORMAT_UNKNOWN;
    activeIndexUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    activeIndexUavDesc.Buffer.FirstElement = 0;
    activeIndexUavDesc.Buffer.NumElements = maxParticles_;
    activeIndexUavDesc.Buffer.StructureByteStride = sizeof(uint32_t);
    activeIndexUavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    device->CreateUnorderedAccessView(activeIndexResource_.Get(), nullptr,
                                      &activeIndexUavDesc,
                                      activeIndexUavCpuHandle_);

    constexpr UINT counterBufferSize = 16;
    auto activeCountDesc = CD3DX12_RESOURCE_DESC::Buffer(
        counterBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ThrowIfFailed(device->CreateCommittedResource(
                      &defaultHeap, D3D12_HEAP_FLAG_NONE, &activeCountDesc,
                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                      IID_PPV_ARGS(&activeCountResource_)),
                  "CreateCommittedResource(GPUParticleActiveCount) failed");

    activeCountUavIndex_ = srvManager_->Allocate();
    activeCountUavCpuHandle_ = srvManager_->GetCpuHandle(activeCountUavIndex_);
    activeCountUavGpuHandle_ = srvManager_->GetGpuHandle(activeCountUavIndex_);

    D3D12_UNORDERED_ACCESS_VIEW_DESC rawUavDesc{};
    rawUavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    rawUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    rawUavDesc.Buffer.FirstElement = 0;
    rawUavDesc.Buffer.NumElements = counterBufferSize / sizeof(uint32_t);
    rawUavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    device->CreateUnorderedAccessView(activeCountResource_.Get(), nullptr,
                                      &rawUavDesc, activeCountUavCpuHandle_);

    const UINT drawArgsBufferSize = sizeof(D3D12_DRAW_ARGUMENTS);
    auto drawArgsDesc = CD3DX12_RESOURCE_DESC::Buffer(
        drawArgsBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ThrowIfFailed(device->CreateCommittedResource(
                      &defaultHeap, D3D12_HEAP_FLAG_NONE, &drawArgsDesc,
                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                      IID_PPV_ARGS(&drawArgsResource_)),
                  "CreateCommittedResource(GPUParticleDrawArgs) failed");
    drawArgsState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    drawArgsUavIndex_ = srvManager_->Allocate();
    drawArgsUavCpuHandle_ = srvManager_->GetCpuHandle(drawArgsUavIndex_);
    drawArgsUavGpuHandle_ = srvManager_->GetGpuHandle(drawArgsUavIndex_);

    rawUavDesc.Buffer.NumElements = drawArgsBufferSize / sizeof(uint32_t);
    device->CreateUnorderedAccessView(drawArgsResource_.Get(), nullptr,
                                      &rawUavDesc, drawArgsUavCpuHandle_);
}

void GPUParticleSystem::CreateConstantBuffers() {
    auto *device = dxCommon_->GetDevice();
    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);

    auto updateDesc = CD3DX12_RESOURCE_DESC::Buffer(
        Align256(sizeof(UpdateConstantBufferData)));
    ThrowIfFailed(device->CreateCommittedResource(
                      &uploadHeap, D3D12_HEAP_FLAG_NONE, &updateDesc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&updateConstantBuffer_)),
                  "CreateCommittedResource(GPUParticleUpdateCB) failed");
    ThrowIfFailed(updateConstantBuffer_->Map(
                      0, nullptr, reinterpret_cast<void **>(&mappedUpdateCB_)),
                  "GPUParticleUpdateCB Map failed");

    auto emitterDesc =
        CD3DX12_RESOURCE_DESC::Buffer(Align256(sizeof(EmitterForGPU)));
    ThrowIfFailed(device->CreateCommittedResource(
                      &uploadHeap, D3D12_HEAP_FLAG_NONE, &emitterDesc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&emitterConstantBuffer_)),
                  "CreateCommittedResource(GPUParticleEmitterCB) failed");
    ThrowIfFailed(emitterConstantBuffer_->Map(
                      0, nullptr, reinterpret_cast<void **>(&mappedEmitterCB_)),
                  "GPUParticleEmitterCB Map failed");

    auto drawDesc =
        CD3DX12_RESOURCE_DESC::Buffer(Align256(sizeof(DrawConstantBufferData)));
    ThrowIfFailed(device->CreateCommittedResource(
                      &uploadHeap, D3D12_HEAP_FLAG_NONE, &drawDesc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&drawConstantBuffer_)),
                  "CreateCommittedResource(GPUParticleDrawCB) failed");
    ThrowIfFailed(drawConstantBuffer_->Map(
                      0, nullptr, reinterpret_cast<void **>(&mappedDrawCB_)),
                  "GPUParticleDrawCB Map failed");
}

void GPUParticleSystem::ReleaseResources() {
    if (srvManager_) {
        if (particleSrvIndex_ != UINT32_MAX) {
            srvManager_->Free(particleSrvIndex_);
            particleSrvIndex_ = UINT32_MAX;
        }
        if (particleUavIndex_ != UINT32_MAX) {
            srvManager_->Free(particleUavIndex_);
            particleUavIndex_ = UINT32_MAX;
        }
        if (freeListUavIndex_ != UINT32_MAX) {
            srvManager_->Free(freeListUavIndex_);
            freeListUavIndex_ = UINT32_MAX;
        }
        if (freeListIndexUavIndex_ != UINT32_MAX) {
            srvManager_->Free(freeListIndexUavIndex_);
            freeListIndexUavIndex_ = UINT32_MAX;
        }
        if (activeIndexSrvIndex_ != UINT32_MAX) {
            srvManager_->Free(activeIndexSrvIndex_);
            activeIndexSrvIndex_ = UINT32_MAX;
        }
        if (activeIndexUavIndex_ != UINT32_MAX) {
            srvManager_->Free(activeIndexUavIndex_);
            activeIndexUavIndex_ = UINT32_MAX;
        }
        if (activeCountUavIndex_ != UINT32_MAX) {
            srvManager_->Free(activeCountUavIndex_);
            activeCountUavIndex_ = UINT32_MAX;
        }
        if (drawArgsUavIndex_ != UINT32_MAX) {
            srvManager_->Free(drawArgsUavIndex_);
            drawArgsUavIndex_ = UINT32_MAX;
        }
    }

    if (updateConstantBuffer_ && mappedUpdateCB_) {
        updateConstantBuffer_->Unmap(0, nullptr);
        mappedUpdateCB_ = nullptr;
    }
    if (emitterConstantBuffer_ && mappedEmitterCB_) {
        emitterConstantBuffer_->Unmap(0, nullptr);
        mappedEmitterCB_ = nullptr;
    }
    if (drawConstantBuffer_ && mappedDrawCB_) {
        drawConstantBuffer_->Unmap(0, nullptr);
        mappedDrawCB_ = nullptr;
    }

    updateConstantBuffer_.Reset();
    emitterConstantBuffer_.Reset();
    drawConstantBuffer_.Reset();
    particleResource_.Reset();
    particleUploadResource_.Reset();
    freeListResource_.Reset();
    freeListUploadResource_.Reset();
    freeListIndexResource_.Reset();
    freeListIndexUploadResource_.Reset();
    activeIndexResource_.Reset();
    activeCountResource_.Reset();
    drawArgsResource_.Reset();
    updatePSO_.Reset();
    argsPSO_.Reset();
    drawPSO_.Reset();
    updateRootSignature_.Reset();
    argsRootSignature_.Reset();
    drawRootSignature_.Reset();
    drawCommandSignature_.Reset();
    activeIndexState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    drawArgsState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    updatePending_ = false;
    activeTimeRemaining_ = 0.0f;
}

