#pragma once
#include <DirectXMath.h>
#include <d3d12.h>
#include <wrl.h>

class DirectXCommon;

struct VignetteParams {
    DirectX::XMFLOAT3 color{0.06f, 0.0f, 0.0f};
    float intensity = 0.0f;
    float innerRadius = 0.42f;
    float power = 2.8f;
    float roundness = 1.35f;
};

class VignetteRenderer {
  public:
    void Initialize(DirectXCommon *dxCommon);
    void Draw(const VignetteParams &params);

  private:
    struct VignetteConstBuffer {
        DirectX::XMFLOAT4 colorIntensity;
        DirectX::XMFLOAT4 settings;
    };

    void CreateRootSignature();
    void CreatePipelineState();
    void CreateConstantBuffer();
    void UpdateConstants(const VignetteParams &params);

  private:
    DirectXCommon *dxCommon_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
    Microsoft::WRL::ComPtr<ID3D12Resource> constBuffer_;
};
