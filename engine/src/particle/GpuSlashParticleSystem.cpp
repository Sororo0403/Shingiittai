#include "GpuSlashParticleSystem.h"

#include "Camera.h"
#include "DirectXCommon.h"
#include "DxHelpers.h"
#include "DxUtils.h"
#include "ShaderCompiler.h"
#include "SrvManager.h"
#include "TextureManager.h"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace DirectX;
using Microsoft::WRL::ComPtr;
using namespace DxUtils;

namespace {

constexpr uint32_t kMaxEmitRequestsPerFrame = 64;
constexpr uint32_t kThreadsPerGroup = 64;

uint32_t AlignUpU32(uint32_t v, uint32_t a) { return (v + a - 1u) / a * a; }

} // namespace

void GpuSlashParticleSystem::Initialize(DirectXCommon *dxCommon,
                                        SrvManager *srvManager,
                                        TextureManager *textureManager,
                                        uint32_t maxParticles) {
    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
    textureManager_ = textureManager;
    maxParticles_ = (std::max)(1u, maxParticles);

    time_ = 0.0f;
    gpuBuffersInitialized_ = false;
    particleBufferState_ = D3D12_RESOURCE_STATE_COPY_DEST;
    controlBufferState_ = D3D12_RESOURCE_STATE_COPY_DEST;

    pendingEmits_.clear();
    pendingEmits_.reserve(kMaxEmitRequestsPerFrame);

    CreateRootSignatures();
    CreatePipelineStates();
    CreateBuffers();
}

void GpuSlashParticleSystem::EmitSlashBurst(const XMFLOAT3 &start,
                                            const XMFLOAT3 &end, uint32_t count,
                                            float emitScale, bool isSweep) {
    if (count == 0) {
        return;
    }
    if (pendingEmits_.size() >= kMaxEmitRequestsPerFrame) {
        return;
    }

    EmitRequest req{};
    req.start = start;
    req.end = end;
    req.count = count;
    req.emitScale = emitScale;
    req.isSweep = isSweep ? 1u : 0u;
    pendingEmits_.push_back(req);
}

void GpuSlashParticleSystem::Render(const Camera &camera, float deltaTime) {
    if (dxCommon_ == nullptr) {
        pendingEmits_.clear();
        return;
    }

    auto *cmd = dxCommon_->GetCommandList();
    if (cmd == nullptr) {
        pendingEmits_.clear();
        return;
    }

    EnsureGpuBuffersInitialized(cmd);

    time_ += deltaTime;

    UpdateCameraCB(camera);
    UploadEmitRequests();
    DispatchEmitAndUpdate(deltaTime);
    DrawParticles(camera);

    pendingEmits_.clear();
}

