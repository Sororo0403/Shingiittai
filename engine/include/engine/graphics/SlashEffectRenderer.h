#pragma once
#include <DirectXMath.h>
#include <d3d12.h>
#include <wrl.h>

class Camera;
class DirectXCommon;
class SrvManager;
class TextureManager;

class SlashEffectRenderer {
  public:
    void Initialize(DirectXCommon *dxCommon, SrvManager *srvManager,
                    TextureManager *textureManager);

    void DrawEnemySlash(const Camera &camera,
                        const DirectX::XMFLOAT3 &startWorld,
                        const DirectX::XMFLOAT3 &endWorld, float phaseAlpha,
                        float actionTime, bool isSweep);

  private:
    struct ConstantBufferData {
        DirectX::XMFLOAT2 startUv = {0.5f, 0.5f};
        DirectX::XMFLOAT2 endUv = {0.5f, 0.5f};

        float coreThickness = 0.010f;
        float glowThickness = 0.030f;
        float phaseAlpha = 0.0f;
        float actionTime = 0.0f;

        DirectX::XMFLOAT4 innerColor = {1.0f, 1.0f, 1.0f, 1.0f};
        DirectX::XMFLOAT4 outerColor = {0.72f, 0.55f, 1.0f, 1.0f};
        DirectX::XMFLOAT4 darkColor = {0.08f, 0.02f, 0.12f, 1.0f};

        float tipFade = 0.92f;
        float noiseScale = 22.0f;
        float sweepFlag = 0.0f;
        float passMode = 0.0f;

        float streakOffset = 0.0f;
        float streakThicknessMul = 1.0f;
        float streakIntensity = 1.0f;
        float reserved1 = 0.0f;
    };

  private:
    void CreateRootSignature();
    void CreatePipelineState();
    void CreateConstantBuffer();
    void CreateDarkPipelineState();
    bool ProjectWorldToUv(const Camera &camera,
                          const DirectX::XMFLOAT3 &worldPos,
                          DirectX::XMFLOAT2 &outUv) const;

  private:
    DirectXCommon *dxCommon_ = nullptr;
    SrvManager *srvManager_ = nullptr;
    TextureManager *textureManager_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
    Microsoft::WRL::ComPtr<ID3D12Resource> constBuffer_;

    ConstantBufferData *mappedCB_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> darkPipelineState_;
};