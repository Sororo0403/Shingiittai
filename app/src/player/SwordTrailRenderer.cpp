#include "SwordTrailRenderer.h"
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

float LengthSq(const XMFLOAT3 &v) { return v.x * v.x + v.y * v.y + v.z * v.z; }

XMFLOAT3 Sub(const XMFLOAT3 &a, const XMFLOAT3 &b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

XMFLOAT3 Add(const XMFLOAT3 &a, const XMFLOAT3 &b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

XMFLOAT3 Scale(const XMFLOAT3 &v, float s) {
    return {v.x * s, v.y * s, v.z * s};
}

XMFLOAT3 Lerp(const XMFLOAT3 &a, const XMFLOAT3 &b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t};
}

XMFLOAT3 MakeSlashGhostOffset(const Sword &sword) {
    XMFLOAT2 slashDir = sword.GetSlashDirection();
    float dirLenSq = slashDir.x * slashDir.x + slashDir.y * slashDir.y;
    if (dirLenSq <= 0.0001f) {
        slashDir = {0.75f, -0.35f};
        dirLenSq = slashDir.x * slashDir.x + slashDir.y * slashDir.y;
    }

    const float invLen = 1.0f / std::sqrt(dirLenSq);
    slashDir.x *= invLen;
    slashDir.y *= invLen;

    constexpr float length = 0.34f;
    return {-slashDir.x * length, slashDir.y * length, -0.08f * length};
}

float DistanceSq(const XMFLOAT3 &a, const XMFLOAT3 &b) {
    return LengthSq(Sub(a, b));
}

XMFLOAT3 NormalizeSafe(const XMFLOAT3 &v, const XMFLOAT3 &fallback) {
    const float lengthSq = LengthSq(v);
    if (lengthSq <= 0.0001f) {
        return fallback;
    }

    const float invLength = 1.0f / std::sqrt(lengthSq);
    return Scale(v, invLength);
}

float SmoothStep(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

void SwordTrailRenderer::Initialize(DirectXCommon *dxCommon) {
    dxCommon_ = dxCommon;
    CreateRootSignature();
    CreatePipelineState();
    CreateBuffers();
}

void SwordTrailRenderer::Reset() {
    for (TrailState &trail : trails_) {
        trail.samples.clear();
        trail.wasActive = false;
    }
    vertexCount_ = 0;
}

void SwordTrailRenderer::Update(const Player &player, float deltaTime) {
    const auto swords = player.GetSwords();
    const auto slashStates = player.GetSwordSlashStates();
    const auto damages = player.GetSwordAttackDamages();

    for (size_t i = 0; i < kSwordCount; ++i) {
        UpdateOneSword(i, swords[i], slashStates[i], damages[i], deltaTime);
    }

    BuildVertices();
}

void SwordTrailRenderer::Draw(const Camera &camera) {
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

void SwordTrailRenderer::UpdateOneSword(size_t index, const Sword *sword,
                                        bool isSlashing, float damage,
                                        float deltaTime) {
    (void)damage;

    if (index >= trails_.size()) {
        return;
    }

    TrailState &trail = trails_[index];

    const float trailLife = kTrailLife;

    for (TrailSample &sample : trail.samples) {
        sample.age += deltaTime;
    }

    trail.samples.erase(std::remove_if(trail.samples.begin(),
                                       trail.samples.end(),
                                       [trailLife](const TrailSample &sample) {
                                           return sample.age >= trailLife;
                                       }),
                        trail.samples.end());

    if (!sword) {
        trail.wasActive = false;
        return;
    }

    if (isSlashing) {
        const bool justStarted = !trail.wasActive;

        const XMFLOAT3 root = sword->GetVisualBladeRootWorld();
        const XMFLOAT3 tip = sword->GetVisualBladeTipWorld();

        if (justStarted) {
            const XMFLOAT3 ghostOffset =
                MakeSlashGhostOffset(*sword);
            AddSample(trail, Add(root, ghostOffset), Add(tip, ghostOffset),
                      true);
        }
        AddSample(trail, root, tip, justStarted);
    }

    trail.wasActive = isSlashing;
}

void SwordTrailRenderer::AddSample(TrailState &trail,
                                   const DirectX::XMFLOAT3 &root,
                                   const DirectX::XMFLOAT3 &tip, bool force) {
    if (!force && !trail.samples.empty()) {
        const TrailSample &last = trail.samples.back();

        const float moveTipSq = DistanceSq(tip, last.tip);
        const float moveRootSq = DistanceSq(root, last.root);
        const float minMoveSq = kMinAddDistance * kMinAddDistance;

        if (moveTipSq < minMoveSq && moveRootSq < minMoveSq) {
            return;
        }
    }

    TrailSample sample{};
    sample.root = root;
    sample.tip = tip;
    sample.age = 0.0f;
    trail.samples.push_back(sample);

    if (trail.samples.size() > kMaxSamplesPerSword) {
        trail.samples.erase(trail.samples.begin());
    }
}

void SwordTrailRenderer::BuildVertices() {
    vertexCount_ = 0;

    uint32_t requiredVertexCount = 0;
    for (const TrailState &trail : trails_) {
        if (trail.samples.size() >= 2) {
            requiredVertexCount +=
                static_cast<uint32_t>((trail.samples.size() - 1) * 6);
        }
    }

    if (requiredVertexCount == 0) {
        return;
    }

    EnsureVertexCapacity(requiredVertexCount);
    if (!mappedVertices_) {
        return;
    }

    const float trailLife = kTrailLife;

    for (const TrailState &trail : trails_) {
        if (trail.samples.size() < 2) {
            continue;
        }

        for (size_t i = 1; i < trail.samples.size(); ++i) {
            const TrailSample &a = trail.samples[i - 1];
            const TrailSample &b = trail.samples[i];
            constexpr float rootBias = 0.58f;
            constexpr float tipExtension = 0.08f;

            const XMFLOAT3 dirA = NormalizeSafe(Sub(a.tip, a.root),
                                                {0.0f, 1.0f, 0.0f});
            const XMFLOAT3 dirB = NormalizeSafe(Sub(b.tip, b.root),
                                                {0.0f, 1.0f, 0.0f});
            const XMFLOAT3 rootA = Lerp(a.root, a.tip, rootBias);
            const XMFLOAT3 tipA = Add(a.tip, Scale(dirA, tipExtension));
            const XMFLOAT3 rootB = Lerp(b.root, b.tip, rootBias);
            const XMFLOAT3 tipB = Add(b.tip, Scale(dirB, tipExtension));

            const float fadeA =
                std::clamp(1.0f - a.age / trailLife, 0.0f, 1.0f);
            const float fadeB =
                std::clamp(1.0f - b.age / trailLife, 0.0f, 1.0f);

            const float segmentFade =
                (std::min)(fadeA, fadeB) *
                SmoothStep(0.0f, 0.28f,
                           static_cast<float>(i) /
                               static_cast<float>(trail.samples.size()));

            const XMFLOAT4 colorA = GetTrailColor(segmentFade);
            const XMFLOAT4 colorB = GetTrailColor(segmentFade);

            const float vA = static_cast<float>(i - 1) /
                             static_cast<float>(
                                 std::max<size_t>(1, trail.samples.size() - 1));
            const float vB = static_cast<float>(i) /
                             static_cast<float>(
                                 std::max<size_t>(1, trail.samples.size() - 1));

            const TrailVertex v0{rootA, {0.0f, vA}, colorA};
            const TrailVertex v1{tipA, {1.0f, vA}, colorA};
            const TrailVertex v2{rootB, {0.0f, vB}, colorB};
            const TrailVertex v3{tipB, {1.0f, vB}, colorB};

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

XMFLOAT4 SwordTrailRenderer::GetTrailColor(float alpha) const {
    return {0.84f, 0.30f, 1.00f, 0.82f * alpha};
}

void SwordTrailRenderer::CreateRootSignature() {
    CD3DX12_ROOT_PARAMETER params[1]{};
    params[0].InitAsConstantBufferView(0);

    CD3DX12_ROOT_SIGNATURE_DESC desc{};
    desc.Init(_countof(params), params, 0, nullptr,
              D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    Microsoft::WRL::ComPtr<ID3DBlob> blob;
    Microsoft::WRL::ComPtr<ID3DBlob> error;

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

    auto vs = ShaderCompiler::Compile(
        L"app/resources/shaders/swordtrail/SwordTrailVS.hlsl", "main",
        "vs_5_0");
    auto ps = ShaderCompiler::Compile(
        L"app/resources/shaders/swordtrail/SwordTrailPS.hlsl", "main",
        "ps_5_0");

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
                  "CreateGraphicsPipelineState(SwordTrail) failed");
}

void SwordTrailRenderer::CreateBuffers() {
    EnsureVertexCapacity(kInitialMaxVertices);

    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    auto cbDesc = CD3DX12_RESOURCE_DESC::Buffer(static_cast<UINT64>(
        (sizeof(ViewProjectionConstBufferData) + 255) & ~255));

    ThrowIfFailed(dxCommon_->GetDevice()->CreateCommittedResource(
                      &uploadHeap, D3D12_HEAP_FLAG_NONE, &cbDesc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&viewProjectionBuffer_)),
                  "CreateCommittedResource(SwordTrailCB) failed");

    ThrowIfFailed(
        viewProjectionBuffer_->Map(
            0, nullptr, reinterpret_cast<void **>(&mappedViewProjection_)),
        "Map(SwordTrailCB) failed");
}

void SwordTrailRenderer::EnsureVertexCapacity(uint32_t vertexCount) {
    if (vertexCount <= vertexCapacity_) {
        return;
    }

    vertexBuffer_.Reset();
    mappedVertices_ = nullptr;
    vertexCapacity_ = (std::max)(vertexCount, kInitialMaxVertices);

    const UINT bufferSize = sizeof(TrailVertex) * vertexCapacity_;

    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    auto vertexDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);

    ThrowIfFailed(dxCommon_->GetDevice()->CreateCommittedResource(
                      &uploadHeap, D3D12_HEAP_FLAG_NONE, &vertexDesc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&vertexBuffer_)),
                  "CreateCommittedResource(SwordTrailVB) failed");

    ThrowIfFailed(vertexBuffer_->Map(
                      0, nullptr, reinterpret_cast<void **>(&mappedVertices_)),
                  "Map(SwordTrailVB) failed");

    vertexBufferView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = bufferSize;
    vertexBufferView_.StrideInBytes = sizeof(TrailVertex);
}
