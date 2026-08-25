#include "model/MeshManager.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include <cstring>
#include <limits>
#include <utility>

using Microsoft::WRL::ComPtr;

namespace {
const Mesh &FallbackMesh() {
    static const Mesh fallback{};
    return fallback;
}

bool CreateUploadBuffer(ID3D12Device *device, const void *data, UINT size,
                        ComPtr<ID3D12Resource> &resource) {
    const CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_UPLOAD);
    const auto resourceDescription = CD3DX12_RESOURCE_DESC::Buffer(size);
    const HRESULT createResult = device->CreateCommittedResource(
        &heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDescription,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&resource));
    if (FAILED(createResult) || !resource) {
        return false;
    }
    void *mapped = nullptr;
    const HRESULT mapResult = resource->Map(0, nullptr, &mapped);
    if (FAILED(mapResult) || mapped == nullptr) {
        return false;
    }
    std::memcpy(mapped, data, size);
    resource->Unmap(0, nullptr);
    return true;
}
} // namespace

MeshManager::~MeshManager() { Finalize(); }

void MeshManager::Initialize(DirectXCommon *dxCommon) {
    if (!dxCommon) {
        Finalize();
        return;
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
    if (!dxCommon_ || !dxCommon_->GetDevice()) {
        return UINT32_MAX;
    }
    if (vertexStride == 0 || vertexCount == 0 || indexCount == 0) {
        return UINT32_MAX;
    }
    if (!vertexData || !indexData) {
        return UINT32_MAX;
    }

    Mesh mesh{};
    mesh.indexCount = indexCount;
    mesh.vertexStride = vertexStride;
    mesh.primitiveTopology = primitiveTopology;

    const uint64_t vbSize64 = static_cast<uint64_t>(vertexStride) *
                              static_cast<uint64_t>(vertexCount);
    const uint64_t ibSize64 =
        sizeof(uint32_t) * static_cast<uint64_t>(indexCount);
    if (vbSize64 > (std::numeric_limits<UINT>::max)() ||
        ibSize64 > (std::numeric_limits<UINT>::max)()) {
        return UINT32_MAX;
    }
    const UINT vbSize = static_cast<UINT>(vbSize64);
    const UINT ibSize = static_cast<UINT>(ibSize64);

    if (!CreateUploadBuffer(dxCommon_->GetDevice(), vertexData, vbSize,
                            mesh.vertexBuffer)) {
        return UINT32_MAX;
    }

    mesh.vbView.BufferLocation = mesh.vertexBuffer->GetGPUVirtualAddress();

    mesh.vbView.SizeInBytes = vbSize;
    mesh.vbView.StrideInBytes = vertexStride;

    if (!CreateUploadBuffer(dxCommon_->GetDevice(), indexData, ibSize,
                            mesh.indexBuffer)) {
        return UINT32_MAX;
    }

    mesh.ibView.BufferLocation = mesh.indexBuffer->GetGPUVirtualAddress();

    mesh.ibView.Format = DXGI_FORMAT_R32_UINT;
    mesh.ibView.SizeInBytes = ibSize;

    if (meshes_.size() >=
        static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
        return UINT32_MAX;
    }
    meshes_.push_back(std::move(mesh));
    uint32_t meshId = static_cast<uint32_t>(meshes_.size() - 1);

    return meshId;
}

const Mesh &MeshManager::GetMesh(uint32_t meshId) const {
    if (!IsValidMeshId(meshId)) {
        return FallbackMesh();
    }
    return meshes_[meshId];
}

bool MeshManager::IsValidMeshId(uint32_t meshId) const {
    return meshId < meshes_.size() && meshes_[meshId].vertexBuffer &&
           meshes_[meshId].indexBuffer && meshes_[meshId].indexCount > 0 &&
           meshes_[meshId].vertexStride > 0;
}
