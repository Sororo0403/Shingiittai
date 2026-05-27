#include "GameScene.h"
#include "ModelManager.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;

void GameScene::UpdateSceneLighting() {
    if (ctx_ == nullptr || ctx_->rendering.model == nullptr) {
        return;
    }

    const XMFLOAT3 &playerPos = player_.GetTransform().position;
    const XMFLOAT3 &enemyPos = enemy_.GetTransform().position;
    const ActionKind actionKind = enemy_.GetActionKind();
    const ActionStep actionStep = enemy_.GetActionStep();
    const bool isAttackKind =
        actionKind == ActionKind::Smash || actionKind == ActionKind::Sweep ||
        actionKind == ActionKind::BladeClash;
    const bool effectFocus =
        (isAttackKind &&
         (actionStep == ActionStep::Charge || actionStep == ActionStep::Active)) ||
        enemy_.IsPhaseTransitionActive();
    XMFLOAT3 accentAnchor = enemy_.GetTransform().position;

    const float pulse = 0.96f + 0.04f * std::sinf(sceneLightTime_ * 2.4f);
    const float actionBoost = 1.0f;
    const float enemyFocusBoost = effectFocus ? 1.18f : 1.0f;
    XMFLOAT4 actionColor = {0.86f, 0.44f, 0.18f, 1.0f};
    switch (actionKind) {
    case ActionKind::Smash:
        actionColor = {1.0f, 0.24f, 0.08f, 1.0f};
        break;
    case ActionKind::Sweep:
        actionColor = {1.0f, 0.58f, 0.14f, 1.0f};
        break;
    case ActionKind::BladeClash:
        actionColor = {1.0f, 0.72f, 0.20f, 1.0f};
        break;
    case ActionKind::Stalk:
        actionColor = {0.58f, 0.62f, 0.48f, 1.0f};
        break;
    default:
        break;
    }
    if (enemy_.IsPhaseTransitionActive()) {
        const float ratio = enemy_.GetPhaseTransitionRatio();
        const float release =
            std::clamp((ratio - 0.88f) / 0.05f, 0.0f, 1.0f);
        actionColor = {1.0f, 0.24f + 0.30f * release, 0.05f, 1.0f};
    }

    SceneLighting lighting{};
    lighting.keyLightDirection = {-0.62f, -0.58f, 0.36f};
    lighting.keyLightColor = {
        1.30f + actionColor.x * 0.05f,
        1.22f + actionColor.y * 0.04f,
        1.10f + actionColor.z * 0.03f,
        1.0f,
    };
    lighting.fillLightDirection = {0.72f, -0.24f, -0.56f};
    lighting.fillLightColor = {
        0.58f + actionColor.x * 0.03f,
        0.60f + actionColor.y * 0.02f,
        0.66f + actionColor.z * 0.02f,
        0.58f,
    };
    lighting.ambientColor = {
        0.39f + actionColor.x * 0.008f,
        0.40f + actionColor.y * 0.008f,
        0.44f + actionColor.z * 0.008f,
        1.0f,
    };
    lighting.lightingParams = {
        52.0f,
        0.30f,
        1.34f,
        0.17f,
    };

    lighting.pointLights[0].positionRange = {
        playerPos.x - 2.00f,
        playerPos.y + 1.55f,
        playerPos.z - 1.80f,
        5.40f,
    };
    lighting.pointLights[0].colorIntensity = {
        1.0f,
        0.74f,
        0.42f,
        0.86f * pulse,
    };

    lighting.pointLights[1].positionRange = {
        accentAnchor.x + 2.20f,
        enemyPos.y + 1.75f,
        accentAnchor.z + 1.20f,
        5.00f,
    };
    lighting.pointLights[1].colorIntensity = {
        1.0f + actionColor.x * 0.04f,
        0.82f + actionColor.y * 0.03f,
        0.52f + actionColor.z * 0.02f,
        0.94f * actionBoost * enemyFocusBoost,
    };

    ctx_->rendering.model->SetSceneLighting(lighting);
}
