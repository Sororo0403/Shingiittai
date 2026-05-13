#include "CollisionDebugRenderer.h"
#include "DirectXCommon.h"
#include "DxHelpers.h"
#include "DxUtils.h"
#include "ShaderCompiler.h"
#include <algorithm>

using namespace DirectX;
using namespace DxUtils;
using Microsoft::WRL::ComPtr;

namespace {

constexpr CollisionManager::LayerMask kLayerPlayer = 1u << 0;
constexpr CollisionManager::LayerMask kLayerEnemy = 1u << 1;
constexpr CollisionManager::LayerMask kLayerPlayerAttack = 1u << 2;
constexpr CollisionManager::LayerMask kLayerEnemyAttack = 1u << 3;
constexpr CollisionManager::LayerMask kLayerPlayerCounter = 1u << 4;
constexpr CollisionManager::LayerMask kLayerEnemyProjectile = 1u << 5;
constexpr CollisionManager::LayerMask kLayerReflectedProjectile = 1u << 6;

constexpr uint32_t kObbLineVertexCount = 24;

XMVECTOR NormalizeQuaternion(const XMFLOAT4 &rotation) {
    XMVECTOR q = XMLoadFloat4(&rotation);
    const float lengthSq = XMVectorGetX(XMVector4LengthSq(q));
    if (lengthSq <= 0.00001f) {
        return XMQuaternionIdentity();
    }
    return XMQuaternionNormalize(q);
}

} // namespace

void CollisionDebugRenderer::Initialize(DirectXCommon *dxCommon) {
    dxCommon_ = dxCommon;
    CreateRootSignature();
    CreatePipelineState();
    CreateBuffers();
}

void CollisionDebugRenderer::Draw(const CollisionManager &collisionManager,
                                  const Camera &camera, bool highlightHits) {
    if (dxCommon_ == nullptr || mappedViewProjection_ == nullptr) {
        return;
    }

    const auto &bodies = collisionManager.GetBodies();
    if (bodies.empty()) {
        return;
    }

    const uint32_t requiredVertexCount =
        static_cast<uint32_t>(bodies.size()) * kObbLineVertexCount;
    EnsureVertexCapacity(requiredVertexCount);
    if (mappedVertices_ == nullptr) {
        return;
    }

    vertexCount_ = 0;
    const std::vector<CollisionManager::Hit> hits =
        highlightHits ? collisionManager.FindPairs()
                      : std::vector<CollisionManager::Hit>{};

    for (const CollisionManager::Body &body : bodies) {
        if (!body.desc.isActive) {
            continue;
        }
        AddOBB(body.desc.box, GetBodyColor(body, hits));
    }

    if (vertexCount_ == 0) {
        return;
    }

    XMStoreFloat4x4(&mappedViewProjection_->matViewProjection,
                    XMMatrixTranspose(camera.GetView() * camera.GetProj()));

    auto *cmd = dxCommon_->GetCommandList();
    cmd->SetGraphicsRootSignature(rootSignature_.Get());
    cmd->SetPipelineState(pipelineState_.Get());
    cmd->SetGraphicsRootConstantBufferView(
        0, viewProjectionBuffer_->GetGPUVirtualAddress());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
    cmd->IASetVertexBuffers(0, 1, &vertexBufferView_);
    cmd->DrawInstanced(vertexCount_, 1, 0, 0);
}

void CollisionDebugRenderer::CreateRootSignature() {
    CD3DX12_ROOT_PARAMETER params[1];
    params[0].InitAsConstantBufferView(0);

    CD3DX12_ROOT_SIGNATURE_DESC desc;
    desc.Init(_countof(params), params, 0, nullptr,
              D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> error;
    ThrowIfFailed(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                              &blob, &error),
                  "D3D12SerializeRootSignature(CollisionDebug) failed");
    ThrowIfFailed(dxCommon_->GetDevice()->CreateRootSignature(
                      0, blob->GetBufferPointer(), blob->GetBufferSize(),
                      IID_PPV_ARGS(&rootSignature_)),
                  "CreateRootSignature(CollisionDebug) failed");
}

void CollisionDebugRenderer::CreatePipelineState() {
    auto *device = dxCommon_->GetDevice();
    auto vs = ShaderCompiler::Compile(
        L"engine/resources/shaders/line/DebugLineVS.hlsl", "main", "vs_5_0");
    auto ps = ShaderCompiler::Compile(
        L"engine/resources/shaders/line/DebugLinePS.hlsl", "main", "ps_5_0");

    D3D12_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
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
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    pso.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    pso.SampleDesc.Count = 1;
    pso.SampleMask = UINT_MAX;
    pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

    D3D12_BLEND_DESC blend = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blend.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
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
                  "CreateGraphicsPipelineState(CollisionDebug) failed");
}

