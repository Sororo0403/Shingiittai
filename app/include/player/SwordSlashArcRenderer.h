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

/// <summary>
/// 命中線の視覚スタイル
/// </summary>
enum class SwordSlashHitLineStyle {
    Normal,
    RedPunish,
};

/// <summary>
/// 斬撃弧、命中線、方向キューを一時エフェクトとして描画する
/// </summary>
class SwordSlashArcRenderer {
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
    /// Emitに対応する演出を生成する
    /// </summary>
    void Emit(const DirectX::XMFLOAT3 &root, const DirectX::XMFLOAT3 &tip,
              const DirectX::XMFLOAT3 &playerPosition,
              const DirectX::XMFLOAT3 &targetPosition, const Camera &camera,
              size_t swordIndex);
    /// <summary>
    /// EmitHitLineに対応する演出を生成する
    /// </summary>
    void
    EmitHitLine(const DirectX::XMFLOAT3 &position,
                const DirectX::XMFLOAT3 &direction, const Camera &camera,
                float power,
                const DirectX::XMFLOAT2 &slashDirection = {0.0f, 0.0f},
                SwordSlashHitLineStyle style = SwordSlashHitLineStyle::Normal);
    /// <summary>
    /// EmitParryLineに対応する演出を生成する
    /// </summary>
    void EmitParryLine(const DirectX::XMFLOAT3 &position,
                       const DirectX::XMFLOAT3 &direction, const Camera &camera,
                       float power,
                       const DirectX::XMFLOAT2 &slashDirection = {0.0f, 0.0f});
    /// <summary>
    /// EmitCinematicCutLineに対応する演出を生成する
    /// </summary>
    void EmitCinematicCutLine(const DirectX::XMFLOAT3 &position,
                              const DirectX::XMFLOAT3 &direction,
                              const Camera &camera, float power);
    /// <summary>
    /// EmitEnemyWindSlashに対応する演出を生成する
    /// </summary>
    void EmitEnemyWindSlash(const DirectX::XMFLOAT3 &position, float yaw,
                            const Camera &camera, bool horizontal, float power);
    /// <summary>
    /// EmitDirectionCueLineに対応する演出を生成する
    /// </summary>
    void EmitDirectionCueLine(const DirectX::XMFLOAT3 &position,
                              const DirectX::XMFLOAT2 &direction,
                              const Camera &camera,
                              const DirectX::XMFLOAT4 &color,
                              bool releaseCounterCueVisible,
                              float sizeScale = 1.0f);
    /// <summary>
    /// ClearDirectionCueLinesが管理する状態を消去する
    /// </summary>
    void ClearDirectionCueLines();
    /// <summary>
    /// 入力と状態を1フレーム進める
    /// </summary>
    void Update(float deltaTime);
    /// <summary>
    /// 現在の状態を描画する
    /// </summary>
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
        bool instantLineReveal = false;
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
    bool AppendLineVertices(const ArcInstance &arc, float ageRate);
    bool AppendArcVertices(const ArcInstance &arc, float ageRate);
    ArcInstance &AcquireTransientArc();

  private:
    static constexpr size_t kDirectionCueArcCount = 24;
    static constexpr size_t kMaxArcs = 56;
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
    size_t nextDirectionCueArc_ = 0;
    size_t nextArc_ = 0;

    std::array<ArcInstance, kMaxArcs> arcs_{};
};
