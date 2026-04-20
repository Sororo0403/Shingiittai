#include "SlashEffectRenderer.h"
#include "Camera.h"
#include "DirectXCommon.h"
#include "DxHelpers.h"
#include "DxUtils.h"
#include "ShaderCompiler.h"
#include "SrvManager.h"
#include "TextureManager.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;
using namespace DxUtils;
using Microsoft::WRL::ComPtr;

namespace {

float Clamp01(float v) { return (std::max)(0.0f, (std::min)(1.0f, v)); }

XMFLOAT4 LerpColor(const XMFLOAT4 &a, const XMFLOAT4 &b, float t) {
    t = Clamp01(t);
    return {
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
        a.w + (b.w - a.w) * t,
    };
}

} // namespace

void SlashEffectRenderer::Initialize(DirectXCommon *dxCommon,
                                     SrvManager *srvManager,
                                     TextureManager *textureManager) {
    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
    textureManager_ = textureManager;

    CreateRootSignature();
    CreatePipelineState();
    CreateDarkPipelineState();
    CreateConstantBuffer();
}

void SlashEffectRenderer::DrawEnemySlash(const Camera &camera,
                                         const XMFLOAT3 &startWorld,
                                         const XMFLOAT3 &endWorld,
                                         float phaseAlpha, float actionTime,
                                         bool isSweep) {
    if (phaseAlpha <= 0.001f) {
        return;
    }

    XMFLOAT2 startUv{};
    XMFLOAT2 endUv{};
    if (!ProjectWorldToUv(camera, startWorld, startUv)) {
        return;
    }
    if (!ProjectWorldToUv(camera, endWorld, endUv)) {
        return;
    }

    float dx = endUv.x - startUv.x;
    float dy = endUv.y - startUv.y;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.0005f) {
        return;
    }

    // 方向・法線
    const float invLen = 1.0f / len;
    const float dirX = dx * invLen;
    const float dirY = dy * invLen;
    const float perpX = -dirY;
    const float perpY = dirX;

    // 今の見た目は「均一な帯」に寄りすぎているので、
    // 始点/終点を少しずらして非対称にする
    XMFLOAT2 mainStart = startUv;
    XMFLOAT2 mainEnd = endUv;

    if (isSweep) {
        // sweep は横薙ぎっぽく、少し長く・少し斜めに崩す
        mainStart.x -= dirX * 0.030f;
        mainStart.y -= dirY * 0.030f;
        mainEnd.x += dirX * 0.010f;
        mainEnd.y += dirY * 0.010f;

        mainStart.x -= perpX * 0.010f;
        mainStart.y -= perpY * 0.010f;
        mainEnd.x += perpX * 0.005f;
        mainEnd.y += perpY * 0.005f;
    } else {
        // smash は細く鋭く、少し前方へ抜ける感じ
        mainStart.x -= dirX * 0.015f;
        mainStart.y -= dirY * 0.015f;
        mainEnd.x += dirX * 0.025f;
        mainEnd.y += dirY * 0.025f;

        mainStart.x += perpX * 0.004f;
        mainStart.y += perpY * 0.004f;
        mainEnd.x -= perpX * 0.008f;
        mainEnd.y -= perpY * 0.008f;
    }

    // 予備動作の帯感を弱めて、Active で立つように少し非線形化
    const float shapedPhase =
        Clamp01(phaseAlpha * phaseAlpha * 0.85f + phaseAlpha * 0.25f);

    mappedCB_->startUv = mainStart;
    mappedCB_->endUv = mainEnd;
    mappedCB_->phaseAlpha = shapedPhase;
    mappedCB_->actionTime = actionTime;

    if (isSweep) {
        // かなり細くする。今は太すぎて煙っぽい
        mappedCB_->coreThickness = 0.0060f;
        mappedCB_->glowThickness = 0.0220f;

        mappedCB_->innerColor = {1.00f, 1.00f, 1.00f, 1.0f};
        mappedCB_->outerColor = {0.72f, 0.58f, 1.00f, 1.0f};
        mappedCB_->darkColor = {0.03f, 0.01f, 0.08f, 1.0f};

        // 先端を早めに細くする
        mappedCB_->tipFade = 0.82f;
        mappedCB_->noiseScale = 34.0f;
        mappedCB_->sweepFlag = 1.0f;
    } else {
        mappedCB_->coreThickness = 0.0050f;
        mappedCB_->glowThickness = 0.0180f;

        mappedCB_->innerColor = {1.00f, 1.00f, 1.00f, 1.0f};
        mappedCB_->outerColor = {0.55f, 0.78f, 1.00f, 1.0f};
        mappedCB_->darkColor = {0.02f, 0.03f, 0.08f, 1.0f};

        mappedCB_->tipFade = 0.88f;
        mappedCB_->noiseScale = 28.0f;
        mappedCB_->sweepFlag = 0.0f;
    }

    const float pulse = 0.5f + 0.5f * std::sinf(actionTime * 28.0f);
    mappedCB_->outerColor =
        LerpColor(mappedCB_->outerColor,
                  isSweep ? XMFLOAT4{0.96f, 0.68f, 1.00f, 1.0f}
                          : XMFLOAT4{0.68f, 0.90f, 1.00f, 1.0f},
                  0.12f * pulse);

    auto *cmd = dxCommon_->GetCommandList();

    ID3D12DescriptorHeap *heaps[] = {srvManager_->GetHeap()};
    cmd->SetDescriptorHeaps(1, heaps);

        cmd->SetGraphicsRootSignature(rootSignature_.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // =========================================================
    // Pass 1: 黒 streak
    // =========================================================
    mappedCB_->passMode = 1.0f;
    mappedCB_->streakOffset = isSweep ? 0.12f : 0.08f;
    mappedCB_->streakThicknessMul = isSweep ? 1.25f : 1.10f;
    mappedCB_->streakIntensity = isSweep ? 0.95f : 0.82f;

    cmd->SetPipelineState(darkPipelineState_.Get());
    cmd->SetGraphicsRootConstantBufferView(
        0, constBuffer_->GetGPUVirtualAddress());
    cmd->DrawInstanced(3, 1, 0, 0);

    // 薄い残像にも黒 streak を1枚
    {
        XMFLOAT2 ghostStart = mainStart;
        XMFLOAT2 ghostEnd = mainEnd;

        const float ghostBack = isSweep ? 0.020f : 0.014f;
        const float ghostSide = isSweep ? 0.010f : 0.006f;

        ghostStart.x -= dirX * ghostBack - perpX * ghostSide;
        ghostStart.y -= dirY * ghostBack - perpY * ghostSide;
        ghostEnd.x -= dirX * (ghostBack * 0.45f) + perpX * (ghostSide * 0.55f);
        ghostEnd.y -= dirY * (ghostBack * 0.45f) + perpY * (ghostSide * 0.55f);

        mappedCB_->startUv = ghostStart;
        mappedCB_->endUv = ghostEnd;
        mappedCB_->phaseAlpha = shapedPhase * (isSweep ? 0.22f : 0.18f);
        mappedCB_->passMode = 1.0f;
        mappedCB_->streakOffset = isSweep ? 0.18f : 0.12f;
        mappedCB_->streakThicknessMul = isSweep ? 1.35f : 1.18f;
        mappedCB_->streakIntensity = isSweep ? 0.55f : 0.46f;

        cmd->SetGraphicsRootConstantBufferView(
            0, constBuffer_->GetGPUVirtualAddress());
        cmd->DrawInstanced(3, 1, 0, 0);
    }

    // =========================================================
    // Pass 2: 発光本体
    // =========================================================
    mappedCB_->startUv = mainStart;
    mappedCB_->endUv = mainEnd;
    mappedCB_->phaseAlpha = shapedPhase;
    mappedCB_->passMode = 0.0f;

    if (isSweep) {
        mappedCB_->coreThickness = 0.0060f;
        mappedCB_->glowThickness = 0.0220f;
        mappedCB_->innerColor = {1.00f, 1.00f, 1.00f, 1.0f};
        mappedCB_->outerColor = {0.72f, 0.58f, 1.00f, 1.0f};
        mappedCB_->darkColor = {0.03f, 0.01f, 0.08f, 1.0f};
        mappedCB_->tipFade = 0.82f;
        mappedCB_->noiseScale = 34.0f;
        mappedCB_->sweepFlag = 1.0f;
    } else {
        mappedCB_->coreThickness = 0.0050f;
        mappedCB_->glowThickness = 0.0180f;
        mappedCB_->innerColor = {1.00f, 1.00f, 1.00f, 1.0f};
        mappedCB_->outerColor = {0.55f, 0.78f, 1.00f, 1.0f};
        mappedCB_->darkColor = {0.02f, 0.03f, 0.08f, 1.0f};
        mappedCB_->tipFade = 0.88f;
        mappedCB_->noiseScale = 28.0f;
        mappedCB_->sweepFlag = 0.0f;
    }

       mappedCB_->outerColor =
        LerpColor(mappedCB_->outerColor,
                  isSweep ? XMFLOAT4{0.96f, 0.68f, 1.00f, 1.0f}
                          : XMFLOAT4{0.68f, 0.90f, 1.00f, 1.0f},
                  0.12f * pulse);

    cmd->SetPipelineState(pipelineState_.Get());
    cmd->SetGraphicsRootConstantBufferView(
        0, constBuffer_->GetGPUVirtualAddress());
    cmd->DrawInstanced(3, 1, 0, 0);

    // 発光残像
    {
        XMFLOAT2 ghostStart = mainStart;
        XMFLOAT2 ghostEnd = mainEnd;

        const float ghostBack = isSweep ? 0.020f : 0.014f;
        const float ghostSide = isSweep ? 0.010f : 0.006f;

        ghostStart.x -= dirX * ghostBack - perpX * ghostSide;
        ghostStart.y -= dirY * ghostBack - perpY * ghostSide;
        ghostEnd.x -= dirX * (ghostBack * 0.45f) + perpX * (ghostSide * 0.55f);
        ghostEnd.y -= dirY * (ghostBack * 0.45f) + perpY * (ghostSide * 0.55f);

        mappedCB_->startUv = ghostStart;
        mappedCB_->endUv = ghostEnd;
        mappedCB_->phaseAlpha = shapedPhase * (isSweep ? 0.33f : 0.26f);
        mappedCB_->passMode = 0.0f;

        if (isSweep) {
            mappedCB_->coreThickness = 0.0040f;
            mappedCB_->glowThickness = 0.0300f;
            mappedCB_->innerColor = {0.92f, 0.92f, 1.00f, 1.0f};
            mappedCB_->outerColor = {0.48f, 0.40f, 0.92f, 1.0f};
        } else {
            mappedCB_->coreThickness = 0.0035f;
            mappedCB_->glowThickness = 0.0240f;
            mappedCB_->innerColor = {0.90f, 0.96f, 1.00f, 1.0f};
            mappedCB_->outerColor = {0.36f, 0.52f, 0.90f, 1.0f};
        }

        mappedCB_->darkColor = {0.01f, 0.01f, 0.04f, 1.0f};
        mappedCB_->tipFade = isSweep ? 0.78f : 0.84f;
        mappedCB_->noiseScale = isSweep ? 38.0f : 32.0f;

        cmd->SetGraphicsRootConstantBufferView(
            0, constBuffer_->GetGPUVirtualAddress());
        cmd->DrawInstanced(3, 1, 0, 0);
    }
}

