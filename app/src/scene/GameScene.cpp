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
        {0.82f, 0.78f, 0.70f, 1.0f},
        {0.54f, 0.50f, 0.46f, 1.0f},
        {0.78f, 0.36f, 0.16f, 1.0f},
        {0.22f, 0.20f, 0.18f, 1.0f},
    };

    size_t materialIndex = 0;
    for (ModelSubMesh &subMesh : model->subMeshes) {
        subMesh.textureId = rustTextureId;

        Material material = modelManager->GetMaterial(subMesh.materialId);
        material.enableTexture = 1;
        material.color = metalTints[materialIndex % metalTints.size()];
        material.reflectionStrength = (materialIndex % 3 == 0) ? 0.74f : 0.48f;
        material.reflectionFresnelStrength =
            (materialIndex % 3 == 0) ? 0.64f : 0.38f;
        material.reflectionRoughness = (materialIndex % 3 == 0) ? 0.18f : 0.42f;
        material.enableDissolve = 0;
        material.dissolveEdgeColor = {1.0f, 0.42f, 0.12f, 1.0f};
        modelManager->SetMaterial(subMesh.materialId, material);
        ++materialIndex;
    }
}

} // namespace

void GameScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    ctx_->postEffectRenderer->SetColorMode(PostEffectRenderer::ColorMode::Sepia);
    ctx_->postEffectRenderer->SetVignettingStrength(0.34f);
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
    const uint32_t enemyRustTextureId =
        texture->CreateRustedMetalTexture(512, 512);
    const uint32_t worldRustTextureId =
        texture->CreateRustedMetalTexture(768, 768);
    ApplyWeatheredMetalMaterials(model, playerModel, worldRustTextureId,
                                 {{0.66f, 0.63f, 0.56f, 0.42f},
                                  {0.34f, 0.32f, 0.30f, 0.42f},
                                  {0.58f, 0.30f, 0.15f, 0.42f}},
                                 0.58f, 0.42f, 0.34f);
    ApplyWeatheredMetalMaterials(model, swordModel, worldRustTextureId,
                                 {{0.90f, 0.82f, 0.62f, 1.0f},
                                  {0.58f, 0.58f, 0.54f, 1.0f},
                                  {0.86f, 0.40f, 0.15f, 1.0f}},
                                 0.82f, 0.70f, 0.18f);
    ApplyRustedRobotMaterials(model, enemyModel, enemyRustTextureId);
    ApplyWeatheredMetalMaterials(model, bulletModel, worldRustTextureId,
                                 {{0.95f, 0.72f, 0.36f, 1.0f},
                                  {0.70f, 0.78f, 0.76f, 1.0f},
                                  {0.48f, 0.24f, 0.12f, 1.0f}},
                                 0.76f, 0.58f, 0.26f);
    arenaNoiseTextureId_ = worldRustTextureId;
    arenaFloorModelId_ = model->CreatePlane(
        arenaNoiseTextureId_,
        MakeArenaMaterial({0.46f, 0.31f, 0.22f, 1.0f}, true, 0.30f, 0.62f));
    arenaLowPolyTerrainModelId_ = model->CreateLowPolyTerrain(
        worldRustTextureId,
        MakeArenaMaterial({0.23f, 0.20f, 0.17f, 1.0f}, true, 0.16f, 0.78f), 42,
        78.0f, 7.2f, 12.0f, 0x4107u);
    arenaCenterDiskModelId_ = model->CreateRing(
        worldRustTextureId,
        MakeArenaMaterial({0.78f, 0.42f, 0.16f, 1.0f}, true, 0.42f, 0.36f), 96,
        1.95f, 0.0f);
    arenaSpokeModelId_ = model->CreatePlane(
        worldRustTextureId,
        MakeArenaMaterial({0.26f, 0.22f, 0.19f, 1.0f}, true, 0.28f, 0.54f));
    arenaInnerRingModelId_ = model->CreateRing(
        worldRustTextureId,
        MakeArenaMaterial({0.48f, 0.74f, 0.60f, 1.0f}, true, 0.36f, 0.44f), 96,
        4.9f, 4.35f);
    arenaOuterRingModelId_ = model->CreateRing(
        worldRustTextureId,
        MakeArenaMaterial({0.62f, 0.30f, 0.12f, 1.0f}, true, 0.34f, 0.50f), 128,
        12.3f, 11.6f);
    arenaColumnModelId_ = model->CreateCylinder(
        worldRustTextureId,
        MakeArenaMaterial({0.36f, 0.30f, 0.25f, 1.0f}, true, 0.32f, 0.58f), 24,
        0.26f, 0.38f, 5.4f);
    arenaColumnCapModelId_ = model->CreateCylinder(
        worldRustTextureId,
        MakeArenaMaterial({0.72f, 0.38f, 0.15f, 1.0f}, true, 0.40f, 0.38f), 32,
        0.68f, 0.78f, 0.24f);
    arenaDomeModelId_ = model->CreateCylinder(
        worldRustTextureId,
        MakeArenaMaterial({0.22f, 0.42f, 0.36f, 0.20f}, true, 0.02f, 0.84f), 128,
        4.5f, 13.5f, 8.8f);
    arenaBarrierRingModelId_ = model->CreateRing(
        worldRustTextureId,
        MakeArenaMaterial({0.98f, 0.48f, 0.14f, 0.55f}, true, 0.04f, 0.66f), 128,
        13.1f, 12.9f);
    sparkParticles_.Initialize(dx, ctx_->srv, texture, particleTextureId_, 2048);
    sparkParticles_.SetEmission(1, 1000.0f);
    sparkParticles_.SetEmitterRadius(0.08f);
    explosionParticles_.Initialize(dx, ctx_->srv, texture, particleTextureId_,
                                   1024);
    explosionParticles_.SetEmission(1, 1000.0f);
    explosionParticles_.SetEmitterRadius(0.25f);
    smokeParticles_.Initialize(dx, ctx_->srv, texture, particleTextureId_, 1536);
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

    hud_.Update(*ctx_, player_.GetHP(), enemy_.GetHP());

    ctx_->model->UpdateAnimation(playerModelId_, playerDeltaTime);

    player_.Update(input, playerDeltaTime, enemy_.GetTransform().position,
                   cameraYaw_);
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
    if (showCollisionDebug_) {
        collisionDebugRenderer_.Draw(collisionManager_, camera_);
    }
    ctx_->model->PostDraw();

    sparkParticles_.Draw(camera_);
    explosionParticles_.Draw(camera_);
    smokeParticles_.Draw(camera_);
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
                                  static_cast<uint32_t>(96.0f + power * 46.0f),
                                  0.13f, GPUParticleSystem::BurstStyle::Sparks,
                                  {1.00f, 0.68f, 0.28f, 1.0f}, direction,
                                  1.55f + power * 0.38f);
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
    const XMFLOAT4 calmColor = {0.84f, 0.44f, 0.16f, 0.66f};
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
    const XMFLOAT4 arenaGlowColor = LerpColor(calmColor, attackColor, 0.55f + 0.45f * huePulse);
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
    centerEffect.intensity = 0.18f + actionGlow + 0.06f * huePulse;
    centerEffect.fresnelPower = 1.6f;
    centerEffect.noiseAmount = 0.18f;
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
            spokeEffect.intensity = 0.12f + actionGlow * 0.8f;
            spokeEffect.fresnelPower = 1.3f;
            spokeEffect.noiseAmount = 0.14f;
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
    centerEffect.intensity *= 0.85f;
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
        0.22f + actionGlow * 0.75f + 0.06f * std::sinf(sceneLightTime_ * 1.8f);
    barrierEffect.fresnelPower = 1.2f;
    barrierEffect.noiseAmount = 0.48f;
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
