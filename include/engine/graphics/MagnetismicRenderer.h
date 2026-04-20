#pragma once
#include <cstdint>
#include <d3d12.h>
#include <wrl.h>

using Microsoft::WRL::ComPtr;

struct MagnetVec3 {
    float x;
    float y;
    float z;
};

struct MagnetVec4 {
    float x;
    float y;
    float z;
    float w;
};

struct MagnetMatrix4x4 {
    float m[4][4];
};

struct MagnetVertex {
    float position[3];
    float uv[2];
};

struct MagnetVSConstants {
    MagnetMatrix4x4 world;
    MagnetMatrix4x4 viewProj;
};

struct MagnetPSConstants {
    float time;
    float intensity;
    float alpha;
    float scale;

    float swirlA;
    float swirlB;
    float noiseScale;
    float stepScale;

    float colorA[4];
    float colorB[4];
    float params0[4]; // x=brightness y=distFade z=innerBoost w=unused
};

struct MagnetismicInstance {
    MagnetVec3 position = {0.0f, 0.0f, 0.0f};
    float size = 2.85f;

    float time = 0.0f;
    float intensity = 1.25f;
    float alpha = 1.0f;

    float swirlA = 2.5f;
    float swirlB = 2.0f;
    float noiseScale = 3.5f;
    float stepScale = 1.0f;

    // 中心の白ピンク寄り
    MagnetVec4 colorA = {1.18f, 0.34f, 0.78f, 1.0f};

    // 外周のシアン～ブルー寄り
    MagnetVec4 colorB = {0.03f, 0.58f, 1.25f, 1.0f};

    // Shadertoy寄りの見え方に合わせた推奨値
    float brightness = 1.45f;
    float distFade = 0.35f;
    float innerBoost = 1.22f;
};

class MagnetismicRenderer {
  public:
    bool Initialize(ID3D12Device *device, DXGI_FORMAT rtvFormat,
                    DXGI_FORMAT dsvFormat, const wchar_t *vsPath,
                    const wchar_t *psPath);

    void BeginFrame(float deltaTime);

    void Draw(ID3D12GraphicsCommandList *commandList,
              const MagnetismicInstance &inst, const MagnetMatrix4x4 &viewProj,
              const MagnetVec3 &cameraRight, const MagnetVec3 &cameraUp);

  private:
    bool CreateRootSignature(ID3D12Device *device);
    bool CreatePipelineState(ID3D12Device *device, DXGI_FORMAT rtvFormat,
                             DXGI_FORMAT dsvFormat, const wchar_t *vsPath,
                             const wchar_t *psPath);
    bool CreateQuadResources(ID3D12Device *device);
    bool CreateConstantBuffers(ID3D12Device *device);

    void UpdateQuadVertices(const MagnetismicInstance &inst,
                            const MagnetVec3 &cameraRight,
                            const MagnetVec3 &cameraUp);

    static MagnetMatrix4x4 MakeIdentity();

  private:
    float elapsedTime_ = 0.0f;

    ComPtr<ID3D12RootSignature> rootSignature_;
    ComPtr<ID3D12PipelineState> pipelineState_;

    ComPtr<ID3D12Resource> vertexBuffer_;
    D3D12_VERTEX_BUFFER_VIEW vbView_{};

    ComPtr<ID3D12Resource> vsConstantBuffer_;
    ComPtr<ID3D12Resource> psConstantBuffer_;

    MagnetVertex *mappedVB_ = nullptr;
    MagnetVSConstants *mappedVS_ = nullptr;
    MagnetPSConstants *mappedPS_ = nullptr;
};