#pragma once
#include <functional>

namespace AppSceneServices {
using VoidCallback = std::function<void()>;
using BoolCallback = std::function<bool()>;

inline VoidCallback requestHandTrackingStart;
inline VoidCallback requestHandTrackingRestart;
inline BoolCallback isCameraDeviceAvailable;
inline BoolCallback isHandTrackingReady;

inline void ConfigureHandTracking(VoidCallback start, VoidCallback restart,
                                  BoolCallback cameraAvailable,
                                  BoolCallback handReady) {
    requestHandTrackingStart = std::move(start);
    requestHandTrackingRestart = std::move(restart);
    isCameraDeviceAvailable = std::move(cameraAvailable);
    isHandTrackingReady = std::move(handReady);
}

inline bool HasHandTrackingStart() {
    return static_cast<bool>(requestHandTrackingStart);
}

inline void RequestHandTrackingStart() {
    if (requestHandTrackingStart) {
        requestHandTrackingStart();
    }
}

inline void RequestHandTrackingRestart() {
    if (requestHandTrackingRestart) {
        requestHandTrackingRestart();
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
}
