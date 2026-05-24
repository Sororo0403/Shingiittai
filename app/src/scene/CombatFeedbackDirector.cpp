#include "CombatFeedbackDirector.h"
#include "PostProcessSystem.h"
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

void CombatFeedbackDirector::Initialize(PostProcessSystem *postProcessSystem) {
    postProcessSystem_ = postProcessSystem;
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

    if (postProcessSystem_) {
        PostProcessProfile profile = postProcessSystem_->GetProfile();
        profile.radialBlur.strength = 0.0f;
        profile.radialBlur.sampleCount = 14;
        profile.randomNoise.mode = PostProcessRandomMode::None;
        profile.randomNoise.strength = 0.0f;
        profile.sceneDim.strength = 0.0f;
        profile.vignette.enabled = false;
        profile.vignette.strength = 0.0f;
        postProcessSystem_->SetProfile(profile);
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

    if (!postProcessSystem_) {
        return;
    }

    const float postRatio =
        postDuration_ > kMinVectorLength ? postTimer_ / postDuration_ : 0.0f;
    const float eased = EaseOut(postRatio);
    PostProcessProfile profile = postProcessSystem_->GetProfile();
    profile.radialBlur.center[0] = 0.5f;
    profile.radialBlur.center[1] = 0.5f;
    profile.radialBlur.strength = radialBlurStrength_ * eased;
    profile.radialBlur.sampleCount = 18;
    const float vignetteStrength = vignetteBoost_ * eased;
    profile.vignette.enabled = vignetteStrength > 0.001f;
    profile.vignette.strength = vignetteStrength;
    profile.vignette.scale = 11.0f;
    profile.vignette.power = 1.15f;
    profile.randomNoise.time = sceneTime;
    profile.randomNoise.scale = 320.0f;
    profile.noise.time = sceneTime;

    if (randomStrength_ * eased > 0.001f) {
        profile.randomNoise.mode = PostProcessRandomMode::OverlayNoise;
        profile.randomNoise.strength = randomStrength_ * eased;
    } else {
        profile.randomNoise.mode = PostProcessRandomMode::None;
        profile.randomNoise.strength = 0.0f;
    }
    profile.sceneDim.strength = 0.0f;
    postProcessSystem_->SetProfile(profile);
}

void CombatFeedbackDirector::PushEvent(const CombatFeedbackEvent &event) {
    const float power = std::clamp(event.power, 0.5f, 4.0f);

    switch (event.type) {
    case CombatFeedbackEventType::PlayerSlashHit:
        AddHitStop(0.045f + 0.012f * power, 0.16f);
        AddCameraShake(0.13f, 0.016f + 0.005f * power,
                       0.010f + 0.003f * power);
        AddPostFlash(0.13f, 0.016f + 0.007f * power, 0.025f, 0.04f);
        fovKickDeg_ = (std::max)(fovKickDeg_, 1.6f + 0.42f * power);
        break;
    case CombatFeedbackEventType::PlayerDamaged:
        AddHitStop(0.115f, 0.035f);
        AddCameraShake(0.24f, 0.046f, 0.030f);
        AddPostFlash(0.22f, 0.034f, 0.072f, 0.12f);
        fovKickDeg_ = (std::max)(fovKickDeg_, 2.6f);
        break;
    case CombatFeedbackEventType::CounterSuccess:
        AddHitStop(0.285f, 0.012f);
        AddCameraShake(0.42f, 0.082f, 0.052f);
        AddPostFlash(0.48f, 0.22f, 0.13f, 0.26f);
        fovKickDeg_ = (std::max)(fovKickDeg_, 7.0f);
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

    const float shakeA = std::sinf(sceneTime * 72.0f);
    const float shakeB = std::cosf(sceneTime * 103.0f + 0.7f);
    const float horizontal = shakeHorizontal_ * ratio * shakeA;
    const float vertical = shakeVertical_ * ratio * shakeB;

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