void SlashEffectRenderer::CreateConstantBuffer() {
    const UINT size = Align256(sizeof(ConstantBufferData));

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
    auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(size);

    ThrowIfFailed(dxCommon_->GetDevice()->CreateCommittedResource(
                      &heapProps, D3D12_HEAP_FLAG_NONE, &resourceDesc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&constBuffer_)),
                  "CreateCommittedResource(SlashEffect CB) failed");

    ThrowIfFailed(
        constBuffer_->Map(0, nullptr, reinterpret_cast<void **>(&mappedCB_)),
        "SlashEffect CB Map failed");

    *mappedCB_ = ConstantBufferData{};
}

void SlashEffectRenderer::CreateRootSignature() {
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
                  "D3D12SerializeRootSignature(SlashEffect) failed");

    ThrowIfFailed(dxCommon_->GetDevice()->CreateRootSignature(
                      0, blob->GetBufferPointer(), blob->GetBufferSize(),
                      IID_PPV_ARGS(&rootSignature_)),
                  "CreateRootSignature(SlashEffect) failed");
}

void SlashEffectRenderer::CreatePipelineState() {
    auto *device = dxCommon_->GetDevice();

    auto vs = ShaderCompiler::Compile(L"resources/shaders/slash/Slash.VS.hlsl",
                                      "main", "vs_5_0");

    auto ps = ShaderCompiler::Compile(L"resources/shaders/slash/Slash.PS.hlsl",
                                      "main", "ps_5_0");

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
                  "CreateGraphicsPipelineState(SlashEffect) failed");
}

