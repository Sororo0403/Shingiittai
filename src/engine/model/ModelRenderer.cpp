#include "ModelRenderer.h"
#include "DirectXCommon.h"
#include "DxHelpers.h"
#include "DxUtils.h"
#include "MaterialManager.h"
#include "MeshManager.h"
#include "ShaderCompiler.h"
#include "SrvManager.h"
#include "TextureManager.h"

using namespace DirectX;
using namespace DxUtils;
using Microsoft::WRL::ComPtr;

struct ConstBufferData {
    XMFLOAT4X4 matWVP;
};

void ModelRenderer::Initialize(DirectXCommon *dxCommon, SrvManager *srvManager,
                               MeshManager *meshManager,
                               TextureManager *textureManager,
                               MaterialManager *materialManager) {
    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
    meshManager_ = meshManager;
    textureManager_ = textureManager;
    materialManager_ = materialManager;

    CreateRootSignature();
    CreatePipelineState();
    CreateConstantBuffer();
    CreateBoneBuffer();
}

void ModelRenderer::PreDraw() {
    auto cmd = dxCommon_->GetCommandList();

    ID3D12DescriptorHeap *heaps[] = {srvManager_->GetHeap()};
    cmd->SetDescriptorHeaps(1, heaps);

    cmd->SetGraphicsRootSignature(rootSignature_.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    drawIndex_ = 0;
}

void ModelRenderer::Draw(const Model &model, const Transform &transform,
                         const Camera &camera) {
    if (drawIndex_ >= kMaxDraws) {
        return;
    }

    auto cmd = dxCommon_->GetCommandList();
    const Mesh &mesh = meshManager_->GetMesh(model.meshId);

    const Material &material = materialManager_->GetMaterial(model.materialId);

    bool transparent = material.color.w < 1.0f;

    if (transparent) {
        cmd->SetPipelineState(transparentPSO_.Get());
    } else {
        cmd->SetPipelineState(opaquePSO_.Get());
    }

    XMVECTOR q = XMQuaternionNormalize(XMLoadFloat4(&transform.rotation));

    XMMATRIX world =
        XMMatrixScaling(transform.scale.x, transform.scale.y,
                        transform.scale.z) *
        XMMatrixRotationQuaternion(q) *
        XMMatrixTranslation(transform.position.x, transform.position.y,
                            transform.position.z);

    XMMATRIX wvp = world * camera.GetView() * camera.GetProj();

    auto *dst =
        reinterpret_cast<ConstBufferData *>(mappedCB_ + cbStride_ * drawIndex_);

    XMStoreFloat4x4(&dst->matWVP, XMMatrixTranspose(wvp));

    D3D12_GPU_VIRTUAL_ADDRESS cbAddr =
        constBuffer_->GetGPUVirtualAddress() + cbStride_ * drawIndex_;

    XMMATRIX id = XMMatrixTranspose(XMMatrixIdentity());

    for (int i = 0; i < kMaxBones; i++) {
        XMStoreFloat4x4(&mappedBones_[i], id);
    }

    if (!model.finalBoneMatrices.empty()) {
        for (size_t i = 0; i < model.finalBoneMatrices.size() && i < kMaxBones;
             i++) {

            XMMATRIX m = XMLoadFloat4x4(&model.finalBoneMatrices[i]);
            m = XMMatrixTranspose(m);
            XMStoreFloat4x4(&mappedBones_[i], m);
        }
    }

    cmd->SetGraphicsRootConstantBufferView(0, cbAddr);
    cmd->SetGraphicsRootConstantBufferView(1,
                                           boneBuffer_->GetGPUVirtualAddress());

    cmd->SetGraphicsRootConstantBufferView(
        2, materialManager_->GetGPUVirtualAddress(model.materialId));

    cmd->SetGraphicsRootDescriptorTable(
        3, textureManager_->GetGpuHandle(model.textureId));

    cmd->IASetVertexBuffers(0, 1, &mesh.vbView);
    cmd->IASetIndexBuffer(&mesh.ibView);

    cmd->DrawIndexedInstanced(mesh.indexCount, 1, 0, 0, 0);

    drawIndex_++;
}

void ModelRenderer::PostDraw() {}

void ModelRenderer::CreateConstantBuffer() {
    cbStride_ = Align256(sizeof(ConstBufferData));
    UINT totalSize = cbStride_ * kMaxDraws;

    CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);
    auto desc = CD3DX12_RESOURCE_DESC::Buffer(totalSize);

    ThrowIfFailed(dxCommon_->GetDevice()->CreateCommittedResource(
                      &heap, D3D12_HEAP_FLAG_NONE, &desc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&constBuffer_)),
                  "CreateCommittedResource(ConstantBuffer) failed");

    ThrowIfFailed(
        constBuffer_->Map(0, nullptr, reinterpret_cast<void **>(&mappedCB_)),
        "ConstantBuffer Map failed");
}

