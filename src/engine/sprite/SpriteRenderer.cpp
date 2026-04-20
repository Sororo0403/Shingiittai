#include "SpriteRenderer.h"
#include "DirectXCommon.h"
#include "DxHelpers.h"
#include "DxUtils.h"
#include "ShaderCompiler.h"
#include "Sprite.h"
#include "SrvManager.h"
#include "TextureManager.h"

using namespace DirectX;
using namespace DxUtils;

struct SpriteVertex {
    XMFLOAT3 pos;
    XMFLOAT2 uv;
    XMFLOAT4 color;
};

struct SpriteConstBuffer {
    XMFLOAT4X4 mat;
};

void SpriteRenderer::Initialize(DirectXCommon *dxCommon,
                                TextureManager *textureManager,
                                SrvManager *srvManager, int width, int height) {
    dxCommon_ = dxCommon;
    textureManager_ = textureManager;
    srvManager_ = srvManager;

    CreateRootSignature();
    CreatePipelineState();
    CreateVertexBuffer();
    CreateConstantBuffer();
    UpdateProjection(width, height);
}

void SpriteRenderer::Draw(const Sprite &sprite) {
    auto cmd = dxCommon_->GetCommandList();
    float l = sprite.position.x;
    float t = sprite.position.y;
    float r = sprite.position.x + sprite.size.x;
    float b = sprite.position.y + sprite.size.y;

    auto drawPass = [&](PipelineKind pipelineKind, const XMFLOAT4 &color) {
        if (activePipelineKind_ != pipelineKind) {
            activePipelineKind_ = pipelineKind;
            cmd->SetPipelineState(
                pipelineStates_[static_cast<uint32_t>(activePipelineKind_)]
                    .Get());
        }

        SpriteVertex vertices[6] = {
            {{l, t, 0.0f}, {0.0f, 0.0f}, color},
            {{r, t, 0.0f}, {1.0f, 0.0f}, color},
            {{l, b, 0.0f}, {0.0f, 1.0f}, color},

            {{l, b, 0.0f}, {0.0f, 1.0f}, color},
            {{r, t, 0.0f}, {1.0f, 0.0f}, color},
            {{r, b, 0.0f}, {1.0f, 1.0f}, color},
        };

        SpriteVertex *mapped = nullptr;
        vertexBuffer_->Map(0, nullptr, reinterpret_cast<void **>(&mapped));
        memcpy(mapped, vertices, sizeof(vertices));
        vertexBuffer_->Unmap(0, nullptr);

        cmd->SetGraphicsRootDescriptorTable(
            1, textureManager_->GetGpuHandle(sprite.textureId));
        cmd->DrawInstanced(6, 1, 0, 0);
    };

    switch (sprite.blendMode) {
    case SpriteBlendMode::Modulate:
        drawPass(PipelineKind::Modulate, sprite.color);
        break;
    case SpriteBlendMode::DarkSmoke: {
        // DarkSmoke is a composite: first darken the background, then add
        // a softer tinted haze on top so the cloud remains visible.
        const XMFLOAT4 darkenColor = {sprite.color.x * 0.60f,
                                      sprite.color.y * 0.60f,
                                      sprite.color.z * 0.60f,
                                      sprite.color.w * 1.10f};
        const XMFLOAT4 tintColor = {sprite.color.x,
                                    sprite.color.y,
                                    sprite.color.z,
                                    sprite.color.w * 0.64f};
        drawPass(PipelineKind::Modulate, darkenColor);
        drawPass(PipelineKind::Alpha, tintColor);
        break;
    }
    case SpriteBlendMode::Alpha:
    default:
        drawPass(PipelineKind::Alpha, sprite.color);
        break;
    }
}

void SpriteRenderer::PreDraw() {
    auto cmd = dxCommon_->GetCommandList();

    ID3D12DescriptorHeap *heaps[] = {srvManager_->GetHeap()};
    cmd->SetDescriptorHeaps(1, heaps);

    activePipelineKind_ = PipelineKind::Alpha;
    cmd->SetPipelineState(
        pipelineStates_[static_cast<uint32_t>(activePipelineKind_)].Get());
    cmd->SetGraphicsRootSignature(rootSignature_.Get());

    cmd->SetGraphicsRootConstantBufferView(
        0, constBuffer_->GetGPUVirtualAddress());

    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->IASetVertexBuffers(0, 1, &vbView_);
}

void SpriteRenderer::PostDraw() {}

void SpriteRenderer::CreateVertexBuffer() {
    UINT size = sizeof(SpriteVertex) * 6;

    CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);
    auto desc = CD3DX12_RESOURCE_DESC::Buffer(size);

    ThrowIfFailed(dxCommon_->GetDevice()->CreateCommittedResource(
                      &heap, D3D12_HEAP_FLAG_NONE, &desc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&vertexBuffer_)),
                  "Create sprite VB failed");

    vbView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
    vbView_.SizeInBytes = size;
    vbView_.StrideInBytes = sizeof(SpriteVertex);
}