bool SlashEffectRenderer::ProjectWorldToUv(const Camera &camera,
                                           const XMFLOAT3 &worldPos,
                                           XMFLOAT2 &outUv) const {
    const XMVECTOR pos = XMVectorSet(worldPos.x, worldPos.y, worldPos.z, 1.0f);

    const XMMATRIX view = camera.GetView();
    const XMMATRIX proj = camera.GetProj();

    XMVECTOR clip = XMVector4Transform(pos, view);
    clip = XMVector4Transform(clip, proj);

    const float w = XMVectorGetW(clip);
    if (std::fabs(w) < 0.00001f) {
        return false;
    }

    const float x = XMVectorGetX(clip) / w;
    const float y = XMVectorGetY(clip) / w;
    const float z = XMVectorGetZ(clip) / w;

    if (z < 0.0f || z > 1.2f) {
        return false;
    }

    outUv.x = x * 0.5f + 0.5f;
    outUv.y = -y * 0.5f + 0.5f;
    return true;
}

void SlashEffectRenderer::CreateDarkPipelineState() {
    auto *device = dxCommon_->GetDevice();

    auto vs = ShaderCompiler::Compile(L"resources/shaders/slash/Slash.VS.hlsl",
                                      "main", "vs_5_0");

    auto ps = ShaderCompiler::Compile(L"resources/shaders/slash/Slash.PS.hlsl",
                                      "main", "ps_5_0");

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

    // 黒 streak は加算ではなく通常 alpha
    D3D12_BLEND_DESC blend = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blend.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.BlendState = blend;

    D3D12_DEPTH_STENCIL_DESC depth = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    depth.DepthEnable = FALSE;
    depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    desc.DepthStencilState = depth;

    ThrowIfFailed(device->CreateGraphicsPipelineState(
                      &desc, IID_PPV_ARGS(&darkPipelineState_)),
                  "CreateGraphicsPipelineState(SlashEffect Dark) failed");
}