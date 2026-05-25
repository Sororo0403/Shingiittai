#include "SwordSlashArcRenderer.h"
#include "DirectXCommon.h"
#include "DxHelpers.h"
#include "DxUtils.h"
#include "ShaderCompiler.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;
using namespace DxUtils;
using Microsoft::WRL::ComPtr;

namespace {
constexpr float kPi = 3.14159265f;

XMFLOAT3 Add(const XMFLOAT3 &a, const XMFLOAT3 &b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

XMFLOAT3 Sub(const XMFLOAT3 &a, const XMFLOAT3 &b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

XMFLOAT3 Scale(const XMFLOAT3 &v, float s) {
    return {v.x * s, v.y * s, v.z * s};
}

float LengthSq(const XMFLOAT3 &v) { return v.x * v.x + v.y * v.y + v.z * v.z; }

XMFLOAT3 NormalizeSafe(const XMFLOAT3 &v, const XMFLOAT3 &fallback) {
    const float lenSq = LengthSq(v);
    if (lenSq <= 0.0001f) {
        return fallback;
    }
    const float invLen = 1.0f / std::sqrt(lenSq);
    return Scale(v, invLen);
}

XMFLOAT3 Cross(const XMFLOAT3 &a, const XMFLOAT3 &b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

float SmoothStep(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

XMFLOAT3 PointOnArc(const XMFLOAT3 &center, const XMFLOAT3 &axisA,
                   const XMFLOAT3 &axisB, float angle, float radius) {
    return Add(center,
               Add(Scale(axisA, std::cos(angle) * radius),
                   Scale(axisB, std::sin(angle) * radius)));
}

} // namespace

void SwordSlashArcRenderer::Initialize(DirectXCommon *dxCommon) {
    dxCommon_ = dxCommon;
    CreateRootSignature();
    CreatePipelineState();
    CreateBuffers();
}

void SwordSlashArcRenderer::Reset() {
    for (ArcInstance &arc : arcs_) {
        arc.active = false;
        arc.age = 0.0f;
    }
    vertexCount_ = 0;
    nextArc_ = 0;
}

void SwordSlashArcRenderer::Emit(const XMFLOAT3 &root, const XMFLOAT3 &tip,
                                 const XMFLOAT3 &playerPosition,
                                 const XMFLOAT3 &targetPosition,
                                 const Camera &camera,
                                 size_t swordIndex) {
    ArcInstance &arc = arcs_[nextArc_];
    nextArc_ = (nextArc_ + 1) % arcs_.size();

    const XMFLOAT3 bladeDir = NormalizeSafe(Sub(tip, root), {0.0f, 1.0f, 0.0f});
    const XMFLOAT3 bladeCenter = Scale(Add(root, tip), 0.5f);
    XMFLOAT3 attackDir = Sub(targetPosition, playerPosition);
    attackDir.y = 0.0f;
    attackDir = NormalizeSafe(attackDir, {0.0f, 0.0f, 1.0f});
    const XMFLOAT3 cameraForward =
        NormalizeSafe(AppCameraForward(camera),
                      {0.0f, 0.0f, 1.0f});
    const XMFLOAT3 worldUp{0.0f, 1.0f, 0.0f};
    XMFLOAT3 cameraRight = NormalizeSafe(Cross(worldUp, cameraForward),
                                         {1.0f, 0.0f, 0.0f});
    XMFLOAT3 cameraUp = NormalizeSafe(Cross(cameraForward, cameraRight),
                                      {0.0f, 1.0f, 0.0f});

    const float handedness = (swordIndex % 2 == 0) ? -1.0f : 1.0f;
    XMFLOAT3 attackRight = NormalizeSafe(Cross(worldUp, attackDir), cameraRight);
    attackRight = Scale(attackRight, handedness);
    XMFLOAT3 arcA = NormalizeSafe(Add(Scale(attackRight, 0.78f),
                                      Scale(cameraRight, 0.22f)),
                                  attackRight);
    XMFLOAT3 arcB = NormalizeSafe(Add(Scale(worldUp, 0.72f),
                                      Scale(cameraUp, 0.28f)),
                                  worldUp);

    constexpr float radius = 2.10f;
    arc.center = Add(playerPosition, Scale(attackDir, radius * 0.58f));
    arc.center = Add(arc.center, Scale(attackRight, radius * 0.16f));
    arc.center = Add(arc.center, Scale(Sub(bladeCenter, playerPosition), 0.18f));
    arc.center.y += 0.98f;
    arc.axisA = arcA;
    arc.axisB = arcB;
    arc.radius = radius;
    arc.thickness = 0.20f;
    arc.life = 0.13f;
    arc.age = 0.0f;
    arc.color = {0.32f, 0.62f, 1.00f, 0.86f};
    arc.startAngle = -0.82f * kPi;
    arc.endAngle = -0.05f * kPi;
    arc.isLine = false;
    arc.active = true;
}

void SwordSlashArcRenderer::EmitHitLine(const XMFLOAT3 &position,
                                        const XMFLOAT3 &direction,
                                        const Camera &camera, float power,
                                        const XMFLOAT2 &slashDirection) {
    const XMFLOAT3 cameraForward =
        NormalizeSafe(AppCameraForward(camera),
                      {0.0f, 0.0f, 1.0f});
    const XMFLOAT3 worldUp{0.0f, 1.0f, 0.0f};
    const XMFLOAT3 cameraRight =
        NormalizeSafe(Cross(worldUp, cameraForward), {1.0f, 0.0f, 0.0f});
    const XMFLOAT3 cameraUp =
        NormalizeSafe(Cross(cameraForward, cameraRight), {0.0f, 1.0f, 0.0f});

    XMFLOAT3 hitDir = direction;
    hitDir.y *= 0.35f;
    hitDir = NormalizeSafe(hitDir, cameraRight);
    XMFLOAT3 screenDiagonal =
        NormalizeSafe(Add(Scale(cameraRight, hitDir.x >= 0.0f ? 0.90f : -0.90f),
                          Scale(cameraUp, 0.42f)),
                      cameraRight);
    XMFLOAT3 lineDir = NormalizeSafe(Add(Scale(screenDiagonal, 0.62f),
                                         Scale(hitDir, 0.38f)),
                                     screenDiagonal);
    const float slashDirLenSq =
        slashDirection.x * slashDirection.x + slashDirection.y * slashDirection.y;
    if (slashDirLenSq > 0.010f) {
        const float invSlashDirLen = 1.0f / std::sqrt(slashDirLenSq);
        const XMFLOAT2 normalizedSlashDir{
            slashDirection.x * invSlashDirLen,
            slashDirection.y * invSlashDirLen};
        lineDir = NormalizeSafe(
            Add(Scale(cameraRight, normalizedSlashDir.x),
                Scale(cameraUp, normalizedSlashDir.y)),
            lineDir);
    }
    XMFLOAT3 lineNormal =
        NormalizeSafe(Cross(cameraForward, lineDir), cameraUp);

    const float clampedPower = std::clamp(power, 0.6f, 4.0f);

    auto emitStroke = [&](const XMFLOAT3 &axis, float alongOffset,
                          float normalOffset, float heightOffset,
                          float halfLength, float thickness, float life,
                          float delay, const XMFLOAT4 &color) {
        ArcInstance &arc = arcs_[nextArc_];
        nextArc_ = (nextArc_ + 1) % arcs_.size();

        const XMFLOAT3 strokeNormal =
            NormalizeSafe(Cross(cameraForward, axis), lineNormal);
        arc.center = Add(position, Scale(axis, alongOffset));
        arc.center = Add(arc.center, Scale(strokeNormal, normalOffset));
        arc.center.y += heightOffset;
        arc.axisA = axis;
        arc.axisB = strokeNormal;
        arc.radius = halfLength;
        arc.thickness = thickness;
        arc.life = life;
        arc.age = -delay;
        arc.color = color;
        arc.startAngle = 0.0f;
        arc.endAngle = 0.0f;
        arc.isLine = true;
        arc.active = true;
    };

    emitStroke(lineDir, -0.12f, 0.00f, 0.50f,
               3.88f + clampedPower * 0.70f,
               0.155f + clampedPower * 0.025f, 0.195f, 0.0f,
               {0.82f, 0.82f, 1.00f, 0.42f});
    emitStroke(lineDir, -0.10f, 0.00f, 0.52f,
               4.15f + clampedPower * 0.74f,
               0.060f + clampedPower * 0.010f, 0.138f, 0.0f,
               {1.00f, 0.98f, 1.00f, 1.00f});
    emitStroke(lineDir, -0.34f, -0.055f, 0.47f,
               3.38f + clampedPower * 0.60f,
               0.090f + clampedPower * 0.012f, 0.172f, 0.018f,
               {0.96f, 0.84f, 1.00f, 0.50f});

    const XMFLOAT3 shardUp =
        NormalizeSafe(Add(Scale(lineNormal, 0.78f), Scale(lineDir, 0.22f)),
                      lineNormal);
    const XMFLOAT3 shardDown =
        NormalizeSafe(Add(Scale(lineNormal, -0.68f), Scale(lineDir, 0.34f)),
                      Scale(lineNormal, -1.0f));
    const XMFLOAT3 shardBack =
        NormalizeSafe(Add(Scale(cameraUp, 0.64f), Scale(lineDir, -0.28f)),
                      cameraUp);

    emitStroke(shardUp, 0.22f, 0.02f, 0.52f, 1.05f + clampedPower * 0.12f,
               0.018f, 0.108f, 0.0f, {0.94f, 0.97f, 1.0f, 0.78f});
    emitStroke(shardDown, 0.16f, -0.02f, 0.48f, 0.72f + clampedPower * 0.08f,
               0.014f, 0.095f, 0.010f, {0.86f, 0.92f, 1.0f, 0.62f});
    emitStroke(shardBack, 0.10f, 0.00f, 0.58f, 0.78f + clampedPower * 0.08f,
               0.012f, 0.088f, 0.016f, {1.0f, 1.0f, 1.0f, 0.66f});
}

void SwordSlashArcRenderer::Update(float deltaTime) {
    for (ArcInstance &arc : arcs_) {
        if (!arc.active) {
            continue;
        }
        arc.age += deltaTime;
        if (arc.age >= arc.life) {
            arc.active = false;
        }
    }
    BuildVertices();
}

void SwordSlashArcRenderer::Draw(const Camera &camera) {
    if (!dxCommon_ || !mappedViewProjection_ || vertexCount_ == 0) {
        return;
    }

    XMStoreFloat4x4(&mappedViewProjection_->matViewProjection,
                    XMMatrixTranspose(camera.GetView() * camera.GetProj()));

    auto *cmd = dxCommon_->GetCommandList();
    cmd->SetGraphicsRootSignature(rootSignature_.Get());
    cmd->SetPipelineState(pipelineState_.Get());
    cmd->SetGraphicsRootConstantBufferView(
        0, viewProjectionBuffer_->GetGPUVirtualAddress());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->IASetVertexBuffers(0, 1, &vertexBufferView_);
    cmd->DrawInstanced(vertexCount_, 1, 0, 0);
}

void SwordSlashArcRenderer::BuildVertices() {
    vertexCount_ = 0;
    EnsureVertexCapacity(kInitialMaxVertices);
    if (!mappedVertices_) {
        return;
    }

    for (const ArcInstance &arc : arcs_) {
        if (!arc.active) {
            continue;
        }

        const float ageRate = std::clamp(arc.age / arc.life, 0.0f, 1.0f);
        if (arc.age < 0.0f) {
            continue;
        }
        const float grow = SmoothStep(0.0f, 0.20f, ageRate);
        const float fade = 1.0f - SmoothStep(0.46f, 1.0f, ageRate);
        if (arc.isLine) {
            const float growLine = SmoothStep(0.0f, 0.18f, ageRate);
            const float fadeLine = 1.0f - SmoothStep(0.35f, 1.0f, ageRate);
            const float halfLength = arc.radius * growLine;
            const float halfThickness = arc.thickness * (1.0f + 0.45f * (1.0f - ageRate));
            const XMFLOAT3 start = Add(arc.center, Scale(arc.axisA, -halfLength));
            const XMFLOAT3 end = Add(arc.center, Scale(arc.axisA, halfLength));
            XMFLOAT4 color = arc.color;
            color.w *= fadeLine;
            const ArcVertex v0{Add(start, Scale(arc.axisB, -halfThickness)),
                               {0.0f, 0.0f}, color};
            const ArcVertex v1{Add(start, Scale(arc.axisB, halfThickness)),
                               {1.0f, 0.0f}, color};
            const ArcVertex v2{Add(end, Scale(arc.axisB, -halfThickness)),
                               {0.0f, 1.0f}, color};
            const ArcVertex v3{Add(end, Scale(arc.axisB, halfThickness)),
                               {1.0f, 1.0f}, color};
            if (vertexCount_ + 6 > vertexCapacity_) {
                return;
            }
            mappedVertices_[vertexCount_++] = v0;
            mappedVertices_[vertexCount_++] = v1;
            mappedVertices_[vertexCount_++] = v2;
            mappedVertices_[vertexCount_++] = v2;
            mappedVertices_[vertexCount_++] = v1;
            mappedVertices_[vertexCount_++] = v3;
            continue;
        }

        const float radiusBoost = 1.0f + 0.12f * ageRate;
        const float innerRadius = (arc.radius - arc.thickness * 0.5f) * radiusBoost;
        const float outerRadius = (arc.radius + arc.thickness * 0.5f) * radiusBoost;
        const float visibleEnd = arc.startAngle + (arc.endAngle - arc.startAngle) * grow;

        for (uint32_t i = 1; i <= kSegments; ++i) {
            const float t0 = static_cast<float>(i - 1) / static_cast<float>(kSegments);
            const float t1 = static_cast<float>(i) / static_cast<float>(kSegments);
            const float a0 = arc.startAngle + (visibleEnd - arc.startAngle) * t0;
            const float a1 = arc.startAngle + (visibleEnd - arc.startAngle) * t1;

            XMFLOAT4 color0 = arc.color;
            XMFLOAT4 color1 = arc.color;
            const float tipFade0 = SmoothStep(0.0f, 0.10f, t0) *
                                   (1.0f - SmoothStep(0.90f, 1.0f, t0));
            const float tipFade1 = SmoothStep(0.0f, 0.10f, t1) *
                                   (1.0f - SmoothStep(0.90f, 1.0f, t1));
            color0.w *= fade * tipFade0;
            color1.w *= fade * tipFade1;

            const ArcVertex v0{PointOnArc(arc.center, arc.axisA, arc.axisB,
                                           a0, innerRadius),
                               {0.0f, t0}, color0};
            const ArcVertex v1{PointOnArc(arc.center, arc.axisA, arc.axisB,
                                           a0, outerRadius),
                               {1.0f, t0}, color0};
            const ArcVertex v2{PointOnArc(arc.center, arc.axisA, arc.axisB,
                                           a1, innerRadius),
                               {0.0f, t1}, color1};
            const ArcVertex v3{PointOnArc(arc.center, arc.axisA, arc.axisB,
                                           a1, outerRadius),
                               {1.0f, t1}, color1};

            if (vertexCount_ + 6 > vertexCapacity_) {
                return;
            }

            mappedVertices_[vertexCount_++] = v0;
            mappedVertices_[vertexCount_++] = v1;
            mappedVertices_[vertexCount_++] = v2;
            mappedVertices_[vertexCount_++] = v2;
            mappedVertices_[vertexCount_++] = v1;
            mappedVertices_[vertexCount_++] = v3;
        }
    }
}

void SwordSlashArcRenderer::CreateRootSignature() {
    CD3DX12_ROOT_PARAMETER params[1]{};
    params[0].InitAsConstantBufferView(0);

    CD3DX12_ROOT_SIGNATURE_DESC desc{};
    desc.Init(_countof(params), params, 0, nullptr,
              D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> error;
    ThrowIfFailed(D3D12SerializeRootSignature(
                      &desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error),
                  "D3D12SerializeRootSignature(SwordSlashArc) failed");
    ThrowIfFailed(dxCommon_->GetDevice()->CreateRootSignature(
                      0, blob->GetBufferPointer(), blob->GetBufferSize(),
                      IID_PPV_ARGS(&rootSignature_)),
                  "CreateRootSignature(SwordSlashArc) failed");
}

void SwordSlashArcRenderer::CreatePipelineState() {
    auto *device = dxCommon_->GetDevice();

    auto vs = ShaderCompiler::Compile(
        L"app/resources/shaders/sword_arc/SwordArcVS.hlsl", "main",
        "vs_6_6");
    auto ps = ShaderCompiler::Compile(
        L"app/resources/shaders/sword_arc/SwordArcPS.hlsl", "main",
        "ps_6_6");

    D3D12_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = rootSignature_.Get();
    pso.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    pso.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    pso.InputLayout = {layout, _countof(layout)};
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DirectXCommon::kSceneColorFormat;
    pso.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    pso.SampleDesc.Count = 1;
    pso.SampleMask = UINT_MAX;

    D3D12_RASTERIZER_DESC rasterizer = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    rasterizer.CullMode = D3D12_CULL_MODE_NONE;
    pso.RasterizerState = rasterizer;

    D3D12_BLEND_DESC blend = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blend.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
    blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.BlendState = blend;

    D3D12_DEPTH_STENCIL_DESC depth = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    depth.DepthEnable = FALSE;
    depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    pso.DepthStencilState = depth;

    ThrowIfFailed(device->CreateGraphicsPipelineState(
                      &pso, IID_PPV_ARGS(&pipelineState_)),
                  "CreateGraphicsPipelineState(SwordSlashArc) failed");
}

void SwordSlashArcRenderer::CreateBuffers() {
    EnsureVertexCapacity(kInitialMaxVertices);

    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    auto cbDesc = CD3DX12_RESOURCE_DESC::Buffer(static_cast<UINT64>(
        (sizeof(ViewProjectionConstBufferData) + 255) & ~255));

    ThrowIfFailed(dxCommon_->GetDevice()->CreateCommittedResource(
                      &uploadHeap, D3D12_HEAP_FLAG_NONE, &cbDesc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&viewProjectionBuffer_)),
                  "CreateCommittedResource(SwordSlashArcCB) failed");
    ThrowIfFailed(viewProjectionBuffer_->Map(
                      0, nullptr,
                      reinterpret_cast<void **>(&mappedViewProjection_)),
                  "Map(SwordSlashArcCB) failed");
}

void SwordSlashArcRenderer::EnsureVertexCapacity(uint32_t vertexCount) {
    if (vertexCount <= vertexCapacity_) {
        return;
    }

    vertexBuffer_.Reset();
    mappedVertices_ = nullptr;
    vertexCapacity_ = (std::max)(vertexCount, kInitialMaxVertices);

    const UINT bufferSize = sizeof(ArcVertex) * vertexCapacity_;
    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    auto vertexDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);

    ThrowIfFailed(dxCommon_->GetDevice()->CreateCommittedResource(
                      &uploadHeap, D3D12_HEAP_FLAG_NONE, &vertexDesc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&vertexBuffer_)),
                  "CreateCommittedResource(SwordSlashArcVB) failed");
    ThrowIfFailed(vertexBuffer_->Map(
                      0, nullptr, reinterpret_cast<void **>(&mappedVertices_)),
                  "Map(SwordSlashArcVB) failed");

    vertexBufferView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = bufferSize;
    vertexBufferView_.StrideInBytes = sizeof(ArcVertex);
}
