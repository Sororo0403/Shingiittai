#pragma once
#include <DirectXMath.h>
#include <cstdint>
#include <d3d12.h>
#include <vector>
#include <wrl.h>

class Camera;
class DirectXCommon;
class SrvManager;
class TextureManager;

class SwordTrailRenderer {
  public:
    void Initialize(DirectXCommon *dxCommon, SrvManager *srvManager,
                    TextureManager *textureManager, uint32_t maxPoints = 24);

    void Reset();

    void BeginFrame(float deltaTime);
    void AddPoint(const DirectX::XMFLOAT3 &baseWorld,
                  const DirectX::XMFLOAT3 &tipWorld, float width = 1.0f);
    void EndFrame();

    void Draw(const Camera &camera);

    void SetEnabled(bool enabled) { enabled_ = enabled; }
    void SetLifeTime(float lifeTime) { lifeTime_ = lifeTime; }

  private:
    struct TrailPoint {
        DirectX::XMFLOAT3 base;
        float width = 1.0f;

        DirectX::XMFLOAT3 tip;
        float age = 0.0f;
    };

    struct Vertex {
        DirectX::XMFLOAT3 position;
        float side; // 0 = base, 1 = tip

        float t;     // along trail
        float alpha; // life fade
        float width; // width factor
        float pad0;
    };

    struct MaterialCB {
        DirectX::XMFLOAT4X4 viewProj;

            DirectX::XMFLOAT4 innerColor = {0.92f, 0.94f, 1.00f, 1.0f};
        DirectX::XMFLOAT4 outerColor = {0.56f, 0.30f, 0.96f, 1.0f};
        DirectX::XMFLOAT4 darkColor = {0.01f, 0.00f, 0.03f, 1.0f};

        float globalAlpha = 0.92f;
        float noiseScale = 24.0f;
        float corePower = 1.0f;
        float edgePower = 1.0f;

        float time = 0.0f;
        float pad1 = 0.0f;
        float pad2 = 0.0f;
        float pad3 = 0.0f;
    };

  private:
    void CreateRootSignature();
    void CreatePipelineState();
    void CreateBuffers();
    void RebuildVertices();
    void UploadVertices();

  private:
    DirectXCommon *dxCommon_ = nullptr;
    SrvManager *srvManager_ = nullptr;
    TextureManager *textureManager_ = nullptr;

    bool enabled_ = true;
    float time_ = 0.0f;
    float deltaTime_ = 0.0f;
    float lifeTime_ = 0.20f;
    uint32_t maxPoints_ = 24;

    std::vector<TrailPoint> points_;
    std::vector<Vertex> vertices_;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> materialCB_;

    D3D12_VERTEX_BUFFER_VIEW vbView_{};

    Vertex *mappedVB_ = nullptr;
    MaterialCB *mappedCB_ = nullptr;
};