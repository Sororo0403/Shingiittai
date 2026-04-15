#pragma once
#include <DirectXMath.h>
#include <d3d12.h>
#include <wrl.h>

class DirectXCommon;
class SrvManager;

struct WarpPostEffectParamGPU {
    DirectX::XMFLOAT2 center = {0.5f, 0.5f};
    float radius = 0.0f;
    float strength = 0.0f;
    float time = 0.0f;

    DirectX::XMFLOAT2 center2 = {0.5f, 0.5f};
    float radius2 = 0.0f;
    float strength2 = 0.0f;
    float enabled = 0.0f;
};

class WarpPostEffectRenderer {
  public:
    void Initialize(DirectXCommon *dxCommon, SrvManager *srvManager);
    void Draw(const WarpPostEffectParamGPU &param,
              D3D12_GPU_DESCRIPTOR_HANDLE sceneSrvHandle);

  private:
    void CreateRootSignature();
    void CreatePipelineState();
    void CreateConstantBuffer();

  private:
    struct ConstantBufferData {
        DirectX::XMFLOAT2 center;
        float radius;
        float strength;
        float time;

        DirectX::XMFLOAT2 center2;
        float radius2;
        float strength2;
        float enabled;

        float padding[3];
    };

    DirectXCommon *dxCommon_ = nullptr;
    SrvManager *srvManager_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
    Microsoft::WRL::ComPtr<ID3D12Resource> constBuffer_;

    ConstantBufferData *mappedCB_ = nullptr;
};