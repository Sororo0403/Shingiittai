#include "SwordTrailRenderer.h"

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

float Clamp01(float v) { return (std::max)(0.0f, (std::min)(1.0f, v)); }

} // namespace

void SwordTrailRenderer::Initialize(DirectXCommon *dxCommon,
                                    SrvManager *srvManager,
                                    TextureManager *textureManager,
                                    uint32_t maxPoints) {
    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
    textureManager_ = textureManager;
    maxPoints_ = (std::max)(2u, maxPoints);

    points_.reserve(maxPoints_);
    vertices_.reserve(maxPoints_ * 2);

    CreateRootSignature();
    CreatePipelineState();
    CreateBuffers();
}

void SwordTrailRenderer::Reset() {
    points_.clear();
    vertices_.clear();
}

void SwordTrailRenderer::BeginFrame(float deltaTime) {
    deltaTime_ = deltaTime;
    time_ += deltaTime;

    for (auto &p : points_) {
        p.age += deltaTime;
    }

    points_.erase(std::remove_if(points_.begin(), points_.end(),
                                 [this](const TrailPoint &p) {
                                     return p.age >= lifeTime_;
                                 }),
                  points_.end());
}

void SwordTrailRenderer::AddPoint(const XMFLOAT3 &baseWorld,
                                  const XMFLOAT3 &tipWorld, float width) {
    if (!enabled_) {
        return;
    }

    if (!points_.empty()) {
        const TrailPoint &last = points_.back();
        const float dx = last.tip.x - tipWorld.x;
        const float dy = last.tip.y - tipWorld.y;
        const float dz = last.tip.z - tipWorld.z;
        const float distSq = dx * dx + dy * dy + dz * dz;

        // 微小移動は追加しない
        if (distSq < 0.00005f) {
            return;
        }
    }

    TrailPoint p{};
    p.base = baseWorld;
    p.tip = tipWorld;
    p.width = width;
    p.age = 0.0f;

    points_.push_back(p);
    if (points_.size() > maxPoints_) {
        points_.erase(points_.begin());
    }
}

void SwordTrailRenderer::EndFrame() {
    RebuildVertices();
    UploadVertices();
}

