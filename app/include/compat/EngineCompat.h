#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "camera/Camera.h"
#include "model/ModelDrawEffect.h"
#include <DirectXMath.h>
#include <cmath>

// 旧ゲームコードの呼び出しを現行エンジンAPIへ接続する移行用エイリアス。
// 呼び出し側を新APIへ置換できた時点で、このマクロ群は削除できる。
#define SetEmission(...) GetEmitterSettings()
#define SetEmitterRadius(...) GetEmitterSettings()

#define CreateRustedMetalTexture(...) GetWhiteTextureId()
#define CreateArenaStoneTexture(...) GetWhiteTextureId()

#define enableDissolve customParams.x
#define dissolveThreshold customParams.y
#define dissolveEdgeWidth customParams.z
#define dissolveEdgeColor customParams3

/// <summary>
/// カメラを指定したワールド座標へ向け、行列を更新する
/// </summary>
inline void AppLookAt(Camera &camera, const DirectX::XMFLOAT3 &target) {
    const DirectX::XMFLOAT3 &position = camera.GetPosition();
    const float dx = target.x - position.x;
    const float dy = target.y - position.y;
    const float dz = target.z - position.z;
    const float yaw = std::atan2f(dx, dz);
    const float horizontal = std::sqrt(dx * dx + dz * dz);
    const float pitch = -std::atan2f(dy, horizontal);
    camera.SetRotation({pitch, yaw, 0.0f});
    camera.UpdateMatrices();
}

/// <summary>
/// カメラ回転から正規化済みの前方向ベクトルを求める
/// </summary>
inline DirectX::XMFLOAT3 AppCameraForward(const Camera &camera) {
    const DirectX::XMFLOAT3 &rotation = camera.GetRotation();
    const float cp = std::cos(rotation.x);
    return {std::sin(rotation.y) * cp, -std::sin(rotation.x),
            std::cos(rotation.y) * cp};
}

/// <summary>
/// アプリ側の演出指定をGPUパーティクル設定へ変換するための分類
/// </summary>
enum class AppParticleBurstStyle {
    Explosion,
    Sparks,
    Flash,
    Smoke,
    SlashLine,
    SpiritSparkle,
};
