#include "TextureManager.h"
#include "DirectXCommon.h"
#include "DxHelpers.h"
#include "DxUtils.h"
#include "SrvManager.h"
#include "Texture.h"

#include <DirectXTex.h>
#include <stdexcept>

using namespace DirectX;
using namespace DxUtils;

void TextureManager::Initialize(DirectXCommon *dxCommon,
                                SrvManager *srvManager) {
    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
}

uint32_t TextureManager::Load(const std::wstring &filePath) {
    // 画像読み込み
    ScratchImage scratch;
    TexMetadata metadata{};

    ThrowIfFailed(
        LoadFromWICFile(filePath.c_str(), WIC_FLAGS_NONE, &metadata, scratch),
        "LoadFromWICFile failed");

    const Image *image = scratch.GetImage(0, 0, 0);
    if (!image) {
        throw std::runtime_error("scratch.GetImage failed");
    }

    // GPU テクスチャ作成
    Texture texture;

    auto texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        metadata.format, static_cast<UINT>(metadata.width),
        static_cast<UINT>(metadata.height));

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);

    ThrowIfFailed(dxCommon_->GetDevice()->CreateCommittedResource(
                      &heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
                      D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                      IID_PPV_ARGS(&texture.resource)),
                  "Create texture resource failed");

    // Upload buffer
    UINT64 uploadSize =
        GetRequiredIntermediateSize(texture.resource.Get(), 0, 1);

    Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer;

    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);

    ThrowIfFailed(dxCommon_->GetDevice()->CreateCommittedResource(
                      &uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&uploadBuffer)),
                  "Create upload buffer failed");

    // Upload コマンド
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> cmdList;

    dxCommon_->GetDevice()->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));

    dxCommon_->GetDevice()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              allocator.Get(), nullptr,
                                              IID_PPV_ARGS(&cmdList));

    D3D12_SUBRESOURCE_DATA sub{};
    sub.pData = image->pixels;
    sub.RowPitch = image->rowPitch;
    sub.SlicePitch = image->slicePitch;

    UpdateSubresources(cmdList.Get(), texture.resource.Get(),
                       uploadBuffer.Get(), 0, 0, 1, &sub);

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        texture.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    cmdList->ResourceBarrier(1, &barrier);
    cmdList->Close();

    ID3D12CommandList *lists[] = {cmdList.Get()};
    dxCommon_->GetCommandQueue()->ExecuteCommandLists(1, lists);
    dxCommon_->WaitForGpu();

    // SRV 作成
    uint32_t srvIndex = srvManager_->Allocate();

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    if (metadata.format == DXGI_FORMAT_R8G8B8A8_UNORM) {
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    } else {
        srvDesc.Format = metadata.format;
    }

    dxCommon_->GetDevice()->CreateShaderResourceView(
        texture.resource.Get(), &srvDesc, srvManager_->GetCpuHandle(srvIndex));

    // 登録
    texture.width = static_cast<int>(metadata.width);
    texture.height = static_cast<int>(metadata.height);
    textures_.push_back({.texture = std::move(texture), .srvIndex = srvIndex});

    return static_cast<uint32_t>(textures_.size() - 1);
}

D3D12_GPU_DESCRIPTOR_HANDLE
TextureManager::GetGpuHandle(uint32_t textureId) const {
    return srvManager_->GetGpuHandle(textures_[textureId].srvIndex);
}

ID3D12Resource *TextureManager::GetResource(uint32_t textureId) const {
    return textures_[textureId].texture.resource.Get();
}

uint32_t TextureManager::GetWidth(uint32_t id) const {
    return textures_[id].texture.width;
}

uint32_t TextureManager::GetHeight(uint32_t id) const {
    return textures_[id].texture.height;
}
