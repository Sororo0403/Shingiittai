#include "GameScene.h"
#include "DirectXCommon.h"
#include "EnemyAnimationDebugScene.h"
#include "Input.h"
#include "Material.h"
#include "Model.h"
#include "ModelManager.h"
#include "SpriteManager.h"
#include "TextureManager.h"
#include "WinApp.h"
#include "PostEffectRenderer.h"
#include "SceneManager.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <vector>

using namespace DirectX;

namespace {
constexpr float kPi = 3.14159265f;

Material MakeArenaMaterial(const XMFLOAT4 &color, bool useTexture = false,
                           float reflection = 0.34f,
                           float roughness = 0.48f) {
    Material material{};
    material.color = color;
    material.enableTexture = useTexture ? 1 : 0;
    material.reflectionStrength = reflection;
    material.reflectionFresnelStrength = reflection * 0.58f;
    material.reflectionRoughness = roughness;
    return material;
}

XMFLOAT4 LerpColor(const XMFLOAT4 &from, const XMFLOAT4 &to, float t) {
    return {
        from.x + (to.x - from.x) * t,
        from.y + (to.y - from.y) * t,
        from.z + (to.z - from.z) * t,
        from.w + (to.w - from.w) * t,
    };
}

XMFLOAT4 MakeQuat(float pitch, float yaw, float roll) {
    XMFLOAT4 q{};
    XMStoreFloat4(&q, XMQuaternionRotationRollPitchYaw(pitch, yaw, roll));
    return q;
}

bool IsEnemyTelegraphKind(ActionKind kind) {
    return kind == ActionKind::Smash || kind == ActionKind::Sweep ||
           kind == ActionKind::Shot || kind == ActionKind::Wave ||
           kind == ActionKind::Nova;
}

bool IsEnemyMeleeKind(ActionKind kind) {
    return kind == ActionKind::Smash || kind == ActionKind::Sweep;
}

bool IsWarpTelegraphStep(ActionKind kind, ActionStep step) {
    return kind == ActionKind::Warp &&
           (step == ActionStep::Start || step == ActionStep::Move ||
            step == ActionStep::End);
}

float TelegraphDuration(ActionKind kind, ActionStep step) {
    if (kind == ActionKind::Warp) {
        return step == ActionStep::End ? 0.35f : 0.55f;
    }
    if (kind == ActionKind::Nova && step == ActionStep::Active) {
        return 0.52f;
    }

    switch (kind) {
    case ActionKind::Smash:
        return 0.85f;
    case ActionKind::Sweep:
        return 1.05f;
    case ActionKind::Shot:
        return 0.95f;
    case ActionKind::Wave:
        return 1.15f;
    case ActionKind::Nova:
        return 2.00f;
    default:
        return 1.0f;
    }
}

void ApplyWeatheredMetalMaterials(ModelManager *modelManager, uint32_t modelId,
                                  uint32_t rustTextureId,
                                  const std::vector<XMFLOAT4> &palette,
                                  float reflection, float fresnel,
                                  float roughness) {
    if (!modelManager || palette.empty()) {
        return;
    }

    Model *model = modelManager->GetModel(modelId);
    if (!model) {
        return;
    }

    size_t colorIndex = 0;
    for (ModelSubMesh &subMesh : model->subMeshes) {
        subMesh.textureId = rustTextureId;

        Material material = modelManager->GetMaterial(subMesh.materialId);
        material.enableTexture = 1;
        material.color = palette[colorIndex % palette.size()];
        material.reflectionStrength = reflection;
        material.reflectionFresnelStrength = fresnel;
        material.reflectionRoughness = roughness;
        material.enableDissolve = 0;
        material.dissolveEdgeColor = {1.0f, 0.44f, 0.14f, 1.0f};
        modelManager->SetMaterial(subMesh.materialId, material);
        ++colorIndex;
    }
}

void ApplyRustedRobotMaterials(ModelManager *modelManager, uint32_t modelId,
                               uint32_t rustTextureId) {
    if (!modelManager) {
        return;
    }

    Model *model = modelManager->GetModel(modelId);
    if (!model) {
        return;
    }

    const std::vector<XMFLOAT4> metalTints = {
        {0.48f, 0.46f, 0.42f, 1.0f},
        {0.32f, 0.30f, 0.28f, 1.0f},
        {0.46f, 0.22f, 0.12f, 1.0f},
        {0.14f, 0.13f, 0.12f, 1.0f},
    };

    size_t materialIndex = 0;
    for (ModelSubMesh &subMesh : model->subMeshes) {
        subMesh.textureId = rustTextureId;

        Material material = modelManager->GetMaterial(subMesh.materialId);
        material.enableTexture = 1;
        material.color = metalTints[materialIndex % metalTints.size()];
        material.reflectionStrength = (materialIndex % 3 == 0) ? 0.34f : 0.22f;
        material.reflectionFresnelStrength =
            (materialIndex % 3 == 0) ? 0.24f : 0.16f;
        material.reflectionRoughness = (materialIndex % 3 == 0) ? 0.50f : 0.64f;
        material.enableDissolve = 0;
        material.dissolveEdgeColor = {1.0f, 0.42f, 0.12f, 1.0f};
        modelManager->SetMaterial(subMesh.materialId, material);
        ++materialIndex;
    }
}

} // namespace

void GameScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    ctx_->postEffectRenderer->SetColorMode(PostEffectRenderer::ColorMode::None);
    ctx_->postEffectRenderer->SetVignettingStrength(0.36f);
    ctx_->postEffectRenderer->SetVignettingEnabled(true);
    combatFeedback_.Initialize(ctx_->postEffectRenderer);

    float aspect = static_cast<float>(ctx_->winApp->GetWidth()) /
                   static_cast<float>(ctx_->winApp->GetHeight());

    camera_.Initialize(aspect);
    camera_.SetMode(CameraMode::LookAt);
    camera_.UpdateMatrices();
    camera_.SetPerspectiveFovDeg(currentFovDeg_);
    collisionDebugRenderer_.Initialize(ctx_->dxCommon);

    DirectXCommon *dx = ctx_->dxCommon;
    ModelManager *model = ctx_->model;
    TextureManager *texture = ctx_->texture;

    dx->BeginUpload();

    uint32_t playerModel =
        model->Load(L"app/resources/models/player/player.glb");
    uint32_t swordModel = model->Load(L"app/resources/models/player/sword.glb");
    uint32_t enemyModel = model->Load(L"app/resources/models/boss/boss.gltf");
    uint32_t bulletModel =
        ctx_->model->Load(L"app/resources/models/bullet/bullet.obj");
    particleTextureId_ = texture->Load(L"app/resources/sprites/smoke.png");
    animeSlashTextureId_ =
        texture->Load(L"app/resources/sprites/anime_slash.png");
    const uint32_t enemyRustTextureId =
        texture->CreateRustedMetalTexture(512, 512);
    const uint32_t worldRustTextureId =
        texture->CreateRustedMetalTexture(768, 768);
    ApplyWeatheredMetalMaterials(model, playerModel, worldRustTextureId,
                                 {{0.46f, 0.44f, 0.40f, 1.0f},
                                  {0.28f, 0.27f, 0.25f, 1.0f},
                                  {0.40f, 0.22f, 0.14f, 1.0f}},
                                 0.24f, 0.16f, 0.62f);
    ApplyWeatheredMetalMaterials(model, swordModel, worldRustTextureId,
                                 {{0.56f, 0.50f, 0.38f, 1.0f},
                                  {0.36f, 0.36f, 0.34f, 1.0f},
                                  {0.48f, 0.24f, 0.12f, 1.0f}},
                                 0.32f, 0.22f, 0.56f);
    ApplyRustedRobotMaterials(model, enemyModel, enemyRustTextureId);
    ApplyWeatheredMetalMaterials(model, bulletModel, worldRustTextureId,
                                 {{0.95f, 0.72f, 0.36f, 1.0f},
                                  {0.70f, 0.78f, 0.76f, 1.0f},
                                  {0.48f, 0.24f, 0.12f, 1.0f}},
                                 0.76f, 0.58f, 0.26f);
    arenaNoiseTextureId_ = worldRustTextureId;
    arenaFloorModelId_ = model->CreatePlane(
        arenaNoiseTextureId_,
        MakeArenaMaterial({0.16f, 0.14f, 0.13f, 1.0f}, true, 0.10f, 0.84f));
    arenaLowPolyTerrainModelId_ = model->CreateLowPolyTerrain(
        worldRustTextureId,
        MakeArenaMaterial({0.11f, 0.10f, 0.095f, 1.0f}, true, 0.06f, 0.90f), 42,
        78.0f, 7.2f, 12.0f, 0x4107u);
    arenaCenterDiskModelId_ = model->CreateRing(
        worldRustTextureId,
        MakeArenaMaterial({0.36f, 0.18f, 0.10f, 1.0f}, true, 0.14f, 0.70f), 96,
        1.95f, 0.0f);
    arenaSpokeModelId_ = model->CreatePlane(
        worldRustTextureId,
        MakeArenaMaterial({0.13f, 0.12f, 0.11f, 1.0f}, true, 0.08f, 0.80f));
    arenaInnerRingModelId_ = model->CreateRing(
        worldRustTextureId,
        MakeArenaMaterial({0.16f, 0.30f, 0.27f, 1.0f}, true, 0.10f, 0.72f), 96,
        4.9f, 4.35f);
    arenaOuterRingModelId_ = model->CreateRing(
        worldRustTextureId,
        MakeArenaMaterial({0.22f, 0.13f, 0.09f, 1.0f}, true, 0.10f, 0.78f), 128,
        12.3f, 11.6f);
    arenaColumnModelId_ = model->CreateCylinder(
        worldRustTextureId,
        MakeArenaMaterial({0.15f, 0.13f, 0.12f, 1.0f}, true, 0.12f, 0.78f), 24,
        0.26f, 0.38f, 5.4f);
    arenaColumnCapModelId_ = model->CreateCylinder(
        worldRustTextureId,
        MakeArenaMaterial({0.34f, 0.16f, 0.08f, 1.0f}, true, 0.16f, 0.66f), 32,
        0.68f, 0.78f, 0.24f);
    arenaDomeModelId_ = model->CreateCylinder(
        worldRustTextureId,
        MakeArenaMaterial({0.035f, 0.070f, 0.070f, 0.38f}, true, 0.00f, 0.92f), 128,
        4.5f, 13.5f, 8.8f);
    arenaBarrierRingModelId_ = model->CreateRing(
        worldRustTextureId,
        MakeArenaMaterial({0.80f, 0.24f, 0.08f, 0.42f}, true, 0.02f, 0.82f), 128,
        13.1f, 12.9f);
    enemyTelegraphPlaneModelId_ = model->CreatePlane(
        worldRustTextureId,
        MakeArenaMaterial({1.0f, 0.26f, 0.08f, 0.34f}, true, 0.06f, 0.74f));
    enemyTelegraphRingModelId_ = model->CreateRing(
        worldRustTextureId,
        MakeArenaMaterial({1.0f, 0.34f, 0.10f, 0.42f}, true, 0.04f, 0.72f), 96,
        1.0f, 0.78f);
    enemySlashPlaneModelId_ = model->CreatePlane(
        animeSlashTextureId_,
        MakeArenaMaterial({1.0f, 1.0f, 1.0f, 0.96f}, true, 0.0f, 0.88f));
    sparkParticles_.Initialize(dx, ctx_->srv, texture, particleTextureId_, 4096);
    sparkParticles_.SetEmission(1, 1000.0f);
    sparkParticles_.SetEmitterRadius(0.08f);
    explosionParticles_.Initialize(dx, ctx_->srv, texture, particleTextureId_,
                                   2048);
    explosionParticles_.SetEmission(1, 1000.0f);
    explosionParticles_.SetEmitterRadius(0.25f);
    smokeParticles_.Initialize(dx, ctx_->srv, texture, particleTextureId_, 2048);
    smokeParticles_.SetEmission(1, 1000.0f);
    smokeParticles_.SetEmitterRadius(0.40f);
    dx->EndUpload();

    texture->ReleaseUploadBuffers();

    player_.Initialize(playerModel, swordModel, selectedWeaponType_);
    playerModelId_ = playerModel;
    enemy_.Initialize(enemyModel, bulletModel);
    enemyModelId_ = enemyModel;

    cameraYaw_ = 0.0f;
    cameraPitch_ = 0.0f;
    isLockOn_ = true;
    rushChargeAssistStrength_ = 4.0f;
    rushChargeAssistMaxStep_ = 6.0f;
    rushActiveAssistStrength_ = 5.0f;
    rushActiveAssistMaxStep_ = 8.0f;
    rushLeadDistance_ = 2.5f;
    currentFovDeg_ = normalFovDeg_;
    targetFovDeg_ = normalFovDeg_;

    const DirectX::XMFLOAT3 &playerPos = player_.GetTransform().position;
    const DirectX::XMFLOAT3 &enemyPos = enemy_.GetTransform().position;
    lockOnOrbitCameraPos_ = {playerPos.x, playerPos.y + lockOnOrbitHeight_,
                             playerPos.z - lockOnOrbitRadius_};
    lockOnLookAt_ = playerViewCamera_
                        ? DirectX::XMFLOAT3{enemyPos.x,
                                            enemyPos.y +
                                                playerViewLockOnLookHeight_,
                                            enemyPos.z}
                        : DirectX::XMFLOAT3{
                              playerPos.x * lockOnLookPlayerWeight_ +
                                  enemyPos.x * lockOnLookEnemyWeight_,
                              (playerPos.y + cameraLookHeight_) * 0.52f +
                                  (enemyPos.y + 1.30f) * 0.48f,
                              playerPos.z * lockOnLookPlayerWeight_ +
                                  enemyPos.z * lockOnLookEnemyWeight_};
    if (Model *playerModelData = model->GetModel(playerModelId_)) {
        if (!playerModelData->animations.empty()) {
            model->PlayAnimation(playerModelId_,
                                 playerModelData->currentAnimation, true);
        }
    }
    bullet_.Initialize(bulletModel);

    SyncEnemyAnimation();
    UpdateSceneLighting();
    counterCinematicActive_ = false;
    enemyAnimationFrozen_ = false;
    hud_.Initialize(*ctx_);
}

