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
    const float actionBoost =
        enemy_.GetActionKind() == ActionKind::Warp   ? 1.35f
        : enemy_.GetActionKind() == ActionKind::Wave ? 1.20f
        : enemy_.GetActionKind() == ActionKind::Shot ? 1.10f
                                                     : 1.0f;

    XMFLOAT3 duelCenter = {
        (playerPos.x + enemyPos.x) * 0.5f,
        (playerPos.y + enemyPos.y) * 0.5f,
        (playerPos.z + enemyPos.z) * 0.5f,
    };

    SceneLighting lighting{};
    lighting.keyLightDirection = {-0.52f, -1.0f, 0.22f};
    lighting.keyLightColor = {1.32f, 1.32f, 1.28f, 1.0f};
    lighting.fillLightDirection = {0.72f, -0.24f, -0.58f};
    lighting.fillLightColor = {0.88f, 0.88f, 0.86f, 0.42f};
    lighting.ambientColor = {0.42f, 0.42f, 0.40f, 1.0f};
    lighting.lightingParams = {78.0f, 0.20f, 1.8f, 0.04f};

    lighting.pointLights[0].positionRange = {
        duelCenter.x,
        duelCenter.y + 2.4f,
        duelCenter.z - 0.7f,
        8.5f,
    };
    lighting.pointLights[0].colorIntensity = {
        0.96f,
        0.96f,
        0.92f,
        0.70f * pulse,
    };

    lighting.pointLights[1].positionRange = {
        accentAnchor.x,
        enemyPos.y + 1.6f,
        accentAnchor.z + 0.35f,
        6.8f,
    };
    lighting.pointLights[1].colorIntensity = {
        0.72f,
        0.72f,
        0.70f,
        0.45f * actionBoost,
    };

    ctx_->model->SetSceneLighting(lighting);
}