void ModelRenderer::CreateRootSignature() {
    CD3DX12_ROOT_PARAMETER params[4];

    params[0].InitAsConstantBufferView(0);
    params[1].InitAsConstantBufferView(1);
    params[2].InitAsConstantBufferView(2);

    CD3DX12_DESCRIPTOR_RANGE range;
    range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    params[3].InitAsDescriptorTable(1, &range);

    CD3DX12_STATIC_SAMPLER_DESC sampler(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);

    CD3DX12_ROOT_SIGNATURE_DESC desc;
    desc.Init(_countof(params), params, 1, &sampler,
              D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> blob, error;

    ThrowIfFailed(D3D12SerializeRootSignature(
                      &desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error),
                  "D3D12SerializeRootSignature failed");

    ThrowIfFailed(dxCommon_->GetDevice()->CreateRootSignature(
                      0, blob->GetBufferPointer(), blob->GetBufferSize(),
                      IID_PPV_ARGS(&rootSignature_)),
                  "CreateRootSignature failed");
}

void ModelRenderer::CreatePipelineState() {
    auto device = dxCommon_->GetDevice();

    auto vs = ShaderCompiler::Compile(L"resources/shaders/model/ModelVS.hlsl",
                                      "main", "vs_5_0");

    auto ps = ShaderCompiler::Compile(L"resources/shaders/model/ModelPS.hlsl",
                                      "main", "ps_5_0");

    D3D12_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"BONEINDEX", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"BONEWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
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
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    pso.SampleDesc.Count = 1;
    pso.SampleMask = UINT_MAX;
    pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

    // 不透明PSO
    D3D12_BLEND_DESC blend = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    blend.RenderTarget[0].BlendEnable = FALSE;
    pso.BlendState = blend;

    D3D12_DEPTH_STENCIL_DESC opaqueDepth =
        CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    opaqueDepth.DepthEnable = TRUE;
    opaqueDepth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    opaqueDepth.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    pso.DepthStencilState = opaqueDepth;

    ThrowIfFailed(
        device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&opaquePSO_)),
        "CreateGraphicsPipelineState(Opaque) failed");

    // 透明PSO
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blend.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.BlendState = blend;

    D3D12_DEPTH_STENCIL_DESC transparentDepth =
        CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    transparentDepth.DepthEnable = TRUE;
    transparentDepth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    transparentDepth.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    pso.DepthStencilState = transparentDepth;

    ThrowIfFailed(device->CreateGraphicsPipelineState(
                      &pso, IID_PPV_ARGS(&transparentPSO_)),
                  "CreateGraphicsPipelineState(Transparent) failed");
}

void ModelRenderer::CreateBoneBuffer() {
    UINT size = Align256(sizeof(XMFLOAT4X4) * kMaxBones);

    CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);
    auto desc = CD3DX12_RESOURCE_DESC::Buffer(size);

    ThrowIfFailed(dxCommon_->GetDevice()->CreateCommittedResource(
                      &heap, D3D12_HEAP_FLAG_NONE, &desc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&boneBuffer_)),
                  "CreateCommittedResource(BoneBuffer) failed");

    ThrowIfFailed(
        boneBuffer_->Map(0, nullptr, reinterpret_cast<void **>(&mappedBones_)),
        "BoneBuffer Map failed");
}