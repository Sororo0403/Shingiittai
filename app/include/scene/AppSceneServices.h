#pragma once
#include <functional>

namespace AppSceneServices {
using VoidCallback = std::function<void()>;
using BoolCallback = std::function<bool()>;

inline VoidCallback requestHandTrackingStart;
inline BoolCallback isCameraDeviceAvailable;
inline BoolCallback isHandTrackingReady;

inline void ConfigureHandTracking(VoidCallback start, BoolCallback cameraAvailable,
                                  BoolCallback handReady) {
    requestHandTrackingStart = std::move(start);
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
