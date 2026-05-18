#include "GameScene.h"
#include "ModelManager.h"
#include <algorithm>
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
        actionKind == ActionKind::Shot ||
        actionKind == ActionKind::BladeClash ||
        actionKind == ActionKind::Wave ||
        actionKind == ActionKind::Cage || actionKind == ActionKind::Nova;
    const bool effectFocus =
        (isAttackKind &&
         (actionStep == ActionStep::Charge || actionStep == ActionStep::Active)) ||
        enemy_.IsNovaImpactWindow() || enemy_.IsPhaseTransitionActive();
    XMFLOAT3 accentAnchor = enemy_.GetTransform().position;
    if (actionKind == ActionKind::Warp) {
        accentAnchor = enemy_.GetWarpTargetPos();
    }

    const float pulse = 0.96f + 0.04f * std::sinf(sceneLightTime_ * 2.4f);
    const float actionBoost =
        actionKind == ActionKind::Nova   ? 1.22f
        : actionKind == ActionKind::Warp ? 1.12f
        : actionKind == ActionKind::Cage ? 1.11f
        : actionKind == ActionKind::Wave ? 1.08f
        : actionKind == ActionKind::BladeClash ? 1.14f
        : actionKind == ActionKind::Shot ? 1.18f
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
        actionColor = {0.42f, 0.92f, 1.0f, 1.0f};
        break;
    case ActionKind::BladeClash:
        actionColor = {0.42f, 1.0f, 0.66f, 1.0f};
        break;
    case ActionKind::Wave:
        actionColor = {0.56f, 0.82f, 0.48f, 1.0f};
        break;
    case ActionKind::Cage:
        actionColor = {0.40f, 0.86f, 0.88f, 1.0f};
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
    if (enemy_.IsPhaseTransitionActive() && !bladeClashFinishActive_) {
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

    if (bladeClashFinishActive_ && bladeClashFinishPlayerWon_) {
        const float ratio =
            bladeClashFinishDuration_ > 0.0001f
                ? std::clamp(bladeClashFinishTimer_ / bladeClashFinishDuration_,
                             0.0f, 1.0f)
                : 1.0f;
        const float impact =
            1.0f - std::clamp((ratio - 0.58f) / 0.42f, 0.0f, 1.0f);
        lighting.keyLightDirection = {-0.40f, -0.54f, 0.62f};
        lighting.keyLightColor = {1.48f, 1.42f, 1.26f, 1.0f};
        lighting.fillLightDirection = {0.66f, -0.16f, -0.72f};
        lighting.fillLightColor = {0.66f, 0.68f, 0.74f, 0.60f};
        lighting.ambientColor = {0.36f, 0.37f, 0.40f, 1.0f};
        lighting.lightingParams = {86.0f, 0.52f, 1.16f, 0.18f};
        lighting.pointLights[0].positionRange = {
            playerPos.x - bladeClashDirection_.x * 1.05f,
            playerPos.y + 1.45f,
            playerPos.z - bladeClashDirection_.z * 1.05f,
            8.20f,
        };
        lighting.pointLights[0].colorIntensity = {
            1.0f,
            0.92f,
            0.72f,
            1.75f + 0.55f * impact,
        };
        lighting.pointLights[1].positionRange = {
            enemyPos.x + bladeClashDirection_.z * 1.15f,
            enemyPos.y + 1.75f,
            enemyPos.z - bladeClashDirection_.x * 1.15f,
            6.80f,
        };
        lighting.pointLights[1].colorIntensity = {
            1.0f,
            0.90f,
            0.70f,
            1.25f,
        };
    }

    ctx_->model->SetSceneLighting(lighting);
}
