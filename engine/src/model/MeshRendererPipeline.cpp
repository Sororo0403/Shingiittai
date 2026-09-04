#include "model/MeshRenderer.h"

#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/DxUtils.h"
#include "graphics/ShaderCompiler.h"
#include "graphics/ShaderPaths.h"
#include "graphics/SrvManager.h"
#include "model/Vertex.h"
#include "texture/TextureManager.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

using namespace DirectX;
using namespace DxUtils;
using Microsoft::WRL::ComPtr;

namespace {

struct PerObjectConstBufferData {
    XMFLOAT4X4 matWVP;
    XMFLOAT4X4 matWorld;
    XMFLOAT4X4 matWorldInverseTranspose;
};

struct SceneConstBufferData {
    struct PointLightData {
        XMFLOAT4 positionRange;
        XMFLOAT4 colorIntensity;
    };

    XMFLOAT4 cameraPos;
    XMFLOAT4 keyLightDirection;
    XMFLOAT4 keyLightColor;
    XMFLOAT4 fillLightDirection;
    XMFLOAT4 fillLightColor;
    XMFLOAT4 ambientColor;
    PointLightData pointLights[2];
    XMFLOAT4 lightingParams;
    XMFLOAT4 fogColor;
    XMFLOAT4 fogParams;
    XMFLOAT4X4 viewProjection;
    XMFLOAT4X4 lightViewProjection;
    XMFLOAT4 shadowParams;
    XMFLOAT4 shadowFilterParams;
    XMFLOAT4 customSceneParams0;
    XMFLOAT4 customSceneParams1;
};

XMVECTOR LoadNormalizedQuaternionOrIdentity(const XMFLOAT4 &rotation) {
    if (!std::isfinite(rotation.x) || !std::isfinite(rotation.y) ||
        !std::isfinite(rotation.z) || !std::isfinite(rotation.w)) {
        return XMQuaternionIdentity();
    }
    XMVECTOR q = XMLoadFloat4(&rotation);
    const float lengthSq = XMVectorGetX(XMVector4LengthSq(q));
    if (!std::isfinite(lengthSq) || lengthSq <= 0.000001f) {
        return XMQuaternionIdentity();
    }
    return XMQuaternionNormalize(q);
}

XMMATRIX MakeWorldMatrix(const Transform &transform) {
    const Transform safeTransform = SanitizeTransformForDraw(transform);
    XMVECTOR q = LoadNormalizedQuaternionOrIdentity(safeTransform.rotation);
    return XMMatrixScaling(safeTransform.scale.x, safeTransform.scale.y,
                           safeTransform.scale.z) *
           XMMatrixRotationQuaternion(q) *
           XMMatrixTranslation(safeTransform.position.x,
                               safeTransform.position.y,
                               safeTransform.position.z);
}

XMMATRIX MakeWorldInverseTranspose(const XMMATRIX &world) {
    const XMVECTOR determinant = XMMatrixDeterminant(world);
    const float determinantValue = XMVectorGetX(determinant);
    if (!std::isfinite(determinantValue) ||
        std::abs(determinantValue) <= 0.000001f) {
        return XMMatrixIdentity();
    }
    return XMMatrixTranspose(XMMatrixInverse(nullptr, world));
}

bool IsTransparentMaterial(const Material &material) {
    return material.blendMode == static_cast<int32_t>(BlendMode::Transparent) ||
           material.color.w < 1.0f;
}

D3D12_CULL_MODE ToD3D12CullMode(const MaterialCullMode mode) {
    switch (mode) {
    case MaterialCullMode::None:
        return D3D12_CULL_MODE_NONE;
    case MaterialCullMode::Front:
        return D3D12_CULL_MODE_FRONT;
    case MaterialCullMode::Back:
    default:
        return D3D12_CULL_MODE_BACK;
    }
}

size_t PipelineVariantIndex(bool transparent, MaterialCullMode cullMode,
                            bool depthWrite) {
    const size_t blendIndex = transparent ? 1 : 0;
    const size_t cullIndex = static_cast<size_t>(cullMode);
    const size_t depthIndex = depthWrite ? 1 : 0;
    return blendIndex * 6 + cullIndex * 2 + depthIndex;
}

size_t PipelineVariantIndex(const Material &material) {
    const Material drawMaterial = NormalizeMaterialForDraw(material);
    MaterialCullMode cullMode =
        static_cast<MaterialCullMode>(drawMaterial.cullMode);
    if (drawMaterial.cullMode < static_cast<int32_t>(MaterialCullMode::None) ||
        drawMaterial.cullMode > static_cast<int32_t>(MaterialCullMode::Back)) {
        cullMode = MaterialCullMode::Back;
    }
    return PipelineVariantIndex(IsTransparentMaterial(drawMaterial), cullMode,
                                drawMaterial.depthWrite != 0);
}

uint32_t ResolveNormalTextureId(TextureManager *textureManager,
                                uint32_t normalTextureId) {
    return normalTextureId == UINT32_MAX
               ? textureManager->GetDefaultNormalTextureId()
               : normalTextureId;
}

uint32_t ResolveBaseColorTextureId(const Material &material,
                                   uint32_t fallbackTextureId) {
    return material.baseColorTextureId == UINT32_MAX
               ? fallbackTextureId
               : material.baseColorTextureId;
}

uint32_t ResolveNormalTextureId(TextureManager *textureManager,
                                const Material &material,
                                uint32_t fallbackTextureId) {
    const uint32_t textureId = material.normalTextureId == UINT32_MAX
                                   ? fallbackTextureId
                                   : material.normalTextureId;
    return ResolveNormalTextureId(textureManager, textureId);
}

void CreateMeshPipelineState(
    ID3D12Device *device, ID3D12RootSignature *rootSignature,
    D3D12_SHADER_BYTECODE vertexShader, D3D12_SHADER_BYTECODE pixelShader,
    D3D12_INPUT_LAYOUT_DESC inputLayout, bool transparent,
    MaterialCullMode cullMode, bool depthWrite,
    ComPtr<ID3D12PipelineState> &pipelineState, const char *errorMessage) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = rootSignature;
    pso.VS = vertexShader;
    pso.PS = pixelShader;
    pso.InputLayout = inputLayout;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DirectXCommon::kSceneColorFormat;
    pso.DSVFormat = DirectXCommon::kDepthStencilFormat;
    pso.SampleDesc.Count = 1;
    pso.SampleMask = UINT_MAX;
    pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    pso.RasterizerState.CullMode = ToD3D12CullMode(cullMode);

