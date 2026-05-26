#pragma once
#include <functional>

namespace AppSceneServices {
using StartCallback = std::function<bool()>;
using BoolCallback = std::function<bool()>;
using VoidCallback = std::function<void()>;

inline StartCallback requestHandTrackingStart;
inline VoidCallback requestHandTrackingStop;
inline BoolCallback isCameraDeviceAvailable;
inline BoolCallback isHandTrackingReady;

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
}
