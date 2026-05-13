#include "TextureManager.h"
#include "DirectXCommon.h"
#include "DxHelpers.h"
#include "DxUtils.h"
#include "SrvManager.h"
#include "Texture.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cwctype>
#include <filesystem>
#include <cmath>
#include <stdexcept>
#include <system_error>
#include <vector>

static std::filesystem::path CanonicalizePath(const std::filesystem::path &path) {
    std::error_code ec;
    const std::filesystem::path canonical =
        std::filesystem::weakly_canonical(path, ec);
    if (!ec) {
        return canonical;
    }

    return path.lexically_normal();
}

static std::filesystem::path ResolveTexturePath(const std::wstring &path) {
    const std::filesystem::path input(path);
    const std::filesystem::path normalized = input.lexically_normal();
    if (normalized.is_absolute()) {
        return CanonicalizePath(normalized);
    }

    const std::filesystem::path cwd = std::filesystem::current_path();
    const std::filesystem::path direct = cwd / normalized;
    if (std::filesystem::exists(direct)) {
        return CanonicalizePath(direct);
    }

    for (std::filesystem::path dir = cwd; !dir.empty(); dir = dir.parent_path()) {
        const std::filesystem::path candidate = dir / normalized;
        if (std::filesystem::exists(candidate)) {
            return CanonicalizePath(candidate);
        }

        if (dir == dir.root_path()) {
            break;
        }
    }

    return CanonicalizePath(direct);
}

static std::wstring NormalizePathKey(const std::filesystem::path &path) {
    std::wstring key = path.lexically_normal().wstring();

#ifdef _WIN32
    std::transform(key.begin(), key.end(), key.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
#endif

    return key;
}

static float Fade(float t) { return t * t * (3.0f - 2.0f * t); }

static float Lerp(float a, float b, float t) { return a + (b - a) * t; }

static float HashNoise(int32_t x, int32_t y, uint32_t seed) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u +
                 static_cast<uint32_t>(y) * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13u)) * 1274126177u;
    h ^= h >> 16u;
    return static_cast<float>(h & 0x00FFFFFFu) / static_cast<float>(0x00FFFFFFu);
}

static float ValueNoise(float x, float y, uint32_t seed) {
    const int32_t x0 = static_cast<int32_t>(std::floor(x));
    const int32_t y0 = static_cast<int32_t>(std::floor(y));
    const float tx = Fade(x - static_cast<float>(x0));
    const float ty = Fade(y - static_cast<float>(y0));

    const float n00 = HashNoise(x0, y0, seed);
    const float n10 = HashNoise(x0 + 1, y0, seed);
    const float n01 = HashNoise(x0, y0 + 1, seed);
    const float n11 = HashNoise(x0 + 1, y0 + 1, seed);

    return Lerp(Lerp(n00, n10, tx), Lerp(n01, n11, tx), ty);
}

static uint32_t PackRgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255u) {
    return (static_cast<uint32_t>(a) << 24u) |
           (static_cast<uint32_t>(b) << 16u) |
           (static_cast<uint32_t>(g) << 8u) | static_cast<uint32_t>(r);
}

static float Saturate(float value) {
    return (std::clamp)(value, 0.0f, 1.0f);
}

