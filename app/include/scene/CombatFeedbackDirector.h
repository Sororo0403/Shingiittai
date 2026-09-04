#pragma once
#include "Camera.h"
#include "PostEffectManager.h"
#include <DirectXMath.h>
#include <cstddef>

/// <summary>
/// 戦闘フィードバックへ渡す演出イベントの種類
/// </summary>
enum class CombatFeedbackEventType {
    PlayerSlashHit,
    PlayerDamaged,
    MistimedCounterSlash,
    CounterSuccess,
    EnemyProjectileReflect,
    BladeClashGuardBreak,
    BladeClashPierce,
};

/// <summary>
/// ヒットストップやカメラ揺れを生成するためのイベント情報
/// </summary>
struct CombatFeedbackEvent {
    CombatFeedbackEventType type = CombatFeedbackEventType::PlayerSlashHit;
    DirectX::XMFLOAT3 position = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 direction = {0.0f, 0.0f, 1.0f};
    float power = 1.0f;
    size_t swordIndex = 0;
};

/// <summary>
/// 戦闘イベントを時間倍率、画角変化、カメラ揺れへ変換する
/// </summary>
class CombatFeedbackDirector {
  public:
    /// <summary>
    /// ~CombatFeedbackDirectorに対応する公開処理を実行する
    /// </summary>
    ~CombatFeedbackDirector();

    /// <summary>
    /// 使用するリソースと初期状態を準備する
    /// </summary>
    void Initialize(PostEffectManager *postEffectManager);
    /// <summary>
    /// Resetが管理する状態を初期値へ戻す
    /// </summary>
    void Reset();
    /// <summary>
    /// 入力と状態を1フレーム進める
    /// </summary>
    void Update(float deltaTime, float sceneTime);

    /// <summary>
    /// PushEventに対応するイベントを処理待ちキューへ追加する
    /// </summary>
    void PushEvent(const CombatFeedbackEvent &event);

    /// <summary>
    /// GetGameplayTimeScaleに対応する現在値を取得する
    /// </summary>
    float GetGameplayTimeScale() const;
    /// <summary>
    /// GetFovKickDegに対応する現在値を取得する
    /// </summary>
    float GetFovKickDeg() const;
    /// <summary>
    /// ApplyCameraImpulseに対応する結果を適用する
    /// </summary>
    void ApplyCameraImpulse(DirectX::XMFLOAT3 &cameraPosition,
                            DirectX::XMFLOAT3 &lookAt, float sceneTime) const;
    /// <summary>
    /// AddCameraShakeに対応する要素を追加する
    /// </summary>
    void AddCameraShake(float duration, float horizontal, float vertical);

  private:
    void UpdateHitStopTimer(float deltaTime);
    void UpdateShakeTimer(float deltaTime);
    void UpdatePostTimer(float deltaTime);
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
