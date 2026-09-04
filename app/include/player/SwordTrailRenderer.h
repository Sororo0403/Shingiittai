#pragma once
#include "Camera.h"
#include "Player.h"
#include <DirectXMath.h>
#include <array>
#include <cstdint>
#include <d3d12.h>
#include <vector>
#include <wrl.h>

class DirectXCommon;

/// <summary>
/// 剣先の履歴から半透明の斬撃軌跡を構築して描画する
/// </summary>
class SwordTrailRenderer {
  public:
    /// <summary>
    /// 使用するリソースと初期状態を準備する
    /// </summary>
    void Initialize(DirectXCommon *dxCommon);
    /// <summary>
    /// Resetが管理する状態を初期値へ戻す
    /// </summary>
    void Reset();

    /// <summary>
    /// 入力と状態を1フレーム進める
    /// </summary>
    void Update(const Player &player, float deltaTime);
    /// <summary>
    /// SuppressSlashUntilInactiveに対応する処理を一時的に抑制する
    /// </summary>
    void SuppressSlashUntilInactive(size_t swordIndex);
    /// <summary>
    /// 現在の状態を描画する
    /// </summary>
    void Draw(const Camera &camera);

  private:
    struct TrailSample {
        DirectX::XMFLOAT3 root{};
        DirectX::XMFLOAT3 tip{};
        float age = 0.0f;
    };

    struct TrailState {
        std::vector<TrailSample> samples;
        bool wasActive = false;
        bool suppressUntilInactive = false;
    };

    struct TrailVertex {
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

    void UpdateOneSword(size_t index, const Sword *sword, bool isSlashing,
                        float damage, float deltaTime);

    void AddSample(TrailState &trail, const DirectX::XMFLOAT3 &root,
                   const DirectX::XMFLOAT3 &tip, bool force);
    void BuildVertices();

    DirectX::XMFLOAT4 GetTrailColor(float alpha) const;

  private:
    static constexpr size_t kSwordCount = Player::kSwordCount;
    static constexpr uint32_t kInitialMaxVertices = 256;
    static constexpr uint32_t kMaxSamplesPerSword = 18;

    static constexpr float kTrailLife = 0.10f;
    static constexpr float kMinAddDistance = 0.035f;

    DirectXCommon *dxCommon_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> viewProjectionBuffer_;

    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    TrailVertex *mappedVertices_ = nullptr;
    ViewProjectionConstBufferData *mappedViewProjection_ = nullptr;

    uint32_t vertexCapacity_ = 0;
    uint32_t vertexCount_ = 0;

    std::array<TrailState, kSwordCount> trails_{};
};