static float Smoothstep(float edge0, float edge1, float value) {
    const float t = Saturate((value - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

static std::wstring MakeGeneratedTextureKey(const wchar_t *prefix,
                                            uint32_t width, uint32_t height) {
    return std::wstring(prefix) + L":" + std::to_wstring(width) + L"x" +
           std::to_wstring(height);
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
    generatedTextureToId_.clear();

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

    CreateTexture(&image, 1, metadata);
}

uint32_t TextureManager::Load(const std::wstring &filePath) {
    const std::filesystem::path resolvedPath = ResolveTexturePath(filePath);
    const std::wstring pathKey = NormalizePathKey(resolvedPath);

    auto it = filePathToTextureId_.find(pathKey);
    if (it != filePathToTextureId_.end()) {
        return it->second;
    }

    ScratchImage scratch;
    TexMetadata metadata{};

    const std::wstring ext = resolvedPath.extension().wstring();

    if (_wcsicmp(ext.c_str(), L".dds") == 0) {
        const std::string message =
            "LoadFromDDSFile failed: " + resolvedPath.string();
        ThrowIfFailed(
            LoadFromDDSFile(resolvedPath.c_str(), DDS_FLAGS_NONE, &metadata,
                            scratch),
            message.c_str());
    } else {
        const std::string message =
            "LoadFromWICFile failed: " + resolvedPath.string();
        ThrowIfFailed(
            LoadFromWICFile(resolvedPath.c_str(), WIC_FLAGS_NONE, &metadata,
                            scratch),
            message.c_str());
    }

    uint32_t id =
        CreateTexture(scratch.GetImages(), scratch.GetImageCount(), metadata);
    filePathToTextureId_[pathKey] = id;

    return id;
}

uint32_t TextureManager::LoadFromMemory(const uint8_t *data, size_t size) {
    ScratchImage scratch;
    TexMetadata metadata{};

    ThrowIfFailed(
        LoadFromWICMemory(data, size, WIC_FLAGS_NONE, &metadata, scratch),
        "LoadFromWICMemory failed");

    uint32_t id =
        CreateTexture(scratch.GetImages(), scratch.GetImageCount(), metadata);

    return id;
}

uint32_t TextureManager::CreateNoiseTexture(uint32_t width, uint32_t height) {
    width = (std::max)(width, 1u);
    height = (std::max)(height, 1u);
    const std::wstring cacheKey = MakeGeneratedTextureKey(L"noise", width, height);
    auto cached = generatedTextureToId_.find(cacheKey);
    if (cached != generatedTextureToId_.end()) {
        return cached->second;
    }

    std::vector<uint32_t> pixels(static_cast<size_t>(width) * height);
    constexpr uint32_t seed = 0xC65D1A5Bu;

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(width);
            const float v = static_cast<float>(y) / static_cast<float>(height);

            float amplitude = 0.55f;
            float frequency = 4.0f;
            float value = 0.0f;
            float totalAmplitude = 0.0f;
            for (uint32_t octave = 0; octave < 5; ++octave) {
                value += ValueNoise(u * frequency, v * frequency,
                                    seed + octave * 97u) *
                         amplitude;
                totalAmplitude += amplitude;
                amplitude *= 0.5f;
                frequency *= 2.0f;
            }

            value = (std::clamp)(value / totalAmplitude, 0.0f, 1.0f);
            const uint8_t gray = static_cast<uint8_t>(value * 255.0f);
            pixels[static_cast<size_t>(y) * width + x] =
                0xFF000000u | (static_cast<uint32_t>(gray) << 16u) |
                (static_cast<uint32_t>(gray) << 8u) |
                static_cast<uint32_t>(gray);
        }
    }

    Image image{};
    image.width = width;
    image.height = height;
    image.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    image.rowPitch = static_cast<size_t>(width) * sizeof(uint32_t);
    image.slicePitch = image.rowPitch * height;
    image.pixels = reinterpret_cast<uint8_t *>(pixels.data());

    TexMetadata metadata{};
    metadata.width = width;
    metadata.height = height;
    metadata.depth = 1;
    metadata.arraySize = 1;
    metadata.mipLevels = 1;
    metadata.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    metadata.dimension = TEX_DIMENSION_TEXTURE2D;

    const uint32_t id = CreateTexture(&image, 1, metadata);
    generatedTextureToId_[cacheKey] = id;
    return id;
}

uint32_t TextureManager::CreateRustedMetalTexture(uint32_t width,
                                                  uint32_t height) {
    width = (std::max)(width, 1u);
    height = (std::max)(height, 1u);
    const std::wstring cacheKey =
        MakeGeneratedTextureKey(L"rusted_metal", width, height);
    auto cached = generatedTextureToId_.find(cacheKey);
    if (cached != generatedTextureToId_.end()) {
        return cached->second;
    }

    std::vector<uint32_t> pixels(static_cast<size_t>(width) * height);
    constexpr uint32_t seed = 0x8E71C0DEu;

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(width);
            const float v = static_cast<float>(y) / static_cast<float>(height);

            const float broadRust =
                ValueNoise(u * 5.0f, v * 5.0f, seed + 13u);
            const float fineRust =
                ValueNoise(u * 38.0f, v * 38.0f, seed + 41u);
            const float pitting =
                ValueNoise(u * 96.0f, v * 96.0f, seed + 73u);
            const float grime =
                ValueNoise(u * 14.0f + 7.0f, v * 20.0f, seed + 109u);
            const float scratchNoise =
                ValueNoise(u * 180.0f, v * 28.0f, seed + 151u);

            const float panelLineX =
                1.0f - Smoothstep(0.010f, 0.024f, std::fabs(std::fmod(u * 5.0f, 1.0f) - 0.5f));
            const float panelLineY =
                1.0f - Smoothstep(0.010f, 0.024f, std::fabs(std::fmod(v * 4.0f, 1.0f) - 0.5f));
            const float panelLine = Saturate((panelLineX + panelLineY) * 0.34f);

            const float scratchBand =
                std::pow(Saturate(1.0f - std::fabs(scratchNoise - 0.50f) * 28.0f),
                         1.8f);
            const float rustMask =
                Saturate((broadRust - 0.38f) * 1.55f + fineRust * 0.26f +
                         panelLine * 0.40f);
            const float darkOxide =
                Saturate((grime - 0.42f) * 1.3f + pitting * 0.28f);
            const float exposedMetal =
                Saturate((1.0f - rustMask) * 0.65f + scratchBand * 0.75f -
                         darkOxide * 0.35f);

            float r = 0.18f;
            float g = 0.19f;
            float b = 0.19f;

            const float steel = exposedMetal;
            r = Lerp(r, 0.56f, steel);
            g = Lerp(g, 0.59f, steel);
            b = Lerp(b, 0.58f, steel);

            const float rust = rustMask;
            r = Lerp(r, 0.70f, rust);
            g = Lerp(g, 0.27f + fineRust * 0.10f, rust);
            b = Lerp(b, 0.075f, rust);

            const float soot = darkOxide;
            r = Lerp(r, 0.065f, soot * 0.78f);
            g = Lerp(g, 0.060f, soot * 0.78f);
            b = Lerp(b, 0.055f, soot * 0.78f);

            const float rivetGridX =
                std::fabs(std::fmod(u * 5.0f, 1.0f) - 0.08f);
            const float rivetGridY =
                std::fabs(std::fmod(v * 4.0f, 1.0f) - 0.08f);
            const float rivetGrid =
                rivetGridX < rivetGridY ? rivetGridX : rivetGridY;
            const float rivet =
                1.0f - Smoothstep(0.018f, 0.036f, rivetGrid);
            r = Lerp(r, 0.74f, rivet * 0.42f);
            g = Lerp(g, 0.68f, rivet * 0.42f);
            b = Lerp(b, 0.57f, rivet * 0.42f);

            const float lineDarken = panelLine * 0.42f;
            r *= 1.0f - lineDarken;
            g *= 1.0f - lineDarken;
            b *= 1.0f - lineDarken;

            pixels[static_cast<size_t>(y) * width + x] = PackRgba(
                static_cast<uint8_t>(Saturate(r) * 255.0f),
                static_cast<uint8_t>(Saturate(g) * 255.0f),
                static_cast<uint8_t>(Saturate(b) * 255.0f), 255u);
        }
    }

    Image image{};
    image.width = width;
    image.height = height;
    image.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    image.rowPitch = static_cast<size_t>(width) * sizeof(uint32_t);
    image.slicePitch = image.rowPitch * height;
    image.pixels = reinterpret_cast<uint8_t *>(pixels.data());

    TexMetadata metadata{};
    metadata.width = width;
    metadata.height = height;
    metadata.depth = 1;
    metadata.arraySize = 1;
    metadata.mipLevels = 1;
    metadata.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    metadata.dimension = TEX_DIMENSION_TEXTURE2D;

    const uint32_t id = CreateTexture(&image, 1, metadata);
    generatedTextureToId_[cacheKey] = id;
    return id;
}

