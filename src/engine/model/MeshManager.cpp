#include "MeshManager.h"
#include "DirectXCommon.h"
#include "DxHelpers.h"
#include "DxUtils.h"
#include <sstream>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace {

void DebugLog(const std::string &message) {
#ifdef _WIN32
    OutputDebugStringA((message + "\n").c_str());
#endif
}

}

using namespace DxUtils;
using Microsoft::WRL::ComPtr;

void MeshManager::Initialize(DirectXCommon *dxCommon) { dxCommon_ = dxCommon; }

uint32_t MeshManager::CreateMesh(const void *vertexData, uint32_t vertexStride,
                                 uint32_t vertexCount,
                                 const uint32_t *indexData,
                                 uint32_t indexCount) {
    {
        std::ostringstream oss;
        oss << "[MeshManager] CreateMesh begin vtxCount=" << vertexCount
            << " vtxStride=" << vertexStride << " idxCount=" << indexCount
            << " vtxPtr=" << vertexData << " idxPtr=" << indexData;
        DebugLog(oss.str());
    }

    Mesh mesh{};
    mesh.indexCount = indexCount;
    mesh.vertexStride = vertexStride;

    UINT vbSize = vertexStride * vertexCount;

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
    auto vbDesc = CD3DX12_RESOURCE_DESC::Buffer(vbSize);

    ThrowIfFailed(dxCommon_->GetDevice()->CreateCommittedResource(
                      &heapProps, D3D12_HEAP_FLAG_NONE, &vbDesc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&mesh.vertexBuffer)),
                  "Create VertexBuffer failed");

    void *vbMapped = nullptr;
    mesh.vertexBuffer->Map(0, nullptr, &vbMapped);
    if (vbSize > 0 && vertexData) {
        memcpy(vbMapped, vertexData, vbSize);
    }
    mesh.vertexBuffer->Unmap(0, nullptr);

    mesh.vbView.BufferLocation = mesh.vertexBuffer->GetGPUVirtualAddress();

    mesh.vbView.SizeInBytes = vbSize;
    mesh.vbView.StrideInBytes = vertexStride;

    UINT ibSize = sizeof(uint32_t) * indexCount;

    auto ibDesc = CD3DX12_RESOURCE_DESC::Buffer(ibSize);

    ThrowIfFailed(dxCommon_->GetDevice()->CreateCommittedResource(
                      &heapProps, D3D12_HEAP_FLAG_NONE, &ibDesc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&mesh.indexBuffer)),
                  "Create IndexBuffer failed");

    void *ibMapped = nullptr;
    mesh.indexBuffer->Map(0, nullptr, &ibMapped);
    if (ibSize > 0 && indexData) {
        memcpy(ibMapped, indexData, ibSize);
    }
    mesh.indexBuffer->Unmap(0, nullptr);

    mesh.ibView.BufferLocation = mesh.indexBuffer->GetGPUVirtualAddress();

    mesh.ibView.Format = DXGI_FORMAT_R32_UINT;
    mesh.ibView.SizeInBytes = ibSize;

    meshes_.push_back(mesh);
    uint32_t meshId = static_cast<uint32_t>(meshes_.size() - 1);

    {
        std::ostringstream oss;
        oss << "[MeshManager] CreateMesh success meshId=" << meshId
            << " vbSize=" << vbSize << " ibSize=" << ibSize;
        DebugLog(oss.str());
    }

    return meshId;
}

const Mesh &MeshManager::GetMesh(uint32_t meshId) const {
    return meshes_.at(meshId);
}