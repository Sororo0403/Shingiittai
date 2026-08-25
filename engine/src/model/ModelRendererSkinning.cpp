#include "model/ModelRenderer.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/DxUtils.h"
#include "graphics/ShaderCompiler.h"
#include "graphics/ShaderPaths.h"
#include "graphics/SrvManager.h"
#include "model/MaterialManager.h"
#include "model/MeshManager.h"
#include "model/Vertex.h"
#include "texture/TextureManager.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace DirectX;
using namespace DxUtils;
using Microsoft::WRL::ComPtr;

namespace {

constexpr UINT kSkinningThreadCount = 1024u;

uint32_t CheckedUint32Count(size_t count, const char *message) {
    (void)message;
    if (count > (std::numeric_limits<uint32_t>::max)()) {
        return UINT32_MAX;
    }
    return static_cast<uint32_t>(count);
}

UINT CheckedBufferSize(size_t elementSize, uint32_t count,
                       const char *message) {
    (void)message;
    if (count == 0 ||
        elementSize > (std::numeric_limits<size_t>::max)() / count) {
        return 0;
    }
    const size_t bytes = elementSize * count;
    if (bytes > (std::numeric_limits<UINT>::max)()) {
        return 0;
    }
    return static_cast<UINT>(bytes);
}

bool IsTransparentMaterial(const Material &material) {
    return material.blendMode == static_cast<int32_t>(BlendMode::Transparent) ||
           material.color.w < 1.0f;
}

class ScopedSrvAllocations {
  public:
    explicit ScopedSrvAllocations(SrvManager *srvManager)
        : srvManager_(srvManager) {}
    ~ScopedSrvAllocations() {
        if (srvManager_ == nullptr) {
            return;
        }
        for (UINT index : indices_) {
            srvManager_->FreeIfAllocated(index);
        }
    }

    UINT Allocate() {
        if (srvManager_ == nullptr || !srvManager_->CanAllocate()) {
            return UINT_MAX;
        }
        const UINT index = srvManager_->Allocate();
        if (index == UINT_MAX) {
            return UINT_MAX;
        }
        indices_.push_back(index);
        return index;
    }

    void Commit() { indices_.clear(); }

    ScopedSrvAllocations(const ScopedSrvAllocations &) = delete;
    ScopedSrvAllocations &operator=(const ScopedSrvAllocations &) = delete;

  private:
    SrvManager *srvManager_ = nullptr;
    std::vector<UINT> indices_;
};

class SkinClusterMapGuard {
  public:
    explicit SkinClusterMapGuard(Model &model) : model_(model) {}
    ~SkinClusterMapGuard() {
        if (!active_) {
            return;
        }
        for (ModelSubMesh &subMesh : model_.subMeshes) {
            SkinCluster &skinCluster = subMesh.skinCluster;
            if (skinCluster.influenceResource &&
                skinCluster.mappedInfluence != nullptr) {
                skinCluster.influenceResource->Unmap(0, nullptr);
                skinCluster.mappedInfluence = nullptr;
            }
            if (skinCluster.paletteResource &&
                skinCluster.mappedPalette != nullptr) {
                skinCluster.paletteResource->Unmap(0, nullptr);
                skinCluster.mappedPalette = nullptr;
            }
        }
    }

    SkinClusterMapGuard(const SkinClusterMapGuard &) = delete;
    SkinClusterMapGuard &operator=(const SkinClusterMapGuard &) = delete;

    void Commit() { active_ = false; }

  private:
    Model &model_;
    bool active_ = true;
};

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

}

static XMFLOAT4X4 StoreMatrix(const XMMATRIX &matrix) {
    XMFLOAT4X4 result{};
    XMStoreFloat4x4(&result, matrix);
    return result;
}

