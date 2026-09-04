#pragma once
#include "SceneContext.h"
#include "SoundManager.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

/// <summary>
/// 複数シーンで共有する設定、音声、ハンドトラッキング連携を提供する
/// </summary>
namespace AppSceneServices {
using StartCallback = std::function<bool()>;
using BoolCallback = std::function<bool()>;
using VoidCallback = std::function<void()>;

inline constexpr const wchar_t *kMenuBgmPath =
    L"app/resources/audio/bgm/bgm_MenuTheme.wav";
inline constexpr const wchar_t *kSelectSePath =
    L"app/resources/audio/se/ui/se_Select.mp3";
inline constexpr const wchar_t *kSelectedSePath =
    L"app/resources/audio/se/ui/se_Selected.mp3";
inline constexpr const wchar_t *kCancelSePath =
    L"app/resources/audio/se/ui/se_Cancel.mp3";
inline constexpr float kMenuBgmBaseVolume = 0.24f;

inline StartCallback requestHandTrackingStart;
inline VoidCallback requestHandTrackingStop;
inline BoolCallback isCameraDeviceAvailable;
inline BoolCallback isHandTrackingReady;
inline uint32_t menuBgmSoundId = SoundManager::kInvalidSoundId;
inline uint32_t menuBgmVoiceHandle = SoundManager::kInvalidVoiceHandle;
inline uint32_t selectSeSoundId = SoundManager::kInvalidSoundId;
inline uint32_t selectedSeSoundId = SoundManager::kInvalidSoundId;
inline uint32_t cancelSeSoundId = SoundManager::kInvalidSoundId;
inline float bgmVolume = 1.0f;
inline float seVolume = 1.0f;
inline float cameraSensitivity = 0.5f;
inline float cameraSlashSensitivity = 0.5f;
inline float cameraVerticalSensitivity = 0.5f;
inline float cameraHorizontalSensitivity = 0.5f;
inline std::array<float, 2> cameraHandSensitivity = {0.5f, 0.5f};
inline std::array<float, 2> cameraHandSlashSensitivity = {0.5f, 0.5f};
inline std::array<float, 2> cameraHandVerticalSensitivity = {0.5f, 0.5f};
inline std::array<float, 2> cameraHandHorizontalSensitivity = {0.5f, 0.5f};
inline float mouseSlashSensitivity = 0.5f;

/// <summary>
/// ハンド入力の追従性、斬撃判定、異常値抑制に使う詳細設定
/// </summary>
struct CameraAdvancedSettings {
    float singleHandLeftThreshold = 0.45f;
    float singleHandRightThreshold = 0.55f;
    float handControlGainX = 3.10f;
    float handControlGainY = 3.70f;
    float handReachCompensationMin = 0.70f;
    float handReachCompensationMax = 1.45f;
    float handMinReachForCompensation = 0.12f;
    float handSlashThreshold = 0.96f;
    float handSlashResetThreshold = 0.34f;
    float handVerticalSlashThresholdScale = 0.82f;
    float handHardSensitivityGainScale = 0.70f;
    float handEasySensitivityGainScale = 1.55f;
    float handHardSensitivityThresholdScale = 1.30f;
    float handEasySensitivityThresholdScale = 0.62f;
    float handSlashRearmNeutralRadius = 0.20f;
    float handSlashNeutralRearmSeconds = 0.12f;
    float handSlashCooldownSeconds = 0.38f;
    float handControlSmoothing = 18.0f;
    float handFastControlSmoothing = 46.0f;
    float handFastMotionDistance = 0.045f;
    float handTiltMaxRadians = 0.52f;
    float handTiltSmoothing = 10.0f;
    float handTiltMinWidth = 0.12f;
    float handReacquireSuppressSeconds = 0.24f;
    float handReacquireSlashThreshold = 0.090f;
    float handEdgeExitSuppressSeconds = 0.44f;
    float handTrackingJumpThreshold = 0.30f;
    float handTrackingTeleportThreshold = 0.62f;
    float handJumpSlashDistanceThreshold = 0.085f;
    float handPreLossDirectionThreshold = 0.30f;
    float handPreLossNetDistanceThreshold = 0.026f;
    float handPreLossDirectionMaxAgeSeconds = 0.14f;
    float syntheticLostSlashSeconds = 0.16f;
    float handMotionWindowSeconds = 0.16f;
    float handStableNetDistanceThreshold = 0.017f;
    float handSlashNetDistanceThreshold = 0.031f;
    float handVelocitySlashNetDistanceThreshold = 0.0100f;
    float handStableConsistencyThreshold = 0.48f;
    float handReferenceVisualScale = 0.095f;
    float handMinVisualScale = 0.045f;
    float handMaxVisualScale = 0.180f;
    float handFarThresholdScale = 0.58f;
    float handNearThresholdScale = 1.18f;
};

inline CameraAdvancedSettings cameraAdvancedSettings{};

/// <summary>
/// 共通メニューで再生する操作効果音
/// </summary>
enum class MenuSe {
    Select,
    Selected,
    Cancel,
};

inline void ConfigureHandTracking(StartCallback start, VoidCallback stop,
                                  BoolCallback cameraAvailable,
                                  BoolCallback handReady) {
    requestHandTrackingStart = std::move(start);
    requestHandTrackingStop = std::move(stop);
    isCameraDeviceAvailable = std::move(cameraAvailable);
    isHandTrackingReady = std::move(handReady);
}

inline bool HasHandTrackingStart() {
    return static_cast<bool>(requestHandTrackingStart);
}

inline bool RequestHandTrackingStart() {
    if (requestHandTrackingStart) {
        return requestHandTrackingStart();
    }
    return false;
}

inline bool HasHandTrackingStop() {
    return static_cast<bool>(requestHandTrackingStop);
}

inline void RequestHandTrackingStop() {
    if (requestHandTrackingStop) {
        requestHandTrackingStop();
    }
}

inline bool HasCameraDeviceAvailable() {
    return static_cast<bool>(isCameraDeviceAvailable);
}

inline bool IsCameraDeviceAvailable() {
    return !isCameraDeviceAvailable || isCameraDeviceAvailable();
}

inline bool HasHandTrackingReady() {
    return static_cast<bool>(isHandTrackingReady);
}

inline bool IsHandTrackingReady() {
    return !isHandTrackingReady || isHandTrackingReady();
}

inline float GetBgmVolume() { return bgmVolume; }

inline float GetSeVolume() { return seVolume; }

inline size_t ClampCameraHandIndex(size_t handIndex) {
    return handIndex < cameraHandSensitivity.size()
               ? handIndex
               : cameraHandSensitivity.size() - 1;
}

inline float GetCameraSensitivity() { return cameraSensitivity; }

inline float GetCameraSensitivity(size_t handIndex) {
    return cameraHandSensitivity[ClampCameraHandIndex(handIndex)];
}

inline float GetCameraSlashSensitivity() { return cameraSlashSensitivity; }

inline float GetCameraSlashSensitivity(size_t handIndex) {
    return cameraHandSlashSensitivity[ClampCameraHandIndex(handIndex)];
}

inline float GetCameraVerticalSensitivity() {
    return cameraVerticalSensitivity;
}

inline float GetCameraVerticalSensitivity(size_t handIndex) {
    return cameraHandVerticalSensitivity[ClampCameraHandIndex(handIndex)];
}

inline float GetCameraHorizontalSensitivity() {
    return cameraHorizontalSensitivity;
}

inline float GetCameraHorizontalSensitivity(size_t handIndex) {
    return cameraHandHorizontalSensitivity[ClampCameraHandIndex(handIndex)];
}

inline float GetMouseSlashSensitivity() { return mouseSlashSensitivity; }

inline CameraAdvancedSettings &GetCameraAdvancedSettings() {
    return cameraAdvancedSettings;
}

inline const CameraAdvancedSettings &GetCameraAdvancedSettingsConst() {
    return cameraAdvancedSettings;
}

inline void SetBgmVolume(const SceneContext &ctx, float volume) {
    bgmVolume = std::clamp(volume, 0.0f, 1.0f);
    if (ctx.systems.sound != nullptr &&
        menuBgmVoiceHandle != SoundManager::kInvalidVoiceHandle) {
        ctx.systems.sound->SetVoiceVolume(menuBgmVoiceHandle,
                                          kMenuBgmBaseVolume * bgmVolume);
    }
}

inline void SetSeVolume(float volume) {
    seVolume = std::clamp(volume, 0.0f, 1.0f);
}

inline void SetCameraSensitivity(float sensitivity) {
    cameraSensitivity = std::clamp(sensitivity, 0.0f, 1.0f);
    cameraHandSensitivity = {cameraSensitivity, cameraSensitivity};
}

inline void SetCameraSensitivity(size_t handIndex, float sensitivity) {
    cameraHandSensitivity[ClampCameraHandIndex(handIndex)] =
        std::clamp(sensitivity, 0.0f, 1.0f);
    cameraSensitivity =
        (cameraHandSensitivity[0] + cameraHandSensitivity[1]) * 0.5f;
}

inline void SetCameraSlashSensitivity(float sensitivity) {
    cameraSlashSensitivity = std::clamp(sensitivity, 0.0f, 1.0f);
    cameraHandSlashSensitivity = {cameraSlashSensitivity,
                                  cameraSlashSensitivity};
}

inline void SetCameraSlashSensitivity(size_t handIndex, float sensitivity) {
    cameraHandSlashSensitivity[ClampCameraHandIndex(handIndex)] =
        std::clamp(sensitivity, 0.0f, 1.0f);
    cameraSlashSensitivity =
        (cameraHandSlashSensitivity[0] + cameraHandSlashSensitivity[1]) * 0.5f;
}

inline void SetCameraVerticalSensitivity(float sensitivity) {
    cameraVerticalSensitivity = std::clamp(sensitivity, 0.0f, 1.0f);
    cameraHandVerticalSensitivity = {cameraVerticalSensitivity,
                                     cameraVerticalSensitivity};
}

inline void SetCameraVerticalSensitivity(size_t handIndex, float sensitivity) {
    cameraHandVerticalSensitivity[ClampCameraHandIndex(handIndex)] =
        std::clamp(sensitivity, 0.0f, 1.0f);
    cameraVerticalSensitivity =
        (cameraHandVerticalSensitivity[0] + cameraHandVerticalSensitivity[1]) *
        0.5f;
}

inline void SetCameraHorizontalSensitivity(float sensitivity) {
    cameraHorizontalSensitivity = std::clamp(sensitivity, 0.0f, 1.0f);
    cameraHandHorizontalSensitivity = {cameraHorizontalSensitivity,
                                       cameraHorizontalSensitivity};
}

inline void SetCameraHorizontalSensitivity(size_t handIndex,
                                           float sensitivity) {
    cameraHandHorizontalSensitivity[ClampCameraHandIndex(handIndex)] =
        std::clamp(sensitivity, 0.0f, 1.0f);
    cameraHorizontalSensitivity = (cameraHandHorizontalSensitivity[0] +
                                   cameraHandHorizontalSensitivity[1]) *
                                  0.5f;
}

inline void SetMouseSlashSensitivity(float sensitivity) {
    mouseSlashSensitivity = std::clamp(sensitivity, 0.0f, 1.0f);
}

inline void StartMenuBgm(const SceneContext &ctx) {
    if (ctx.systems.sound == nullptr ||
        menuBgmVoiceHandle != SoundManager::kInvalidVoiceHandle) {
        return;
    }

    menuBgmSoundId = ctx.systems.sound->LoadOrCreateSilent(kMenuBgmPath);
    menuBgmVoiceHandle = ctx.systems.sound->Play(
        menuBgmSoundId, kMenuBgmBaseVolume * bgmVolume, true);
}

inline void StopMenuBgm(const SceneContext *ctx) {
    if (ctx == nullptr || ctx->systems.sound == nullptr ||
        menuBgmVoiceHandle == SoundManager::kInvalidVoiceHandle) {
        return;
    }

    ctx->systems.sound->Stop(menuBgmVoiceHandle);
    menuBgmVoiceHandle = SoundManager::kInvalidVoiceHandle;
}

inline void PlayMenuSe(const SceneContext &ctx, MenuSe se,
                       float volume = 0.95f) {
    if (ctx.systems.sound == nullptr) {
        return;
    }

    uint32_t *soundId = nullptr;
    const wchar_t *path = nullptr;
    switch (se) {
    case MenuSe::Select:
        soundId = &selectSeSoundId;
        path = kSelectSePath;
        break;
    case MenuSe::Selected:
        soundId = &selectedSeSoundId;
        path = kSelectedSePath;
        break;
    case MenuSe::Cancel:
        soundId = &cancelSeSoundId;
        path = kCancelSePath;
        break;
    }

    if (soundId == nullptr || path == nullptr) {
        return;
    }
    if (*soundId == SoundManager::kInvalidSoundId) {
        *soundId = ctx.systems.sound->LoadOrCreateSilent(path);
    }
    ctx.systems.sound->Play(*soundId, volume * seVolume, false);
}
} // namespace AppSceneServices