void GameScene::Update() {
    Input *input = ctx_->input;
#ifdef _DEBUG
    if (input->IsKeyTrigger(DIK_F7)) {
        sceneManager_->ChangeScene(std::make_unique<EnemyAnimationDebugScene>());
        return;
    }
#endif
    if (input->IsKeyTrigger(DIK_F3)) {
        showCollisionDebug_ = !showCollisionDebug_;
    }

    const float baseDeltaTime = ctx_->deltaTime;
    combatFeedback_.Update(baseDeltaTime, sceneLightTime_);
    const float gameplayDeltaTime = baseDeltaTime * ComputeGameplayTimeScale();
    const float playerDeltaTime =
        counterCinematicActive_ ? baseDeltaTime : gameplayDeltaTime;
    const float enemyDeltaTime = counterCinematicActive_
                                     ? (baseDeltaTime * counterTimeScale_)
                                     : gameplayDeltaTime;
    UpdateCamera(input);

    ctx_->model->UpdateAnimation(playerModelId_, playerDeltaTime);

    player_.Update(input, playerDeltaTime, enemy_.GetTransform().position,
                   cameraYaw_);
    hud_.Update(*ctx_, player_.GetHP(), enemy_.GetHP());
    sceneLightTime_ += baseDeltaTime;

    enemy_.Update(BuildPlayerCombatObservation(), enemyDeltaTime);
    UpdateSceneLighting();

    SyncEnemyAnimation();
    SetEnemyAnimationFrozen(counterCinematicActive_);
    if (!enemyAnimationFrozen_) {
        ctx_->model->UpdateAnimation(enemyModelId_, enemyDeltaTime);
    }
    ApplyEnemyProceduralAnimation();

    UpdateBattleCamera();
    camera_.UpdateMatrices();

    UpdateEnemyTelegraphEffects(gameplayDeltaTime);
    UpdateCombat(gameplayDeltaTime);
    sparkParticles_.Update(baseDeltaTime);
    explosionParticles_.Update(baseDeltaTime);
    smokeParticles_.Update(baseDeltaTime);
}

void GameScene::Draw() {
    ctx_->model->PreDraw();

    DrawArena();
    player_.Draw(ctx_->model, camera_, !playerViewCamera_);
    enemy_.Draw(ctx_->model, camera_);
    DrawEnemyTelegraph();
    if (showCollisionDebug_) {
        collisionDebugRenderer_.Draw(collisionManager_, camera_);
    }
    ctx_->model->PostDraw();

    sparkParticles_.Draw(camera_);
    explosionParticles_.Draw(camera_);
    smokeParticles_.Draw(camera_);
}