void GpuSlashParticleSystem::CreateRootSignatures() {
    auto *device = dxCommon_->GetDevice();

    {
        CD3DX12_ROOT_PARAMETER params[4]{};
        params[0].InitAsConstantBufferView(0);  // b0 SimCB
        params[1].InitAsShaderResourceView(0);  // t0 EmitRequestBuffer
        params[2].InitAsUnorderedAccessView(0); // u0 ParticleBuffer
        params[3].InitAsUnorderedAccessView(1); // u1 ControlBuffer

        CD3DX12_ROOT_SIGNATURE_DESC desc{};
        desc.Init(_countof(params), params, 0, nullptr);

        ComPtr<ID3DBlob> blob;
        ComPtr<ID3DBlob> error;
        ThrowIfFailed(D3D12SerializeRootSignature(
                          &desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error),
                      "D3D12SerializeRootSignature(GpuSlash Emit) failed");
        ThrowIfFailed(device->CreateRootSignature(
                          0, blob->GetBufferPointer(), blob->GetBufferSize(),
                          IID_PPV_ARGS(&emitRootSignature_)),
                      "CreateRootSignature(GpuSlash Emit) failed");
    }

    {
        CD3DX12_ROOT_PARAMETER params[3]{};
        params[0].InitAsConstantBufferView(0);  // b0 SimCB
        params[1].InitAsUnorderedAccessView(0); // u0 ParticleBuffer
        params[2].InitAsUnorderedAccessView(1); // u1 ControlBuffer

        CD3DX12_ROOT_SIGNATURE_DESC desc{};
        desc.Init(_countof(params), params, 0, nullptr);

        ComPtr<ID3DBlob> blob;
        ComPtr<ID3DBlob> error;
        ThrowIfFailed(D3D12SerializeRootSignature(
                          &desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error),
                      "D3D12SerializeRootSignature(GpuSlash Update) failed");
        ThrowIfFailed(device->CreateRootSignature(
                          0, blob->GetBufferPointer(), blob->GetBufferSize(),
                          IID_PPV_ARGS(&updateRootSignature_)),
                      "CreateRootSignature(GpuSlash Update) failed");
    }

    {
        CD3DX12_ROOT_PARAMETER params[2]{};
        params[0].InitAsConstantBufferView(0); // b0 CameraCB
        params[1].InitAsShaderResourceView(0); // t0 ParticleBuffer

        CD3DX12_STATIC_SAMPLER_DESC sampler(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);

        CD3DX12_ROOT_SIGNATURE_DESC desc{};
        desc.Init(_countof(params), params, 1, &sampler,
                  D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> blob;
        ComPtr<ID3DBlob> error;
        ThrowIfFailed(D3D12SerializeRootSignature(
                          &desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error),
                      "D3D12SerializeRootSignature(GpuSlash Draw) failed");
        ThrowIfFailed(device->CreateRootSignature(
                          0, blob->GetBufferPointer(), blob->GetBufferSize(),
                          IID_PPV_ARGS(&drawRootSignature_)),
                      "CreateRootSignature(GpuSlash Draw) failed");
    }
}

void GpuSlashParticleSystem::CreatePipelineStates() {
    auto *device = dxCommon_->GetDevice();

    {
        auto cs =
            CompileShader(L"engine/resources/shaders/slash/SlashParticleEmit.CS.hlsl",
                          "main", "cs_5_0");

        D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = emitRootSignature_.Get();
        desc.CS = {cs->GetBufferPointer(), cs->GetBufferSize()};

        ThrowIfFailed(
            device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&emitPSO_)),
            "CreateComputePipelineState(Slash Emit) failed");
    }

    {
        auto cs = CompileShader(
            L"engine/resources/shaders/slash/SlashParticleUpdate.CS.hlsl", "main",
            "cs_5_0");

        D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = updateRootSignature_.Get();
        desc.CS = {cs->GetBufferPointer(), cs->GetBufferSize()};

        ThrowIfFailed(device->CreateComputePipelineState(
                          &desc, IID_PPV_ARGS(&updatePSO_)),
                      "CreateComputePipelineState(Slash Update) failed");
    }

    {
        auto vs =
            CompileShader(L"engine/resources/shaders/slash/SlashParticleDraw.VS.hlsl",
                          "main", "vs_5_0");
        auto ps =
            CompileShader(L"engine/resources/shaders/slash/SlashParticleDraw.PS.hlsl",
                          "main", "ps_5_0");

        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = drawRootSignature_.Get();
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
        blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blend.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
        blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blend.RenderTarget[0].RenderTargetWriteMask =
            D3D12_COLOR_WRITE_ENABLE_ALL;
        desc.BlendState = blend;

        D3D12_DEPTH_STENCIL_DESC depth =
            CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        depth.DepthEnable = FALSE;
        depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        desc.DepthStencilState = depth;

        ThrowIfFailed(
            device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&drawPSO_)),
            "CreateGraphicsPipelineState(Slash Draw) failed");
    }
}

void GpuSlashParticleSystem::CreateBuffers() {
    const uint64_t particleBufferSize =
        sizeof(ParticleGpu) * static_cast<uint64_t>(maxParticles_);
    const uint64_t controlBufferSize = sizeof(ControlData);
    const uint64_t emitBufferSize =
        sizeof(EmitRequest) * static_cast<uint64_t>(kMaxEmitRequestsPerFrame);

    // UAV 用 Default バッファは COPY_DEST で作る。
    // Initialize() 中では command list を触らず、最初の Render
    // で安全に初期化する。
    particleBuffer_ =
        CreateBufferResource(particleBufferSize, D3D12_HEAP_TYPE_DEFAULT,
                             D3D12_RESOURCE_STATE_COPY_DEST, true);
    controlBuffer_ =
        CreateBufferResource(controlBufferSize, D3D12_HEAP_TYPE_DEFAULT,
                             D3D12_RESOURCE_STATE_COPY_DEST, true);

    particleInitUpload_ =
        CreateBufferResource(particleBufferSize, D3D12_HEAP_TYPE_UPLOAD,
                             D3D12_RESOURCE_STATE_GENERIC_READ, false);
    controlInitUpload_ =
        CreateBufferResource(controlBufferSize, D3D12_HEAP_TYPE_UPLOAD,
                             D3D12_RESOURCE_STATE_GENERIC_READ, false);

    emitRequestBuffer_ =
        CreateBufferResource(emitBufferSize, D3D12_HEAP_TYPE_UPLOAD,
                             D3D12_RESOURCE_STATE_GENERIC_READ, false);

    cameraCB_ =
        CreateBufferResource(Align256(sizeof(CameraCB)), D3D12_HEAP_TYPE_UPLOAD,
                             D3D12_RESOURCE_STATE_GENERIC_READ, false);
    simCB_ =
        CreateBufferResource(Align256(sizeof(SimCB)), D3D12_HEAP_TYPE_UPLOAD,
                             D3D12_RESOURCE_STATE_GENERIC_READ, false);

    ThrowIfFailed(
        cameraCB_->Map(0, nullptr, reinterpret_cast<void **>(&mappedCameraCB_)),
        "Map(CameraCB) failed");
    ThrowIfFailed(
        simCB_->Map(0, nullptr, reinterpret_cast<void **>(&mappedSimCB_)),
        "Map(SimCB) failed");
    ThrowIfFailed(
        emitRequestBuffer_->Map(
            0, nullptr, reinterpret_cast<void **>(&mappedEmitRequests_)),
        "Map(EmitRequestBuffer) failed");

    std::memset(mappedCameraCB_, 0, sizeof(CameraCB));
    std::memset(mappedSimCB_, 0, sizeof(SimCB));
    std::memset(
        mappedEmitRequests_, 0,
        static_cast<size_t>(sizeof(EmitRequest) * kMaxEmitRequestsPerFrame));

    // GPU初期化用アップロード領域をゼロ埋め
    {
        void *mapped = nullptr;
        ThrowIfFailed(particleInitUpload_->Map(
                          0, nullptr, reinterpret_cast<void **>(&mapped)),
                      "Map(Particle Init Upload) failed");
        std::memset(mapped, 0, static_cast<size_t>(particleBufferSize));
        particleInitUpload_->Unmap(0, nullptr);
    }

    {
        ControlData *mapped = nullptr;
        ThrowIfFailed(controlInitUpload_->Map(
                          0, nullptr, reinterpret_cast<void **>(&mapped)),
                      "Map(Control Init Upload) failed");
        mapped->head = 0;
        mapped->pad0 = 0;
        mapped->pad1 = 0;
        mapped->pad2 = 0;
        controlInitUpload_->Unmap(0, nullptr);
    }
}

