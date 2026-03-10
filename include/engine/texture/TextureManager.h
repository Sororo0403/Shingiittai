#pragma once
#include "Texture.h"
#include <cstdint>
#include <d3d12.h>
#include <string>
#include <vector>
#include <wrl.h>

class DirectXCommon;
class SrvManager;

class TextureManager {
  public:
    /// <summary>
    /// 初期化処理
    /// </summary>
    /// <param name="dxCommon">DirectXCommonインスタンス</param>
    /// <param name="srvManager">SrvManagerインスタンス</param>
    void Initialize(DirectXCommon *dxCommon, SrvManager *srvManager);

    /// <summary>
    /// テクスチャをロードしてidを返す
    /// </summary>
    /// <param name="filePath">ロードするテクスチャのファイルパス</param>
    /// <returns>テクスチャid</returns>
    uint32_t Load(const std::wstring &filePath);

    /// <summary>
    /// ロード時に使った一時UploadBufferを解放
    /// </summary>
    void ReleaseUploadBuffers();

    // Getter
    D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(uint32_t textureId) const;
    ID3D12Resource *GetResource(uint32_t textureId) const;
    uint32_t GetWidth(uint32_t id) const;
    uint32_t GetHeight(uint32_t id) const;

  private:
    struct Entry {
        Texture texture;
        uint32_t srvIndex = 0;
    };

  private:
    DirectXCommon *dxCommon_ = nullptr;
    SrvManager *srvManager_ = nullptr;

    std::vector<Entry> textures_;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> uploadBuffers_;
};