    D3D12_BLEND_DESC blend = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    blend.RenderTarget[0].BlendEnable = transparent ? TRUE : FALSE;
    blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blend.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.BlendState = blend;

    D3D12_DEPTH_STENCIL_DESC depth =
        CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    depth.DepthEnable = TRUE;
    depth.DepthWriteMask = depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL
                                      : D3D12_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    pso.DepthStencilState = depth;

    ThrowIfFailed(device->CreateGraphicsPipelineState(
                      &pso, IID_PPV_ARGS(&pipelineState)),
                  errorMessage);
}

void CreateInstancedShadowPipelineState(
    ID3D12Device *device, ID3D12RootSignature *rootSignature,
    D3D12_SHADER_BYTECODE vertexShader, D3D12_SHADER_BYTECODE pixelShader,
    D3D12_INPUT_LAYOUT_DESC inputLayout,
    ComPtr<ID3D12PipelineState> &pipelineState) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = rootSignature;
    pso.VS = vertexShader;
    pso.PS = pixelShader;
    pso.InputLayout = inputLayout;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 0;
    pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pso.SampleDesc.Count = 1;
    pso.SampleMask = UINT_MAX;
    pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    pso.RasterizerState.DepthBias = 1000;
    pso.RasterizerState.SlopeScaledDepthBias = 1.5f;
    pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

    D3D12_DEPTH_STENCIL_DESC depth =
        CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    depth.DepthEnable = TRUE;
    depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    pso.DepthStencilState = depth;

    ThrowIfFailed(device->CreateGraphicsPipelineState(
                      &pso, IID_PPV_ARGS(&pipelineState)),
                  "CreateGraphicsPipelineState(CustomInstancedShadow) failed");
}

} // namespace