void SwordTrailRenderer::Draw(const Camera &camera) {
    if (!enabled_) {
        return;
    }
    if (vertices_.size() < 4) {
        return;
    }

    const XMMATRIX view = camera.GetView();
    const XMMATRIX proj = camera.GetProj();
    const XMMATRIX viewProj = XMMatrixMultiply(view, proj);
    XMStoreFloat4x4(&mappedCB_->viewProj, XMMatrixTranspose(viewProj));

    mappedCB_->time = time_;

    auto *cmd = dxCommon_->GetCommandList();

    if (srvManager_ != nullptr) {
        ID3D12DescriptorHeap *heaps[] = {srvManager_->GetHeap()};
        cmd->SetDescriptorHeaps(1, heaps);
    }

    cmd->SetPipelineState(pipelineState_.Get());
    cmd->SetGraphicsRootSignature(rootSignature_.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    cmd->IASetVertexBuffers(0, 1, &vbView_);
    cmd->SetGraphicsRootConstantBufferView(0,
                                           materialCB_->GetGPUVirtualAddress());
    cmd->DrawInstanced(static_cast<UINT>(vertices_.size()), 1, 0, 0);
}

void SwordTrailRenderer::CreateRootSignature() {
    CD3DX12_ROOT_PARAMETER params[1]{};
    params[0].InitAsConstantBufferView(0);

    CD3DX12_STATIC_SAMPLER_DESC sampler(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);

    CD3DX12_ROOT_SIGNATURE_DESC desc{};
    desc.Init(_countof(params), params, 1, &sampler,
              D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> error;

    ThrowIfFailed(D3D12SerializeRootSignature(
                      &desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error),
                  "D3D12SerializeRootSignature(SwordTrail) failed");

    ThrowIfFailed(dxCommon_->GetDevice()->CreateRootSignature(
                      0, blob->GetBufferPointer(), blob->GetBufferSize(),
                      IID_PPV_ARGS(&rootSignature_)),
                  "CreateRootSignature(SwordTrail) failed");
}

void SwordTrailRenderer::CreatePipelineState() {
    auto *device = dxCommon_->GetDevice();

    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        {
            "POSITION",
            0,
            DXGI_FORMAT_R32G32B32_FLOAT,
            0,
            D3D12_APPEND_ALIGNED_ELEMENT,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            0,
        },
        {
            "TEXCOORD",
            0,
            DXGI_FORMAT_R32_FLOAT,
            0,
            D3D12_APPEND_ALIGNED_ELEMENT,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            0,
        },
        {
            "TEXCOORD",
            1,
            DXGI_FORMAT_R32_FLOAT,
            0,
            D3D12_APPEND_ALIGNED_ELEMENT,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            0,
        },
        {
            "TEXCOORD",
            2,
            DXGI_FORMAT_R32_FLOAT,
            0,
            D3D12_APPEND_ALIGNED_ELEMENT,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            0,
        },
        {
            "TEXCOORD",
            3,
            DXGI_FORMAT_R32_FLOAT,
            0,
            D3D12_APPEND_ALIGNED_ELEMENT,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            0,
        },
    };

    auto vs = ShaderCompiler::Compile(
        L"resources/shaders/trail/SwordTrail.VS.hlsl", "main", "vs_5_0");
    auto ps = ShaderCompiler::Compile(
        L"resources/shaders/trail/SwordTrail.PS.hlsl", "main", "ps_5_0");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = rootSignature_.Get();
    desc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    desc.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    desc.InputLayout = {inputLayout, _countof(inputLayout)};
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
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.BlendState = blend;

    D3D12_DEPTH_STENCIL_DESC depth = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    depth.DepthEnable = FALSE;
    depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    desc.DepthStencilState = depth;

    ThrowIfFailed(device->CreateGraphicsPipelineState(
                      &desc, IID_PPV_ARGS(&pipelineState_)),
                  "CreateGraphicsPipelineState(SwordTrail) failed");
}

void SwordTrailRenderer::CreateBuffers() {
    const UINT vbSize = Align256(sizeof(Vertex) * maxPoints_ * 2);
    const UINT cbSize = Align256(sizeof(MaterialCB));

    {
        CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
        auto desc = CD3DX12_RESOURCE_DESC::Buffer(vbSize);

        ThrowIfFailed(dxCommon_->GetDevice()->CreateCommittedResource(
                          &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                          D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                          IID_PPV_ARGS(&vertexBuffer_)),
                      "CreateCommittedResource(SwordTrail VB) failed");

        ThrowIfFailed(vertexBuffer_->Map(0, nullptr,
                                         reinterpret_cast<void **>(&mappedVB_)),
                      "Map(SwordTrail VB) failed");

        vbView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
        vbView_.SizeInBytes = vbSize;
        vbView_.StrideInBytes = sizeof(Vertex);
    }

    {
        CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
        auto desc = CD3DX12_RESOURCE_DESC::Buffer(cbSize);

        ThrowIfFailed(dxCommon_->GetDevice()->CreateCommittedResource(
                          &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                          D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                          IID_PPV_ARGS(&materialCB_)),
                      "CreateCommittedResource(SwordTrail CB) failed");

        ThrowIfFailed(
            materialCB_->Map(0, nullptr, reinterpret_cast<void **>(&mappedCB_)),
            "Map(SwordTrail CB) failed");

        *mappedCB_ = MaterialCB{};
    }
}

void SwordTrailRenderer::RebuildVertices() {
    vertices_.clear();

    if (points_.size() < 2) {
        return;
    }

    const size_t count = points_.size();
    for (size_t i = 0; i < count; ++i) {
        const TrailPoint &p = points_[i];

        const float t = (count <= 1) ? 0.0f
                                     : static_cast<float>(i) /
                                           static_cast<float>(count - 1);

        float life01 = 1.0f - Clamp01(p.age / (std::max)(lifeTime_, 0.0001f));
        float alpha = std::pow(life01, 1.35f);

        // 先頭と末尾を少し絞る
        float taperHead = 0.35f + 0.65f * std::sin(t * 3.14159265f);
        float taperTail = 1.0f - std::pow(t, 1.65f);
        float taper =
            (std::max)(0.12f, taperHead * (0.55f + 0.45f * taperTail));

        Vertex v0{};
        v0.position = p.base;
        v0.side = 0.0f;
        v0.t = t;
        v0.alpha = alpha;
        v0.width = p.width * taper;

        Vertex v1{};
        v1.position = p.tip;
        v1.side = 1.0f;
        v1.t = t;
        v1.alpha = alpha;
        v1.width = p.width * taper;

        vertices_.push_back(v0);
        vertices_.push_back(v1);
    }
}

void SwordTrailRenderer::UploadVertices() {
    if (mappedVB_ == nullptr) {
        return;
    }

    const size_t maxVertexCount = static_cast<size_t>(maxPoints_) * 2;
    std::memset(mappedVB_, 0, sizeof(Vertex) * maxVertexCount);

    if (!vertices_.empty()) {
        std::memcpy(mappedVB_, vertices_.data(),
                    sizeof(Vertex) * vertices_.size());
    }
}