uint32_t TextureManager::CreateArenaStoneTexture(uint32_t width,
                                                 uint32_t height) {
    width = (std::max)(width, 1u);
    height = (std::max)(height, 1u);
    const std::wstring cacheKey =
        MakeGeneratedTextureKey(L"arena_stone", width, height);
    auto cached = generatedTextureToId_.find(cacheKey);
    if (cached != generatedTextureToId_.end()) {
        return cached->second;
    }

    std::vector<uint32_t> pixels(static_cast<size_t>(width) * height);
    constexpr uint32_t seed = 0x51A7E0A1u;

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(width);
            const float v = static_cast<float>(y) / static_cast<float>(height);

            const float largeGrain =
                ValueNoise(u * 7.0f, v * 7.0f, seed + 11u);
            const float fineGrain =
                ValueNoise(u * 58.0f, v * 58.0f, seed + 29u);
            const float cloud =
                ValueNoise(u * 2.6f + 3.0f, v * 2.1f, seed + 47u);
            const float wear =
                ValueNoise(u * 18.0f, v * 18.0f + 5.0f, seed + 83u);

            const float tileU = std::fmod(u * 7.0f, 1.0f);
            const float tileV = std::fmod(v * 7.0f, 1.0f);
            const float edgeU = (std::min)(tileU, 1.0f - tileU);
            const float edgeV = (std::min)(tileV, 1.0f - tileV);
            const float mortar =
                1.0f - Smoothstep(0.010f, 0.030f, (std::min)(edgeU, edgeV));
            const float tileCenter =
                Smoothstep(0.05f, 0.36f, edgeU) * Smoothstep(0.05f, 0.36f, edgeV);

            const float crackNoise =
                ValueNoise(u * 23.0f + 7.0f, v * 23.0f, seed + 131u);
            const float crack =
                std::pow(Saturate(1.0f - std::fabs(crackNoise - 0.52f) * 24.0f),
                         2.4f) *
                Smoothstep(0.52f, 0.82f, wear);

            float r = 0.60f;
            float g = 0.58f;
            float b = 0.52f;

            const float stoneVariation =
                (largeGrain - 0.5f) * 0.14f + (fineGrain - 0.5f) * 0.08f +
                (cloud - 0.5f) * 0.10f;
            r += stoneVariation;
            g += stoneVariation * 0.92f;
            b += stoneVariation * 0.78f;

            const float sunWear = tileCenter * Smoothstep(0.38f, 0.76f, wear);
            r = Lerp(r, 0.76f, sunWear * 0.28f);
            g = Lerp(g, 0.72f, sunWear * 0.28f);
            b = Lerp(b, 0.63f, sunWear * 0.22f);

            const float coolVein =
                Smoothstep(0.62f, 0.90f,
                           ValueNoise(u * 11.0f - 4.0f, v * 15.0f, seed + 191u));
            r = Lerp(r, 0.54f, coolVein * 0.12f);
            g = Lerp(g, 0.62f, coolVein * 0.12f);
            b = Lerp(b, 0.60f, coolVein * 0.10f);

            r = Lerp(r, 0.36f, mortar * 0.32f + crack * 0.42f);
            g = Lerp(g, 0.34f, mortar * 0.32f + crack * 0.42f);
            b = Lerp(b, 0.31f, mortar * 0.30f + crack * 0.38f);

            pixels[static_cast<size_t>(y) * width + x] = PackRgba(
                static_cast<uint8_t>(Saturate(r) * 255.0f),
                static_cast<uint8_t>(Saturate(g) * 255.0f),
                static_cast<uint8_t>(Saturate(b) * 255.0f), 255u);
        }
    }

    Image image{};
    image.width = width;
    image.height = height;
    image.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    image.rowPitch = static_cast<size_t>(width) * sizeof(uint32_t);
    image.slicePitch = image.rowPitch * height;
    image.pixels = reinterpret_cast<uint8_t *>(pixels.data());

    TexMetadata metadata{};
    metadata.width = width;
    metadata.height = height;
    metadata.depth = 1;
    metadata.arraySize = 1;
    metadata.mipLevels = 1;
    metadata.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    metadata.dimension = TEX_DIMENSION_TEXTURE2D;

    const uint32_t id = CreateTexture(&image, 1, metadata);
    generatedTextureToId_[cacheKey] = id;
    return id;
}

