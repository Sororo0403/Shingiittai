#include "model/MeshManager.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/DxUtils.h"
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

using namespace DxUtils;
using Microsoft::WRL::ComPtr;

MeshManager::~MeshManager() {
    Finalize();
}

void MeshManager::Initialize(DirectXCommon *dxCommon) {
    if (!dxCommon) {
        throw std::runtime_error("MeshManager::Initialize null argument");
    }
    Finalize();
    dxCommon_ = dxCommon;
}

void MeshManager::Finalize() {
    if (dxCommon_ && !dxCommon_->IsDeviceRemoved() &&
        !dxCommon_->IsCommandListRecording()) {
        dxCommon_->WaitForGpuIfPossible();
    }

    meshes_.clear();
    dxCommon_ = nullptr;
}

uint32_t MeshManager::CreateMesh(const void *vertexData, uint32_t vertexStride,
                                 uint32_t vertexCount,
                                 const uint32_t *indexData, uint32_t indexCount,
                                 D3D12_PRIMITIVE_TOPOLOGY primitiveTopology) {
    if (!dxCommon_) {
        throw std::runtime_error("MeshManager is not initialized");
    }
    if (vertexStride == 0 || vertexCount == 0 || indexCount == 0) {
        throw std::runtime_error("CreateMesh received empty mesh data");
    }
    if (!vertexData || !indexData) {
        throw std::runtime_error("CreateMesh received null mesh data");
    }

    Mesh mesh{};
    mesh.indexCount = indexCount;
    mesh.vertexStride = vertexStride;
    mesh.primitiveTopology = primitiveTopology;

    const uint64_t vbSize64 =
        static_cast<uint64_t>(vertexStride) * static_cast<uint64_t>(vertexCount);
    const uint64_t ibSize64 =
        sizeof(uint32_t) * static_cast<uint64_t>(indexCount);
    if (vbSize64 > (std::numeric_limits<UINT>::max)() ||
        ibSize64 > (std::numeric_limits<UINT>::max)()) {
        throw std::runtime_error("CreateMesh buffer is too large");
    }
    const UINT vbSize = static_cast<UINT>(vbSize64);
    const UINT ibSize = static_cast<UINT>(ibSize64);

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
    auto vbDesc = CD3DX12_RESOURCE_DESC::Buffer(vbSize);

    ThrowIfFailed(dxCommon_->GetDevice()->CreateCommittedResource(
                      &heapProps, D3D12_HEAP_FLAG_NONE, &vbDesc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&mesh.vertexBuffer)),
                  "Create VertexBuffer failed");

    void *vbMapped = nullptr;
    ThrowIfFailed(mesh.vertexBuffer->Map(0, nullptr, &vbMapped),
                  "Map VertexBuffer failed");
    memcpy(vbMapped, vertexData, vbSize);
    mesh.vertexBuffer->Unmap(0, nullptr);

    mesh.vbView.BufferLocation = mesh.vertexBuffer->GetGPUVirtualAddress();

    mesh.vbView.SizeInBytes = vbSize;
    mesh.vbView.StrideInBytes = vertexStride;

    auto ibDesc = CD3DX12_RESOURCE_DESC::Buffer(ibSize);

    ThrowIfFailed(dxCommon_->GetDevice()->CreateCommittedResource(
                      &heapProps, D3D12_HEAP_FLAG_NONE, &ibDesc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&mesh.indexBuffer)),
                  "Create IndexBuffer failed");

    void *ibMapped = nullptr;
    ThrowIfFailed(mesh.indexBuffer->Map(0, nullptr, &ibMapped),
                  "Map IndexBuffer failed");
    memcpy(ibMapped, indexData, ibSize);
    mesh.indexBuffer->Unmap(0, nullptr);

    mesh.ibView.BufferLocation = mesh.indexBuffer->GetGPUVirtualAddress();

    mesh.ibView.Format = DXGI_FORMAT_R32_UINT;
    mesh.ibView.SizeInBytes = ibSize;

    if (meshes_.size() >=
        static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
        throw std::runtime_error("MeshManager mesh id overflow");
    }
    meshes_.push_back(std::move(mesh));
    uint32_t meshId = static_cast<uint32_t>(meshes_.size() - 1);

    return meshId;
}

const Mesh &MeshManager::GetMesh(uint32_t meshId) const {
    if (!IsValidMeshId(meshId)) {
        throw std::out_of_range("MeshManager mesh id out of range");
    }
    return meshes_[meshId];
}

bool MeshManager::IsValidMeshId(uint32_t meshId) const {
    return meshId < meshes_.size();
}
