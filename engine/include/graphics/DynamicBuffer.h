#pragma once

#include "graphics/UploadRingBuffer.h"

#include <cstddef>
#include <cstdint>
#include <d3d12.h>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <wrl.h>

class DynamicBuffer {
  public:
    DynamicBuffer() = default;
    ~DynamicBuffer();
    DynamicBuffer(const DynamicBuffer &) = delete;
    DynamicBuffer &operator=(const DynamicBuffer &) = delete;
    DynamicBuffer(DynamicBuffer &&) = delete;
    DynamicBuffer &operator=(DynamicBuffer &&) = delete;

    void Initialize(ID3D12Device *device, size_t capacity,
                    size_t defaultAlignment = 256);
    /// <summary>
    /// 状態をリセットする
    /// </summary>
    void Reset();

    void BeginWrite();
    /// <summary>
    /// 必要な容量を確保する
    /// </summary>
    void Reserve(size_t capacity);

    UploadAllocation Allocate(size_t size, size_t alignment = 0);

    template <class T>
    UploadAllocation Write(const T &value, size_t alignment = 0) {
        UploadAllocation allocation = Allocate(sizeof(T), alignment);
        *static_cast<T *>(allocation.cpu) = value;
        return allocation;
    }

    template <class T>
    UploadAllocation WriteArray(const T *values, size_t count,
                                size_t alignment = 0) {
        if (!values || count == 0) {
            throw std::runtime_error("DynamicBuffer::WriteArray invalid array");
        }
        if (count > (std::numeric_limits<size_t>::max)() / sizeof(T)) {
            throw std::runtime_error("DynamicBuffer::WriteArray size overflow");
        }
        const size_t bytes = sizeof(T) * count;
        UploadAllocation allocation =
            Allocate(bytes, alignment == 0 ? alignof(T) : alignment);
        std::memcpy(allocation.cpu, values, bytes);
        return allocation;
    }

    ID3D12Resource *GetResource() const { return resource_.Get(); }
    /// <summary>
    /// GpuVirtualAddressを取得する
    /// </summary>
    D3D12_GPU_VIRTUAL_ADDRESS GetGpuVirtualAddress() const;
    size_t GetCapacity() const { return capacity_; }
    size_t GetOffset() const { return offset_; }
    size_t GetDefaultAlignment() const { return defaultAlignment_; }

  private:
    /// <summary>
    /// AlignUpを実行する
    /// </summary>
    static size_t AlignUp(size_t value, size_t alignment);
    void CreateResource(size_t capacity);
    void UnmapResource();

    ID3D12Device *device_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    uint8_t *mapped_ = nullptr;
    size_t capacity_ = 0;
    size_t offset_ = 0;
    size_t defaultAlignment_ = 256;
};
