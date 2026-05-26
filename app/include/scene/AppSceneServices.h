#pragma once
#include "SceneContext.h"
#include "SoundManager.h"
#include <cstdint>
#include <functional>

namespace AppSceneServices {
using StartCallback = std::function<bool()>;
using BoolCallback = std::function<bool()>;
using VoidCallback = std::function<void()>;

inline constexpr const wchar_t *kMenuBgmPath =
    L"app/resources/audio/bgm/bgm_MenuTheme.wav";

inline StartCallback requestHandTrackingStart;
inline VoidCallback requestHandTrackingStop;
inline BoolCallback isCameraDeviceAvailable;
inline BoolCallback isHandTrackingReady;
inline uint32_t menuBgmSoundId = SoundManager::kInvalidSoundId;
inline uint32_t menuBgmVoiceHandle = SoundManager::kInvalidVoiceHandle;

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

inline void StartMenuBgm(const SceneContext &ctx) {
    if (ctx.systems.sound == nullptr ||
        menuBgmVoiceHandle != SoundManager::kInvalidVoiceHandle) {
        return;
    }

    menuBgmSoundId = ctx.systems.sound->LoadOrCreateSilent(kMenuBgmPath);
    menuBgmVoiceHandle = ctx.systems.sound->Play(menuBgmSoundId, 0.36f, true);
}

inline void StopMenuBgm(const SceneContext *ctx) {
    if (ctx == nullptr || ctx->systems.sound == nullptr ||
        menuBgmVoiceHandle == SoundManager::kInvalidVoiceHandle) {
        return;
    }

    ctx->systems.sound->Stop(menuBgmVoiceHandle);
    menuBgmVoiceHandle = SoundManager::kInvalidVoiceHandle;
}
}
