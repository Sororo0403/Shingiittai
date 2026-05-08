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
        enemy_.GetActionKind() == ActionKind::Nova   ? 1.80f
        : enemy_.GetActionKind() == ActionKind::Warp ? 1.35f
        : enemy_.GetActionKind() == ActionKind::Wave ? 1.20f
        : enemy_.GetActionKind() == ActionKind::Shot ? 1.10f
                                                     : 1.0f;
    XMFLOAT4 actionColor = {0.86f, 0.44f, 0.18f, 1.0f};
    switch (enemy_.GetActionKind()) {
    case ActionKind::Smash:
        actionColor = {1.0f, 0.24f, 0.08f, 1.0f};
        break;
    case ActionKind::Sweep:
        actionColor = {1.0f, 0.58f, 0.14f, 1.0f};
        break;
    case ActionKind::Shot:
        actionColor = {0.68f, 0.78f, 0.84f, 1.0f};
        break;
    case ActionKind::Wave:
        actionColor = {0.56f, 0.82f, 0.48f, 1.0f};
        break;
    case ActionKind::Nova:
        actionColor = {1.0f, 0.22f, 0.04f, 1.0f};
        break;
    case ActionKind::Warp:
        actionColor = {0.46f, 0.78f, 0.66f, 1.0f};
        break;
    case ActionKind::Stalk:
        actionColor = {0.58f, 0.62f, 0.48f, 1.0f};
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
    lighting.keyLightDirection = {-0.58f, -1.0f, 0.18f};
    lighting.keyLightColor = {
        0.92f + actionColor.x * 0.42f,
        0.72f + actionColor.y * 0.30f,
        0.52f + actionColor.z * 0.22f,
        1.0f,
    };
    lighting.fillLightDirection = {0.72f, -0.24f, -0.58f};
    lighting.fillLightColor = {
        0.22f + actionColor.x * 0.12f,
        0.25f + actionColor.y * 0.18f,
        0.24f + actionColor.z * 0.22f,
        0.50f,
    };
    lighting.ambientColor = {
        0.20f + actionColor.x * 0.08f,
        0.17f + actionColor.y * 0.07f,
        0.14f + actionColor.z * 0.06f,
        1.0f,
    };
    lighting.lightingParams = {96.0f, 0.36f, 1.10f, 0.08f};

    lighting.pointLights[0].positionRange = {
        duelCenter.x,
        duelCenter.y + 2.4f,
        duelCenter.z - 0.7f,
        8.5f,
    };
    lighting.pointLights[0].colorIntensity = {
        0.70f + actionColor.x * 0.72f,
        0.28f + actionColor.y * 0.42f,
        0.10f + actionColor.z * 0.22f,
        1.05f * pulse,
    };

    lighting.pointLights[1].positionRange = {
        accentAnchor.x,
        enemyPos.y + 1.6f,
        accentAnchor.z + 0.35f,
        6.8f,
    };
    lighting.pointLights[1].colorIntensity = {
        0.42f + actionColor.x * 0.26f + 0.08f * colorPulse,
        0.58f + actionColor.y * 0.32f,
        0.46f + actionColor.z * 0.28f + 0.08f * (1.0f - colorPulse),
        0.74f * actionBoost,
    };

    ctx_->model->SetSceneLighting(lighting);
}
