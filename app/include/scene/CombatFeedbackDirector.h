#pragma once
#include "Camera.h"
#include "PostEffectManager.h"
#include <DirectXMath.h>
#include <cstddef>

enum class CombatFeedbackEventType {
    PlayerSlashHit,
    PlayerDamaged,
    CounterSuccess,
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
    ~CombatFeedbackDirector();

    void Initialize(PostEffectManager *postEffectManager);
    void Reset();
    void Update(float deltaTime, float sceneTime);

    void PushEvent(const CombatFeedbackEvent &event);

    float GetGameplayTimeScale() const;
    float GetFovKickDeg() const;
    void ApplyCameraImpulse(DirectX::XMFLOAT3 &cameraPosition,
                            DirectX::XMFLOAT3 &lookAt, float sceneTime) const;
    void AddCameraShake(float duration, float horizontal, float vertical);

  private:
    void AddHitStop(float duration, float timeScale);
    void AddPostFlash(float duration, float blurStrength, float noiseStrength,
                      float vignetteBoost, float primaryTintStrength = 0.0f,
                      const float *primaryTintColor = nullptr,
                      float secondaryTintStrength = 0.0f,
                      const float *secondaryTintColor = nullptr);
    float ShakeRatio() const;

  private:
    PostEffectManager *postEffectManager_ = nullptr;
    PostEffectLayerId postEffectLayer_ = 0;

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
    float primaryTintStrength_ = 0.0f;
    float primaryTintColor_[3]{1.0f, 1.0f, 1.0f};
    float secondaryTintStrength_ = 0.0f;
    float secondaryTintColor_[3]{1.0f, 1.0f, 1.0f};
    float fovKickDeg_ = 0.0f;
};
