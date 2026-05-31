#include "texture/TextureManager.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/DxUtils.h"
#include "graphics/SrvManager.h"
#include "texture/Texture.h"
#include <limits>
#include <stdexcept>

using namespace DirectX;
using namespace DxUtils;
using Microsoft::WRL::ComPtr;

namespace {

class UploadPassScope {
  public:
    UploadPassScope(DirectXCommon *dxCommon, TextureManager *textureManager,
                    bool active)
        : dxCommon_(dxCommon), textureManager_(textureManager), active_(active) {}

    ~UploadPassScope() {
        if (active_ && dxCommon_ != nullptr) {
            dxCommon_->AbortFrame();
            if (textureManager_ != nullptr) {
                textureManager_->ReleaseUploadBuffers();
            }
        }
    }

    void Finish() {
        if (!active_) {
            return;
        }
        dxCommon_->EndUpload();
        if (textureManager_ != nullptr) {
            textureManager_->ReleaseUploadBuffers();
        }
        active_ = false;
    }

  private:
    DirectXCommon *dxCommon_ = nullptr;
    TextureManager *textureManager_ = nullptr;
    bool active_ = false;
};

} // namespace

uint32_t TextureManager::CreateFromRgbaPixels(uint32_t width, uint32_t height,
                                               const uint8_t *pixels) {
    return CreateTexture2D(width, height, DXGI_FORMAT_R8G8B8A8_UNORM, pixels,
                           static_cast<size_t>(width) * 4u);
}

uint32_t TextureManager::CreateTexture2D(uint32_t width, uint32_t height,
                                         DXGI_FORMAT format,
                                         const uint8_t *pixels,
                                         size_t rowPitch) {
    if (width == 0 || height == 0 || !pixels || rowPitch == 0) {
        throw std::runtime_error("CreateTexture2D received invalid pixel data");
    }
    if (DirectX::IsCompressed(format) || DirectX::IsDepthStencil(format)) {
        throw std::runtime_error("CreateTexture2D supports only plain color formats");
    }
    const size_t bitsPerPixel = DirectX::BitsPerPixel(format);
    if (bitsPerPixel == 0) {
        throw std::runtime_error("CreateTexture2D unsupported texture format");
    }
    if (static_cast<size_t>(width) >
        ((std::numeric_limits<size_t>::max)() - 7u) / bitsPerPixel) {
        throw std::runtime_error("CreateTexture2D row pitch overflow");
    }
    const size_t minimumRowPitch =
        (static_cast<size_t>(width) * bitsPerPixel + 7u) / 8u;
    if (rowPitch < minimumRowPitch) {
        throw std::runtime_error("CreateTexture2D row pitch is too small");
    }
    if (rowPitch > (std::numeric_limits<size_t>::max)() / height) {
        throw std::runtime_error("CreateTexture2D slice pitch overflow");
    }

    Image image{};
    image.width = width;
    image.height = height;
    image.format = format;
    image.rowPitch = rowPitch;
    image.slicePitch = rowPitch * height;
    image.pixels = const_cast<uint8_t *>(pixels);

    TexMetadata metadata{};
    metadata.width = width;
    metadata.height = height;
    metadata.depth = 1;
    metadata.arraySize = 1;
    metadata.mipLevels = 1;
    metadata.format = format;
    metadata.dimension = TEX_DIMENSION_TEXTURE2D;

    return CreateTexture(&image, 1, metadata);
}

