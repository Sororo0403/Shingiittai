#include "model/MaterialManager.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/DxUtils.h"
#include <limits>
#include <stdexcept>

using namespace DirectX;
using namespace DxUtils;
using Microsoft::WRL::ComPtr;

namespace {
const Material &FallbackMaterial() {
    static const Material fallback{};
    return fallback;
}
} // namespace

MaterialManager::~MaterialManager() {
    Finalize();
}

void MaterialManager::Initialize(DirectXCommon *dxCommon) {
    if (!dxCommon) {
        Finalize();
        return;
    }
    Finalize();
    dxCommon_ = dxCommon;
}

void MaterialManager::Finalize() {
    if (dxCommon_ && !dxCommon_->IsDeviceRemoved() &&
        !dxCommon_->IsCommandListRecording()) {
        dxCommon_->WaitForGpuIfPossible();
    }

    for (MaterialResource &material : materials_) {
        material.Reset();
    }
    materials_.clear();
    dxCommon_ = nullptr;
}

uint32_t MaterialManager::CreateMaterial(const Material &material) {
    if (!dxCommon_) {
        return UINT32_MAX;
    }

    MaterialResource matRes;
    matRes.material = NormalizeMaterialForDraw(material);

    UINT size = Align256(sizeof(Material));

    CD3DX12_HEAP_PROPERTIES heapProp(D3D12_HEAP_TYPE_UPLOAD);
    auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(size);

    ThrowIfFailed(dxCommon_->GetDevice()->CreateCommittedResource(
                      &heapProp, D3D12_HEAP_FLAG_NONE, &resourceDesc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&matRes.resource)),
                  "CreateCommittedResource(Material) failed");

    ThrowIfFailed(
        matRes.resource->Map(0, nullptr,
                             reinterpret_cast<void **>(&matRes.mappedData)),
        "Material resource Map failed");

    std::memcpy(matRes.mappedData, &matRes.material, sizeof(Material));

    if (materials_.size() >=
        static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
        return UINT32_MAX;
    }
    materials_.push_back(std::move(matRes));
    uint32_t materialId = static_cast<uint32_t>(materials_.size() - 1);

    return materialId;
}

void MaterialManager::SetMaterial(uint32_t materialId,
                                  const Material &material) {
    if (!IsValidMaterialId(materialId)) {
        return;
    }

    materials_[materialId].material = NormalizeMaterialForDraw(material);
    std::memcpy(materials_[materialId].mappedData,
                &materials_[materialId].material, sizeof(Material));
}

D3D12_GPU_VIRTUAL_ADDRESS
MaterialManager::GetGPUVirtualAddress(uint32_t materialId) const {
    if (!IsValidMaterialId(materialId)) {
        return 0;
    }

    return materials_[materialId].resource->GetGPUVirtualAddress();
}

const Material &MaterialManager::GetMaterial(uint32_t materialId) const {
    if (!IsValidMaterialId(materialId)) {
        return FallbackMaterial();
    }
    return materials_[materialId].material;
}

bool MaterialManager::IsValidMaterialId(uint32_t materialId) const {
    return materialId < materials_.size() &&
           materials_[materialId].resource != nullptr &&
           materials_[materialId].mappedData != nullptr;
}