void GameScene::UpdateEnemyTelegraphEffects(float deltaTime) {
    bool emitEnemyTelegraphParticles = false;
    if (!emitEnemyTelegraphParticles) {
        (void)deltaTime;
        enemyTelegraphParticleTimer_ = 0.0f;
        return;
    }

    const ActionKind kind = enemy_.GetActionKind();
    const ActionStep step = enemy_.GetActionStep();
    const bool isChargeTelegraph =
        step == ActionStep::Charge && IsEnemyTelegraphKind(kind);
    const bool isActiveMeleeMarker =
        step == ActionStep::Active && IsEnemyMeleeKind(kind);
    const bool isNovaImpactEffect =
        kind == ActionKind::Nova && enemy_.IsNovaImpactWindow();
    const bool isRecoveryMarker = enemy_.IsPunishableRecovery();
    const bool isWarpTelegraph = IsWarpTelegraphStep(kind, step);
    if (!isChargeTelegraph && !isActiveMeleeMarker && !isNovaImpactEffect &&
        !isRecoveryMarker && !isWarpTelegraph) {
        enemyTelegraphParticleTimer_ = 0.0f;
        return;
    }

    enemyTelegraphParticleTimer_ -= deltaTime;
    if (enemyTelegraphParticleTimer_ > 0.0f) {
        return;
    }

    enemyTelegraphParticleTimer_ =
        isActiveMeleeMarker ? 0.010f
        : isRecoveryMarker ? 0.040f
        : (kind == ActionKind::Nova) ? (isNovaImpactEffect ? 0.010f : 0.024f)
        : (kind == ActionKind::Warp) ? 0.045f
                                     : 0.038f;

    const float yaw = enemy_.GetTelegraphYaw();
    const float forwardX = std::sin(yaw);
    const float forwardZ = std::cos(yaw);
    const XMFLOAT3 upwardPush = {-forwardX * 0.22f, 1.0f, -forwardZ * 0.22f};

    XMFLOAT3 handPos = enemy_.GetRightHandTransform().position;
    handPos.y += 0.18f;

    XMFLOAT3 floorPos = enemy_.GetTransform().position;
    floorPos.x += forwardX * 1.4f;
    floorPos.y += 0.10f;
    floorPos.z += forwardZ * 1.4f;

    if (isRecoveryMarker) {
        XMFLOAT3 weakPoint = enemy_.GetBodyTransform().position;
        weakPoint.y += 1.05f;
        sparkParticles_.EmitBurst(weakPoint, 32, 0.26f,
                                  GPUParticleSystem::BurstStyle::Sparks,
                                  {1.0f, 0.92f, 0.32f, 0.92f},
                                  {0.0f, 1.0f, 0.0f}, 1.22f);
        smokeParticles_.EmitBurst(enemy_.GetTransform().position, 8, 0.42f,
                                  GPUParticleSystem::BurstStyle::Smoke,
                                  {0.76f, 0.58f, 0.22f, 0.42f},
                                  {0.0f, 1.0f, 0.0f}, 0.22f);
        return;
    }

    switch (kind) {
    case ActionKind::Smash:
        explosionParticles_.EmitBurst(
            floorPos, isActiveMeleeMarker ? 92 : 14,
            isActiveMeleeMarker ? 1.42f : 0.36f,
            GPUParticleSystem::BurstStyle::Explosion,
            {1.0f, 0.22f, 0.04f, isActiveMeleeMarker ? 0.92f : 0.66f},
            upwardPush, isActiveMeleeMarker ? 1.32f : 0.42f);
        sparkParticles_.EmitBurst(handPos, isActiveMeleeMarker ? 280 : 58,
                                  isActiveMeleeMarker ? 1.08f : 0.28f,
                                  GPUParticleSystem::BurstStyle::Sparks,
                                  {1.0f, 0.62f, 0.12f, 0.96f}, upwardPush,
                                  isActiveMeleeMarker ? 5.40f : 1.90f);
        smokeParticles_.EmitBurst(floorPos, isActiveMeleeMarker ? 58 : 10,
                                  isActiveMeleeMarker ? 1.56f : 0.46f,
                                  GPUParticleSystem::BurstStyle::Smoke,
                                  {0.62f, 0.24f, 0.12f,
                                   isActiveMeleeMarker ? 0.70f : 0.54f},
                                  upwardPush,
                                  isActiveMeleeMarker ? 0.92f : 0.34f);
        break;
    case ActionKind::Sweep:
        explosionParticles_.EmitBurst(
            enemy_.GetTransform().position, isActiveMeleeMarker ? 86 : 12,
            isActiveMeleeMarker ? 1.62f : 0.42f,
            GPUParticleSystem::BurstStyle::Explosion,
            {1.0f, 0.50f, 0.06f, isActiveMeleeMarker ? 0.90f : 0.60f},
            upwardPush, isActiveMeleeMarker ? 1.20f : 0.38f);
        sparkParticles_.EmitBurst(handPos, isActiveMeleeMarker ? 320 : 54,
                                  isActiveMeleeMarker ? 1.34f : 0.32f,
                                  GPUParticleSystem::BurstStyle::Sparks,
                                  {1.0f, 0.78f, 0.18f, 0.96f}, upwardPush,
                                  isActiveMeleeMarker ? 5.65f : 1.75f);
        smokeParticles_.EmitBurst(enemy_.GetTransform().position,
                                  isActiveMeleeMarker ? 62 : 12,
                                  isActiveMeleeMarker ? 1.72f : 0.68f,
                                  GPUParticleSystem::BurstStyle::Smoke,
                                  {0.62f, 0.36f, 0.12f,
                                   isActiveMeleeMarker ? 0.66f : 0.52f},
                                  upwardPush,
                                  isActiveMeleeMarker ? 0.82f : 0.30f);
        break;
    case ActionKind::Shot:
        explosionParticles_.EmitBurst(handPos, 18, 0.32f,
                                      GPUParticleSystem::BurstStyle::Explosion,
                                      {0.44f, 1.0f, 0.96f, 0.58f}, upwardPush,
                                      0.36f);
        sparkParticles_.EmitBurst(handPos, 62, 0.28f,
                                  GPUParticleSystem::BurstStyle::Sparks,
                                  {0.74f, 1.0f, 0.96f, 0.92f}, upwardPush, 1.85f);
        break;
    case ActionKind::Wave:
        explosionParticles_.EmitBurst(floorPos, 20, 0.56f,
                                      GPUParticleSystem::BurstStyle::Explosion,
                                      {0.42f, 1.0f, 0.24f, 0.62f}, upwardPush,
                                      0.42f);
        sparkParticles_.EmitBurst(handPos, 74, 0.34f,
                                  GPUParticleSystem::BurstStyle::Sparks,
                                  {0.74f, 1.0f, 0.34f, 0.92f}, upwardPush, 1.95f);
        smokeParticles_.EmitBurst(floorPos, 16, 0.72f,
                                  GPUParticleSystem::BurstStyle::Smoke,
                                  {0.28f, 0.58f, 0.22f, 0.58f}, upwardPush, 0.36f);
        break;
    case ActionKind::Nova:
        if (isNovaImpactEffect) {
            explosionParticles_.EmitBurst(enemy_.GetTransform().position, 142, 3.20f,
                                          GPUParticleSystem::BurstStyle::Explosion,
                                          {1.0f, 0.24f, 0.02f, 0.96f}, upwardPush,
                                          1.34f);
            sparkParticles_.EmitBurst(enemy_.GetTransform().position, 260, 2.70f,
                                      GPUParticleSystem::BurstStyle::Sparks,
                                      {1.0f, 0.86f, 0.24f, 0.98f}, upwardPush,
                                      3.80f);
            smokeParticles_.EmitBurst(enemy_.GetTransform().position, 80, 3.20f,
                                      GPUParticleSystem::BurstStyle::Smoke,
                                      {0.66f, 0.20f, 0.08f, 0.72f}, upwardPush,
                                      0.72f);
        } else {
            explosionParticles_.EmitBurst(enemy_.GetTransform().position, 36, 1.24f,
                                          GPUParticleSystem::BurstStyle::Explosion,
                                          {1.0f, 0.34f, 0.08f, 0.78f}, upwardPush,
                                          0.64f);
            sparkParticles_.EmitBurst(handPos, 96, 0.48f,
                                      GPUParticleSystem::BurstStyle::Sparks,
                                      {1.0f, 0.74f, 0.18f, 0.90f}, upwardPush,
                                      2.30f);
            smokeParticles_.EmitBurst(enemy_.GetTransform().position, 26, 1.36f,
                                      GPUParticleSystem::BurstStyle::Smoke,
                                      {0.54f, 0.22f, 0.10f, 0.58f}, upwardPush,
                                      0.46f);
        }
        break;
    case ActionKind::Warp: {
        XMFLOAT3 targetPos =
            enemy_.HasWarpDeparturePos() ? enemy_.GetWarpTargetPos()
                                         : enemy_.GetTransform().position;
        targetPos.y += 0.12f;
        sparkParticles_.EmitBurst(enemy_.GetTransform().position, 22, 0.26f,
                                  GPUParticleSystem::BurstStyle::Sparks,
                                  {0.72f, 0.92f, 1.0f, 0.86f}, upwardPush, 1.18f);
        smokeParticles_.EmitBurst(targetPos, 10, 0.52f,
                                  GPUParticleSystem::BurstStyle::Smoke,
                                  {0.30f, 0.44f, 0.62f, 0.54f}, upwardPush, 0.30f);
        break;
    }
    default:
        break;
    }
}

