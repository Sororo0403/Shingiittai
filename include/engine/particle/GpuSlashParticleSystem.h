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

class GpuSlashParticleSystem {
  public:
    void Initialize(DirectXCommon *dxCommon, SrvManager *srvManager,
                    TextureManager *textureManager, uint32_t maxParticles);

    void EmitSlashBurst(const DirectX::XMFLOAT3 &start,
                        const DirectX::XMFLOAT3 &end, uint32_t count,
                        float emitScale, bool isSweep);

    void Render(const Camera &camera, float deltaTime);

  private:
    struct ParticleGpu {
        DirectX::XMFLOAT3 position;
        float life;

        DirectX::XMFLOAT3 velocity;
        float maxLife;

        DirectX::XMFLOAT4 color;

        DirectX::XMFLOAT2 size;
        float rotation;
        float alive;

        DirectX::XMFLOAT3 accel;
        float type;
    };

    struct EmitRequest {
        DirectX::XMFLOAT3 start;
        float pad0 = 0.0f;

        DirectX::XMFLOAT3 end;
        float emitScale = 1.0f;

        uint32_t count = 0;
        uint32_t isSweep = 0;
        float pad1 = 0.0f;
        float pad2 = 0.0f;
    };

    struct CameraCB {
        DirectX::XMFLOAT4X4 viewProj;
        DirectX::XMFLOAT4X4 invView;
    };

    struct SimCB {
        float deltaTime = 0.0f;
        float time = 0.0f;
        uint32_t emitRequestCount = 0;
        uint32_t maxParticles = 0;
    };

    struct ControlData {
        uint32_t head = 0;
        uint32_t pad0 = 0;
        uint32_t pad1 = 0;
        uint32_t pad2 = 0;
    };

  private:
    void CreateRootSignatures();
    void CreatePipelineStates();
    void CreateBuffers();

    void EnsureGpuBuffersInitialized(ID3D12GraphicsCommandList *cmd);

    void UpdateCameraCB(const Camera &camera);
    void UploadEmitRequests();
    void DispatchEmitAndUpdate(float deltaTime);
    void DrawParticles(const Camera &camera);

    Microsoft::WRL::ComPtr<ID3DBlob>
    CompileShader(const wchar_t *path, const char *entry, const char *target);

    Microsoft::WRL::ComPtr<ID3D12Resource>
    CreateBufferResource(uint64_t size, D3D12_HEAP_TYPE heapType,
                         D3D12_RESOURCE_STATES initialState,
                         bool allowUav = false);

    void Transition(ID3D12GraphicsCommandList *cmd, ID3D12Resource *resource,
                    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);

  private:
    DirectXCommon *dxCommon_ = nullptr;
    SrvManager *srvManager_ = nullptr;
    TextureManager *textureManager_ = nullptr;

    uint32_t maxParticles_ = 0;
    float time_ = 0.0f;

    std::vector<EmitRequest> pendingEmits_;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> emitRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> updateRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> drawRootSignature_;

    Microsoft::WRL::ComPtr<ID3D12PipelineState> emitPSO_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> updatePSO_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> drawPSO_;

    Microsoft::WRL::ComPtr<ID3D12Resource> particleBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> controlBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> emitRequestBuffer_;

    // GPU初期化用アップロード
    Microsoft::WRL::ComPtr<ID3D12Resource> particleInitUpload_;
    Microsoft::WRL::ComPtr<ID3D12Resource> controlInitUpload_;

    Microsoft::WRL::ComPtr<ID3D12Resource> cameraCB_;
    Microsoft::WRL::ComPtr<ID3D12Resource> simCB_;

    CameraCB *mappedCameraCB_ = nullptr;
    SimCB *mappedSimCB_ = nullptr;
    EmitRequest *mappedEmitRequests_ = nullptr;

    bool gpuBuffersInitialized_ = false;

    D3D12_RESOURCE_STATES particleBufferState_ = D3D12_RESOURCE_STATE_COPY_DEST;
    D3D12_RESOURCE_STATES controlBufferState_ = D3D12_RESOURCE_STATE_COPY_DEST;
};