uint32_t TextureManager::CreateTexture(const Image *images, size_t imageCount,
                                       const TexMetadata &metadata) {
    Texture texture;

    auto texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        metadata.format, static_cast<UINT64>(metadata.width),
        static_cast<UINT>(metadata.height),
        static_cast<UINT16>(metadata.arraySize),
        static_cast<UINT16>(metadata.mipLevels));

    CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);

    ThrowIfFailed(dxCommon_->GetDevice()->CreateCommittedResource(
                      &defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
                      D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                      IID_PPV_ARGS(&texture.resource)),
                  "Create texture resource failed");

    std::vector<D3D12_SUBRESOURCE_DATA> subresources(imageCount);
    for (size_t imageIndex = 0; imageIndex < imageCount; ++imageIndex) {
        subresources[imageIndex].pData = images[imageIndex].pixels;
        subresources[imageIndex].RowPitch = images[imageIndex].rowPitch;
        subresources[imageIndex].SlicePitch = images[imageIndex].slicePitch;
    }

    UINT64 uploadSize = GetRequiredIntermediateSize(
        texture.resource.Get(), 0, static_cast<UINT>(subresources.size()));

    ComPtr<ID3D12Resource> uploadBuffer;

    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);

    ThrowIfFailed(dxCommon_->GetDevice()->CreateCommittedResource(
                      &uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&uploadBuffer)),
                  "Create upload buffer failed");

    ID3D12GraphicsCommandList *cmdList = dxCommon_->GetCommandList();

    UpdateSubresources(cmdList, texture.resource.Get(), uploadBuffer.Get(), 0, 0,
                       static_cast<UINT>(subresources.size()),
                       subresources.data());

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        texture.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    cmdList->ResourceBarrier(1, &barrier);

    uploadBuffers_.push_back(uploadBuffer);

    uint32_t srvIndex = srvManager_->Allocate();

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    srvDesc.Format = metadata.format;
    if (metadata.IsCubemap()) {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.TextureCube.MostDetailedMip = 0;
        srvDesc.TextureCube.MipLevels = static_cast<UINT>(metadata.mipLevels);
        srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
    } else if (metadata.dimension == TEX_DIMENSION_TEXTURE2D &&
               metadata.arraySize > 1) {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        srvDesc.Texture2DArray.MostDetailedMip = 0;
        srvDesc.Texture2DArray.MipLevels = static_cast<UINT>(metadata.mipLevels);
        srvDesc.Texture2DArray.FirstArraySlice = 0;
        srvDesc.Texture2DArray.ArraySize = static_cast<UINT>(metadata.arraySize);
        srvDesc.Texture2DArray.PlaneSlice = 0;
        srvDesc.Texture2DArray.ResourceMinLODClamp = 0.0f;
    } else {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = static_cast<UINT>(metadata.mipLevels);
        srvDesc.Texture2D.PlaneSlice = 0;
        srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    }

    dxCommon_->GetDevice()->CreateShaderResourceView(
        texture.resource.Get(), &srvDesc, srvManager_->GetCpuHandle(srvIndex));

    texture.width = static_cast<uint32_t>(metadata.width);
    texture.height = static_cast<uint32_t>(metadata.height);

    textures_.push_back({std::move(texture), srvIndex});

    uint32_t textureId = static_cast<uint32_t>(textures_.size() - 1);

    return textureId;
}

void TextureManager::ReleaseUploadBuffers() { uploadBuffers_.clear(); }

D3D12_GPU_DESCRIPTOR_HANDLE
TextureManager::GetGpuHandle(uint32_t textureId) const {
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