void TextureManager::UpdateTexture2D(uint32_t textureId, const uint8_t *pixels,
                                     size_t rowPitch) {
    if (!dxCommon_) {
        throw std::runtime_error("UpdateTexture2D requires DirectXCommon");
    }
    if (!pixels || rowPitch == 0 || !IsValidTextureId(textureId)) {
        throw std::runtime_error("UpdateTexture2D received invalid input");
    }

    const bool ownsUploadPass = !dxCommon_->IsCommandListRecording();
    if (ownsUploadPass) {
        dxCommon_->BeginUpload();
    }
    UploadPassScope uploadPass(dxCommon_, this, ownsUploadPass);

    Texture &texture = textures_[textureId].texture;
    if (!texture.resource || texture.width <= 0 || texture.height <= 0) {
        throw std::runtime_error("UpdateTexture2D target texture is invalid");
    }

    const UINT frameIndex = dxCommon_->GetBackBufferIndex();
    if (frameIndex < frameUploadBuffers_.size() &&
        lastDynamicUploadFrameIndex_ != frameIndex) {
        frameUploadBuffers_[frameIndex].clear();
        lastDynamicUploadFrameIndex_ = frameIndex;
    }

    D3D12_RESOURCE_DESC textureDesc = texture.resource->GetDesc();
    if (textureDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        textureDesc.DepthOrArraySize != 1 || textureDesc.MipLevels != 1) {
        throw std::runtime_error("UpdateTexture2D target must be a single 2D texture");
    }
    if (textureDesc.Width >
            static_cast<UINT64>((std::numeric_limits<int>::max)()) ||
        textureDesc.Height >
            static_cast<UINT>((std::numeric_limits<int>::max)()) ||
        static_cast<int>(textureDesc.Width) != texture.width ||
        static_cast<int>(textureDesc.Height) != texture.height) {
        throw std::runtime_error("UpdateTexture2D target size mismatch");
    }
    const size_t bitsPerPixel = DirectX::BitsPerPixel(textureDesc.Format);
    if (bitsPerPixel == 0) {
        throw std::runtime_error("UpdateTexture2D unsupported texture format");
    }
    if (DirectX::IsCompressed(textureDesc.Format) ||
        DirectX::IsDepthStencil(textureDesc.Format)) {
        throw std::runtime_error("UpdateTexture2D supports only plain color formats");
    }
    const size_t width = static_cast<size_t>(texture.width);
    if (width >
        ((std::numeric_limits<size_t>::max)() - 7u) / bitsPerPixel) {
        throw std::runtime_error("UpdateTexture2D rowPitch overflow");
    }
    const size_t expectedRowPitch = (width * bitsPerPixel + 7u) / 8u;
    if (rowPitch < expectedRowPitch) {
        throw std::runtime_error("UpdateTexture2D rowPitch is too small");
    }

    D3D12_SUBRESOURCE_DATA subresource{};
    subresource.pData = pixels;
    if (rowPitch >
        static_cast<size_t>((std::numeric_limits<LONG_PTR>::max)())) {
        throw std::runtime_error("UpdateTexture2D rowPitch exceeds D3D12 range");
    }
    if (rowPitch > (std::numeric_limits<size_t>::max)() /
                       static_cast<size_t>(texture.height)) {
        throw std::runtime_error("UpdateTexture2D slice pitch overflow");
    }
    const size_t slicePitch = rowPitch * static_cast<size_t>(texture.height);
    if (slicePitch >
        static_cast<size_t>((std::numeric_limits<LONG_PTR>::max)())) {
        throw std::runtime_error("UpdateTexture2D slicePitch exceeds D3D12 range");
    }
    subresource.RowPitch = static_cast<LONG_PTR>(rowPitch);
    subresource.SlicePitch = static_cast<LONG_PTR>(slicePitch);

    const UINT64 uploadSize =
        GetRequiredIntermediateSize(texture.resource.Get(), 0, 1);

    ComPtr<ID3D12Resource> uploadBuffer;
    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
    ThrowIfFailed(dxCommon_->GetDevice()->CreateCommittedResource(
                      &uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&uploadBuffer)),
                  "Create texture update upload buffer failed");

    if (frameIndex < frameUploadBuffers_.size()) {
        frameUploadBuffers_[frameIndex].push_back(uploadBuffer);
    } else {
        uploadBuffers_.push_back(uploadBuffer);
    }

    ID3D12GraphicsCommandList *cmdList = dxCommon_->GetCommandList();
    if (texture.state != D3D12_RESOURCE_STATE_COPY_DEST) {
        auto toCopyDest = CD3DX12_RESOURCE_BARRIER::Transition(
            texture.resource.Get(), texture.state,
            D3D12_RESOURCE_STATE_COPY_DEST);
        cmdList->ResourceBarrier(1, &toCopyDest);
        texture.state = D3D12_RESOURCE_STATE_COPY_DEST;
    }

    UpdateSubresources(cmdList, texture.resource.Get(), uploadBuffer.Get(), 0,
                       0, 1, &subresource);

    auto toShaderResource = CD3DX12_RESOURCE_BARRIER::Transition(
        texture.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->ResourceBarrier(1, &toShaderResource);
    texture.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    uploadPass.Finish();
}