void GameScene::DrawEnemyTelegraph() {
    const ActionKind kind = enemy_.GetActionKind();
    const ActionStep step = enemy_.GetActionStep();
    const bool isChargeTelegraph =
        step == ActionStep::Charge && IsEnemyTelegraphKind(kind);
    const bool isActiveMeleeMarker =
        step == ActionStep::Active && IsEnemyMeleeKind(kind);
    const bool isNovaJumpWarning =
        kind == ActionKind::Nova && enemy_.IsNovaImpactPending();
    const bool isNovaImpactMarker =
        kind == ActionKind::Nova && enemy_.IsNovaImpactWindow();
    const bool isRecoveryMarker = enemy_.IsPunishableRecovery();
    const bool isWarpTelegraph = IsWarpTelegraphStep(kind, step);
    bool hasProjectileMarkers = false;
    for (const EnemyBullet &bullet : enemy_.GetBullets()) {
        hasProjectileMarkers = hasProjectileMarkers || bullet.isAlive;
    }
    for (const EnemyWave &wave : enemy_.GetWaves()) {
        hasProjectileMarkers = hasProjectileMarkers || wave.isAlive;
    }
    if (!isChargeTelegraph && !isActiveMeleeMarker && !isNovaJumpWarning &&
        !isNovaImpactMarker && !isRecoveryMarker && !isWarpTelegraph &&
        !hasProjectileMarkers) {
        return;
    }

    ModelManager *model = ctx_->model;
    const Transform &enemyTf = enemy_.GetTransform();
    const float yaw = enemy_.GetTelegraphYaw();
    const float forwardX = std::sin(yaw);
    const float forwardZ = std::cos(yaw);
    const float rightX = std::cos(yaw);
    const float rightZ = -std::sin(yaw);
    const float pulse = 0.5f + 0.5f * std::sinf(sceneLightTime_ * 18.0f);
    const float hotPulse = 0.5f + 0.5f * std::sinf(sceneLightTime_ * 31.0f);
    const float duration = TelegraphDuration(kind, step);
    const float progress =
        (std::clamp)(enemy_.GetActionTimerForPresentation() / duration, 0.0f, 1.0f);
    const float countdown = progress * progress;
    const float warningBoost = 0.28f + countdown * 0.54f;

    auto drawTelegraphMode = [&](uint32_t modelId, const Transform &tf,
                                 const XMFLOAT4 &color, float intensity,
                                 float noise, bool additive) {
        ModelDrawEffect effect{};
        effect.enabled = true;
        effect.additiveBlend = additive;
        effect.disableCulling = true;
        effect.color = color;
        effect.intensity =
            additive ? (std::min)(intensity * 1.08f, 2.25f)
                     : (std::min)(intensity * 0.78f, 1.70f);
        effect.fresnelPower = additive ? 0.95f : 0.70f;
        effect.noiseAmount = additive ? noise * 0.18f : noise * 0.03f;
        effect.time = sceneLightTime_;
        model->SetDrawEffect(effect);
        model->Draw(modelId, tf, camera_);
        model->ClearDrawEffect();
    };
    auto drawTelegraph = [&](uint32_t modelId, const Transform &tf,
                             const XMFLOAT4 &color, float intensity,
                             float noise) {
        drawTelegraphMode(modelId, tf, color, intensity, noise, true);
    };
    auto drawGraphic = [&](uint32_t modelId, const Transform &tf,
                           const XMFLOAT4 &color, float intensity,
                           float noise) {
        drawTelegraphMode(modelId, tf, color, intensity, noise, false);
    };

    auto makeFloorPlane = [&](float forwardOffset, float width, float length) {
        Transform tf{};
        tf.position = enemyTf.position;
        tf.position.x += forwardX * forwardOffset;
        tf.position.y = 0.045f;
        tf.position.z += forwardZ * forwardOffset;
        tf.rotation = MakeQuat(-kPi * 0.5f, yaw, 0.0f);
        tf.scale = {width, length, 1.0f};
        return tf;
    };
    auto makeFloorPlaneAt = [&](const XMFLOAT3 &position, float yawRad,
                                float width, float length, float height) {
        Transform tf{};
        tf.position = position;
        tf.position.y = height;
        tf.rotation = MakeQuat(-kPi * 0.5f, yawRad, 0.0f);
        tf.scale = {width, length, 1.0f};
        return tf;
    };
    auto makeAirPlaneAt = [&](const XMFLOAT3 &position, float yawRad,
                              float width, float height, float roll) {
        Transform tf{};
        tf.position = position;
        tf.rotation = MakeQuat(0.0f, yawRad, roll);
        tf.scale = {width, height, 1.0f};
        return tf;
    };

    auto makeFloorRing = [&](float radius) {
        Transform tf{};
        tf.position = enemyTf.position;
        tf.position.y = 0.055f;
        tf.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
        tf.scale = {radius, radius, 1.0f};
        return tf;
    };
    auto makeAirRingAt = [&](const XMFLOAT3 &position, float yawRad, float radius,
                             float roll) {
        Transform tf{};
        tf.position = position;
        tf.rotation = MakeQuat(0.0f, yawRad, roll);
        tf.scale = {radius, radius, 1.0f};
        return tf;
    };
    auto makeFloorRingAt = [&](const XMFLOAT3 &position, float radius) {
        Transform tf{};
        tf.position = position;
        tf.position.y = 0.060f;
        tf.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
        tf.scale = {radius, radius, 1.0f};
        return tf;
    };
    auto makeOffsetFloorPlane = [&](float forwardOffset, float sideOffset,
                                    float width, float length, float height) {
        XMFLOAT3 position = enemyTf.position;
        position.x += forwardX * forwardOffset + rightX * sideOffset;
        position.z += forwardZ * forwardOffset + rightZ * sideOffset;
        return makeFloorPlaneAt(position, yaw, width, length, height);
    };
    auto yawFacingCamera = [&](const XMFLOAT3 &position) {
        const XMFLOAT3 &cameraPos = camera_.GetPosition();
        return std::atan2(cameraPos.x - position.x, cameraPos.z - position.z);
    };
    auto drawLaneFrame = [&](float forwardOffset, float width, float length,
                             const XMFLOAT4 &edgeColor,
                             const XMFLOAT4 &centerColor, float intensity) {
        drawTelegraph(enemyTelegraphPlaneModelId_,
                      makeOffsetFloorPlane(forwardOffset, 0.0f, 0.22f,
                                           length + 0.28f, 0.105f),
                      centerColor, intensity, 0.42f);
        drawTelegraph(enemyTelegraphPlaneModelId_,
                      makeOffsetFloorPlane(forwardOffset, width * 0.5f, 0.14f,
                                           length, 0.100f),
                      edgeColor, intensity * 0.92f, 0.34f);
        drawTelegraph(enemyTelegraphPlaneModelId_,
                      makeOffsetFloorPlane(forwardOffset, -width * 0.5f, 0.14f,
                                           length, 0.100f),
                      edgeColor, intensity * 0.92f, 0.34f);
    };
    auto drawTransverseBar = [&](float forwardOffset, float width,
                                 const XMFLOAT4 &color, float intensity,
                                 float height) {
        XMFLOAT3 position = enemyTf.position;
        position.x += forwardX * forwardOffset;
        position.z += forwardZ * forwardOffset;
        drawTelegraph(enemyTelegraphPlaneModelId_,
                      makeFloorPlaneAt(position, yaw + kPi * 0.5f, 0.18f,
                                       width, height),
                      color, intensity, 0.38f);
    };
    auto drawAnimeHitVolume = [&](const OBB &box, const XMFLOAT4 &floorColor,
                                  const XMFLOAT4 &airColor, float intensity) {
        const XMFLOAT4 ink = {0.015f, 0.014f, 0.012f, 0.72f};
        const XMFLOAT4 paper = {1.0f, 0.98f, 0.82f, 0.56f};
        XMFLOAT3 floorCenter = box.center;
        floorCenter.y = 0.135f;
        drawGraphic(enemyTelegraphPlaneModelId_,
                    makeFloorPlaneAt(floorCenter, yaw, box.size.x + 0.22f,
                                     box.size.z + 0.22f, 0.128f),
                    ink, intensity * 0.70f, 0.02f);
        drawTelegraph(enemyTelegraphPlaneModelId_,
                      makeFloorPlaneAt(floorCenter, yaw, box.size.x, box.size.z,
                                       0.135f),
                      floorColor, intensity, 0.46f);
        drawTelegraph(enemyTelegraphPlaneModelId_,
                      makeFloorPlaneAt(floorCenter, yaw, box.size.x * 0.18f,
                                       box.size.z + 0.36f, 0.144f),
                      paper, intensity * 0.42f, 0.06f);

        XMFLOAT3 airCenter = box.center;
        airCenter.y = (std::max)(0.72f, box.center.y);
        drawGraphic(enemyTelegraphPlaneModelId_,
                    makeAirPlaneAt(airCenter, yaw, box.size.x + 0.18f,
                                   box.size.y + 0.16f, 0.0f),
                    ink, intensity * 0.46f, 0.02f);
        drawTelegraph(enemyTelegraphPlaneModelId_,
                      makeAirPlaneAt(airCenter, yaw, box.size.x, box.size.y,
                                     0.0f),
                      airColor, intensity * 0.74f, 0.42f);
    };
    auto drawAnimeSlash = [&](const XMFLOAT3 &center, float yawRad,
                              float height, float width, float roll,
                              const XMFLOAT4 &coreColor,
                              const XMFLOAT4 &edgeColor, float intensity) {
        const float rightX = std::cos(yawRad);
        const float rightZ = -std::sin(yawRad);
        const float cameraYaw = yawFacingCamera(center);
        const float slashHeight = (std::clamp)(height, 1.20f, 5.25f);
        const float slashWidth = (std::clamp)(width, 0.38f, 1.35f);
        const float slashIntensity = (std::min)(intensity, 2.15f);

        XMFLOAT4 outerEdge = edgeColor;
        outerEdge.w = (std::min)(outerEdge.w * 0.58f, 0.50f);
        XMFLOAT4 hotEdge = edgeColor;
        hotEdge.w = (std::min)(hotEdge.w * 0.46f, 0.42f);
        XMFLOAT4 brightCore = coreColor;
        brightCore.w = (std::min)(brightCore.w * 0.72f, 0.66f);
        XMFLOAT4 ghost = edgeColor;
        ghost.w = (std::min)(ghost.w * 0.24f, 0.22f);
        XMFLOAT4 ink = {0.012f, 0.012f, 0.014f, 0.68f};
        XMFLOAT4 paper = {1.0f, 0.98f, 0.84f, 0.78f};
        XMFLOAT4 popEdge = edgeColor;
        popEdge.w = (std::min)(popEdge.w * 0.82f, 0.64f);

        XMFLOAT3 trailCenter = center;
        trailCenter.x -= rightX * slashWidth * 0.34f;
        trailCenter.z -= rightZ * slashWidth * 0.34f;
        trailCenter.y -= 0.08f;
        drawGraphic(enemySlashPlaneModelId_,
                    makeAirPlaneAt(trailCenter, yawRad, slashWidth * 2.18f,
                                   slashHeight * 0.94f, roll - 0.18f),
                    ink, slashIntensity * 0.78f, 0.02f);
        drawTelegraph(enemySlashPlaneModelId_,
                      makeAirPlaneAt(trailCenter, yawRad, slashWidth * 1.85f,
                                     slashHeight * 0.86f, roll - 0.16f),
                      ghost, slashIntensity * 0.36f, 0.16f);

        XMFLOAT3 offsetEdge = center;
        offsetEdge.x += rightX * slashWidth * 0.10f;
        offsetEdge.z += rightZ * slashWidth * 0.10f;
        offsetEdge.y += 0.04f;
        drawGraphic(enemySlashPlaneModelId_,
                    makeAirPlaneAt(offsetEdge, yawRad, slashWidth * 2.14f,
                                   slashHeight * 1.04f, roll + 0.02f),
                    ink, slashIntensity * 0.92f, 0.02f);
        drawTelegraph(enemySlashPlaneModelId_,
                      makeAirPlaneAt(center, yawRad, slashWidth * 2.00f,
                                     slashHeight * 1.02f, roll),
                      outerEdge, slashIntensity * 0.58f, 0.14f);
        drawTelegraph(enemySlashPlaneModelId_,
                      makeAirPlaneAt(center, cameraYaw, slashWidth * 1.82f,
                                     slashHeight * 0.94f, roll),
                      hotEdge, slashIntensity * 0.50f, 0.10f);
        drawTelegraph(enemySlashPlaneModelId_,
                      makeAirPlaneAt(center, yawRad, slashWidth * 0.92f,
                                     slashHeight * 0.78f, roll),
                      brightCore, slashIntensity * 0.72f, 0.04f);
        drawTelegraph(enemyTelegraphPlaneModelId_,
                      makeAirPlaneAt(center, yawRad, slashWidth * 0.14f,
                                     slashHeight * 0.92f, roll),
                      paper, slashIntensity * 0.56f, 0.03f);

        XMFLOAT3 speedLine = center;
        speedLine.y += slashHeight * 0.10f;
        XMFLOAT3 speedLineBack = speedLine;
        speedLineBack.x -= rightX * slashWidth * 0.74f;
        speedLineBack.z -= rightZ * slashWidth * 0.74f;
        drawGraphic(enemyTelegraphPlaneModelId_,
                    makeAirPlaneAt(speedLineBack, yawRad, slashWidth * 0.16f,
                                   slashHeight * 0.96f, roll + 0.10f),
                    ink, slashIntensity * 0.58f, 0.01f);
        drawTelegraph(enemyTelegraphPlaneModelId_,
                      makeAirPlaneAt(speedLine, yawRad, slashWidth * 0.10f,
                                     slashHeight * 0.84f, roll),
                      {1.0f, 1.0f, 0.72f, 0.38f},
                      slashIntensity * 0.34f, 0.04f);
        for (int i = 0; i < 3; ++i) {
            XMFLOAT3 chip = center;
            const float lane = -0.54f + 0.44f * static_cast<float>(i);
            chip.x += rightX * slashWidth * lane;
            chip.z += rightZ * slashWidth * lane;
            chip.y += slashHeight * (0.26f - 0.18f * static_cast<float>(i));
            drawTelegraph(enemyTelegraphPlaneModelId_,
                          makeAirPlaneAt(chip, yawRad + 0.10f,
                                         slashWidth * (0.13f + 0.03f * i),
                                         slashHeight * (0.34f - 0.04f * i),
                                         roll + 0.28f),
                          i == 1 ? paper : popEdge,
                          slashIntensity * (0.32f - 0.04f * i), 0.03f);
        }
    };
    auto drawAnimeImpact = [&](const XMFLOAT3 &center, float yawRad, float size,
                               const XMFLOAT4 &color, float intensity) {
        const float markSize = (std::clamp)(size, 0.90f, 3.80f);
        const float markIntensity = (std::min)(intensity, 1.65f);

        XMFLOAT4 outer = color;
        outer.w = (std::min)(outer.w * 0.30f, 0.30f);
        XMFLOAT4 core = color;
        core.w = (std::min)(core.w * 0.44f, 0.42f);
        XMFLOAT4 ink = {0.012f, 0.011f, 0.010f, 0.70f};
        XMFLOAT4 paper = {1.0f, 0.97f, 0.78f, 0.70f};

        XMFLOAT3 airCenter = center;
        airCenter.y += 0.02f;
        drawGraphic(enemyTelegraphRingModelId_,
                    makeAirRingAt(airCenter, yawRad, markSize * 0.40f,
                                  0.0f),
                    ink, markIntensity * 0.66f, 0.02f);
        drawTelegraph(enemyTelegraphRingModelId_,
                      makeAirRingAt(airCenter, yawRad, markSize * 0.34f,
                                    0.0f),
                      core, markIntensity * 0.42f, 0.06f);
        drawGraphic(enemyTelegraphPlaneModelId_,
                    makeAirPlaneAt(airCenter, yawRad, markSize * 0.16f,
                                   markSize * 0.78f, kPi * 0.5f),
                    ink, markIntensity * 0.50f, 0.02f);
        drawTelegraph(enemyTelegraphPlaneModelId_,
                      makeAirPlaneAt(airCenter, yawRad, markSize * 0.10f,
                                     markSize * 0.58f, kPi * 0.5f),
                      paper, markIntensity * 0.44f, 0.04f);
        drawGraphic(enemyTelegraphPlaneModelId_,
                    makeAirPlaneAt(airCenter, yawRad + kPi * 0.5f,
                                   markSize * 0.13f, markSize * 0.60f,
                                   kPi * 0.5f),
                    ink, markIntensity * 0.38f, 0.02f);
        drawTelegraph(enemyTelegraphPlaneModelId_,
                      makeAirPlaneAt(airCenter, yawRad + kPi * 0.5f,
                                     markSize * 0.08f, markSize * 0.42f,
                                     kPi * 0.5f),
                      outer, markIntensity * 0.30f, 0.04f);

        XMFLOAT3 floorCenter = center;
        floorCenter.y = 0.095f;
        drawGraphic(enemyTelegraphRingModelId_,
                    makeFloorRingAt(floorCenter, markSize * 0.34f), ink,
                    markIntensity * 0.42f, 0.02f);
        drawTelegraph(enemyTelegraphRingModelId_,
                      makeFloorRingAt(floorCenter, markSize * 0.28f), outer,
                      markIntensity * 0.32f, 0.05f);
        for (int i = 0; i < 4; ++i) {
            const float spokeYaw = yawRad + kPi * 0.25f * static_cast<float>(i);
            XMFLOAT3 spoke = airCenter;
            spoke.x += std::sin(spokeYaw) * markSize * 0.28f;
            spoke.z += std::cos(spokeYaw) * markSize * 0.28f;
            drawTelegraph(enemyTelegraphPlaneModelId_,
                          makeAirPlaneAt(spoke, spokeYaw, markSize * 0.08f,
                                         markSize * 0.42f, 0.0f),
                          i % 2 == 0 ? paper : outer,
                          markIntensity * 0.22f, 0.03f);
        }
    };

    if (isRecoveryMarker) {
        const float recoveryProgress = enemy_.GetRecoveryProgressForPresentation();
        const float recoveryPulse = 0.5f + 0.5f * std::sinf(sceneLightTime_ * 24.0f);
        const float shrink = 1.0f - recoveryProgress * 0.28f;
        drawTelegraph(enemyTelegraphRingModelId_,
                      makeFloorRingAt(enemyTf.position,
                                      (2.10f + recoveryPulse * 0.18f) * shrink),
                      {1.0f, 0.88f, 0.18f, 0.72f},
                      0.86f + recoveryPulse * 0.42f, 0.32f);
        drawTelegraph(enemyTelegraphRingModelId_,
                      makeFloorRingAt(enemyTf.position,
                                      (1.18f + hotPulse * 0.14f) * shrink),
                      {0.42f, 1.0f, 0.58f, 0.58f},
                      0.58f + hotPulse * 0.30f, 0.24f);
        Transform weakLine = makeFloorPlaneAt(enemyTf.position, yaw, 0.28f,
                                              2.65f * shrink, 0.085f);
        drawTelegraph(enemyTelegraphPlaneModelId_, weakLine,
                      {1.0f, 0.96f, 0.32f, 0.56f},
                      0.52f + recoveryPulse * 0.34f, 0.28f);
    }

    if (!isRecoveryMarker) {
    switch (kind) {
    case ActionKind::Smash: {
        const XMFLOAT3 attackSize = enemy_.GetSmashAttackBoxSize();
        const float activeFlash = isActiveMeleeMarker ? 0.82f : 0.0f;
        const float laneWidth = attackSize.x + 0.70f;
        const float laneLength = attackSize.z + 1.10f;
        const float laneOffset = 1.05f;
        drawTelegraph(enemyTelegraphPlaneModelId_,
                      makeFloorPlane(laneOffset, laneWidth, laneLength),
                      {1.0f, 0.10f, 0.02f, 0.54f},
                      warningBoost + pulse * 0.34f, 0.38f);
        drawLaneFrame(laneOffset, laneWidth, laneLength,
                      {1.0f, 0.72f, 0.12f, 0.78f},
                      {1.0f, 0.94f, 0.24f, 0.70f},
                      0.72f + warningBoost + hotPulse * 0.46f);
        drawTransverseBar(laneOffset + laneLength * 0.36f, laneWidth,
                          {1.0f, 0.36f, 0.04f, 0.72f},
                          0.58f + warningBoost + pulse * 0.28f, 0.095f);
        drawTransverseBar(laneOffset, laneWidth * 0.82f,
                          {1.0f, 0.88f, 0.18f, 0.66f},
                          0.54f + warningBoost + hotPulse * 0.28f, 0.108f);
        if (isChargeTelegraph) {
            XMFLOAT3 previewCenter = enemyTf.position;
            previewCenter.x += forwardX * 1.18f;
            previewCenter.y += 1.34f;
            previewCenter.z += forwardZ * 1.18f;
            drawAnimeImpact(previewCenter, yaw, 2.35f + warningBoost * 0.80f,
                            {1.0f, 0.56f, 0.08f, 0.56f},
                            0.72f + warningBoost * 0.72f);
            drawAnimeSlash(previewCenter, yaw, attackSize.y + 1.85f, 0.72f,
                           0.72f, {1.0f, 0.96f, 0.42f, 0.58f},
                           {1.0f, 0.20f, 0.02f, 0.44f},
                           0.82f + warningBoost * 0.68f);
        }
        if (isActiveMeleeMarker) {
            const OBB attackBox = enemy_.GetAttackOBB();
            XMFLOAT3 flashPos = enemyTf.position;
            flashPos.x += forwardX * 1.15f;
            flashPos.z += forwardZ * 1.15f;
            drawAnimeHitVolume(attackBox, {1.0f, 0.06f, 0.00f, 0.80f},
                               {1.0f, 0.42f, 0.05f, 0.76f},
                               2.25f + hotPulse * 0.92f);
            drawTelegraph(enemyTelegraphPlaneModelId_,
                          makeFloorPlane(1.05f, attackSize.x + 0.35f,
                                         attackSize.z + 0.45f),
                          {1.0f, 0.10f, 0.00f, 0.86f},
                          1.10f + activeFlash + hotPulse * 0.42f, 0.72f);
            drawTelegraph(enemyTelegraphPlaneModelId_,
                          makeFloorPlane(1.15f, 0.42f, attackSize.z + 0.95f),
                          {1.0f, 0.90f, 0.24f, 0.78f},
                          1.00f + hotPulse * 0.54f, 0.56f);
            drawTelegraph(enemyTelegraphPlaneModelId_,
                          makeFloorPlaneAt(flashPos, yaw + 0.54f, 0.26f,
                                           attackSize.x + 1.15f, 0.105f),
                          {1.0f, 0.96f, 0.30f, 0.80f},
                          1.28f + hotPulse * 0.62f, 0.56f);
            drawTelegraph(enemyTelegraphPlaneModelId_,
                          makeFloorPlaneAt(flashPos, yaw - 0.54f, 0.26f,
                                           attackSize.x + 1.15f, 0.110f),
                          {1.0f, 0.72f, 0.10f, 0.74f},
                          1.16f + pulse * 0.58f, 0.52f);
            XMFLOAT3 slashCenter = attackBox.center;
            slashCenter.y += 0.82f;
            drawAnimeImpact(slashCenter, yaw, 3.95f + hotPulse * 0.72f,
                            {1.0f, 0.86f, 0.20f, 0.94f},
                            2.20f + hotPulse * 0.92f);
            drawAnimeSlash(slashCenter, yaw, attackSize.y + 3.80f, 1.65f,
                           0.70f, {1.0f, 1.0f, 0.54f, 0.96f},
                           {1.0f, 0.18f, 0.02f, 0.82f},
                           2.55f + hotPulse * 1.10f);
            drawAnimeSlash(slashCenter, yaw, attackSize.y + 3.20f, 1.30f,
                           -0.70f, {1.0f, 0.90f, 0.30f, 0.90f},
                           {1.0f, 0.08f, 0.00f, 0.74f},
                           2.20f + pulse * 0.86f);
            XMFLOAT3 slashHigh = slashCenter;
            slashHigh.y += 0.32f;
            drawAnimeSlash(slashHigh, yaw + 0.18f, attackSize.y + 4.60f, 0.92f,
                           0.08f, {1.0f, 1.0f, 0.72f, 0.86f},
                           {1.0f, 0.46f, 0.04f, 0.66f},
                           1.92f + hotPulse * 0.72f);
            XMFLOAT3 slashLow = slashCenter;
            slashLow.y -= 0.38f;
            drawAnimeSlash(slashLow, yaw - 0.10f, attackSize.y + 2.40f, 0.72f,
                           -1.02f, {1.0f, 0.94f, 0.40f, 0.72f},
                           {1.0f, 0.20f, 0.00f, 0.54f},
                           1.44f + pulse * 0.48f);
            drawTelegraph(enemyTelegraphRingModelId_,
                          makeAirRingAt(slashCenter, yaw, 2.05f + hotPulse * 0.34f,
                                        0.0f),
                          {1.0f, 0.80f, 0.18f, 0.82f},
                          1.82f + hotPulse * 0.72f, 0.54f);
        }
        break;
    }
    case ActionKind::Sweep: {
        const XMFLOAT3 attackSize = enemy_.GetSweepAttackBoxSize();
        const float sweepRadius = (std::max)(attackSize.x, attackSize.z) * 0.62f;
        drawTelegraph(enemyTelegraphRingModelId_, makeFloorRing(sweepRadius + pulse * 0.24f),
                      {1.0f, 0.42f, 0.04f, 0.66f}, warningBoost + pulse * 0.46f,
                      0.48f);
        drawTelegraph(enemyTelegraphRingModelId_, makeFloorRing(sweepRadius * 0.62f + hotPulse * 0.16f),
                      {1.0f, 0.88f, 0.20f, 0.58f}, 0.50f + warningBoost + hotPulse * 0.36f,
                      0.36f);
        drawTelegraph(enemyTelegraphPlaneModelId_,
                      makeFloorPlaneAt(enemyTf.position, yaw + kPi * 0.5f,
                                       0.20f, sweepRadius * 2.12f, 0.095f),
                      {1.0f, 0.86f, 0.18f, 0.66f},
                      0.60f + warningBoost + pulse * 0.34f, 0.34f);
        drawTelegraph(enemyTelegraphPlaneModelId_,
                      makeFloorPlaneAt(enemyTf.position, yaw,
                                       0.18f, sweepRadius * 1.55f, 0.105f),
                      {1.0f, 0.32f, 0.04f, 0.56f},
                      0.52f + warningBoost + hotPulse * 0.28f, 0.30f);
        if (isChargeTelegraph) {
            XMFLOAT3 previewCenter = enemyTf.position;
            previewCenter.y += 1.12f;
            drawAnimeImpact(previewCenter, yaw, sweepRadius * 1.42f,
                            {1.0f, 0.58f, 0.08f, 0.54f},
                            0.68f + warningBoost * 0.66f);
            drawAnimeSlash(previewCenter, yaw + kPi * 0.5f,
                           sweepRadius * 1.82f, 0.64f, 0.0f,
                           {1.0f, 0.96f, 0.36f, 0.56f},
                           {1.0f, 0.24f, 0.02f, 0.42f},
                           0.78f + warningBoost * 0.62f);
        }
        if (isActiveMeleeMarker) {
            const OBB attackBox = enemy_.GetAttackOBB();
            const XMFLOAT3 flashPos = enemyTf.position;
            drawAnimeHitVolume(attackBox, {1.0f, 0.08f, 0.00f, 0.76f},
                               {1.0f, 0.58f, 0.08f, 0.70f},
                               2.10f + hotPulse * 0.82f);
            drawTelegraph(enemyTelegraphRingModelId_,
                          makeFloorRing(sweepRadius + pulse * 0.28f),
                          {1.0f, 0.20f, 0.02f, 0.86f},
                          1.34f + hotPulse * 0.58f, 0.78f);
            drawTelegraph(enemyTelegraphPlaneModelId_,
                          makeFloorPlane(0.42f, attackSize.x + 0.55f,
                                         attackSize.z + 0.40f),
                          {1.0f, 0.74f, 0.16f, 0.72f},
                          1.04f + hotPulse * 0.46f, 0.60f);
            drawTelegraph(enemyTelegraphPlaneModelId_,
                          makeFloorPlaneAt(flashPos, yaw + 1.57079633f,
                                           0.30f, attackSize.x + 1.20f,
                                           0.105f),
                          {1.0f, 0.92f, 0.24f, 0.78f},
                          1.24f + hotPulse * 0.58f, 0.58f);
            drawTelegraph(enemyTelegraphPlaneModelId_,
                          makeFloorPlaneAt(flashPos, yaw, 0.22f,
                                           attackSize.x + 0.70f, 0.112f),
                          {1.0f, 0.58f, 0.08f, 0.70f},
                          1.04f + pulse * 0.50f, 0.48f);
            XMFLOAT3 ringCenter = enemyTf.position;
            ringCenter.y += 1.10f;
            drawTelegraph(enemyTelegraphRingModelId_,
                          makeAirRingAt(ringCenter, yaw, sweepRadius * 1.24f,
                                        kPi * 0.5f),
                          {1.0f, 0.88f, 0.22f, 0.92f},
                          2.26f + hotPulse * 0.92f, 0.68f);
            drawTelegraph(enemyTelegraphRingModelId_,
                          makeAirRingAt(ringCenter, yaw + kPi * 0.33f,
                                        sweepRadius * 1.05f, kPi * 0.5f),
                          {1.0f, 0.28f, 0.04f, 0.78f},
                          1.72f + pulse * 0.66f, 0.56f);
            drawAnimeImpact(ringCenter, yaw, sweepRadius * 2.18f,
                            {1.0f, 0.82f, 0.16f, 0.86f},
                            1.62f + hotPulse * 0.70f);
            drawAnimeSlash(ringCenter, yaw + kPi * 0.5f,
                           sweepRadius * 2.20f, 1.05f, 0.0f,
                           {1.0f, 1.0f, 0.42f, 0.86f},
                           {1.0f, 0.26f, 0.02f, 0.68f},
                           1.90f + hotPulse * 0.72f);
            drawAnimeSlash(ringCenter, yaw,
                           sweepRadius * 1.80f, 0.82f, 0.0f,
                           {1.0f, 0.88f, 0.28f, 0.78f},
                           {1.0f, 0.12f, 0.00f, 0.60f},
                           1.58f + pulse * 0.58f);
            XMFLOAT3 ringHigh = ringCenter;
            ringHigh.y += 0.24f;
            drawAnimeSlash(ringHigh, yaw + kPi * 0.72f,
                           sweepRadius * 2.55f, 0.74f, 0.34f,
                           {1.0f, 0.98f, 0.48f, 0.70f},
                           {1.0f, 0.30f, 0.02f, 0.52f},
                           1.28f + hotPulse * 0.50f);
        }
        break;
    }
    case ActionKind::Shot:
        drawTelegraph(enemyTelegraphPlaneModelId_, makeFloorPlane(4.25f, 0.86f, 8.50f),
                      {0.42f, 0.96f, 0.92f, 0.50f}, warningBoost + pulse * 0.28f,
                      0.32f);
        drawLaneFrame(4.25f, 0.98f, 8.80f,
                      {0.54f, 1.0f, 0.98f, 0.66f},
                      {0.92f, 1.0f, 0.96f, 0.76f},
                      0.64f + warningBoost + hotPulse * 0.34f);
        drawTelegraph(enemyTelegraphPlaneModelId_, makeFloorPlane(4.25f, 0.20f, 8.80f),
                      {0.86f, 1.0f, 0.96f, 0.62f}, 0.58f + warningBoost + hotPulse * 0.34f,
                      0.42f);
        drawTelegraph(enemyTelegraphRingModelId_,
                      makeFloorRingAt(player_.GetTransform().position,
                                      0.90f + hotPulse * 0.12f),
                      {0.80f, 1.0f, 1.0f, 0.62f},
                      0.54f + warningBoost + hotPulse * 0.36f, 0.36f);
        {
            XMFLOAT3 muzzle = enemy_.GetRightHandTransform().position;
            muzzle.y += 0.18f;
            drawTelegraph(enemyTelegraphRingModelId_,
                          makeAirRingAt(muzzle, yaw, 0.62f + hotPulse * 0.12f,
                                        0.0f),
                          {0.72f, 1.0f, 1.0f, 0.72f},
                          0.86f + warningBoost + hotPulse * 0.38f, 0.32f);
            drawTelegraph(enemyTelegraphPlaneModelId_,
                          makeAirPlaneAt(muzzle, yaw, 0.22f, 1.35f, 0.0f),
                          {0.86f, 1.0f, 1.0f, 0.62f},
                          0.76f + warningBoost + pulse * 0.30f, 0.30f);
            XMFLOAT3 shotSlash = muzzle;
            shotSlash.x += forwardX * 0.52f;
            shotSlash.y += 0.26f;
            shotSlash.z += forwardZ * 0.52f;
            drawAnimeImpact(shotSlash, yaw, 2.30f + hotPulse * 0.34f,
                            {0.62f, 1.0f, 1.0f, 0.82f},
                            1.22f + warningBoost * 0.82f);
            drawAnimeSlash(shotSlash, yaw, 2.95f, 0.78f,
                           1.57079633f,
                           {0.88f, 1.0f, 1.0f, 0.84f},
                           {0.22f, 0.92f, 1.0f, 0.68f},
                           1.34f + warningBoost * 0.90f);
        }
        break;
    case ActionKind::Wave:
        drawTelegraph(enemyTelegraphPlaneModelId_, makeFloorPlane(4.45f, 3.70f, 8.90f),
                      {0.42f, 0.96f, 0.30f, 0.44f}, warningBoost + pulse * 0.32f,
                      0.40f);
        drawLaneFrame(4.55f, 3.95f, 9.20f,
                      {0.74f, 1.0f, 0.26f, 0.68f},
                      {0.90f, 1.0f, 0.34f, 0.62f},
                      0.58f + warningBoost + hotPulse * 0.34f);
        drawTelegraph(enemyTelegraphPlaneModelId_, makeFloorPlane(4.70f, 1.05f, 9.20f),
                      {0.86f, 1.0f, 0.36f, 0.54f}, 0.50f + warningBoost + hotPulse * 0.34f,
                      0.48f);
        drawTransverseBar(2.20f, 3.70f, {0.80f, 1.0f, 0.28f, 0.60f},
                          0.46f + warningBoost + pulse * 0.24f, 0.095f);
        drawTransverseBar(4.40f, 3.70f, {0.62f, 1.0f, 0.22f, 0.56f},
                          0.42f + warningBoost + hotPulse * 0.20f, 0.100f);
        drawTransverseBar(6.60f, 3.70f, {0.90f, 1.0f, 0.36f, 0.54f},
                          0.38f + warningBoost + pulse * 0.18f, 0.105f);
        {
            XMFLOAT3 waveCenter = enemyTf.position;
            waveCenter.x += forwardX * 2.65f;
            waveCenter.y += 0.92f;
            waveCenter.z += forwardZ * 2.65f;
            drawAnimeImpact(waveCenter, yaw, 3.45f + hotPulse * 0.45f,
                            {0.62f, 1.0f, 0.24f, 0.76f},
                            1.10f + warningBoost * 0.76f);
            drawAnimeSlash(waveCenter, yaw, 3.80f, 1.18f, 1.57079633f,
                           {0.92f, 1.0f, 0.46f, 0.82f},
                           {0.32f, 1.0f, 0.10f, 0.64f},
                           1.26f + warningBoost * 0.82f);
            XMFLOAT3 waveSide = waveCenter;
            waveSide.y += 0.34f;
            drawAnimeSlash(waveSide, yaw + kPi * 0.5f, 3.10f, 0.86f, 0.20f,
                           {0.86f, 1.0f, 0.36f, 0.66f},
                           {0.20f, 0.88f, 0.06f, 0.50f},
                           0.96f + hotPulse * 0.42f);
        }
        break;
    case ActionKind::Nova:
        drawTelegraph(enemyTelegraphRingModelId_,
                      makeFloorRing(enemy_.GetNovaImpactRadius() + pulse * 0.35f),
                      {1.0f, 0.18f, 0.04f,
                       isNovaImpactMarker ? 0.86f : 0.62f},
                      (isNovaImpactMarker ? 1.42f : 0.70f) + warningBoost +
                          pulse * 0.46f,
                      0.62f);
        drawTelegraph(enemyTelegraphRingModelId_,
                      makeFloorRing(enemy_.GetNovaImpactRadius() * 0.68f +
                                    hotPulse * 0.28f),
                      {1.0f, 0.76f, 0.18f,
                       isNovaImpactMarker ? 0.76f : 0.50f},
                      (isNovaImpactMarker ? 1.06f : 0.48f) + warningBoost +
                          hotPulse * 0.38f,
                      0.52f);
        drawTelegraph(enemyTelegraphPlaneModelId_,
                      makeFloorPlaneAt(enemyTf.position, 0.0f, 0.24f,
                                       enemy_.GetNovaImpactRadius() * 2.0f, 0.104f),
                      {1.0f, 0.52f, 0.06f, 0.52f},
                      0.44f + warningBoost + pulse * 0.26f, 0.42f);
        drawTelegraph(enemyTelegraphPlaneModelId_,
                      makeFloorPlaneAt(enemyTf.position, kPi * 0.5f, 0.24f,
                                       enemy_.GetNovaImpactRadius() * 2.0f, 0.108f),
                      {1.0f, 0.88f, 0.20f, 0.48f},
                      0.40f + warningBoost + hotPulse * 0.24f, 0.38f);
        {
            XMFLOAT3 chargeCenter = enemyTf.position;
            chargeCenter.y += isNovaImpactMarker ? 1.22f : 2.00f;
            drawAnimeImpact(chargeCenter, yaw,
                            (isNovaImpactMarker ? 5.70f : 3.30f) +
                                hotPulse * 0.60f,
                            {1.0f, 0.54f, 0.08f,
                             isNovaImpactMarker ? 0.92f : 0.66f},
                            (isNovaImpactMarker ? 2.10f : 1.10f) +
                                warningBoost * 0.72f);
            drawAnimeSlash(chargeCenter, yaw, isNovaImpactMarker ? 6.20f : 3.65f,
                           isNovaImpactMarker ? 1.18f : 0.78f,
                           1.57079633f,
                           {1.0f, 1.0f, 0.42f,
                            isNovaImpactMarker ? 0.88f : 0.62f},
                           {1.0f, 0.20f, 0.02f,
                            isNovaImpactMarker ? 0.72f : 0.46f},
                           (isNovaImpactMarker ? 1.72f : 0.94f) +
                               warningBoost * 0.62f);
        }
        if (isNovaImpactMarker) {
            XMFLOAT3 burstCenter = enemyTf.position;
            burstCenter.y += 1.12f;
            drawTelegraph(enemyTelegraphRingModelId_,
                          makeAirRingAt(burstCenter, yaw,
                                        enemy_.GetNovaImpactRadius() * 0.58f,
                                        0.0f),
                          {1.0f, 0.78f, 0.12f, 0.76f},
                          1.18f + hotPulse * 0.42f, 0.52f);
            drawTelegraph(enemyTelegraphPlaneModelId_,
                          makeAirPlaneAt(burstCenter, yaw, 0.36f,
                                         enemy_.GetNovaImpactRadius() * 1.42f,
                                         0.0f),
                          {1.0f, 0.22f, 0.03f, 0.70f},
                          1.02f + pulse * 0.34f, 0.44f);
            drawTelegraph(enemyTelegraphPlaneModelId_,
                          makeAirPlaneAt(burstCenter, yaw + kPi * 0.5f, 0.36f,
                                         enemy_.GetNovaImpactRadius() * 1.42f,
                                         0.0f),
                          {1.0f, 0.92f, 0.26f, 0.68f},
                          0.96f + hotPulse * 0.34f, 0.42f);
        }
        break;
    case ActionKind::Warp: {
        XMFLOAT3 departure =
            enemy_.HasWarpDeparturePos() ? enemy_.GetWarpDeparturePos() : enemyTf.position;
        XMFLOAT3 target =
            enemy_.HasWarpDeparturePos() ? enemy_.GetWarpTargetPos() : enemyTf.position;
        drawTelegraph(enemyTelegraphRingModelId_, makeFloorRingAt(departure, 1.25f + pulse * 0.18f),
                      {0.50f, 0.90f, 1.0f, 0.50f}, 0.36f + warningBoost + pulse * 0.26f,
                      0.40f);
        drawTelegraph(enemyTelegraphRingModelId_, makeFloorRingAt(target, 1.55f + hotPulse * 0.20f),
                      {1.0f, 0.78f, 0.30f, 0.58f}, 0.48f + warningBoost + hotPulse * 0.34f,
                      0.50f);
        break;
    }
    default:
        break;
    }
    }

    for (const EnemyBullet &bullet : enemy_.GetBullets()) {
        if (!bullet.isAlive) {
            continue;
        }
        const float bulletYaw = std::atan2(bullet.velocity.x, bullet.velocity.z);
        const XMFLOAT3 size = enemy_.GetBulletHitBoxSize();
        XMFLOAT3 markerPos = bullet.position;
        markerPos.y = 0.075f;
        drawTelegraph(enemyTelegraphRingModelId_,
                      makeFloorRingAt(markerPos, (std::max)(size.x, size.z) * 1.08f +
                                                hotPulse * 0.08f),
                      bullet.isReflected ? XMFLOAT4{0.60f, 0.42f, 1.0f, 0.64f}
                                         : XMFLOAT4{0.50f, 1.0f, 0.96f, 0.66f},
                      0.80f + hotPulse * 0.34f, 0.44f);
        drawTelegraph(enemyTelegraphPlaneModelId_,
                      makeFloorPlaneAt(markerPos, bulletYaw, size.x * 0.55f,
                                       size.z * 1.45f, 0.080f),
                      bullet.isReflected ? XMFLOAT4{0.82f, 0.62f, 1.0f, 0.50f}
                                         : XMFLOAT4{0.86f, 1.0f, 0.96f, 0.48f},
                      0.62f + pulse * 0.22f, 0.32f);
        XMFLOAT3 bulletAir = bullet.position;
        bulletAir.y += size.y * 0.35f;
        drawTelegraph(enemyTelegraphRingModelId_,
                      makeAirRingAt(bulletAir, bulletYaw,
                                    (std::max)(size.x, size.z) * 0.72f +
                                        hotPulse * 0.06f,
                                    0.0f),
                      bullet.isReflected ? XMFLOAT4{0.82f, 0.54f, 1.0f, 0.58f}
                                         : XMFLOAT4{0.68f, 1.0f, 1.0f, 0.62f},
                      0.74f + hotPulse * 0.22f, 0.30f);
        drawAnimeImpact(bulletAir, bulletYaw, 1.45f + hotPulse * 0.18f,
                        bullet.isReflected ? XMFLOAT4{0.86f, 0.58f, 1.0f, 0.72f}
                                           : XMFLOAT4{0.62f, 1.0f, 1.0f, 0.76f},
                        0.92f + hotPulse * 0.34f);
        drawAnimeSlash(bulletAir, bulletYaw, 1.85f, 0.46f, 1.57079633f,
                       bullet.isReflected ? XMFLOAT4{0.94f, 0.82f, 1.0f, 0.70f}
                                          : XMFLOAT4{0.90f, 1.0f, 1.0f, 0.74f},
                       bullet.isReflected ? XMFLOAT4{0.54f, 0.22f, 1.0f, 0.54f}
                                          : XMFLOAT4{0.18f, 0.88f, 1.0f, 0.58f},
                       0.92f + pulse * 0.28f);
    }

    for (const EnemyWave &wave : enemy_.GetWaves()) {
        if (!wave.isAlive) {
            continue;
        }
        const float waveYaw = std::atan2(wave.direction.x, wave.direction.z);
        const XMFLOAT3 size = enemy_.GetWaveHitBoxSize();
        XMFLOAT3 markerPos = wave.position;
        markerPos.y = 0.070f;
        drawTelegraph(enemyTelegraphPlaneModelId_,
                      makeFloorPlaneAt(markerPos, waveYaw, size.x + 0.30f,
                                       size.z + 0.35f, 0.070f),
                      wave.isReflected ? XMFLOAT4{0.70f, 0.50f, 1.0f, 0.58f}
                                       : XMFLOAT4{0.54f, 1.0f, 0.30f, 0.62f},
                      0.86f + hotPulse * 0.36f, 0.48f);
        drawTelegraph(enemyTelegraphPlaneModelId_,
                      makeFloorPlaneAt(markerPos, waveYaw, size.x * 0.34f,
                                       size.z + 0.85f, 0.083f),
                      wave.isReflected ? XMFLOAT4{0.88f, 0.72f, 1.0f, 0.54f}
                                       : XMFLOAT4{0.90f, 1.0f, 0.34f, 0.54f},
                      0.74f + pulse * 0.28f, 0.42f);
        XMFLOAT3 waveAir = wave.position;
        waveAir.y += 0.70f;
        drawTelegraph(enemyTelegraphPlaneModelId_,
                      makeAirPlaneAt(waveAir, waveYaw, size.x + 0.50f,
                                     1.20f + hotPulse * 0.18f, 0.0f),
                      wave.isReflected ? XMFLOAT4{0.84f, 0.62f, 1.0f, 0.52f}
                                       : XMFLOAT4{0.72f, 1.0f, 0.28f, 0.58f},
                      0.74f + hotPulse * 0.26f, 0.36f);
        drawAnimeImpact(waveAir, waveYaw, size.x * 1.04f + hotPulse * 0.24f,
                        wave.isReflected ? XMFLOAT4{0.86f, 0.62f, 1.0f, 0.68f}
                                         : XMFLOAT4{0.66f, 1.0f, 0.26f, 0.70f},
                        0.90f + hotPulse * 0.30f);
        drawAnimeSlash(waveAir, waveYaw, size.x * 0.92f, 0.72f,
                       1.57079633f,
                       wave.isReflected ? XMFLOAT4{0.90f, 0.76f, 1.0f, 0.66f}
                                        : XMFLOAT4{0.92f, 1.0f, 0.42f, 0.70f},
                       wave.isReflected ? XMFLOAT4{0.52f, 0.24f, 1.0f, 0.48f}
                                        : XMFLOAT4{0.24f, 0.86f, 0.06f, 0.52f},
                       0.86f + pulse * 0.28f);
    }
}