void CollisionDebugRenderer::CreateBuffers() {
    EnsureVertexCapacity(kInitialMaxVertices);

    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    auto cbDesc = CD3DX12_RESOURCE_DESC::Buffer(
        static_cast<UINT64>((sizeof(ViewProjectionConstBufferData) + 255) & ~255));
    ThrowIfFailed(dxCommon_->GetDevice()->CreateCommittedResource(
                      &uploadHeap, D3D12_HEAP_FLAG_NONE, &cbDesc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&viewProjectionBuffer_)),
                  "CreateCommittedResource(CollisionDebugCB) failed");
    ThrowIfFailed(viewProjectionBuffer_->Map(
                      0, nullptr,
                      reinterpret_cast<void **>(&mappedViewProjection_)),
                  "Map(CollisionDebugCB) failed");
}

void CollisionDebugRenderer::EnsureVertexCapacity(uint32_t vertexCount) {
    if (vertexCount <= vertexCapacity_) {
        return;
    }

    vertexBuffer_.Reset();
    mappedVertices_ = nullptr;
    vertexCapacity_ = (std::max)(vertexCount, kInitialMaxVertices);

    const UINT bufferSize = sizeof(LineVertex) * vertexCapacity_;
    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    auto vertexDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
    ThrowIfFailed(dxCommon_->GetDevice()->CreateCommittedResource(
                      &uploadHeap, D3D12_HEAP_FLAG_NONE, &vertexDesc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&vertexBuffer_)),
                  "CreateCommittedResource(CollisionDebugVB) failed");
    ThrowIfFailed(vertexBuffer_->Map(0, nullptr,
                                     reinterpret_cast<void **>(&mappedVertices_)),
                  "Map(CollisionDebugVB) failed");

    vertexBufferView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = bufferSize;
    vertexBufferView_.StrideInBytes = sizeof(LineVertex);
}

void CollisionDebugRenderer::AddOBB(const OBB &box, const XMFLOAT4 &color) {
    if (vertexCount_ + kObbLineVertexCount > vertexCapacity_) {
        return;
    }

    const XMVECTOR center = XMLoadFloat3(&box.center);
    const XMVECTOR rotation = NormalizeQuaternion(box.rotation);
    const XMVECTOR axes[3] = {
        XMVector3Rotate(XMVectorSet(box.size.x * 0.5f, 0.0f, 0.0f, 0.0f),
                        rotation),
        XMVector3Rotate(XMVectorSet(0.0f, box.size.y * 0.5f, 0.0f, 0.0f),
                        rotation),
        XMVector3Rotate(XMVectorSet(0.0f, 0.0f, box.size.z * 0.5f, 0.0f),
                        rotation),
    };

    XMFLOAT3 corners[8]{};
    uint32_t cornerIndex = 0;
    for (int x = -1; x <= 1; x += 2) {
        for (int y = -1; y <= 1; y += 2) {
            for (int z = -1; z <= 1; z += 2) {
                XMVECTOR corner = center + axes[0] * static_cast<float>(x) +
                                  axes[1] * static_cast<float>(y) +
                                  axes[2] * static_cast<float>(z);
                XMStoreFloat3(&corners[cornerIndex++], corner);
            }
        }
    }

    constexpr uint32_t edges[12][2] = {
        {0, 1}, {0, 2}, {0, 4}, {3, 1}, {3, 2}, {3, 7},
        {5, 1}, {5, 4}, {5, 7}, {6, 2}, {6, 4}, {6, 7},
    };

    for (const auto &edge : edges) {
        mappedVertices_[vertexCount_++] = {corners[edge[0]], color};
        mappedVertices_[vertexCount_++] = {corners[edge[1]], color};
    }
}

XMFLOAT4 CollisionDebugRenderer::GetBodyColor(
    const CollisionManager::Body &body,
    const std::vector<CollisionManager::Hit> &hits) const {
    if (IsHitBody(body.id, hits)) {
        return {1.0f, 0.92f, 0.12f, 1.0f};
    }

    const CollisionManager::LayerMask layer = body.desc.layer;
    if ((layer & kLayerPlayerAttack) != 0) {
        return {0.15f, 0.78f, 1.0f, 0.95f};
    }
    if ((layer & kLayerPlayerCounter) != 0) {
        return {0.05f, 1.0f, 0.62f, 0.95f};
    }
    if ((layer & kLayerEnemyAttack) != 0) {
        return {1.0f, 0.22f, 0.16f, 0.95f};
    }
    if ((layer & kLayerEnemyProjectile) != 0) {
        return {1.0f, 0.48f, 0.12f, 0.95f};
    }
    if ((layer & kLayerReflectedProjectile) != 0) {
        return {0.70f, 0.42f, 1.0f, 0.95f};
    }
    if ((layer & kLayerPlayer) != 0) {
        return {0.25f, 1.0f, 0.25f, 0.95f};
    }
    if ((layer & kLayerEnemy) != 0) {
        return {1.0f, 0.18f, 0.45f, 0.95f};
    }
    return {1.0f, 1.0f, 1.0f, 0.85f};
}

bool CollisionDebugRenderer::IsHitBody(
    CollisionManager::BodyId bodyId,
    const std::vector<CollisionManager::Hit> &hits) const {
    for (const CollisionManager::Hit &hit : hits) {
        if (hit.a == bodyId || hit.b == bodyId) {
            return true;
        }
    }
    return false;
}
