#pragma once
#include "camera/Camera.h"
#include <DirectXMath.h>
#include <cmath>

struct ModelDrawEffect {
    bool enabled = false;
    bool additiveBlend = false;
    bool disableCulling = false;
    bool forceOpaqueMaterial = false;
    DirectX::XMFLOAT4 color{1.0f, 1.0f, 1.0f, 1.0f};
    float intensity = 0.0f;
    float fresnelPower = 1.0f;
    float noiseAmount = 0.0f;
    float time = 0.0f;
};

#define SetDrawEffect(...) GetRenderer()
#define ClearDrawEffect() GetRenderer()

#define ResetEffects() SetSpecialMode(PostEffectSpecialMode::None)
#define SetVignettingEnabled(enabled)                                           \
    SetSpecialMode((enabled) ? PostEffectSpecialMode::Vignette                  \
                             : PostEffectSpecialMode::None)
#define SetVignettingStrength(strength) SetVignette((strength), 0.72f)
#define SetVignettingShape(...) GetSpecialMode()
#define SetSceneDimStrength(...) GetSpecialMode()
#define SetRadialBlurStrength(strength) SetRadialBlur(strength)
#define SetRadialBlurCenter(...) GetSpecialMode()
#define SetRadialBlurSampleCount(...) GetSpecialMode()
#define SetRandomMode(...) GetSpecialMode()
#define SetRandomStrength(...) GetSpecialMode()
#define SetRandomTime(time) SetNoiseTime(time)
#define SetRandomScale(...) GetSpecialMode()

#define SetClearColor(...) GetDevice()
#define ResetClearColor() GetDevice()

#define SetEmission(...) GetEmitterSettings()
#define SetEmitterRadius(...) GetEmitterSettings()

#define CreateRustedMetalTexture(...) GetWhiteTextureId()
#define CreateArenaStoneTexture(...) GetWhiteTextureId()
#define CreateLowPolyTerrain(textureId, material, ...) CreatePlane(textureId, material)
#define CreateBox(textureId, material, ...) CreatePlane(textureId, material)

#define enableDissolve customParams.x
#define dissolveThreshold customParams.y
#define dissolveEdgeWidth customParams.z
#define dissolveEdgeColor customParams3

inline void AppLookAt(Camera &camera, const DirectX::XMFLOAT3 &target) {
    const DirectX::XMFLOAT3 &position = camera.GetPosition();
    const float dx = target.x - position.x;
    const float dy = target.y - position.y;
    const float dz = target.z - position.z;
    const float yaw = std::atan2f(dx, dz);
    const float horizontal = std::sqrt(dx * dx + dz * dz);
    const float pitch = -std::atan2f(dy, horizontal);
    camera.SetRotation({pitch, yaw, 0.0f});
    camera.UpdateMatrices();
}

inline DirectX::XMFLOAT3 AppCameraForward(const Camera &camera) {
    const DirectX::XMFLOAT3 &rotation = camera.GetRotation();
    const float cp = std::cos(rotation.x);
    return {std::sin(rotation.y) * cp, -std::sin(rotation.x),
            std::cos(rotation.y) * cp};
}

enum class AppParticleBurstStyle {
    Explosion,
    Sparks,
    Flash,
    Smoke,
    SlashLine,
    SpiritSparkle,
};
