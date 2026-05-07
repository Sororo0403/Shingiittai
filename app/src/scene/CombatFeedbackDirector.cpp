#include "CombatFeedbackDirector.h"
#include "PostEffectRenderer.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace {
constexpr float kMinVectorLength = 0.0001f;

float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float EaseOut(float value) {
    const float t = Clamp01(value);
    return 1.0f - (1.0f - t) * (1.0f - t);
}

XMFLOAT3 Scale(const XMFLOAT3 &v, float s) {
    return {v.x * s, v.y * s, v.z * s};
}

XMFLOAT3 Add(const XMFLOAT3 &a, const XMFLOAT3 &b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

} // namespace

void CombatFeedbackDirector::Initialize(PostEffectRenderer *postEffectRenderer) {
    postEffectRenderer_ = postEffectRenderer;
    Reset();
}

void CombatFeedbackDirector::Reset() {
    hitStopTimer_ = 0.0f;
    hitStopDuration_ = 0.0f;
    hitStopTimeScale_ = 1.0f;
    shakeTimer_ = 0.0f;
    shakeDuration_ = 0.0f;
    shakeHorizontal_ = 0.0f;
    shakeVertical_ = 0.0f;
    postTimer_ = 0.0f;
    postDuration_ = 0.0f;
    radialBlurStrength_ = 0.0f;
    randomStrength_ = 0.0f;
    vignetteBoost_ = 0.0f;
    fovKickDeg_ = 0.0f;

    if (postEffectRenderer_) {
        postEffectRenderer_->SetRadialBlurStrength(0.0f);
        postEffectRenderer_->SetRadialBlurSampleCount(14);
        postEffectRenderer_->SetRandomMode(PostEffectRenderer::RandomMode::None);
        postEffectRenderer_->SetRandomStrength(0.0f);
        postEffectRenderer_->SetVignettingStrength(baseVignetteStrength_);
    }
}

void CombatFeedbackDirector::Update(float deltaTime, float sceneTime) {
    if (hitStopTimer_ > 0.0f) {
        hitStopTimer_ = (std::max)(0.0f, hitStopTimer_ - deltaTime);
    }
    if (hitStopTimer_ <= 0.0f) {
        hitStopDuration_ = 0.0f;
        hitStopTimeScale_ = 1.0f;
    }
    if (shakeTimer_ > 0.0f) {
        shakeTimer_ = (std::max)(0.0f, shakeTimer_ - deltaTime);
    }
    if (shakeTimer_ <= 0.0f) {
        shakeDuration_ = 0.0f;
        shakeHorizontal_ = 0.0f;
        shakeVertical_ = 0.0f;
    }
    if (postTimer_ > 0.0f) {
        postTimer_ = (std::max)(0.0f, postTimer_ - deltaTime);
    }
    if (postTimer_ <= 0.0f) {
        postDuration_ = 0.0f;
        radialBlurStrength_ = 0.0f;
        randomStrength_ = 0.0f;
        vignetteBoost_ = 0.0f;
        fovKickDeg_ = 0.0f;
    }

    if (!postEffectRenderer_) {
        return;
    }

    const float postRatio =
        postDuration_ > kMinVectorLength ? postTimer_ / postDuration_ : 0.0f;
    const float eased = EaseOut(postRatio);
    postEffectRenderer_->SetRadialBlurCenter(0.5f, 0.5f);
    postEffectRenderer_->SetRadialBlurStrength(radialBlurStrength_ * eased);
    postEffectRenderer_->SetRadialBlurSampleCount(18);
    postEffectRenderer_->SetVignettingStrength(baseVignetteStrength_ +
                                               vignetteBoost_ * eased);
    postEffectRenderer_->SetRandomTime(sceneTime);
    postEffectRenderer_->SetRandomScale(320.0f);

    if (randomStrength_ * eased > 0.001f) {
        postEffectRenderer_->SetRandomMode(
            PostEffectRenderer::RandomMode::OverlayNoise);
        postEffectRenderer_->SetRandomStrength(randomStrength_ * eased);
    } else {
        postEffectRenderer_->SetRandomMode(PostEffectRenderer::RandomMode::None);
        postEffectRenderer_->SetRandomStrength(0.0f);
    }
}