static XMMATRIX MakeSafeInverseTranspose(const XMMATRIX &matrix) {
    const XMVECTOR determinant = XMMatrixDeterminant(matrix);
    const float determinantValue = XMVectorGetX(determinant);
    if (!std::isfinite(determinantValue) ||
        std::abs(determinantValue) <= 0.000001f) {
        return XMMatrixIdentity();
    }

    return XMMatrixTranspose(XMMatrixInverse(nullptr, matrix));
}

static void NormalizeInfluence(VertexInfluence &influence) {
    float totalWeight = 0.0f;
    for (float weight : influence.weights) {
        totalWeight += weight;
    }

    if (totalWeight <= 0.00001f) {
        return;
    }

    for (float &weight : influence.weights) {
        weight /= totalWeight;
    }
}

static bool HasSkinningDescriptors(const SkinCluster &skinCluster) {
    return skinCluster.inputVertexSrvGpuHandle.ptr != 0 &&
           skinCluster.influenceSrvGpuHandle.ptr != 0 &&
           skinCluster.paletteSrvGpuHandle.ptr != 0 &&
           skinCluster.skinnedVertexUavGpuHandle.ptr != 0;
}

static bool CreateSkinnedVertexResources(
    ID3D12Device *device, SrvManager *srvManager, MeshManager *meshManager,
    ScopedSrvAllocations &allocations, ModelSubMesh &subMesh) {
    SkinCluster &cluster = subMesh.skinCluster;
    const Mesh &mesh = meshManager->GetMesh(subMesh.meshId);
    const UINT influenceBytes = CheckedBufferSize(
        sizeof(VertexInfluence), subMesh.vertexCount,
        "ModelRenderer influence buffer size overflow");
    if (influenceBytes == 0) {
        return false;
    }
    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    auto influenceDesc = CD3DX12_RESOURCE_DESC::Buffer(influenceBytes);
    ThrowIfFailed(device->CreateCommittedResource(
                      &uploadHeap, D3D12_HEAP_FLAG_NONE, &influenceDesc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&cluster.influenceResource)),
                  "CreateCommittedResource(InfluenceBuffer) failed");
    ThrowIfFailed(cluster.influenceResource->Map(
                      0, nullptr,
                      reinterpret_cast<void **>(&cluster.mappedInfluence)),
                  "InfluenceBuffer Map failed");
    cluster.influenceCount = subMesh.vertexCount;
    std::memset(cluster.mappedInfluence, 0, influenceBytes);

    const UINT inputIndex = allocations.Allocate();
    if (inputIndex == UINT_MAX) {
        return false;
    }
    cluster.inputVertexSrvIndex = inputIndex;
    cluster.inputVertexSrvCpuHandle = srvManager->GetCpuHandle(inputIndex);
    cluster.inputVertexSrvGpuHandle = srvManager->GetGpuHandle(inputIndex);
    if (cluster.inputVertexSrvCpuHandle.ptr == 0 ||
        cluster.inputVertexSrvGpuHandle.ptr == 0) {
        return false;
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC vertexSrv{};
    vertexSrv.Format = DXGI_FORMAT_UNKNOWN;
    vertexSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    vertexSrv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    vertexSrv.Buffer.NumElements = subMesh.vertexCount;
    vertexSrv.Buffer.StructureByteStride = sizeof(Vertex);
    device->CreateShaderResourceView(mesh.vertexBuffer.Get(), &vertexSrv,
                                     cluster.inputVertexSrvCpuHandle);

    const UINT influenceIndex = allocations.Allocate();
    if (influenceIndex == UINT_MAX) {
        return false;
    }
    cluster.influenceSrvIndex = influenceIndex;
    cluster.influenceSrvCpuHandle = srvManager->GetCpuHandle(influenceIndex);
    cluster.influenceSrvGpuHandle = srvManager->GetGpuHandle(influenceIndex);
    if (cluster.influenceSrvCpuHandle.ptr == 0 ||
        cluster.influenceSrvGpuHandle.ptr == 0) {
        return false;
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC influenceSrv = vertexSrv;
    influenceSrv.Buffer.StructureByteStride = sizeof(VertexInfluence);
    device->CreateShaderResourceView(cluster.influenceResource.Get(),
                                     &influenceSrv,
                                     cluster.influenceSrvCpuHandle);

    const UINT vertexBytes = CheckedBufferSize(
        sizeof(Vertex), subMesh.vertexCount,
        "ModelRenderer skinned vertex buffer size overflow");
    if (vertexBytes == 0) {
        return false;
    }
    CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
    auto vertexDesc = CD3DX12_RESOURCE_DESC::Buffer(
        vertexBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ThrowIfFailed(device->CreateCommittedResource(
                      &defaultHeap, D3D12_HEAP_FLAG_NONE, &vertexDesc,
                      D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, nullptr,
                      IID_PPV_ARGS(&cluster.skinnedVertexResource)),
                  "CreateCommittedResource(SkinnedVertexBuffer) failed");
    cluster.skinnedVertexBufferView = {
        cluster.skinnedVertexResource->GetGPUVirtualAddress(), vertexBytes,
        sizeof(Vertex)};
    cluster.skinnedVertexState =
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    cluster.lastSkinningFrame = 0;
    cluster.skinningValid = false;

    const UINT uavIndex = allocations.Allocate();
    if (uavIndex == UINT_MAX) {
        return false;
    }
    cluster.skinnedVertexUavIndex = uavIndex;
    cluster.skinnedVertexUavCpuHandle = srvManager->GetCpuHandle(uavIndex);
    cluster.skinnedVertexUavGpuHandle = srvManager->GetGpuHandle(uavIndex);
    if (cluster.skinnedVertexUavCpuHandle.ptr == 0 ||
        cluster.skinnedVertexUavGpuHandle.ptr == 0) {
        return false;
    }
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.Format = DXGI_FORMAT_UNKNOWN;
    uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Buffer.NumElements = subMesh.vertexCount;
    uav.Buffer.StructureByteStride = sizeof(Vertex);
    device->CreateUnorderedAccessView(cluster.skinnedVertexResource.Get(),
                                      nullptr, &uav,
                                      cluster.skinnedVertexUavCpuHandle);
    return true;
}

static void PopulateSkinInfluences(const Model &model, ModelSubMesh &subMesh) {
    SkinCluster &cluster = subMesh.skinCluster;
    for (const auto &[jointName, weightData] : subMesh.skinClusterData) {
        const auto joint = model.boneMap.find(jointName);
        if (joint == model.boneMap.end() ||
            joint->second >= cluster.inverseBindPoseMatrices.size()) {
            continue;
        }
        const uint32_t jointIndex = joint->second;
        cluster.inverseBindPoseMatrices[jointIndex] =
            weightData.inverseBindPoseMatrix;
        for (const VertexWeightData &weight : weightData.vertexWeights) {
            if (weight.vertexIndex >= cluster.influenceCount) {
                continue;
            }
            VertexInfluence &influence =
                cluster.mappedInfluence[weight.vertexIndex];
            for (uint32_t slot = 0; slot < kNumMaxInfluence; ++slot) {
                if (influence.weights[slot] == 0.0f) {
                    influence.weights[slot] = weight.weight;
                    influence.jointIndices[slot] =
                        static_cast<int32_t>(jointIndex);
                    break;
                }
            }
        }
    }
    for (uint32_t index = 0; index < cluster.influenceCount; ++index) {
        NormalizeInfluence(cluster.mappedInfluence[index]);
    }
}

static bool CreatePaletteResource(ID3D12Device *device, SrvManager *srvManager,
                                  ScopedSrvAllocations &allocations,
                                  uint32_t jointCount, SkinCluster &cluster) {
    const UINT paletteBytes = CheckedBufferSize(
        sizeof(WellForGPU), jointCount,
        "ModelRenderer palette buffer size overflow");
    if (paletteBytes == 0) {
        return false;
    }
    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    auto paletteDesc = CD3DX12_RESOURCE_DESC::Buffer(paletteBytes);
    ThrowIfFailed(device->CreateCommittedResource(
                      &uploadHeap, D3D12_HEAP_FLAG_NONE, &paletteDesc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&cluster.paletteResource)),
                  "CreateCommittedResource(PaletteBuffer) failed");
    ThrowIfFailed(cluster.paletteResource->Map(
                      0, nullptr,
                      reinterpret_cast<void **>(&cluster.mappedPalette)),
                  "PaletteBuffer Map failed");
    cluster.paletteCount = jointCount;
    for (uint32_t index = 0; index < jointCount; ++index) {
        cluster.mappedPalette[index].skeletonSpaceMatrix =
            StoreMatrix(XMMatrixTranspose(XMMatrixIdentity()));
        cluster.mappedPalette[index].skeletonSpaceInverseTransposeMatrix =
            StoreMatrix(XMMatrixTranspose(XMMatrixIdentity()));
    }
    const UINT srvIndex = allocations.Allocate();
    if (srvIndex == UINT_MAX) {
        return false;
    }
    cluster.paletteSrvIndex = srvIndex;
    cluster.paletteSrvCpuHandle = srvManager->GetCpuHandle(srvIndex);
    cluster.paletteSrvGpuHandle = srvManager->GetGpuHandle(srvIndex);
    if (cluster.paletteSrvCpuHandle.ptr == 0 ||
        cluster.paletteSrvGpuHandle.ptr == 0) {
        return false;
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_UNKNOWN;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv.Buffer.NumElements = jointCount;
    srv.Buffer.StructureByteStride = sizeof(WellForGPU);
    device->CreateShaderResourceView(cluster.paletteResource.Get(), &srv,
                                     cluster.paletteSrvCpuHandle);
    return true;
}

void ModelRenderer::CreateSkinClusters(Model &model) {
    if (!dxCommon_ || !srvManager_ || !meshManager_) {
        return;
    }
    auto *device = dxCommon_->GetDevice();
    if (device == nullptr) {
        return;
    }

    ScopedSrvAllocations allocations(srvManager_);
    SkinClusterMapGuard mapGuard(model);
    for (ModelSubMesh &subMesh : model.subMeshes) {
        if (!meshManager_->IsValidMeshId(subMesh.meshId)) {
            continue;
        }
        const uint32_t jointCount = std::max<uint32_t>(
            1, CheckedUint32Count(model.bones.size(),
                                  "ModelRenderer bone count overflow"));
        if (jointCount == UINT32_MAX) {
            continue;
        }
        const bool needsSkinnedBuffers =
            subMesh.vertexCount > 0 && !subMesh.skinClusterData.empty();
        if (!srvManager_->CanAllocate(needsSkinnedBuffers ? 4u : 1u)) {
            return;
        }

        SkinCluster &cluster = subMesh.skinCluster;
        cluster.inverseBindPoseMatrices.assign(
            jointCount, StoreMatrix(XMMatrixIdentity()));
        if (needsSkinnedBuffers) {
            if (!CreateSkinnedVertexResources(device, srvManager_, meshManager_,
                                              allocations, subMesh)) {
                continue;
            }
            PopulateSkinInfluences(model, subMesh);
        }
        if (!CreatePaletteResource(device, srvManager_, allocations, jointCount,
                                   cluster)) {
            continue;
        }
    }

    UpdateSkinClusters(model);
    mapGuard.Commit();
    allocations.Commit();
}
void ModelRenderer::UpdateSkinClusters(Model &model) {
    for (auto &subMesh : model.subMeshes) {
        SkinCluster &skinCluster = subMesh.skinCluster;
        if (!skinCluster.mappedPalette || skinCluster.paletteCount == 0) {
            continue;
        }

        skinCluster.skinningValid = false;

        if (model.bones.empty() || model.skeletonSpaceMatrices.empty()) {
            skinCluster.mappedPalette[0].skeletonSpaceMatrix =
                StoreMatrix(XMMatrixTranspose(XMMatrixIdentity()));
            skinCluster.mappedPalette[0].skeletonSpaceInverseTransposeMatrix =
                StoreMatrix(XMMatrixTranspose(XMMatrixIdentity()));
            continue;
        }

        const uint32_t jointCount = std::min<uint32_t>(
            skinCluster.paletteCount,
            static_cast<uint32_t>(model.skeletonSpaceMatrices.size()));

        for (uint32_t jointIndex = 0; jointIndex < jointCount; ++jointIndex) {
            const XMMATRIX inverseBindPose =
                jointIndex < skinCluster.inverseBindPoseMatrices.size()
                    ? XMLoadFloat4x4(
                          &skinCluster.inverseBindPoseMatrices[jointIndex])
                    : XMMatrixIdentity();
            XMMATRIX skeletonSpace =
                XMLoadFloat4x4(&model.skeletonSpaceMatrices[jointIndex]);
            XMMATRIX skinningMatrix = inverseBindPose * skeletonSpace;
            XMMATRIX skinningInverseTranspose =
                MakeSafeInverseTranspose(skinningMatrix);

            XMStoreFloat4x4(
                &skinCluster.mappedPalette[jointIndex].skeletonSpaceMatrix,
                XMMatrixTranspose(skinningMatrix));
            XMStoreFloat4x4(&skinCluster.mappedPalette[jointIndex]
                                 .skeletonSpaceInverseTransposeMatrix,
                            XMMatrixTranspose(skinningInverseTranspose));
        }
    }
}
void ModelRenderer::CreateSkinningRootSignature() {
    CD3DX12_ROOT_PARAMETER params[5]{};

    params[0].InitAsConstants(1, 0);

    CD3DX12_DESCRIPTOR_RANGE inputVertexRange{};
    inputVertexRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    params[1].InitAsDescriptorTable(1, &inputVertexRange);

    CD3DX12_DESCRIPTOR_RANGE influenceRange{};
    influenceRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
    params[2].InitAsDescriptorTable(1, &influenceRange);

    CD3DX12_DESCRIPTOR_RANGE matrixPaletteRange{};
    matrixPaletteRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);
    params[3].InitAsDescriptorTable(1, &matrixPaletteRange);

    CD3DX12_DESCRIPTOR_RANGE skinnedVertexRange{};
    skinnedVertexRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
    params[4].InitAsDescriptorTable(1, &skinnedVertexRange);

    CD3DX12_ROOT_SIGNATURE_DESC desc;
    desc.Init(_countof(params), params, 0, nullptr);

    ComPtr<ID3DBlob> blob, error;

    ThrowIfFailed(D3D12SerializeRootSignature(
                      &desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error),
                  "D3D12SerializeRootSignature(Skinning) failed");

    ThrowIfFailed(dxCommon_->GetDevice()->CreateRootSignature(
                      0, blob->GetBufferPointer(), blob->GetBufferSize(),
                      IID_PPV_ARGS(&skinningRootSignature_)),
                  "CreateRootSignature(Skinning) failed");
}
void ModelRenderer::CreateSkinningPipelineState() {
    auto cs =
        ShaderCompiler::Compile(ShaderPaths::SkinningCS, "main", "cs_6_6");

    D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = skinningRootSignature_.Get();
    pso.CS = {cs->GetBufferPointer(), cs->GetBufferSize()};

    ThrowIfFailed(dxCommon_->GetDevice()->CreateComputePipelineState(
                      &pso, IID_PPV_ARGS(&skinningPSO_)),
                  "CreateComputePipelineState(Skinning) failed");
}

bool ModelRenderer::NeedsSkinningDispatch(const ModelSubMesh &subMesh) const {
    const SkinCluster &skinCluster = subMesh.skinCluster;
    return skinCluster.skinnedVertexResource && subMesh.vertexCount > 0 &&
           HasSkinningDescriptors(skinCluster) &&
           (!skinCluster.skinningValid ||
            skinCluster.lastSkinningFrame != skinningFrameId_);
}

void ModelRenderer::PrepareSkinning(const Model &model) {
    DispatchSkinningBatch(model);
}

void ModelRenderer::PrepareSkinning(
    const std::vector<const Model *> &models) {
    DispatchSkinningBatch(models);
}

void ModelRenderer::DispatchSkinningBatch(const Model &model) {
    std::vector<const ModelSubMesh *> jobs;
    jobs.reserve(model.subMeshes.size());

    for (const auto &subMesh : model.subMeshes) {
        if (NeedsSkinningDispatch(subMesh)) {
            jobs.push_back(&subMesh);
        }
    }

    DispatchSkinningJobs(jobs);
}

void ModelRenderer::DispatchSkinningBatch(
    const std::vector<const Model *> &models) {
    std::vector<const ModelSubMesh *> jobs;
    for (const Model *model : models) {
        if (!model) {
            continue;
        }
        jobs.reserve(jobs.size() + model->subMeshes.size());
        for (const auto &subMesh : model->subMeshes) {
            if (NeedsSkinningDispatch(subMesh)) {
                jobs.push_back(&subMesh);
            }
        }
    }

    DispatchSkinningJobs(jobs);
}

void ModelRenderer::DispatchSkinningJobs(
    const std::vector<const ModelSubMesh *> &jobs) {
    for (const ModelSubMesh *job : jobs) {
        if (job) {
            DispatchSkinning(*job);
        }
    }
}

void ModelRenderer::DispatchSkinning(const ModelSubMesh &subMesh) {
    const SkinCluster &skinCluster = subMesh.skinCluster;
    if (!NeedsSkinningDispatch(subMesh) || dxCommon_ == nullptr ||
        srvManager_ == nullptr || skinningRootSignature_ == nullptr ||
        skinningPSO_ == nullptr) {
        return;
    }

    auto cmd = dxCommon_->GetCommandList();
    ID3D12DescriptorHeap *heaps[] = {srvManager_->GetHeap()};
    cmd->SetDescriptorHeaps(1, heaps);

    if (skinCluster.skinnedVertexState !=
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        auto toUav = CD3DX12_RESOURCE_BARRIER::Transition(
            skinCluster.skinnedVertexResource.Get(),
            skinCluster.skinnedVertexState,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmd->ResourceBarrier(1, &toUav);
        skinCluster.skinnedVertexState =
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    cmd->SetPipelineState(skinningPSO_.Get());
    currentGraphicsPipelineState_ = nullptr;
    cmd->SetComputeRootSignature(skinningRootSignature_.Get());
    cmd->SetComputeRoot32BitConstant(0, subMesh.vertexCount, 0);
    cmd->SetComputeRootDescriptorTable(1, skinCluster.inputVertexSrvGpuHandle);
    cmd->SetComputeRootDescriptorTable(2, skinCluster.influenceSrvGpuHandle);
    cmd->SetComputeRootDescriptorTable(3, skinCluster.paletteSrvGpuHandle);
    cmd->SetComputeRootDescriptorTable(
        4, skinCluster.skinnedVertexUavGpuHandle);

    const UINT threadGroupCount =
        (subMesh.vertexCount + kSkinningThreadCount - 1u) /
        kSkinningThreadCount;
    cmd->Dispatch(threadGroupCount, 1, 1);

    auto uavBarrier =
        CD3DX12_RESOURCE_BARRIER::UAV(skinCluster.skinnedVertexResource.Get());
    cmd->ResourceBarrier(1, &uavBarrier);

    auto toVertex = CD3DX12_RESOURCE_BARRIER::Transition(
        skinCluster.skinnedVertexResource.Get(),
        skinCluster.skinnedVertexState,
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    cmd->ResourceBarrier(1, &toVertex);
    skinCluster.skinnedVertexState =
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    skinCluster.lastSkinningFrame = skinningFrameId_;
    skinCluster.skinningValid = true;
}