void SpriteRenderer::CreateConstantBuffer() {
    UINT size = Align256(sizeof(SpriteConstBuffer));

    CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);
    auto desc = CD3DX12_RESOURCE_DESC::Buffer(size);

    ThrowIfFailed(dxCommon_->GetDevice()->CreateCommittedResource(
                      &heap, D3D12_HEAP_FLAG_NONE, &desc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&constBuffer_)),
                  "Create sprite CB failed");

    SpriteConstBuffer *mapped = nullptr;
    constBuffer_->Map(0, nullptr, reinterpret_cast<void **>(&mapped));
    mapped->mat = matProjection_;
    constBuffer_->Unmap(0, nullptr);
}

void SpriteRenderer::UpdateProjection(int width, int height) {
    XMMATRIX ortho = XMMatrixOrthographicOffCenterLH(
        0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 0.0f,
        1.0f);

    XMStoreFloat4x4(&matProjection_, XMMatrixTranspose(ortho));

    SpriteConstBuffer *mapped = nullptr;
    constBuffer_->Map(0, nullptr, reinterpret_cast<void **>(&mapped));
    mapped->mat = matProjection_;
    constBuffer_->Unmap(0, nullptr);
}

void SpriteRenderer::CreateRootSignature() {
    CD3DX12_ROOT_PARAMETER params[2]{};
    params[0].InitAsConstantBufferView(0);

    CD3DX12_DESCRIPTOR_RANGE range{};
    range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    params[1].InitAsDescriptorTable(1, &range);

    CD3DX12_STATIC_SAMPLER_DESC sampler{};
    sampler.Init(0);

    CD3DX12_ROOT_SIGNATURE_DESC desc{};
    desc.Init(_countof(params), params, 1, &sampler,
              D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    Microsoft::WRL::ComPtr<ID3DBlob> blob, error;
    ThrowIfFailed(D3D12SerializeRootSignature(
                      &desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error),
                  "SerializeRootSignature failed");

    ThrowIfFailed(dxCommon_->GetDevice()->CreateRootSignature(
                      0, blob->GetBufferPointer(), blob->GetBufferSize(),
                      IID_PPV_ARGS(&rootSignature_)),
                  "CreateRootSignature failed");
}

void SpriteRenderer::CreatePipelineState() {
    auto vs = ShaderCompiler::Compile(L"resources/shaders/sprite/SpriteVS.hlsl",
                                      "main", "vs_5_0");
    auto psAlpha =
        ShaderCompiler::Compile(L"resources/shaders/sprite/SpritePS.hlsl",
                                "main", "ps_5_0");
    auto psModulate =
        ShaderCompiler::Compile(L"resources/shaders/sprite/SpritePS.hlsl",
                                "mainModulate", "ps_5_0");
    auto psDarkSmoke =
        ShaderCompiler::Compile(L"resources/shaders/sprite/SpritePS.hlsl",
                                "mainDarkSmoke", "ps_5_0");

    D3D12_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 20,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = rootSignature_.Get();
    desc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    desc.InputLayout = {layout, _countof(layout)};
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    desc.SampleDesc.Count = 1;
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
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

    desc.PS = {psAlpha->GetBufferPointer(), psAlpha->GetBufferSize()};
    ThrowIfFailed(dxCommon_->GetDevice()->CreateGraphicsPipelineState(
                      &desc,
                      IID_PPV_ARGS(&pipelineStates_[static_cast<uint32_t>(
                          PipelineKind::Alpha)])),
                  "Create alpha sprite pipeline failed");

    rt.SrcBlend = D3D12_BLEND_ZERO;
    rt.DestBlend = D3D12_BLEND_SRC_COLOR;
    rt.SrcBlendAlpha = D3D12_BLEND_ZERO;
    rt.DestBlendAlpha = D3D12_BLEND_ONE;
    desc.BlendState = blend;
    desc.PS = {psModulate->GetBufferPointer(), psModulate->GetBufferSize()};
    ThrowIfFailed(dxCommon_->GetDevice()->CreateGraphicsPipelineState(
                      &desc,
                      IID_PPV_ARGS(&pipelineStates_[static_cast<uint32_t>(
                          PipelineKind::Modulate)])),
                  "Create modulate sprite pipeline failed");

    rt.SrcBlend = D3D12_BLEND_ONE;
    rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    rt.SrcBlendAlpha = D3D12_BLEND_ONE;
    rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    desc.BlendState = blend;
    desc.PS = {psDarkSmoke->GetBufferPointer(), psDarkSmoke->GetBufferSize()};
    ThrowIfFailed(dxCommon_->GetDevice()->CreateGraphicsPipelineState(
                      &desc,
                      IID_PPV_ARGS(&pipelineStates_[static_cast<uint32_t>(
                          PipelineKind::DarkSmoke)])),
                  "Create dark smoke sprite pipeline failed");
}
