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

    const float pulse = 0.82f + 0.18f * std::sinf(sceneLightTime_ * 2.4f);
    const float colorPulse = 0.5f + 0.5f * std::sinf(sceneLightTime_ * 3.1f);
    const float actionBoost =
        actionKind == ActionKind::Nova   ? 1.34f
        : actionKind == ActionKind::Warp ? 1.14f
        : actionKind == ActionKind::Wave ? 1.10f
        : actionKind == ActionKind::Shot ? 1.06f
                                         : 1.0f;
    const float actorDim = effectFocus ? 0.42f : 1.0f;
    const float ambientDim = effectFocus ? 0.36f : 1.0f;
    const float pointDim = effectFocus ? 0.48f : 1.0f;
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

    XMFLOAT3 duelCenter = {
        (playerPos.x + enemyPos.x) * 0.5f,
        (playerPos.y + enemyPos.y) * 0.5f,
        (playerPos.z + enemyPos.z) * 0.5f,
    };

    SceneLighting lighting{};
    lighting.keyLightDirection = {-0.58f, -1.0f, 0.18f};
    lighting.keyLightColor = {
        (1.04f + actionColor.x * 0.20f) * actorDim,
        (0.92f + actionColor.y * 0.16f) * actorDim,
        (0.78f + actionColor.z * 0.12f) * actorDim,
        1.0f,
    };
    lighting.fillLightDirection = {0.72f, -0.24f, -0.58f};
    lighting.fillLightColor = {
        (0.36f + actionColor.x * 0.08f) * actorDim,
        (0.40f + actionColor.y * 0.10f) * actorDim,
        (0.42f + actionColor.z * 0.12f) * actorDim,
        0.64f * actorDim,
    };
    lighting.ambientColor = {
        (0.30f + actionColor.x * 0.035f) * ambientDim,
        (0.28f + actionColor.y * 0.035f) * ambientDim,
        (0.25f + actionColor.z * 0.035f) * ambientDim,
        1.0f,
    };
    lighting.lightingParams = {
        72.0f,
        effectFocus ? 0.10f : 0.30f,
        effectFocus ? 0.42f : 0.88f,
        effectFocus ? 0.00f : 0.04f,
    };

    lighting.pointLights[0].positionRange = {
        duelCenter.x,
        duelCenter.y + 2.4f,
        duelCenter.z - 0.7f,
        8.5f,
    };
    lighting.pointLights[0].colorIntensity = {
        0.74f + actionColor.x * 0.36f,
        0.50f + actionColor.y * 0.24f,
        0.34f + actionColor.z * 0.18f,
        0.82f * pulse * pointDim,
    };

    lighting.pointLights[1].positionRange = {
        accentAnchor.x,
        enemyPos.y + 1.6f,
        accentAnchor.z + 0.35f,
        6.8f,
    };
    lighting.pointLights[1].colorIntensity = {
        0.38f + actionColor.x * 0.16f + 0.04f * colorPulse,
        0.46f + actionColor.y * 0.18f,
        0.42f + actionColor.z * 0.18f + 0.04f * (1.0f - colorPulse),
        0.54f * actionBoost * pointDim,
    };

    ctx_->model->SetSceneLighting(lighting);
}