void GameScene::DispatchCombatFeedback(const CombatFeedbackEvent &event) {
    combatFeedback_.PushEvent(event);
    EmitCombatParticles(event);
}

void GameScene::EmitCombatParticles(const CombatFeedbackEvent &event) {
    XMFLOAT3 position = event.position;
    position.y += 0.08f;

    XMFLOAT3 direction = event.direction;
    const float power = (std::max)(0.6f, event.power);

    switch (event.type) {
    case CombatFeedbackEventType::PlayerSlashHit:
        sparkParticles_.EmitBurst(position,
                                  static_cast<uint32_t>(128.0f + power * 58.0f),
                                  0.13f, GPUParticleSystem::BurstStyle::Sparks,
                                  {1.00f, 0.68f, 0.28f, 1.0f}, direction,
                                  1.85f + power * 0.45f);
        break;
    case CombatFeedbackEventType::PlayerGuard:
        sparkParticles_.EmitBurst(position, 148, 0.22f,
                                  GPUParticleSystem::BurstStyle::Sparks,
                                  {1.00f, 0.74f, 0.36f, 1.0f}, direction, 1.95f);
        explosionParticles_.EmitBurst(position, 24, 0.18f,
                                      GPUParticleSystem::BurstStyle::Explosion,
                                      {0.92f, 0.50f, 0.22f, 1.0f}, direction,
                                      0.72f);
        smokeParticles_.EmitBurst(position, 16, 0.28f,
                                  GPUParticleSystem::BurstStyle::Smoke,
                                  {0.42f, 0.36f, 0.31f, 1.0f}, direction, 0.48f);
        break;
    case CombatFeedbackEventType::PlayerDamaged:
        sparkParticles_.EmitBurst(position, 172, 0.27f,
                                  GPUParticleSystem::BurstStyle::Sparks,
                                  {1.00f, 0.52f, 0.20f, 1.0f}, direction, 2.15f);
        explosionParticles_.EmitBurst(position, 72, 0.36f,
                                      GPUParticleSystem::BurstStyle::Explosion,
                                      {1.00f, 0.30f, 0.10f, 1.0f}, direction,
                                      1.15f);
        smokeParticles_.EmitBurst(position, 58, 0.48f,
                                  GPUParticleSystem::BurstStyle::Smoke,
                                  {0.48f, 0.36f, 0.28f, 1.0f}, direction, 0.78f);
        break;
    case CombatFeedbackEventType::CounterSuccess:
        sparkParticles_.EmitBurst(position, 280, 0.40f,
                                  GPUParticleSystem::BurstStyle::Sparks,
                                  {1.00f, 0.76f, 0.26f, 1.0f}, direction, 2.1f);
        explosionParticles_.EmitBurst(position, 136, 0.56f,
                                      GPUParticleSystem::BurstStyle::Explosion,
                                      {1.00f, 0.46f, 0.12f, 1.0f}, direction,
                                      1.48f);
        smokeParticles_.EmitBurst(position, 96, 0.70f,
                                  GPUParticleSystem::BurstStyle::Smoke,
                                  {0.36f, 0.31f, 0.27f, 1.0f}, direction, 0.92f);
        break;
    case CombatFeedbackEventType::ProjectileReflect:
        sparkParticles_.EmitBurst(position, 190, 0.28f,
                                  GPUParticleSystem::BurstStyle::Sparks,
                                  {0.96f, 0.86f, 0.64f, 1.0f}, direction, 2.15f);
        explosionParticles_.EmitBurst(position, 78, 0.36f,
                                      GPUParticleSystem::BurstStyle::Explosion,
                                      {0.68f, 0.78f, 0.72f, 1.0f}, direction,
                                      1.12f);
        smokeParticles_.EmitBurst(position, 44, 0.44f,
                                  GPUParticleSystem::BurstStyle::Smoke,
                                  {0.38f, 0.34f, 0.31f, 1.0f}, direction, 0.66f);
        break;
    }
}