void GpuSlashParticleSystem::EnsureGpuBuffersInitialized(
    ID3D12GraphicsCommandList *cmd) {
    if (gpuBuffersInitialized_) {
        return;
    }

    // COPY_DEST のままゼロ初期化データを転送
    cmd->CopyBufferRegion(particleBuffer_.Get(), 0, particleInitUpload_.Get(),
                          0, particleBuffer_->GetDesc().Width);
    cmd->CopyBufferRegion(controlBuffer_.Get(), 0, controlInitUpload_.Get(), 0,
                          sizeof(ControlData));

    // 以後 UAV として使う
    Transition(cmd, particleBuffer_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(cmd, controlBuffer_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    particleBufferState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    controlBufferState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    gpuBuffersInitialized_ = true;
}

void GpuSlashParticleSystem::UpdateCameraCB(const Camera &camera) {
    const XMMATRIX view = camera.GetView();
    const XMMATRIX proj = camera.GetProj();
    const XMMATRIX viewProj = XMMatrixMultiply(view, proj);
    const XMMATRIX invView = XMMatrixInverse(nullptr, view);

    XMStoreFloat4x4(&mappedCameraCB_->viewProj, XMMatrixTranspose(viewProj));
    XMStoreFloat4x4(&mappedCameraCB_->invView, XMMatrixTranspose(invView));
}

void GpuSlashParticleSystem::UploadEmitRequests() {
    const size_t count = (std::min)(
        pendingEmits_.size(), static_cast<size_t>(kMaxEmitRequestsPerFrame));

    if (count > 0) {
        std::memcpy(mappedEmitRequests_, pendingEmits_.data(),
                    sizeof(EmitRequest) * count);
    }
    if (count < kMaxEmitRequestsPerFrame) {
        std::memset(mappedEmitRequests_ + count, 0,
                    sizeof(EmitRequest) * (kMaxEmitRequestsPerFrame - count));
    }
}

void GpuSlashParticleSystem::DispatchEmitAndUpdate(float deltaTime) {
    auto *cmd = dxCommon_->GetCommandList();

    mappedSimCB_->deltaTime = deltaTime;
    mappedSimCB_->time = time_;
    mappedSimCB_->emitRequestCount = static_cast<uint32_t>((std::min)(
        pendingEmits_.size(), static_cast<size_t>(kMaxEmitRequestsPerFrame)));
    mappedSimCB_->maxParticles = maxParticles_;

    if (particleBufferState_ != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        Transition(cmd, particleBuffer_.Get(), particleBufferState_,
                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        particleBufferState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    if (controlBufferState_ != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        Transition(cmd, controlBuffer_.Get(), controlBufferState_,
                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        controlBufferState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    if (mappedSimCB_->emitRequestCount > 0) {
        cmd->SetPipelineState(emitPSO_.Get());
        cmd->SetComputeRootSignature(emitRootSignature_.Get());
        cmd->SetComputeRootConstantBufferView(0,
                                              simCB_->GetGPUVirtualAddress());
        cmd->SetComputeRootShaderResourceView(
            1, emitRequestBuffer_->GetGPUVirtualAddress());
        cmd->SetComputeRootUnorderedAccessView(
            2, particleBuffer_->GetGPUVirtualAddress());
        cmd->SetComputeRootUnorderedAccessView(
            3, controlBuffer_->GetGPUVirtualAddress());

        cmd->Dispatch(mappedSimCB_->emitRequestCount, 1, 1);

        D3D12_RESOURCE_BARRIER uavBarriers[2] = {
            CD3DX12_RESOURCE_BARRIER::UAV(particleBuffer_.Get()),
            CD3DX12_RESOURCE_BARRIER::UAV(controlBuffer_.Get()),
        };
        cmd->ResourceBarrier(2, uavBarriers);
    }

    cmd->SetPipelineState(updatePSO_.Get());
    cmd->SetComputeRootSignature(updateRootSignature_.Get());
    cmd->SetComputeRootConstantBufferView(0, simCB_->GetGPUVirtualAddress());
    cmd->SetComputeRootUnorderedAccessView(
        1, particleBuffer_->GetGPUVirtualAddress());
    cmd->SetComputeRootUnorderedAccessView(
        2, controlBuffer_->GetGPUVirtualAddress());

    const uint32_t groups = (std::max)(
        1u, AlignUpU32(maxParticles_, kThreadsPerGroup) / kThreadsPerGroup);
    cmd->Dispatch(groups, 1, 1);

    D3D12_RESOURCE_BARRIER uavBarriers[2] = {
        CD3DX12_RESOURCE_BARRIER::UAV(particleBuffer_.Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(controlBuffer_.Get()),
    };
    cmd->ResourceBarrier(2, uavBarriers);
}

void GpuSlashParticleSystem::DrawParticles(const Camera &camera) {
    (void)camera;

    auto *cmd = dxCommon_->GetCommandList();

    const D3D12_RESOURCE_STATES drawState =
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    if (particleBufferState_ != drawState) {
        Transition(cmd, particleBuffer_.Get(), particleBufferState_, drawState);
        particleBufferState_ = drawState;
    }

    if (srvManager_ != nullptr) {
        ID3D12DescriptorHeap *heaps[] = {srvManager_->GetHeap()};
        cmd->SetDescriptorHeaps(1, heaps);
    }

    cmd->SetPipelineState(drawPSO_.Get());
    cmd->SetGraphicsRootSignature(drawRootSignature_.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    cmd->SetGraphicsRootConstantBufferView(0,
                                           cameraCB_->GetGPUVirtualAddress());
    cmd->SetGraphicsRootShaderResourceView(
        1, particleBuffer_->GetGPUVirtualAddress());

    // 1インスタンス = 1粒子
    cmd->DrawInstanced(4, maxParticles_, 0, 0);
}

ComPtr<ID3DBlob> GpuSlashParticleSystem::CompileShader(const wchar_t *path,
                                                       const char *entry,
                                                       const char *target) {
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ComPtr<ID3DBlob> shaderBlob;
    ComPtr<ID3DBlob> errorBlob;

    const HRESULT hr =
        D3DCompileFromFile(path, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                           entry, target, flags, 0, &shaderBlob, &errorBlob);

    if (FAILED(hr)) {
        if (errorBlob != nullptr) {
            OutputDebugStringA(
                static_cast<const char *>(errorBlob->GetBufferPointer()));
        }
        ThrowIfFailed(hr, "D3DCompileFromFile(GpuSlashParticleSystem) failed");
    }

    return shaderBlob;
}

ComPtr<ID3D12Resource> GpuSlashParticleSystem::CreateBufferResource(
    uint64_t size, D3D12_HEAP_TYPE heapType, D3D12_RESOURCE_STATES initialState,
    bool allowUav) {
    CD3DX12_HEAP_PROPERTIES heapProps(heapType);

    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
    if (allowUav) {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }

    auto desc = CD3DX12_RESOURCE_DESC::Buffer(size, flags);

    ComPtr<ID3D12Resource> resource;
    ThrowIfFailed(dxCommon_->GetDevice()->CreateCommittedResource(
                      &heapProps, D3D12_HEAP_FLAG_NONE, &desc, initialState,
                      nullptr, IID_PPV_ARGS(&resource)),
                  "CreateCommittedResource(GpuSlash Buffer) failed");
    return resource;
}

void GpuSlashParticleSystem::Transition(ID3D12GraphicsCommandList *cmd,
                                        ID3D12Resource *resource,
                                        D3D12_RESOURCE_STATES before,
                                        D3D12_RESOURCE_STATES after) {
    if (before == after) {
        return;
    }

    D3D12_RESOURCE_BARRIER barrier =
        CD3DX12_RESOURCE_BARRIER::Transition(resource, before, after);
    cmd->ResourceBarrier(1, &barrier);
}