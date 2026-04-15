#include "TextureManager.h"
#include "DirectXCommon.h"
#include "DxHelpers.h"
#include "DxUtils.h"
#include "SrvManager.h"
#include "Texture.h"
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <filesystem>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace {

std::wstring NormalizePathKey(const std::wstring &path) {
    std::filesystem::path fsPath(path);
    std::wstring key = fsPath.lexically_normal().wstring();

#ifdef _WIN32
    std::transform(key.begin(), key.end(), key.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
#endif

    return key;
}

void DebugLog(const std::string &message) {
#ifdef _WIN32
    OutputDebugStringA((message + "\n").c_str());
#endif
}

void DebugLogW(const std::wstring &message) {
#ifdef _WIN32
    std::wstring withNewLine = message + L"\n";
    OutputDebugStringW(withNewLine.c_str());
#endif
}

}

using namespace DirectX;
using namespace DxUtils;
using Microsoft::WRL::ComPtr;

void TextureManager::Initialize(DirectXCommon *dxCommon,
                                SrvManager *srvManager) {
    dxCommon_ = dxCommon;
    srvManager_ = srvManager;

    textures_.clear();
    uploadBuffers_.clear();
    filePathToTextureId_.clear();

    uint32_t whitePixel = 0xFFFFFFFF;
    Image image{};
    image.width = 1;
    image.height = 1;
    image.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    image.rowPitch = sizeof(uint32_t);
    image.slicePitch = sizeof(uint32_t);
    image.pixels = reinterpret_cast<uint8_t *>(&whitePixel);

    TexMetadata metadata{};
    metadata.width = 1;
    metadata.height = 1;
    metadata.depth = 1;
    metadata.arraySize = 1;
    metadata.mipLevels = 1;
    metadata.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    metadata.dimension = TEX_DIMENSION_TEXTURE2D;

    CreateTexture(&image, metadata);
}

uint32_t TextureManager::Load(const std::wstring &filePath) {
    const std::wstring pathKey = NormalizePathKey(filePath);

    auto it = filePathToTextureId_.find(pathKey);
    if (it != filePathToTextureId_.end()) {
        std::ostringstream oss;
        oss << "[TextureManager] Load cache hit textureId=" << it->second;
        DebugLog(oss.str());
        return it->second;
    }

    DebugLogW(L"[TextureManager] Load begin path='" + pathKey + L"'");

    ScratchImage scratch;
    TexMetadata metadata{};

    ThrowIfFailed(
        LoadFromWICFile(pathKey.c_str(), WIC_FLAGS_NONE, &metadata, scratch),
        "LoadFromWICFile failed");

    const Image *image = scratch.GetImage(0, 0, 0);
    if (!image) {
        throw std::runtime_error("scratch.GetImage failed");
    }

    uint32_t id = CreateTexture(image, metadata);
    filePathToTextureId_[pathKey] = id;

    {
        std::ostringstream oss;
        oss << "[TextureManager] Load success textureId=" << id
            << " size=" << metadata.width << "x" << metadata.height;
        DebugLog(oss.str());
    }

    return id;
}

uint32_t TextureManager::LoadFromMemory(const uint8_t *data, size_t size) {
    {
        std::ostringstream oss;
        oss << "[TextureManager] LoadFromMemory begin data=" << data
            << " size=" << size;
        DebugLog(oss.str());
    }

    ScratchImage scratch;
    TexMetadata metadata{};

    ThrowIfFailed(
        LoadFromWICMemory(data, size, WIC_FLAGS_NONE, &metadata, scratch),
        "LoadFromWICMemory failed");

    const Image *image = scratch.GetImage(0, 0, 0);
    if (!image) {
        throw std::runtime_error("scratch.GetImage failed");
    }

    uint32_t id = CreateTexture(image, metadata);

    {
        std::ostringstream oss;
        oss << "[TextureManager] LoadFromMemory success textureId=" << id
            << " size=" << metadata.width << "x" << metadata.height;
        DebugLog(oss.str());
    }

    return id;
}

uint32_t TextureManager::CreateTexture(const Image *image,
                                       const TexMetadata &metadata) {
    {
        std::ostringstream oss;
        oss << "[TextureManager] CreateTexture begin format=" << metadata.format
            << " size=" << metadata.width << "x" << metadata.height
            << " image=" << image;
        DebugLog(oss.str());
    }

    Texture texture;

    auto texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        metadata.format, static_cast<UINT>(metadata.width),
        static_cast<UINT>(metadata.height));

    CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);

    ThrowIfFailed(dxCommon_->GetDevice()->CreateCommittedResource(
                      &defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
                      D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                      IID_PPV_ARGS(&texture.resource)),
                  "Create texture resource failed");

    UINT64 uploadSize =
        GetRequiredIntermediateSize(texture.resource.Get(), 0, 1);

    ComPtr<ID3D12Resource> uploadBuffer;

    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);

    ThrowIfFailed(dxCommon_->GetDevice()->CreateCommittedResource(
                      &uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&uploadBuffer)),
                  "Create upload buffer failed");

    ID3D12GraphicsCommandList *cmdList = dxCommon_->GetCommandList();

    D3D12_SUBRESOURCE_DATA sub{};
    sub.pData = image->pixels;
    sub.RowPitch = image->rowPitch;
    sub.SlicePitch = image->slicePitch;

    UpdateSubresources(cmdList, texture.resource.Get(), uploadBuffer.Get(), 0,
                       0, 1, &sub);

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        texture.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    cmdList->ResourceBarrier(1, &barrier);

    uploadBuffers_.push_back(uploadBuffer);

    uint32_t srvIndex = srvManager_->Allocate();

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    srvDesc.Format = metadata.format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    dxCommon_->GetDevice()->CreateShaderResourceView(
        texture.resource.Get(), &srvDesc, srvManager_->GetCpuHandle(srvIndex));

    texture.width = static_cast<uint32_t>(metadata.width);
    texture.height = static_cast<uint32_t>(metadata.height);

    textures_.push_back({std::move(texture), srvIndex});

    uint32_t textureId = static_cast<uint32_t>(textures_.size() - 1);

    {
        std::ostringstream oss;
        oss << "[TextureManager] CreateTexture success textureId=" << textureId
            << " srvIndex=" << srvIndex;
        DebugLog(oss.str());
    }

    return textureId;
}

void TextureManager::ReleaseUploadBuffers() { uploadBuffers_.clear(); }

D3D12_GPU_DESCRIPTOR_HANDLE
TextureManager::GetGpuHandle(uint32_t textureId) const {
    if (textureId >= textures_.size()) {
        std::ostringstream oss;
        oss << "[TextureManager] GetGpuHandle invalid textureId=" << textureId
            << " size=" << textures_.size();
        DebugLog(oss.str());
    }
    return srvManager_->GetGpuHandle(textures_.at(textureId).srvIndex);
}

ID3D12Resource *TextureManager::GetResource(uint32_t textureId) const {
    return textures_.at(textureId).texture.resource.Get();
}

uint32_t TextureManager::GetWidth(uint32_t id) const {
    return textures_.at(id).texture.width;
}

uint32_t TextureManager::GetHeight(uint32_t id) const {
    return textures_.at(id).texture.height;
}