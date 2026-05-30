#include "graphics/SrvManager.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/DxUtils.h"
#include <algorithm>
#include <stdexcept>

using namespace DxUtils;

void SrvManager::Initialize(DirectXCommon *dxCommon, UINT maxSrvCount) {
    if (!dxCommon) {
        throw std::runtime_error("SrvManager::Initialize null argument");
    }
    if (maxSrvCount == 0) {
        throw std::runtime_error("SrvManager::Initialize invalid descriptor count");
    }

    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = maxSrvCount;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    ThrowIfFailed(dxCommon->GetDevice()->CreateDescriptorHeap(
                      &desc, IID_PPV_ARGS(&heap_)),
                  "Create SRV Heap failed");

    descriptorSize_ = dxCommon->GetDevice()->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    maxSrvCount_ = maxSrvCount;
    currentIndex_ = 0;
    freeList_.clear();
    allocated_.assign(maxSrvCount_, false);
}

UINT SrvManager::Allocate() {
    if (!freeList_.empty()) {
        const UINT index = freeList_.back();
        freeList_.pop_back();
        allocated_[index] = true;
        return index;
    }

    if (currentIndex_ >= maxSrvCount_) {
        throw std::runtime_error("SRV descriptor heap exhausted");
    }

    const UINT index = currentIndex_++;
    allocated_[index] = true;
    return index;
}

UINT SrvManager::AllocateRange(UINT count) {
    if (count == 0) {
        return UINT_MAX;
    }
    if (count > maxSrvCount_) {
        throw std::runtime_error("SRV descriptor heap exhausted");
    }
    if (count == 1) {
        return Allocate();
    }

    for (UINT startIndex = 0; count <= currentIndex_ &&
                              startIndex <= currentIndex_ - count;
         ++startIndex) {
        bool available = true;
        for (UINT offset = 0; offset < count; ++offset) {
            if (allocated_[startIndex + offset]) {
                available = false;
                startIndex += offset;
                break;
            }
        }

        if (!available) {
            continue;
        }

        for (UINT offset = 0; offset < count; ++offset) {
            const UINT index = startIndex + offset;
            allocated_[index] = true;
            auto freeIt = std::find(freeList_.begin(), freeList_.end(), index);
            if (freeIt != freeList_.end()) {
                freeList_.erase(freeIt);
            }
        }
        return startIndex;
    }

    if (count > maxSrvCount_ - currentIndex_) {
        throw std::runtime_error("SRV descriptor heap exhausted");
    }

    const UINT startIndex = currentIndex_;
    currentIndex_ += count;
    for (UINT index = startIndex; index < currentIndex_; ++index) {
        allocated_[index] = true;
    }
    return startIndex;
}

void SrvManager::Free(UINT index) {
    if (index >= maxSrvCount_) {
        throw std::out_of_range("SRV descriptor index out of range");
    }
    if (index >= currentIndex_ || !allocated_[index]) {
        throw std::runtime_error("SRV descriptor double free or invalid free");
    }
    allocated_[index] = false;
    freeList_.push_back(index);
}

D3D12_CPU_DESCRIPTOR_HANDLE
SrvManager::GetCpuHandle(UINT index) const {
    if (index >= maxSrvCount_) {
        throw std::out_of_range("SRV descriptor index out of range");
    }

    return CD3DX12_CPU_DESCRIPTOR_HANDLE(
        heap_->GetCPUDescriptorHandleForHeapStart(), index, descriptorSize_);
}

D3D12_GPU_DESCRIPTOR_HANDLE
SrvManager::GetGpuHandle(UINT index) const {
    if (index >= maxSrvCount_) {
        throw std::out_of_range("SRV descriptor index out of range");
    }

    return CD3DX12_GPU_DESCRIPTOR_HANDLE(
        heap_->GetGPUDescriptorHandleForHeapStart(), index, descriptorSize_);
}
