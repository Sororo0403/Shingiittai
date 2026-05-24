#pragma once
#include "Camera.h"
#include "PostProcessSystem.h"
#include <DirectXMath.h>
#include <cstddef>

enum class CombatFeedbackEventType {
    PlayerSlashHit,
    PlayerGuard,
    PlayerDamaged,
    CounterSuccess,
    ProjectileReflect,
    BladeClashGuardBreak,
    BladeClashPierce,
};

struct CombatFeedbackEvent {
    CombatFeedbackEventType type = CombatFeedbackEventType::PlayerSlashHit;
    DirectX::XMFLOAT3 position = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 direction = {0.0f, 0.0f, 1.0f};
    float power = 1.0f;
    size_t swordIndex = 0;
};

class CombatFeedbackDirector {
  public:
    void Initialize(PostProcessSystem *postProcessSystem);
    void Reset();
    void Update(float deltaTime, float sceneTime);

    void PushEvent(const CombatFeedbackEvent &event);

    float GetGameplayTimeScale() const;
    float GetFovKickDeg() const;
    void ApplyCameraImpulse(DirectX::XMFLOAT3 &cameraPosition,
                            DirectX::XMFLOAT3 &lookAt,
                            float sceneTime) const;

  private:
    void AddHitStop(float duration, float timeScale);
    void AddCameraShake(float duration, float horizontal, float vertical);
    void AddPostFlash(float duration, float blurStrength, float noiseStrength,
                      float vignetteBoost);
    float ShakeRatio() const;

  private:
    PostProcessSystem *postProcessSystem_ = nullptr;

    float hitStopTimer_ = 0.0f;
    float hitStopDuration_ = 0.0f;
    float hitStopTimeScale_ = 1.0f;

    float shakeTimer_ = 0.0f;
    float shakeDuration_ = 0.0f;
    float shakeHorizontal_ = 0.0f;
    float shakeVertical_ = 0.0f;

    float postTimer_ = 0.0f;
    float postDuration_ = 0.0f;
    float radialBlurStrength_ = 0.0f;
    float randomStrength_ = 0.0f;
    float vignetteBoost_ = 0.0f;
    float fovKickDeg_ = 0.0f;

    float baseVignetteStrength_ = 0.20f;
};
