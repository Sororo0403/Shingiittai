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
    const ActionKind actionKind = enemy_.GetActionKind();
    const ActionStep actionStep = enemy_.GetActionStep();
    const bool isAttackKind =
        actionKind == ActionKind::Smash || actionKind == ActionKind::Sweep ||
        actionKind == ActionKind::Shot || actionKind == ActionKind::Wave ||
        actionKind == ActionKind::Nova;
    const bool effectFocus =
        (isAttackKind &&
         (actionStep == ActionStep::Charge || actionStep == ActionStep::Active)) ||
        enemy_.IsNovaImpactWindow();
    XMFLOAT3 accentAnchor = enemy_.GetTransform().position;
    if (actionKind == ActionKind::Warp) {
        accentAnchor = enemy_.GetWarpTargetPos();
    }

    const float pulse = 0.96f + 0.04f * std::sinf(sceneLightTime_ * 2.4f);
    const float colorPulse = 0.5f + 0.5f * std::sinf(sceneLightTime_ * 3.1f);
    const float actionBoost =
        actionKind == ActionKind::Nova   ? 1.22f
        : actionKind == ActionKind::Warp ? 1.12f
        : actionKind == ActionKind::Wave ? 1.08f
        : actionKind == ActionKind::Shot ? 1.06f
                                         : 1.0f;
    const float enemyFocusBoost = effectFocus ? 1.18f : 1.0f;
    XMFLOAT4 actionColor = {0.86f, 0.44f, 0.18f, 1.0f};
    switch (actionKind) {
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

    SceneLighting lighting{};
    lighting.keyLightDirection = {-0.50f, -1.0f, 0.22f};
    lighting.keyLightColor = {
        0.92f + actionColor.x * 0.08f,
        0.84f + actionColor.y * 0.06f,
        0.72f + actionColor.z * 0.04f,
        1.0f,
    };
    lighting.fillLightDirection = {0.72f, -0.24f, -0.56f};
    lighting.fillLightColor = {
        0.28f + actionColor.x * 0.04f,
        0.34f + actionColor.y * 0.05f,
        0.40f + actionColor.z * 0.06f,
        0.56f,
    };
    lighting.ambientColor = {
        0.24f + actionColor.x * 0.018f,
        0.23f + actionColor.y * 0.018f,
        0.21f + actionColor.z * 0.018f,
        1.0f,
    };
    lighting.lightingParams = {
        58.0f,
        0.34f,
        1.7f,
        0.18f,
    };

    lighting.pointLights[0].positionRange = {
        playerPos.x,
        playerPos.y + 3.35f,
        playerPos.z - 0.10f,
        6.40f,
    };
    lighting.pointLights[0].colorIntensity = {
        0.66f,
        0.82f,
        1.0f,
        1.20f * pulse,
    };

    lighting.pointLights[1].positionRange = {
        accentAnchor.x,
        enemyPos.y + 3.55f,
        accentAnchor.z + 0.05f,
        6.30f,
    };
    lighting.pointLights[1].colorIntensity = {
        2.20f + actionColor.x * 0.48f + 0.04f * colorPulse,
        1.55f + actionColor.y * 0.24f,
        1.05f + actionColor.z * 0.16f + 0.04f * (1.0f - colorPulse),
        5.70f * actionBoost * enemyFocusBoost,
    };

    ctx_->model->SetSceneLighting(lighting);
}
