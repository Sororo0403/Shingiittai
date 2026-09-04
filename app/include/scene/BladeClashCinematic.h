#pragma once

#include <DirectXMath.h>
#include <algorithm>
#include <cmath>

/// <summary>
/// 鍔迫り合い決着時の時間変換と敗北姿勢を計算する
/// </summary>
namespace BladeClashCinematic {

constexpr float kPi = 3.14159265f;
constexpr float kWinGuardBreakLead = 0.58f;
constexpr float kWinGuardBreakImpactTime = 0.28f;
constexpr float kWinActionSlow = 0.64f;
constexpr float kGuardBreakSlowStart = 0.21f;
constexpr float kGuardBreakSlowEnd = 0.36f;
constexpr float kGuardBreakSlowScale = 0.18f;
constexpr float kGuardBreakRecoilDistance = 0.30f;
constexpr float kGuardBreakDrop = 0.26f;
constexpr float kGuardBreakLift = 0.16f;
constexpr float kGuardBreakPose = 0.74f;
constexpr float kWinGuardBreakCameraMoveStart = 0.42f;
constexpr float kWinGuardBreakCameraMoveEnd = 0.52f;
constexpr float kLossHitTime = 0.78f;
constexpr float kLossSlideStartTime = 1.12f;
constexpr float kLossWallImpactTime = 1.58f;
constexpr float kLossTotalRetreat = 15.80f;

/// <summary>
/// 鍔迫り合い敗北演出中の位置と各アニメーション進捗を保持する
/// </summary>
struct LossPose {
    DirectX::XMFLOAT3 position = {0.0f, 0.0f, 0.0f};
    float leanT = 0.0f;
    float recoilT = 0.0f;
    float slideT = 0.0f;
    float leanEase = 0.0f;
    float recoilEase = 0.0f;
    float slideEase = 0.0f;
};

inline float Clamp01(float value) { return std::clamp(value, 0.0f, 1.0f); }

inline float Smooth01(float value) {
    const float t = Clamp01(value);
    return t * t * (3.0f - 2.0f * t);
}

inline float WinActionTimer(float finishTimer) {
    return (std::max)(0.0f, finishTimer - kWinGuardBreakLead) / kWinActionSlow;
}

inline float GuardBreakTimeScale(float finishTimer) {
    if (finishTimer < kGuardBreakSlowStart ||
        finishTimer >= kGuardBreakSlowEnd) {
        return 1.0f;
    }
    const float slowT = Clamp01((finishTimer - kGuardBreakSlowStart) /
                                (kGuardBreakSlowEnd - kGuardBreakSlowStart));
    const float snapHold = std::sinf(slowT * kPi);
    return 1.0f - (1.0f - kGuardBreakSlowScale) * snapHold;
}

inline LossPose EvaluateLossPose(float finishTimer,
                                 const DirectX::XMFLOAT3 &start,
                                 const DirectX::XMFLOAT3 &direction) {
    LossPose pose{};
    pose.leanT = Clamp01(finishTimer / kLossHitTime);
    pose.recoilT = Clamp01((finishTimer - kLossHitTime) / 0.36f);
    pose.slideT = Clamp01((finishTimer - kLossSlideStartTime) / 0.52f);
    pose.leanEase = Smooth01(pose.leanT);
    pose.recoilEase = 1.0f - std::pow(1.0f - pose.recoilT, 3.0f);

    const float settleT = Clamp01((pose.slideT - 0.74f) / 0.26f);
    const float settleEase = Smooth01(settleT);
    pose.slideEase =
        pose.slideT < 0.18f
            ? 0.06f * std::pow(Clamp01(pose.slideT / 0.18f), 2.0f)
        : pose.slideT < 0.74f
            ? 0.06f +
                  0.88f *
                      (1.0f -
                       std::pow(1.0f - Clamp01((pose.slideT - 0.18f) / 0.56f),
                                5.0f))
            : 0.94f + 0.06f * settleEase;

    const float retreat = 0.24f * pose.leanEase + 0.56f * pose.recoilEase +
                          (kLossTotalRetreat - 0.80f) * pose.slideEase;
    pose.position = {start.x - direction.x * retreat, start.y,
                     start.z - direction.z * retreat};
    pose.position.y += std::sinf(pose.recoilT * kPi) * 0.16f;
    pose.position.y += std::sinf(pose.slideT * kPi) * 0.92f;
    return pose;
}

} // namespace BladeClashCinematic
