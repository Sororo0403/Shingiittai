#pragma once

#include "camera/Camera.h"
#include "debug/DebugLog.h"
#include "effect/EffectAsset.h"
#include "particle/GPUParticleSystem.h"
#include "particle/ParticleEmitterSettings.h"
#include "scene/SceneContext.h"

#include <DirectXMath.h>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class EffectManager {
  public:
    enum class PlushColorTheme {
        Default = 0,
        TeddyBear = 1,
        PastelToy = 2,
        DarkPlush = 3,
    };

    struct PlayDesc {
        DirectX::XMFLOAT3 position{0.0f, 0.0f, 0.0f};
        DirectX::XMFLOAT3 normal{0.0f, 1.0f, 0.0f};
        DirectX::XMFLOAT3 slashDirection{1.0f, 0.0f, 0.0f};
        float power = 0.5f;
        float scale = 1.0f;
        PlushColorTheme colorTheme = PlushColorTheme::Default;
    };

    void LoadEffect(const std::string &name, const std::string &path);
    bool InitializeGpu(const SceneRenderServices &rendering);
    void Play(const std::string &name, const DirectX::XMFLOAT3 &worldPosition);
    void Play(const std::string &name, const PlayDesc &desc);
    void Update(float deltaTime);
    void Draw(const Camera &camera);

    DirectX::XMFLOAT3 GetCameraShakeOffset() const;
    bool IsGpuInitialized() const { return gpuInitialized_; }
    size_t GetActiveEffectCount() const { return activeEffects_.size(); }

  private:
    struct CameraShakeInstance {
        CameraShakeDesc desc{};
        float elapsed = 0.0f;
    };

    struct ParticleLayerRuntime {
        ParticleLayerDesc desc{};
        GPUParticleSystem system{};
        uint32_t textureId = 0;
        bool initialized = false;
    };

    static ParticleEmitterSettings
    BuildEmitterSettings(const ParticleLayerDesc &desc,
                         const DirectX::XMFLOAT3 &position);
    static ParticleEmitterSettings BuildEmitterSettings(
        const ParticleLayerDesc &desc, const PlayDesc &playDesc);

    std::optional<size_t> FindAssetIndex(const std::string &name) const;
    size_t GetRuntimeLayerOffset(size_t assetIndex) const;
    void BuildRuntimesFromLoadedAssets();
    void Log(std::string_view state, std::string_view value,
             std::initializer_list<DebugLogField> fields = {}) const;

    std::vector<EffectAsset> assets_;
    std::vector<std::unique_ptr<ParticleLayerRuntime>> particleLayers_;
    std::vector<ActiveEffectInstance> activeEffects_;
    std::vector<CameraShakeInstance> cameraShakes_;
    bool gpuInitialized_ = false;
};
