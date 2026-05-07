#pragma once
#include "Camera.h"
#include "CollisionManager.h"
#include <DirectXMath.h>
#include <cstdint>
#include <d3d12.h>
#include <vector>
#include <wrl.h>

class DirectXCommon;

class CollisionDebugRenderer {
  public:
    void Initialize(DirectXCommon *dxCommon);
    void Draw(const CollisionManager &collisionManager, const Camera &camera,
              bool highlightHits = true);

  private:
    struct LineVertex {
        DirectX::XMFLOAT3 position{};
        DirectX::XMFLOAT4 color{};
    };

    struct ViewProjectionConstBufferData {
        DirectX::XMFLOAT4X4 matViewProjection{};
    };

    void CreateRootSignature();
    void CreatePipelineState();
    void CreateBuffers();
    void EnsureVertexCapacity(uint32_t vertexCount);
    void AddOBB(const OBB &box, const DirectX::XMFLOAT4 &color);
    DirectX::XMFLOAT4 GetBodyColor(
        const CollisionManager::Body &body,
        const std::vector<CollisionManager::Hit> &hits) const;
    bool IsHitBody(CollisionManager::BodyId bodyId,
                   const std::vector<CollisionManager::Hit> &hits) const;

  private:
    static constexpr uint32_t kInitialMaxVertices = 2048;

    DirectXCommon *dxCommon_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> viewProjectionBuffer_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    LineVertex *mappedVertices_ = nullptr;
    ViewProjectionConstBufferData *mappedViewProjection_ = nullptr;
    uint32_t vertexCapacity_ = 0;
    uint32_t vertexCount_ = 0;
};
