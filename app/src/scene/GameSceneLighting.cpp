#include "GameScene.h"
#include "ModelManager.h"
#include <cmath>

using namespace DirectX;

void GameScene::UpdateSceneLighting() {
    if (ctx_ == nullptr || ctx_->model == nullptr) {
        return;
    }

    const XMFLOAT3 &playerPos = player_.GetTransform().position;
    const XMFLOAT3 &enemyPos = enemy_.GetTransform().position;
    XMFLOAT3 accentAnchor = enemy_.GetTransform().position;
    if (enemy_.GetActionKind() == ActionKind::Warp) {
        accentAnchor = enemy_.GetWarpTargetPos();
    }

    const float pulse = 0.82f + 0.18f * std::sinf(sceneLightTime_ * 2.4f);
    const float colorPulse = 0.5f + 0.5f * std::sinf(sceneLightTime_ * 3.1f);
    const float actionBoost =
        enemy_.GetActionKind() == ActionKind::Warp   ? 1.35f
        : enemy_.GetActionKind() == ActionKind::Wave ? 1.20f
        : enemy_.GetActionKind() == ActionKind::Shot ? 1.10f
                                                     : 1.0f;
    XMFLOAT4 actionColor = {0.28f, 0.86f, 1.0f, 1.0f};
    switch (enemy_.GetActionKind()) {
    case ActionKind::Smash:
        actionColor = {1.0f, 0.16f, 0.20f, 1.0f};
        break;
    case ActionKind::Sweep:
        actionColor = {1.0f, 0.84f, 0.18f, 1.0f};
        break;
    case ActionKind::Shot:
        actionColor = {0.24f, 0.58f, 1.0f, 1.0f};
        break;
    case ActionKind::Wave:
        actionColor = {0.16f, 1.0f, 0.58f, 1.0f};
        break;
    case ActionKind::Warp:
        actionColor = {0.78f, 0.18f, 1.0f, 1.0f};
        break;
    case ActionKind::Stalk:
        actionColor = {0.24f, 1.0f, 0.86f, 1.0f};
        break;
    default:
        break;
    }

    XMFLOAT3 duelCenter = {
        (playerPos.x + enemyPos.x) * 0.5f,
        (playerPos.y + enemyPos.y) * 0.5f,
        (playerPos.z + enemyPos.z) * 0.5f,
    };

    SceneLighting lighting{};
    lighting.keyLightDirection = {-0.52f, -1.0f, 0.22f};
    lighting.keyLightColor = {
        1.10f + actionColor.x * 0.32f,
        1.02f + actionColor.y * 0.28f,
        1.00f + actionColor.z * 0.34f,
        1.0f,
    };
    lighting.fillLightDirection = {0.72f, -0.24f, -0.58f};
    lighting.fillLightColor = {
        0.34f + (1.0f - actionColor.x) * 0.34f,
        0.34f + (1.0f - actionColor.y) * 0.30f,
        0.42f + (1.0f - actionColor.z) * 0.34f,
        0.56f,
    };
    lighting.ambientColor = {
        0.30f + actionColor.x * 0.12f,
        0.30f + actionColor.y * 0.12f,
        0.34f + actionColor.z * 0.14f,
        1.0f,
    };
    lighting.lightingParams = {132.0f, 0.44f, 1.18f, 0.06f};

    lighting.pointLights[0].positionRange = {
        duelCenter.x,
        duelCenter.y + 2.4f,
        duelCenter.z - 0.7f,
        8.5f,
    };
    lighting.pointLights[0].colorIntensity = {
        0.32f + actionColor.x * 0.90f,
        0.32f + actionColor.y * 0.90f,
        0.36f + actionColor.z * 0.92f,
        0.88f * pulse,
    };

    lighting.pointLights[1].positionRange = {
        accentAnchor.x,
        enemyPos.y + 1.6f,
        accentAnchor.z + 0.35f,
        6.8f,
    };
    lighting.pointLights[1].colorIntensity = {
        1.0f - actionColor.x * 0.45f + 0.20f * colorPulse,
        1.0f - actionColor.y * 0.35f,
        1.0f - actionColor.z * 0.45f + 0.16f * (1.0f - colorPulse),
        0.62f * actionBoost,
    };

    ctx_->model->SetSceneLighting(lighting);
}
