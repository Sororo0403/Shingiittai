#pragma once
#include <DirectXMath.h>
#include <d3d12.h>
#include <wrl.h>

class DirectXCommon;
class SrvManager;

struct ElectricRingParamGPU {
    DirectX::XMFLOAT2 center = {0.5f, 0.5f};
    float radius = 0.0f;
    float time = 0.0f;

    float ringWidth = 0.015f;
    float distortionWidth = 0.045f;
    float distortionStrength = 0.018f;
    float swirlStrength = 0.006f;

    float cloudScale = 3.5f;
    float cloudIntensity = 1.4f;
    float brightness = 2.4f;
    float haloIntensity = 1.0f;

    DirectX::XMFLOAT2 aspectInvAspect = {1.0f, 1.0f}; // x=aspect, y=1/aspect
    float innerFade = 0.85f;
    float outerFade = 1.0f;
    float enabled = 1.0f;
};

class ElectricRingEffectRenderer {
  public:
    void Initialize(DirectXCommon *dxCommon, SrvManager *srvManager);

    // sceneSrvHandle : SceneColor の SRV
    // noise0SrvHandle / noise1SrvHandle : ノイズテクスチャ SRV
    void DrawDistortion(const ElectricRingParamGPU &param,
                        D3D12_GPU_DESCRIPTOR_HANDLE sceneSrvHandle,
                        D3D12_GPU_DESCRIPTOR_HANDLE noise0SrvHandle,
                        D3D12_GPU_DESCRIPTOR_HANDLE noise1SrvHandle);

    void DrawPlasma(const ElectricRingParamGPU &param,
                    D3D12_GPU_DESCRIPTOR_HANDLE noise0SrvHandle,
                    D3D12_GPU_DESCRIPTOR_HANDLE noise1SrvHandle);

  private:
    void CreateRootSignature();
    void CreateDistortionPipelineState();
    void CreatePlasmaPipelineState();
    void CreateConstantBuffer();

  private:
    struct ConstantBufferData {
        DirectX::XMFLOAT2 center;
        float radius;
        float time;

        float ringWidth;
        float distortionWidth;
        float distortionStrength;
        float swirlStrength;

        float cloudScale;
        float cloudIntensity;
        float brightness;
        float haloIntensity;

        DirectX::XMFLOAT2 aspectInvAspect;
        float innerFade;
        float outerFade;
        float enabled;
    };

    DirectXCommon *dxCommon_ = nullptr;
    SrvManager *srvManager_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> distortionPSO_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> plasmaPSO_;
    Microsoft::WRL::ComPtr<ID3D12Resource> constBuffer_;

    ConstantBufferData *mappedCB_ = nullptr;
};