void MeshRenderer::CreateRootSignature() {
    CD3DX12_ROOT_PARAMETER params[6]{};
    params[0].InitAsConstantBufferView(0);
    params[1].InitAsConstantBufferView(1);
    params[2].InitAsConstantBufferView(2);

    CD3DX12_DESCRIPTOR_RANGE textureRange{};
    textureRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    params[3].InitAsDescriptorTable(1, &textureRange);

    CD3DX12_DESCRIPTOR_RANGE shadowRange{};
    shadowRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3);
    params[4].InitAsDescriptorTable(1, &shadowRange);

    CD3DX12_DESCRIPTOR_RANGE normalRange{};
    normalRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4);
    params[5].InitAsDescriptorTable(1, &normalRange);

    CD3DX12_STATIC_SAMPLER_DESC sampler(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;

    CD3DX12_ROOT_SIGNATURE_DESC desc;
    desc.Init(_countof(params), params, 1, &sampler,
              D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> blob, error;
    ThrowIfFailed(D3D12SerializeRootSignature(
                      &desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error),
                  "D3D12SerializeRootSignature(MeshRenderer) failed");
    ThrowIfFailed(dxCommon_->GetDevice()->CreateRootSignature(
                      0, blob->GetBufferPointer(), blob->GetBufferSize(),
                      IID_PPV_ARGS(&rootSignature_)),
                  "CreateRootSignature(MeshRenderer) failed");
}

void MeshRenderer::CreateShadowRootSignature() {
    CD3DX12_ROOT_PARAMETER params[4]{};
    params[0].InitAsConstantBufferView(0);
    params[1].InitAsConstantBufferView(1);
    params[2].InitAsConstantBufferView(2);

    CD3DX12_DESCRIPTOR_RANGE textureRange{};
    textureRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    params[3].InitAsDescriptorTable(1, &textureRange);

    CD3DX12_STATIC_SAMPLER_DESC sampler(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;

    CD3DX12_ROOT_SIGNATURE_DESC desc;
    desc.Init(_countof(params), params, 1, &sampler,
              D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> blob, error;
    ThrowIfFailed(D3D12SerializeRootSignature(
                      &desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error),
                  "D3D12SerializeRootSignature(MeshShadow) failed");
    ThrowIfFailed(dxCommon_->GetDevice()->CreateRootSignature(
                      0, blob->GetBufferPointer(), blob->GetBufferSize(),
                      IID_PPV_ARGS(&shadowRootSignature_)),
                  "CreateRootSignature(MeshShadow) failed");
}

void MeshRenderer::CreatePipelineStates() {
    auto *device = dxCommon_->GetDevice();
    auto vs = ShaderCompiler::Compile(ShaderPaths::MeshVS, "main", "vs_6_6");
    auto instancedVs =
        ShaderCompiler::Compile(ShaderPaths::MeshInstancedVS, "main", "vs_6_6");
    auto ps = ShaderCompiler::Compile(ShaderPaths::MeshPS, "main", "ps_6_6");

    D3D12_INPUT_ELEMENT_DESC baseLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"CUSTOM", 0, DXGI_FORMAT_R32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_INPUT_ELEMENT_DESC instancedLayout[] = {
        baseLayout[0],
        baseLayout[1],
        baseLayout[2],
        baseLayout[3],
        baseLayout[4],
        baseLayout[5],
        {"WORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0,
         D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"WORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"WORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"WORLD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"INSTANCECOLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"INSTANCEFADE", 0, DXGI_FORMAT_R32_FLOAT, 1,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"INSTANCESEED", 0, DXGI_FORMAT_R32_UINT, 1,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
    };

    for (bool transparent : {false, true}) {
        for (MaterialCullMode cullMode :
             {MaterialCullMode::None, MaterialCullMode::Front,
              MaterialCullMode::Back}) {
            for (bool depthWrite : {false, true}) {
                const size_t index =
                    PipelineVariantIndex(transparent, cullMode, depthWrite);
                CreateMeshPipelineState(
                    device, rootSignature_.Get(),
                    {vs->GetBufferPointer(), vs->GetBufferSize()},
                    {ps->GetBufferPointer(), ps->GetBufferSize()},
                    {baseLayout, _countof(baseLayout)}, transparent, cullMode,
                    depthWrite, pipelineStates_[index],
                    "CreateGraphicsPipelineState(MeshRenderer) failed");
                CreateMeshPipelineState(
                    device, rootSignature_.Get(),
                    {instancedVs->GetBufferPointer(),
                     instancedVs->GetBufferSize()},
                    {ps->GetBufferPointer(), ps->GetBufferSize()},
                    {instancedLayout, _countof(instancedLayout)}, transparent,
                    cullMode, depthWrite, instancedPipelineStates_[index],
                    "CreateGraphicsPipelineState(MeshRenderer) failed");
            }
        }
    }
}

uint32_t MeshRenderer::CreatePipeline(const MeshPipelineDesc &desc) {
    if (!dxCommon_ || !rootSignature_) {
        return UINT32_MAX;
    }

    D3D12_INPUT_ELEMENT_DESC baseLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"CUSTOM", 0, DXGI_FORMAT_R32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    MeshPipelineSet pipelineSet = MeshPipelineFactory::CreatePipelineSet(
        dxCommon_->GetDevice(), rootSignature_.Get(), desc,
        {baseLayout, _countof(baseLayout)}, DirectXCommon::kSceneColorFormat,
        DirectXCommon::kDepthStencilFormat);
    if (customPipelines_.size() >=
        static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
        return UINT32_MAX;
    }
    const uint32_t pipelineId = static_cast<uint32_t>(customPipelines_.size());
    customPipelines_.push_back(std::move(pipelineSet));
    return pipelineId;
}

uint32_t MeshRenderer::CreatePipeline(const std::wstring &vertexShaderPath,
                                      const std::wstring &pixelShaderPath) {
    MeshPipelineDesc desc{};
    desc.vertexShader = vertexShaderPath;
    desc.pixelShader = pixelShaderPath;
    desc.variantMode = MeshPipelineVariantMode::MaterialDriven;
    return CreatePipeline(desc);
}

uint32_t MeshRenderer::CreateAdditiveNoDepthPipeline(
    const std::wstring &vertexShaderPath, const std::wstring &pixelShaderPath) {
    MeshPipelineDesc desc{};
    desc.vertexShader = vertexShaderPath;
    desc.pixelShader = pixelShaderPath;
    desc.blend = MeshBlendMode::Additive;
    desc.depth = MeshDepthMode::None;
    desc.cull = MeshCullMode::None;
    desc.variantMode = MeshPipelineVariantMode::Fixed;
    return CreatePipeline(desc);
}

uint32_t MeshRenderer::CreateInstancedPipeline(
    const std::wstring &vertexShaderPath, const std::wstring &pixelShaderPath,
    const std::wstring &shadowVertexShaderPath,
    const std::wstring &shadowPixelShaderPath) {
    if (!dxCommon_ || !rootSignature_ || !shadowRootSignature_) {
        return UINT32_MAX;
    }

    auto *device = dxCommon_->GetDevice();
    InstancedPipelineSet pipelineSet{};
    auto instancedVs =
        ShaderCompiler::Compile(vertexShaderPath, "main", "vs_6_6");
    auto ps = ShaderCompiler::Compile(pixelShaderPath, "main", "ps_6_6");

    D3D12_INPUT_ELEMENT_DESC instancedLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"CUSTOM", 0, DXGI_FORMAT_R32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"WORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0,
         D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"WORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"WORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"WORLD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"INSTANCECOLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"INSTANCEFADE", 0, DXGI_FORMAT_R32_FLOAT, 1,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"INSTANCESEED", 0, DXGI_FORMAT_R32_UINT, 1,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
    };

    for (bool transparent : {false, true}) {
        for (MaterialCullMode cullMode :
             {MaterialCullMode::None, MaterialCullMode::Front,
              MaterialCullMode::Back}) {
            for (bool depthWrite : {false, true}) {
                const size_t index =
                    PipelineVariantIndex(transparent, cullMode, depthWrite);
                CreateMeshPipelineState(
                    device, rootSignature_.Get(),
                    {instancedVs->GetBufferPointer(),
                     instancedVs->GetBufferSize()},
                    {ps->GetBufferPointer(), ps->GetBufferSize()},
                    {instancedLayout, _countof(instancedLayout)}, transparent,
                    cullMode, depthWrite, pipelineSet.pipelineStates[index],
                    "CreateGraphicsPipelineState(CustomInstancedMesh) failed");
            }
        }
    }

    auto shadowInstancedVs =
        ShaderCompiler::Compile(shadowVertexShaderPath, "main", "vs_6_6");
    auto shadowPs =
        ShaderCompiler::Compile(shadowPixelShaderPath, "main", "ps_6_6");

    CreateInstancedShadowPipelineState(
        device, shadowRootSignature_.Get(),
        {shadowInstancedVs->GetBufferPointer(),
         shadowInstancedVs->GetBufferSize()},
        {shadowPs->GetBufferPointer(), shadowPs->GetBufferSize()},
        {instancedLayout, _countof(instancedLayout)},
        pipelineSet.shadowPipelineState);

    if (customInstancedPipelines_.size() >=
        static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
        return UINT32_MAX;
    }
    const uint32_t pipelineId =
        static_cast<uint32_t>(customInstancedPipelines_.size());
    customInstancedPipelines_.push_back(std::move(pipelineSet));
    return pipelineId;
}

void MeshRenderer::CreateShadowPipelineStates() {
    auto *device = dxCommon_->GetDevice();
    auto vs =
        ShaderCompiler::Compile(ShaderPaths::MeshShadowVS, "main", "vs_6_6");
    auto instancedVs = ShaderCompiler::Compile(
        ShaderPaths::MeshShadowInstancedVS, "main", "vs_6_6");
    auto ps =
        ShaderCompiler::Compile(ShaderPaths::MeshShadowPS, "main", "ps_6_6");

    D3D12_INPUT_ELEMENT_DESC baseLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"CUSTOM", 0, DXGI_FORMAT_R32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_INPUT_ELEMENT_DESC instancedLayout[] = {
        baseLayout[0],
        baseLayout[1],
        baseLayout[2],
        baseLayout[3],
        baseLayout[4],
        baseLayout[5],
        {"WORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0,
         D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"WORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"WORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"WORLD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"INSTANCECOLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"INSTANCEFADE", 0, DXGI_FORMAT_R32_FLOAT, 1,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"INSTANCESEED", 0, DXGI_FORMAT_R32_UINT, 1,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
    };

    auto makePso = [&](D3D12_SHADER_BYTECODE vertexShader,
                       D3D12_INPUT_LAYOUT_DESC inputLayout,
                       ComPtr<ID3D12PipelineState> &psoOut) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature = shadowRootSignature_.Get();
        pso.VS = vertexShader;
        pso.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
        pso.InputLayout = inputLayout;
        pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso.NumRenderTargets = 0;
        pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        pso.SampleDesc.Count = 1;
        pso.SampleMask = UINT_MAX;
        pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        pso.RasterizerState.DepthBias = 1000;
        pso.RasterizerState.SlopeScaledDepthBias = 1.5f;
        pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

        D3D12_DEPTH_STENCIL_DESC depth =
            CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        depth.DepthEnable = TRUE;
        depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        pso.DepthStencilState = depth;

        ThrowIfFailed(
            device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&psoOut)),
            "CreateGraphicsPipelineState(MeshShadow) failed");
    };

    makePso({vs->GetBufferPointer(), vs->GetBufferSize()},
            {baseLayout, _countof(baseLayout)}, shadowPSO_);
    makePso({instancedVs->GetBufferPointer(), instancedVs->GetBufferSize()},
            {instancedLayout, _countof(instancedLayout)}, instancedShadowPSO_);
}
