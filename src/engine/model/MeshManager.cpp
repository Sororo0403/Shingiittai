#include "MeshManager.h"
#include "DirectXCommon.h"
#include "DxHelpers.h"
#include "DxUtils.h"

using namespace DxUtils;
using Microsoft::WRL::ComPtr;

void MeshManager::Initialize(DirectXCommon *dxCommon) { dxCommon_ = dxCommon; }

uint32_t MeshManager::CreateMesh(const void *vertexData, uint32_t vertexStride,
                                 uint32_t vertexCount,
                                 const uint16_t *indexData,
                                 uint32_t indexCount) {
    Mesh mesh{};
    mesh.indexCount = indexCount;
    mesh.vertexStride = vertexStride;

    // Vertex Buffer
    UINT vbSize = vertexStride * vertexCount;

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
    auto vbDesc = CD3DX12_RESOURCE_DESC::Buffer(vbSize);

    ThrowIfFailed(dxCommon_->GetDevice()->CreateCommittedResource(
                      &heapProps, D3D12_HEAP_FLAG_NONE, &vbDesc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&mesh.vertexBuffer)),
                  "CreateCommittedResource(VertexBuffer) failed");

    void *vbMapped = nullptr;
    mesh.vertexBuffer->Map(0, nullptr, &vbMapped);
    memcpy(vbMapped, vertexData, vbSize);
    mesh.vertexBuffer->Unmap(0, nullptr);

    mesh.vbView.BufferLocation = mesh.vertexBuffer->GetGPUVirtualAddress();
    mesh.vbView.SizeInBytes = vbSize;
    mesh.vbView.StrideInBytes = vertexStride;

    // Index Buffer
    UINT ibSize = sizeof(uint16_t) * indexCount;
    auto ibDesc = CD3DX12_RESOURCE_DESC::Buffer(ibSize);

    ThrowIfFailed(dxCommon_->GetDevice()->CreateCommittedResource(
                      &heapProps, D3D12_HEAP_FLAG_NONE, &ibDesc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&mesh.indexBuffer)),
                  "CreateCommittedResource(IndexBuffer) failed");

    void *ibMapped = nullptr;
    mesh.indexBuffer->Map(0, nullptr, &ibMapped);
    memcpy(ibMapped, indexData, ibSize);
    mesh.indexBuffer->Unmap(0, nullptr);

    mesh.ibView.BufferLocation = mesh.indexBuffer->GetGPUVirtualAddress();
    mesh.ibView.Format = DXGI_FORMAT_R16_UINT;
    mesh.ibView.SizeInBytes = ibSize;

    // 登録
    meshes_.push_back(mesh);
    return static_cast<uint32_t>(meshes_.size() - 1);
}

const Mesh &MeshManager::GetMesh(uint32_t meshId) const {
    return meshes_.at(meshId);
}
