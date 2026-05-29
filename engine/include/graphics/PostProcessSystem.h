#pragma once
#include "graphics/PostProcessSettings.h"
#include <d3d12.h>
#include <wrl.h>

class DirectXCommon;
class SrvManager;

class PostProcessSystem {
  public:
    void Initialize(DirectXCommon *dxCommon, SrvManager *srvManager, int width,
                    int height);

    void Resize(int width, int height);

    void Draw(D3D12_GPU_DESCRIPTOR_HANDLE textureHandle,
              D3D12_GPU_DESCRIPTOR_HANDLE depthHandle);

    void SetProfile(const PostProcessProfile &profile);

    const PostProcessProfile &GetProfile() const { return profile_; }
    bool RequiresPostProcess() const;

  private:
    void CreateRootSignature();

    void CreatePipelineState();

    void CreateConstantBuffer();

    void UpdateConstantBuffer();

    DirectXCommon *dxCommon_ = nullptr;
    SrvManager *srvManager_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> copyPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12Resource> constBuffer_;
    PostProcessConstants *mappedConstBuffer_ = nullptr;
    D3D12_VIEWPORT viewport_{};
    D3D12_RECT scissorRect_{};
    PostProcessProfile profile_{};
    int width_ = 1;
    int height_ = 1;
};
