#pragma once
#include "Camera.h"
#include <DirectXMath.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <d3d12.h>
#include <vector>
#include <wrl.h>

class DirectXCommon;

class SwordSlashArcRenderer {
  public:
    void Initialize(DirectXCommon *dxCommon);
    void Reset();

    void Emit(const DirectX::XMFLOAT3 &root, const DirectX::XMFLOAT3 &tip,
              const DirectX::XMFLOAT3 &playerPosition,
              const DirectX::XMFLOAT3 &targetPosition, const Camera &camera,
              size_t swordIndex);
    void EmitHitLine(const DirectX::XMFLOAT3 &position,
                     const DirectX::XMFLOAT3 &direction, const Camera &camera,
                     float power,
                     const DirectX::XMFLOAT2 &slashDirection = {0.0f, 0.0f},
                     bool isCounter = false);
    void EmitParryLine(const DirectX::XMFLOAT3 &position,
                       const DirectX::XMFLOAT3 &direction, const Camera &camera,
                       float power,
                       const DirectX::XMFLOAT2 &slashDirection = {0.0f, 0.0f});
    void EmitDirectionCueLine(const DirectX::XMFLOAT3 &position,
                              const DirectX::XMFLOAT2 &direction,
                              const Camera &camera,
                              const DirectX::XMFLOAT4 &color,
                              bool releaseCounterCueVisible);
    void ClearDirectionCueLines();
    void Update(float deltaTime);
    void Draw(const Camera &camera);

  private:
    struct ArcInstance {
        DirectX::XMFLOAT3 center{};
        DirectX::XMFLOAT3 axisA{1.0f, 0.0f, 0.0f};
        DirectX::XMFLOAT3 axisB{0.0f, 1.0f, 0.0f};
        DirectX::XMFLOAT4 color{0.20f, 0.62f, 1.0f, 1.0f};
        float radius = 1.4f;
        float thickness = 0.18f;
        float startAngle = 0.0f;
        float endAngle = 0.0f;
        float age = 0.0f;
        float life = 0.16f;
        bool isLine = false;
        bool isDirectionCue = false;
        bool active = false;
    };

    struct ArcVertex {
        DirectX::XMFLOAT3 position{};
        DirectX::XMFLOAT2 uv{};
        DirectX::XMFLOAT4 color{};
    };

    struct ViewProjectionConstBufferData {
        DirectX::XMFLOAT4X4 matViewProjection{};
    };

  private:
    void CreateRootSignature();
    void CreatePipelineState();
    void CreateBuffers();
    void EnsureVertexCapacity(uint32_t vertexCount);
    void BuildVertices();
    ArcInstance &AcquireTransientArc();

  private:
    static constexpr size_t kDirectionCueArcCount = 4;
    static constexpr size_t kMaxArcs = 24;
    static constexpr uint32_t kSegments = 28;
    static constexpr uint32_t kInitialMaxVertices = kMaxArcs * kSegments * 6;

    DirectXCommon *dxCommon_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> viewProjectionBuffer_;

    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    ArcVertex *mappedVertices_ = nullptr;
    ViewProjectionConstBufferData *mappedViewProjection_ = nullptr;
    uint32_t vertexCapacity_ = 0;
    uint32_t vertexCount_ = 0;
    size_t nextArc_ = 0;

    std::array<ArcInstance, kMaxArcs> arcs_{};
};