void GameScene::DrawOverlay() { hud_.Draw(*ctx_); }

void GameScene::DrawArena() {
    ModelManager *model = ctx_->model;
    const ActionKind actionKind = enemy_.GetActionKind();
    const float huePulse = 0.5f + 0.5f * std::sinf(sceneLightTime_ * 2.2f);
    const XMFLOAT4 calmColor = {0.62f, 0.46f, 0.34f, 0.50f};
    const XMFLOAT4 attackColor =
        actionKind == ActionKind::Smash ? XMFLOAT4{1.00f, 0.28f, 0.06f, 0.86f}
        : actionKind == ActionKind::Sweep
            ? XMFLOAT4{1.00f, 0.62f, 0.18f, 0.78f}
        : actionKind == ActionKind::Warp
            ? XMFLOAT4{0.48f, 0.82f, 0.66f, 0.82f}
        : actionKind == ActionKind::Wave
            ? XMFLOAT4{0.64f, 0.88f, 0.48f, 0.78f}
        : actionKind == ActionKind::Nova
            ? XMFLOAT4{1.00f, 0.18f, 0.02f, 0.94f}
        : actionKind == ActionKind::Shot
            ? XMFLOAT4{0.68f, 0.82f, 0.86f, 0.76f}
            : calmColor;
    const XMFLOAT4 arenaGlowColor = LerpColor(calmColor, attackColor, 0.34f + 0.24f * huePulse);
    const float actionGlow =
        actionKind == ActionKind::Warp   ? 0.22f
        : actionKind == ActionKind::Nova ? 0.32f
        : actionKind == ActionKind::Wave ? 0.15f
        : actionKind == ActionKind::Shot ? 0.10f
                                         : 0.0f;

    Transform terrain{};
    terrain.position = {0.0f, -0.42f, 0.0f};
    terrain.rotation = MakeQuat(0.0f, 0.0f, 0.0f);
    terrain.scale = {1.0f, 1.0f, 1.0f};
    model->Draw(arenaLowPolyTerrainModelId_, terrain, camera_);

    Transform floor{};
    floor.position = {0.0f, -0.04f, 0.0f};
    floor.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
    floor.scale = {28.0f, 28.0f, 1.0f};
    model->Draw(arenaFloorModelId_, floor, camera_);

    Transform centerDisk{};
    centerDisk.position = {0.0f, 0.006f, 0.0f};
    centerDisk.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
    ModelDrawEffect centerEffect{};
    centerEffect.enabled = true;
    centerEffect.additiveBlend = true;
    centerEffect.color = arenaGlowColor;
    centerEffect.intensity = 0.08f + actionGlow * 0.45f + 0.025f * huePulse;
    centerEffect.fresnelPower = 1.6f;
    centerEffect.noiseAmount = 0.08f;
    centerEffect.time = sceneLightTime_;
    model->SetDrawEffect(centerEffect);
    model->Draw(arenaCenterDiskModelId_, centerDisk, camera_);
    model->ClearDrawEffect();

    for (int i = 0; i < 16; ++i) {
        const float angle = static_cast<float>(i) * kPi * 0.125f;
        const float x = std::sinf(angle) * 3.7f;
        const float z = std::cosf(angle) * 3.7f;

        Transform spoke{};
        spoke.position = {x, 0.008f, z};
        spoke.rotation = MakeQuat(-kPi * 0.5f, angle, 0.0f);
        spoke.scale = {0.075f, 4.8f, 1.0f};
        if ((i % 4) == 0) {
            ModelDrawEffect spokeEffect{};
            spokeEffect.enabled = true;
            spokeEffect.additiveBlend = true;
            spokeEffect.color = LerpColor({1.0f, 0.34f, 0.10f, 0.62f},
                                          arenaGlowColor, huePulse);
            spokeEffect.intensity = 0.05f + actionGlow * 0.35f;
            spokeEffect.fresnelPower = 1.3f;
            spokeEffect.noiseAmount = 0.06f;
            spokeEffect.time = sceneLightTime_ + static_cast<float>(i) * 0.1f;
            model->SetDrawEffect(spokeEffect);
        }
        model->Draw(arenaSpokeModelId_, spoke, camera_);
        if ((i % 4) == 0) {
            model->ClearDrawEffect();
        }
    }

    Transform innerRing{};
    innerRing.position = {0.0f, 0.012f, 0.0f};
    innerRing.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
    model->SetDrawEffect(centerEffect);
    model->Draw(arenaInnerRingModelId_, innerRing, camera_);

    Transform outerRing{};
    outerRing.position = {0.0f, 0.018f, 0.0f};
    outerRing.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
    centerEffect.color = LerpColor({0.54f, 0.78f, 0.62f, 0.70f},
                                  arenaGlowColor, 0.45f);
    centerEffect.intensity *= 0.58f;
    model->SetDrawEffect(centerEffect);
    model->Draw(arenaOuterRingModelId_, outerRing, camera_);
    model->ClearDrawEffect();

    for (int i = 0; i < 8; ++i) {
        const float angle = static_cast<float>(i) * kPi * 0.25f;
        const float x = std::sinf(angle) * 11.9f;
        const float z = std::cosf(angle) * 11.9f;

        Transform column{};
        column.position = {x, 0.0f, z};
        column.rotation = MakeQuat(0.0f, -angle, 0.0f);
        model->Draw(arenaColumnModelId_, column, camera_);

        Transform base{};
        base.position = {x, -0.02f, z};
        base.rotation = MakeQuat(0.0f, -angle, 0.0f);
        base.scale = {1.0f, 1.0f, 1.0f};
        model->Draw(arenaColumnCapModelId_, base, camera_);

        Transform cap = base;
        cap.position.y = 5.38f;
        cap.rotation = MakeQuat(kPi, -angle, 0.0f);
        model->Draw(arenaColumnCapModelId_, cap, camera_);
    }

    ModelDrawEffect barrierEffect{};
    barrierEffect.enabled = true;
    barrierEffect.additiveBlend = true;
    barrierEffect.disableCulling = true;
    barrierEffect.color = arenaGlowColor;
    barrierEffect.intensity =
        0.08f + actionGlow * 0.32f + 0.025f * std::sinf(sceneLightTime_ * 1.8f);
    barrierEffect.fresnelPower = 1.2f;
    barrierEffect.noiseAmount = 0.18f;
    barrierEffect.time = sceneLightTime_;
    model->SetDrawEffect(barrierEffect);

    Transform dome{};
    dome.position = {0.0f, 0.0f, 0.0f};
    dome.scale = {1.0f, 1.0f, 1.0f};
    model->Draw(arenaDomeModelId_, dome, camera_);

    for (int i = 0; i < 3; ++i) {
        Transform ring{};
        ring.position = {0.0f, 1.9f + static_cast<float>(i) * 2.15f, 0.0f};
        ring.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
        ring.scale = {1.0f - static_cast<float>(i) * 0.12f,
                      1.0f - static_cast<float>(i) * 0.12f, 1.0f};
        model->Draw(arenaBarrierRingModelId_, ring, camera_);
    }

    model->ClearDrawEffect();
}