void CombatFeedbackDirector::PushEvent(const CombatFeedbackEvent &event) {
    const float power = std::clamp(event.power, 0.5f, 4.0f);

    switch (event.type) {
    case CombatFeedbackEventType::PlayerSlashHit:
        AddHitStop(0.030f + 0.010f * power, 0.20f);
        AddCameraShake(0.13f, 0.018f + 0.006f * power,
                       0.012f + 0.004f * power);
        AddPostFlash(0.13f, 0.020f + 0.010f * power, 0.06f, 0.08f);
        fovKickDeg_ = (std::max)(fovKickDeg_, 1.2f + 0.35f * power);
        break;
    case CombatFeedbackEventType::PlayerGuard:
        AddHitStop(0.025f, 0.24f);
        AddCameraShake(0.12f, 0.018f, 0.010f);
        AddPostFlash(0.10f, 0.018f, 0.05f, 0.06f);
        fovKickDeg_ = (std::max)(fovKickDeg_, 1.0f);
        break;
    case CombatFeedbackEventType::PlayerDamaged:
        AddHitStop(0.045f, 0.18f);
        AddCameraShake(0.22f, 0.050f, 0.032f);
        AddPostFlash(0.20f, 0.030f, 0.16f, 0.18f);
        fovKickDeg_ = (std::max)(fovKickDeg_, 2.6f);
        break;
    case CombatFeedbackEventType::CounterSuccess:
        AddHitStop(0.150f, 0.06f);
        AddCameraShake(0.34f, 0.075f, 0.045f);
        AddPostFlash(0.32f, 0.070f, 0.22f, 0.26f);
        fovKickDeg_ = (std::max)(fovKickDeg_, 4.5f);
        break;
    case CombatFeedbackEventType::ProjectileReflect:
        AddHitStop(0.060f, 0.12f);
        AddCameraShake(0.18f, 0.036f, 0.022f);
        AddPostFlash(0.18f, 0.045f, 0.13f, 0.15f);
        fovKickDeg_ = (std::max)(fovKickDeg_, 2.8f);
        break;
    }

    (void)event.position;
    (void)event.direction;
    (void)event.swordIndex;
}

float CombatFeedbackDirector::GetGameplayTimeScale() const {
    if (hitStopTimer_ <= 0.0f) {
        return 1.0f;
    }
    return hitStopTimeScale_;
}

float CombatFeedbackDirector::GetFovKickDeg() const {
    const float postRatio =
        postDuration_ > kMinVectorLength ? postTimer_ / postDuration_ : 0.0f;
    return fovKickDeg_ * EaseOut(postRatio);
}

void CombatFeedbackDirector::ApplyCameraImpulse(XMFLOAT3 &cameraPosition,
                                                XMFLOAT3 &lookAt,
                                                float sceneTime) const {
    const float ratio = ShakeRatio();
    if (ratio <= 0.0f) {
        return;
    }

    XMVECTOR pos = XMLoadFloat3(&cameraPosition);
    XMVECTOR target = XMLoadFloat3(&lookAt);
    XMVECTOR forward = XMVector3Normalize(XMVectorSubtract(target, pos));
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    XMVECTOR right = XMVector3Cross(up, forward);
    if (XMVectorGetX(XMVector3LengthSq(right)) < kMinVectorLength) {
        right = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
    } else {
        right = XMVector3Normalize(right);
    }

    const float waveA = std::sinf(sceneTime * 72.0f);
    const float waveB = std::cosf(sceneTime * 103.0f + 0.7f);
    const float horizontal = shakeHorizontal_ * ratio * waveA;
    const float vertical = shakeVertical_ * ratio * waveB;

    XMFLOAT3 rightF{};
    XMFLOAT3 upF{};
    XMStoreFloat3(&rightF, right);
    XMStoreFloat3(&upF, up);

    const XMFLOAT3 posOffset =
        Add(Scale(rightF, horizontal), Scale(upF, vertical));
    const XMFLOAT3 lookOffset =
        Add(Scale(rightF, -horizontal * 0.42f),
            Scale(upF, -vertical * 0.55f));

    cameraPosition = Add(cameraPosition, posOffset);
    lookAt = Add(lookAt, lookOffset);
}

void CombatFeedbackDirector::AddHitStop(float duration, float timeScale) {
    if (duration > hitStopTimer_) {
        hitStopTimer_ = duration;
        hitStopDuration_ = duration;
        hitStopTimeScale_ = timeScale;
    }
}

void CombatFeedbackDirector::AddCameraShake(float duration, float horizontal,
                                            float vertical) {
    if (duration > shakeTimer_) {
        shakeTimer_ = duration;
        shakeDuration_ = duration;
    }
    shakeHorizontal_ = (std::max)(shakeHorizontal_, horizontal);
    shakeVertical_ = (std::max)(shakeVertical_, vertical);
}

void CombatFeedbackDirector::AddPostFlash(float duration, float blurStrength,
                                          float noiseStrength,
                                          float vignetteBoost) {
    if (duration > postTimer_) {
        postTimer_ = duration;
        postDuration_ = duration;
    }
    radialBlurStrength_ = (std::max)(radialBlurStrength_, blurStrength);
    randomStrength_ = (std::max)(randomStrength_, noiseStrength);
    vignetteBoost_ = (std::max)(vignetteBoost_, vignetteBoost);
}

float CombatFeedbackDirector::ShakeRatio() const {
    if (shakeDuration_ <= kMinVectorLength || shakeTimer_ <= 0.0f) {
        return 0.0f;
    }

    const float t = Clamp01(shakeTimer_ / shakeDuration_);
    return t * t;
}
