#include "GameScene.h"
#include "BattleResultScene.h"
#include "DirectXCommon.h"
#include "EnemyAnimationDebugScene.h"
#include "Input.h"
#include "Material.h"
#include "Model.h"
#include "ModelManager.h"
#include "SoundManager.h"
#include "SpriteManager.h"
#include "TextureManager.h"
#include "WinApp.h"
#include "PostEffectRenderer.h"
#include "SceneManager.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <string>
#include <vector>

#ifndef IMGUI_DISABLED
#include "imgui.h"
#endif

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

XMFLOAT4 MakeQuat(float pitch, float yaw, float roll) {
    XMFLOAT4 q{};
    XMStoreFloat4(&q, XMQuaternionRotationRollPitchYaw(pitch, yaw, roll));
    return q;
}

float BillboardYawToCamera(const XMFLOAT3 &position, const XMFLOAT3 &cameraPos) {
    return std::atan2f(cameraPos.x - position.x, cameraPos.z - position.z);
}

XMMATRIX MakeWorldMatrixForTransform(const Transform &transform) {
    XMVECTOR q = XMQuaternionNormalize(XMLoadFloat4(&transform.rotation));
    return XMMatrixScaling(transform.scale.x, transform.scale.y,
                           transform.scale.z) *
           XMMatrixRotationQuaternion(q) *
           XMMatrixTranslation(transform.position.x, transform.position.y,
                               transform.position.z);
}

float DistanceSq(const XMFLOAT3 &a, const XMFLOAT3 &b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

XMFLOAT3 Lerp(const XMFLOAT3 &a, const XMFLOAT3 &b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t};
}

XMVECTOR SkinSourcePosition(const Model &model, const ModelSubMesh &subMesh,
                            uint32_t vertexIndex) {
    const XMVECTOR source =
        XMLoadFloat3(&subMesh.sourcePositions[vertexIndex]);
    const SkinCluster &skinCluster = subMesh.skinCluster;
    if (!skinCluster.mappedInfluence ||
        vertexIndex >= skinCluster.influenceCount ||
        model.skeletonSpaceMatrices.empty()) {
        return source;
    }

    const VertexInfluence &influence = skinCluster.mappedInfluence[vertexIndex];
    XMVECTOR skinned = XMVectorZero();
    float totalWeight = 0.0f;
    for (uint32_t i = 0; i < kNumMaxInfluence; ++i) {
        const float weight = influence.weights[i];
        const int32_t jointIndex = influence.jointIndices[i];
        if (weight <= 0.0f || jointIndex < 0 ||
            static_cast<size_t>(jointIndex) >=
                model.skeletonSpaceMatrices.size() ||
            static_cast<size_t>(jointIndex) >=
                skinCluster.inverseBindPoseMatrices.size()) {
            continue;
        }

        const XMMATRIX inverseBindPose =
            XMLoadFloat4x4(&skinCluster.inverseBindPoseMatrices[jointIndex]);
        const XMMATRIX skeletonSpace =
            XMLoadFloat4x4(&model.skeletonSpaceMatrices[jointIndex]);
        skinned += XMVector3Transform(source, inverseBindPose * skeletonSpace) *
                   weight;
        totalWeight += weight;
    }

    if (totalWeight <= 0.0001f) {
        return source;
    }
    return skinned;
}

bool IsLikelyEnemySwordMesh(const ModelSubMesh &subMesh) {
    if (subMesh.sourcePositions.size() < 120 ||
        subMesh.sourcePositions.size() > 1200) {
        return false;
    }

    const float extentX =
        subMesh.sourceBoundsMax.x - subMesh.sourceBoundsMin.x;
    const float extentY =
        subMesh.sourceBoundsMax.y - subMesh.sourceBoundsMin.y;
    const float extentZ =
        subMesh.sourceBoundsMax.z - subMesh.sourceBoundsMin.z;
    return extentY > 5.0f && (extentZ > 3.0f || extentX > 1.0f);
}

bool TryGetEnemySwordMeshSegment(const Model &model, const Transform &transform,
                                 const XMFLOAT3 &enemyBodyPos,
                                 XMFLOAT3 &outRoot, XMFLOAT3 &outTip) {
    XMMATRIX world = MakeWorldMatrixForTransform(transform);
    if (model.hasRootAnimation) {
        world = XMLoadFloat4x4(&model.rootAnimationMatrix) * world;
    }

    for (const ModelSubMesh &subMesh : model.subMeshes) {
        if (!IsLikelyEnemySwordMesh(subMesh)) {
            continue;
        }

        std::vector<XMFLOAT3> worldPositions;
        worldPositions.reserve(subMesh.sourcePositions.size());
        for (uint32_t vertexIndex = 0;
             vertexIndex < static_cast<uint32_t>(subMesh.sourcePositions.size());
             ++vertexIndex) {
            const XMVECTOR skinned =
                SkinSourcePosition(model, subMesh, vertexIndex);
            XMFLOAT3 worldPos{};
            XMStoreFloat3(&worldPos, XMVector3Transform(skinned, world));
            worldPositions.push_back(worldPos);
        }

        float bestDistanceSq = 0.0f;
        XMFLOAT3 bestA{};
        XMFLOAT3 bestB{};
        for (size_t a = 0; a < worldPositions.size(); ++a) {
            for (size_t b = a + 1; b < worldPositions.size(); ++b) {
                const float distanceSq =
                    DistanceSq(worldPositions[a], worldPositions[b]);
                if (distanceSq > bestDistanceSq) {
                    bestDistanceSq = distanceSq;
                    bestA = worldPositions[a];
                    bestB = worldPositions[b];
                }
            }
        }

        if (bestDistanceSq < 0.64f) {
            continue;
        }

        const XMFLOAT3 center = Lerp(bestA, bestB, 0.5f);
        if (center.y < enemyBodyPos.y + 0.25f ||
            DistanceSq(center, enemyBodyPos) > 64.0f) {
            continue;
        }

        outRoot = bestA;
        outTip = bestB;
        return true;
    }

    return false;
}

float GetChargeStanceSettleTime(ActionKind kind) {
    switch (kind) {
    case ActionKind::Smash:
        return 0.46f;
    case ActionKind::Sweep:
        return 0.42f;
    case ActionKind::Shot:
    case ActionKind::Wave:
        return 0.48f;
    case ActionKind::Nova:
        return 0.64f;
    default:
        return 0.42f;
    }
}

bool IsChargeStanceSettled(ActionKind kind, ActionStep step, float timer) {
    if (step == ActionStep::Hold) {
        return true;
    }
    if (step != ActionStep::Charge) {
        return false;
    }
    return timer >= GetChargeStanceSettleTime(kind);
}

SwordCounterAxis RequiredVisualCounterAxisForAction(ActionKind kind) {
    switch (kind) {
    case ActionKind::Smash:
        return SwordCounterAxis::Vertical;
    case ActionKind::Sweep:
        return SwordCounterAxis::Horizontal;
    default:
        return SwordCounterAxis::None;
    }
}

SwordCounterAxis RequiredVisualDualCounterAxis(bool handStage) {
    return handStage ? SwordCounterAxis::Horizontal
                     : SwordCounterAxis::Vertical;
}

XMFLOAT3 CounterAxisParticleDirection(SwordCounterAxis axis) {
    switch (axis) {
    case SwordCounterAxis::Vertical:
        return {0.0f, 1.0f, 0.0f};
    case SwordCounterAxis::Horizontal:
        return {1.0f, 0.0f, 0.0f};
    default:
        return {1.0f, 0.0f, 0.0f};
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
        {0.92f, 0.92f, 0.86f, 1.0f},
        {0.78f, 0.84f, 0.86f, 1.0f},
        {0.82f, 0.68f, 0.44f, 1.0f},
        {0.58f, 0.64f, 0.66f, 1.0f},
    };

    size_t materialIndex = 0;
    for (ModelSubMesh &subMesh : model->subMeshes) {
        subMesh.textureId = rustTextureId;

        Material material = modelManager->GetMaterial(subMesh.materialId);
        material.enableTexture = 1;
        material.color = metalTints[materialIndex % metalTints.size()];
        material.reflectionStrength = (materialIndex % 3 == 0) ? 0.46f : 0.30f;
        material.reflectionFresnelStrength =
            (materialIndex % 3 == 0) ? 0.22f : 0.14f;
        material.reflectionRoughness = (materialIndex % 3 == 0) ? 0.46f : 0.58f;
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
    ctx_->postEffectRenderer->SetVignettingStrength(0.0f);
    ctx_->postEffectRenderer->SetVignettingEnabled(false);
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
    const uint32_t arenaStoneTextureId =
        texture->CreateArenaStoneTexture(1024, 1024);
    ApplyWeatheredMetalMaterials(model, playerModel, worldRustTextureId,
                                 {{0.46f, 0.44f, 0.40f, 0.42f},
                                  {0.28f, 0.27f, 0.25f, 0.42f},
                                  {0.40f, 0.22f, 0.14f, 0.42f}},
                                 0.24f, 0.16f, 0.62f);
    ApplyWeatheredMetalMaterials(model, swordModel, worldRustTextureId,
                                 {{1.34f, 1.44f, 1.58f, 1.0f},
                                  {0.34f, 0.78f, 1.0f, 1.0f},
                                  {0.04f, 0.08f, 0.12f, 1.0f}},
                                 0.82f, 0.74f, 0.22f);
    ApplyRustedRobotMaterials(model, enemyModel, enemyRustTextureId);
    ApplyWeatheredMetalMaterials(model, bulletModel, worldRustTextureId,
                                 {{0.95f, 0.72f, 0.36f, 1.0f},
                                  {0.70f, 0.78f, 0.76f, 1.0f},
                                  {0.48f, 0.24f, 0.12f, 1.0f}},
                                 0.76f, 0.58f, 0.26f);
    arenaNoiseTextureId_ = arenaStoneTextureId;
    arenaFloorModelId_ = model->CreatePlane(
        arenaNoiseTextureId_,
        MakeArenaMaterial({0.62f, 0.64f, 0.60f, 1.0f}, true, 0.06f, 0.82f));
    arenaLowPolyTerrainModelId_ = model->CreateLowPolyTerrain(
        arenaStoneTextureId,
        MakeArenaMaterial({0.36f, 0.40f, 0.42f, 1.0f}, true, 0.00f, 0.98f), 42,
        78.0f, 1.25f, 15.0f, 0x4107u);
    arenaCenterDiskModelId_ = model->CreateRing(
        arenaStoneTextureId,
        MakeArenaMaterial({0.34f, 0.36f, 0.34f, 1.0f}, true, 0.06f, 0.82f), 96,
        1.95f, 0.0f);
    arenaSpokeModelId_ = model->CreatePlane(
        arenaStoneTextureId,
        MakeArenaMaterial({0.31f, 0.33f, 0.32f, 1.0f}, true, 0.02f, 0.88f));
    arenaInnerRingModelId_ = model->CreateRing(
        arenaStoneTextureId,
        MakeArenaMaterial({0.38f, 0.44f, 0.40f, 1.0f}, true, 0.04f, 0.84f), 96,
        4.9f, 4.35f);
    arenaOuterRingModelId_ = model->CreateRing(
        arenaStoneTextureId,
        MakeArenaMaterial({0.36f, 0.34f, 0.31f, 1.0f}, true, 0.04f, 0.86f), 128,
        12.3f, 11.6f);
    arenaColumnModelId_ = model->CreateCylinder(
        arenaStoneTextureId,
        MakeArenaMaterial({0.42f, 0.44f, 0.43f, 1.0f}, true, 0.02f, 0.92f), 24,
        0.26f, 0.38f, 5.4f);
    arenaColumnCapModelId_ = model->CreateCylinder(
        arenaStoneTextureId,
        MakeArenaMaterial({0.34f, 0.34f, 0.31f, 1.0f}, true, 0.04f, 0.86f), 32,
        0.68f, 0.78f, 0.24f);
    arenaDomeModelId_ = model->CreateCylinder(
        arenaStoneTextureId,
        MakeArenaMaterial({0.30f, 0.36f, 0.40f, 0.42f}, true, 0.00f, 0.96f), 128,
        4.5f, 13.5f, 8.8f);
    arenaBarrierRingModelId_ = model->CreateRing(
        arenaStoneTextureId,
        MakeArenaMaterial({0.72f, 0.78f, 0.76f, 0.28f}, true, 0.00f, 0.92f), 128,
        13.1f, 12.9f);
    enemyFocusRingModelId_ = model->CreateRing(
        0, MakeArenaMaterial({0.92f, 0.98f, 1.0f, 0.72f}, false, 0.0f,
                             0.42f),
        96, 1.85f, 1.58f);
    enemyWeaponTrailModelId_ = model->CreatePlane(
        0, MakeArenaMaterial({1.0f, 0.92f, 0.62f, 0.82f}, false, 0.0f,
                             0.28f));
    chargeWeakPointModelId_ = model->CreatePlane(
        0, MakeArenaMaterial({1.0f, 0.96f, 0.78f, 0.92f}, false, 0.02f,
                             0.20f));
    sparkParticles_.Initialize(dx, ctx_->srv, texture, particleTextureId_, 8192);
    sparkParticles_.SetEmission(1, 1000.0f);
    sparkParticles_.SetEmitterRadius(0.08f);
    explosionParticles_.Initialize(dx, ctx_->srv, texture, particleTextureId_,
                                   8192);
    explosionParticles_.SetEmission(1, 1000.0f);
    explosionParticles_.SetEmitterRadius(0.25f);
    smokeParticles_.Initialize(dx, ctx_->srv, texture, particleTextureId_, 4096);
    smokeParticles_.SetEmission(1, 1000.0f);
    smokeParticles_.SetEmitterRadius(0.40f);
    dx->EndUpload();

    texture->ReleaseUploadBuffers();

    player_.Initialize(playerModel, swordModel, selectedWeaponType_);
    playerModelId_ = playerModel;
    enemy_.Initialize(enemyModel, bulletModel);
    enemyModelId_ = enemyModel;
    if (ctx_->sound != nullptr) {
        slashSoundId_ =
            ctx_->sound->Load(L"app/resources/sounds/slash_hero.wav");
        enemyReleaseSoundId_ =
            ctx_->sound->Load(L"app/resources/sounds/enemy_release_snap.wav");
        hitSoundId_ =
            ctx_->sound->Load(L"app/resources/sounds/hit_impact.wav");
        counterSoundId_ =
            ctx_->sound->Load(L"app/resources/sounds/counter_burst.wav");
        damageSoundId_ =
            ctx_->sound->Load(L"app/resources/sounds/damage_heavy.wav");
        explosionSoundId_ =
            ctx_->sound->Load(L"app/resources/sounds/explosion_boss.wav");
        soundsLoaded_ = true;
    }
    showJoyConTutorial_ = true;

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
    battleElapsedTime_ = 0.0f;
    battleResultRequested_ = false;
    victorySequenceActive_ = false;
    victorySequenceTimer_ = 0.0f;
    victoryClearTime_ = 0.0f;
    counterCinematicActive_ = false;
    counterCinematicTimer_ = 0.0f;
    enemyAnimationFrozen_ = false;
    chargeWeakPointActionKind_ = ActionKind::None;
    failedChargeWeakPointActionKind_ = ActionKind::None;
    chargeWeakPointBroken_ = false;
    chargeWeakPointFailedThisAction_ = false;
    enemyRedPunishUncounterable_ = false;
    chargeWeakPointSlashCount_ = 0;
    previousChargeWeakPointSlashStates_.fill(false);
    previousSwordSoundStates_.fill(false);
    hud_.Initialize(*ctx_);
}

void GameScene::Update() {
    Input *input = ctx_->input;
#ifdef _DEBUG
    if (input->IsKeyTrigger(DIK_F7)) {
        sceneManager_->ChangeScene(std::make_unique<EnemyAnimationDebugScene>());
        return;
    }
    if (input->IsKeyTrigger(DIK_F8) && !battleResultRequested_) {
        enemy_.TakeDamage(99999.0f);
    }
#endif
    if (input->IsKeyTrigger(DIK_F3)) {
        showCollisionDebug_ = !showCollisionDebug_;
    }
    if (input->IsKeyTrigger(DIK_F1)) {
        showJoyConTutorial_ = !showJoyConTutorial_;
    }

    const float baseDeltaTime = ctx_->deltaTime;
    if (victorySequenceActive_) {
        sceneLightTime_ += baseDeltaTime;
        combatFeedback_.Update(baseDeltaTime, sceneLightTime_);
        UpdateVictorySequence(baseDeltaTime);
        const float victoryPoseRatio = std::clamp(
            (victorySequenceTimer_ - 0.78f) / 2.35f, 0.0f, 1.0f);
        enemy_.ApplyVictoryDefeatPose(
            victoryPoseRatio, victoryEnemyStartPos_,
            player_.GetTransform().position);
        UpdateBattleCamera();
        camera_.UpdateMatrices();
        ctx_->model->UpdateAnimation(playerModelId_, baseDeltaTime * 0.08f);
        ctx_->model->UpdateAnimation(enemyModelId_, baseDeltaTime * 0.002f);
        ApplyEnemyProceduralAnimation();
        sparkParticles_.Update(baseDeltaTime);
        explosionParticles_.Update(baseDeltaTime);
        smokeParticles_.Update(baseDeltaTime);
        return;
    }

    combatFeedback_.Update(baseDeltaTime, sceneLightTime_);
    UpdateChargeWeakPointFocus(baseDeltaTime);
    const float gameplayDeltaTime = baseDeltaTime * ComputeGameplayTimeScale();
    const bool chargeFocusHoldingEnemyAttack =
        chargeWeakPointFocusRatio_ > 0.001f && IsChargeWeakPointFocusActive();
    const float playerDeltaTime =
        counterCinematicActive_ ? baseDeltaTime : gameplayDeltaTime;
    const float enemyDeltaTime =
        counterCinematicActive_
            ? (baseDeltaTime * counterTimeScale_)
            : (chargeFocusHoldingEnemyAttack ? baseDeltaTime
                                             : gameplayDeltaTime);
    UpdateCamera(input);

    ctx_->model->UpdateAnimation(playerModelId_, playerDeltaTime);

    bool forceRangedReflectMove = false;
    for (const auto &bullet : enemy_.GetBullets()) {
        if (bullet.isAlive && !bullet.isReflected) {
            forceRangedReflectMove = true;
            break;
        }
    }
    player_.Update(input, playerDeltaTime, enemy_.GetTransform().position,
                   cameraYaw_, forceRangedReflectMove, baseDeltaTime);
    if (soundsLoaded_ && ctx_->sound != nullptr) {
        const auto slashStates = player_.GetSwordSlashStates();
        for (size_t i = 0; i < slashStates.size(); ++i) {
            if (slashStates[i] && !previousSwordSoundStates_[i]) {
                ctx_->sound->Play(slashSoundId_);
            }
        }
        previousSwordSoundStates_ = slashStates;
    }
    hud_.Update(*ctx_, player_.GetHP(), enemy_.GetHP());
    sceneLightTime_ += baseDeltaTime;
    battleElapsedTime_ += baseDeltaTime;

    const ActionKind previousEnemyActionKind = enemy_.GetActionKind();
    const ActionStep previousEnemyActionStep = enemy_.GetActionStep();
    enemy_.Update(BuildPlayerCombatObservation(), enemyDeltaTime);
    const ActionKind currentEnemyActionKind = enemy_.GetActionKind();
    const ActionStep currentEnemyActionStep = enemy_.GetActionStep();
    if (currentEnemyActionKind != previousEnemyActionKind ||
        currentEnemyActionStep != previousEnemyActionStep) {
        EmitEnemyActionParticles(currentEnemyActionKind, currentEnemyActionStep);
        const bool isEnemyAttackRelease =
            (currentEnemyActionKind == ActionKind::Smash ||
             currentEnemyActionKind == ActionKind::Sweep ||
             currentEnemyActionKind == ActionKind::Shot ||
             currentEnemyActionKind == ActionKind::Wave ||
             currentEnemyActionKind == ActionKind::Nova) &&
            currentEnemyActionStep == ActionStep::Active;
        if (isEnemyAttackRelease) {
            if (soundsLoaded_ && ctx_->sound != nullptr) {
                ctx_->sound->Play(enemyReleaseSoundId_);
            }
        }
    }
    UpdateSceneLighting();

    SyncEnemyAnimation();
    SetEnemyAnimationFrozen(counterCinematicActive_);
    if (!enemyAnimationFrozen_) {
        float enemyAnimationDeltaTime = enemyDeltaTime;
        const ActionKind enemyActionKind = enemy_.GetActionKind();
        const ActionStep enemyActionStep = enemy_.GetActionStep();
        if (enemyActionKind == ActionKind::Smash ||
            enemyActionKind == ActionKind::Sweep ||
            enemyActionKind == ActionKind::Shot ||
            enemyActionKind == ActionKind::Wave ||
            enemyActionKind == ActionKind::Nova) {
            if (IsChargeStanceSettled(enemyActionKind, enemyActionStep,
                                      enemy_.GetActionTimerForPresentation())) {
                enemyAnimationDeltaTime *= 0.035f;
            } else if (enemyActionStep == ActionStep::Active) {
                enemyAnimationDeltaTime *= 4.6f;
            } else if (enemyActionStep == ActionStep::Recovery) {
                enemyAnimationDeltaTime *= 0.72f;
            }
        }
        ctx_->model->UpdateAnimation(enemyModelId_, enemyAnimationDeltaTime);
    }
    ApplyEnemyProceduralAnimation();

    UpdateBattleCamera();
    camera_.UpdateMatrices();

    UpdateCombat(gameplayDeltaTime);
    if (!battleResultRequested_) {
        if (enemy_.GetHP() <= 0.0f) {
            BeginVictorySequence();
            return;
        }
        if (player_.GetHP() <= 0.0f) {
            battleResultRequested_ = true;
            sceneManager_->ChangeScene(std::make_unique<BattleResultScene>(
                BattleResultScene::ResultKind::GameOver, battleElapsedTime_,
                selectedWeaponType_));
            return;
        }
    }
    if (counterCinematicActive_) {
        counterCinematicTimer_ -= baseDeltaTime;
        if (counterCinematicTimer_ <= 0.0f) {
            counterCinematicTimer_ = 0.0f;
            counterCinematicActive_ = false;
            SetEnemyAnimationFrozen(false);
        }
    }
    EmitEnemyCueParticles(baseDeltaTime);
    sparkParticles_.Update(baseDeltaTime);
    explosionParticles_.Update(baseDeltaTime);
    smokeParticles_.Update(baseDeltaTime);
}

void GameScene::Draw() {
    ctx_->model->PreDraw();

    DrawArena();
    player_.Draw(ctx_->model, camera_, !playerViewCamera_);
    if (!(victorySequenceActive_ && victoryFinalExplosionEmitted_)) {
        enemy_.Draw(ctx_->model, camera_);
    }
    if (showCollisionDebug_) {
        collisionDebugRenderer_.Draw(collisionManager_, camera_);
    }
    ctx_->model->PostDraw();

    smokeParticles_.Draw(camera_);
    sparkParticles_.Draw(camera_);
    explosionParticles_.Draw(camera_);
    DrawVictoryFlash();
}

void GameScene::DispatchCombatFeedback(const CombatFeedbackEvent &event) {
    combatFeedback_.PushEvent(event);
    EmitCombatParticles(event);
    if (!soundsLoaded_ || ctx_ == nullptr || ctx_->sound == nullptr) {
        return;
    }

    switch (event.type) {
    case CombatFeedbackEventType::CounterSuccess:
        ctx_->sound->Play(counterSoundId_);
        break;
    case CombatFeedbackEventType::PlayerSlashHit:
        ctx_->sound->Play(hitSoundId_);
        break;
    case CombatFeedbackEventType::PlayerDamaged:
        ctx_->sound->Play(damageSoundId_);
        break;
    case CombatFeedbackEventType::ProjectileReflect:
        ctx_->sound->Play(counterSoundId_);
        break;
    case CombatFeedbackEventType::PlayerGuard:
    default:
        break;
    }
}

void GameScene::EmitCombatParticles(const CombatFeedbackEvent &event) {
    const XMFLOAT3 dir = event.direction;
    switch (event.type) {
    case CombatFeedbackEventType::CounterSuccess:
        explosionParticles_.EmitBurst(
            event.position, 84, 0.95f, GPUParticleSystem::BurstStyle::Explosion,
            {1.0f, 0.84f, 0.22f, 1.0f}, dir, 3.0f);
        sparkParticles_.EmitBurst(event.position, 120, 0.72f,
                                  GPUParticleSystem::BurstStyle::Sparks,
                                  {1.0f, 0.94f, 0.42f, 1.0f}, dir, 4.4f);
        smokeParticles_.EmitBurst(event.position, 36, 1.10f,
                                  GPUParticleSystem::BurstStyle::Smoke,
                                  {0.46f, 0.44f, 0.38f, 0.82f}, dir, 0.9f);
        break;
    case CombatFeedbackEventType::PlayerSlashHit:
        sparkParticles_.EmitBurst(event.position, 34, 0.42f,
                                  GPUParticleSystem::BurstStyle::Sparks,
                                  {0.90f, 1.0f, 1.0f, 1.0f}, dir, 2.8f);
        break;
    case CombatFeedbackEventType::PlayerDamaged:
        explosionParticles_.EmitBurst(
            event.position, 44, 0.55f, GPUParticleSystem::BurstStyle::Explosion,
            {1.0f, 0.24f, 0.06f, 0.90f}, dir, 2.2f);
        break;
    case CombatFeedbackEventType::ProjectileReflect:
        sparkParticles_.EmitBurst(event.position, 64, 0.52f,
                                  GPUParticleSystem::BurstStyle::Sparks,
                                  {0.42f, 0.92f, 1.0f, 1.0f}, dir, 3.6f);
        break;
    default:
        break;
    }
}

void GameScene::EmitEnemyActionParticles(ActionKind kind, ActionStep step) {
    const XMFLOAT3 enemyPos = enemy_.GetTransform().position;
    const float yaw = enemy_.GetTelegraphYaw();
    const XMFLOAT3 forward = {std::sinf(yaw), 0.20f, std::cosf(yaw)};

    XMFLOAT3 origin = enemyPos;
    origin.y += 1.05f;

    if (step == ActionStep::Charge || step == ActionStep::Hold) {
        return;
    } else if (step == ActionStep::Active) {
        origin.x += forward.x * 1.10f;
        origin.z += forward.z * 1.10f;
        switch (kind) {
        case ActionKind::Smash:
            sparkParticles_.EmitBurst(origin, 128, 1.18f,
                                      GPUParticleSystem::BurstStyle::Sparks,
                                      {1.0f, 0.96f, 0.58f, 1.0f}, forward,
                                      4.8f);
            explosionParticles_.EmitBurst(
                origin, 46, 0.72f, GPUParticleSystem::BurstStyle::Explosion,
                {1.0f, 0.42f, 0.08f, 0.82f}, forward, 2.4f);
            break;
        case ActionKind::Sweep:
            sparkParticles_.EmitBurst(origin, 144, 1.22f,
                                      GPUParticleSystem::BurstStyle::Sparks,
                                      {0.72f, 0.98f, 1.0f, 1.0f}, forward,
                                      4.8f);
            break;
        case ActionKind::Shot:
            sparkParticles_.EmitBurst(origin, 76, 0.72f,
                                      GPUParticleSystem::BurstStyle::Sparks,
                                      {0.42f, 0.96f, 1.0f, 1.0f}, forward,
                                      4.2f);
            explosionParticles_.EmitBurst(
                origin, 38, 0.52f, GPUParticleSystem::BurstStyle::Explosion,
                {0.18f, 0.72f, 1.0f, 0.86f}, forward, 2.0f);
            break;
        case ActionKind::Wave:
            sparkParticles_.EmitBurst(origin, 92, 0.95f,
                                      GPUParticleSystem::BurstStyle::Sparks,
                                      {0.34f, 1.0f, 0.58f, 0.98f}, forward,
                                      3.6f);
            explosionParticles_.EmitBurst(
                origin, 62, 1.15f, GPUParticleSystem::BurstStyle::Explosion,
                {0.32f, 0.96f, 0.48f, 0.88f}, forward, 2.2f);
            break;
        case ActionKind::Nova:
            explosionParticles_.EmitBurst(
                origin, 80, 1.35f, GPUParticleSystem::BurstStyle::Explosion,
                {1.0f, 0.72f, 0.18f, 0.90f}, forward, 2.4f);
            break;
        default:
            break;
        }
    }
}

void GameScene::EmitEnemyCueParticles(float deltaTime) {
    enemyCueParticleTimer_ =
        (std::max)(0.0f, enemyCueParticleTimer_ - deltaTime);
    enemyWeakPointParticleTimer_ =
        (std::max)(0.0f, enemyWeakPointParticleTimer_ - deltaTime);
    enemySwordParticleTimer_ =
        (std::max)(0.0f, enemySwordParticleTimer_ - deltaTime);

    if (ctx_ == nullptr) {
        return;
    }

    const ActionKind kind = enemy_.GetActionKind();
    const ActionStep actionStep = enemy_.GetActionStep();
    const XMFLOAT3 enemyPos = enemy_.GetTransform().position;
    const bool chargeDirectionVisible =
        !chargeWeakPointBroken_ &&
        enemy_.GetChargeWeakPointTimeLimitForPresentation() > 0.0f;

    if (chargeDirectionVisible && enemyWeakPointParticleTimer_ <= 0.0f) {
        const size_t directionIndex = static_cast<size_t>(std::clamp(
            chargeWeakPointSlashCount_, 0,
            static_cast<int>(chargeWeakPointRequiredDirections_.size() - 1)));
        const XMFLOAT2 slashDir =
            chargeWeakPointRequiredDirections_[directionIndex];
        XMFLOAT3 cuePos = enemyPos;
        const XMFLOAT3 cameraPos = camera_.GetPosition();
        float toCameraX = cameraPos.x - enemyPos.x;
        float toCameraZ = cameraPos.z - enemyPos.z;
        float toCameraLen =
            std::sqrt(toCameraX * toCameraX + toCameraZ * toCameraZ);
        if (toCameraLen < 0.0001f) {
            toCameraLen = 1.0f;
        }
        cuePos.x += (toCameraX / toCameraLen) * 0.84f;
        cuePos.y += 1.64f;
        cuePos.z += (toCameraZ / toCameraLen) * 0.84f;

        const XMFLOAT4 cueColor{0.22f, 1.0f, 0.34f, 1.0f};
        sparkParticles_.EmitBurst(cuePos, 178, 1.50f,
                                  GPUParticleSystem::BurstStyle::SlashLine,
                                  cueColor, {slashDir.x, slashDir.y, 0.0f},
                                  0.86f);
        enemyWeakPointParticleTimer_ = 0.050f;
    }

    constexpr float kReleaseCounterWindowDuration = 0.62f;
    const float releaseAnticipation = enemy_.GetReleaseAnticipationRatio();
    const bool dualCounterCueVisible =
        kind == ActionKind::Shot && enemy_.IsDualCounterWindow();
    const bool preReleaseCounterCueVisible =
        !chargeDirectionVisible && actionStep != ActionStep::Active &&
        releaseAnticipation > 0.0f;
    const bool activeReleaseCounterCueVisible =
        actionStep == ActionStep::Active &&
        enemy_.GetActionTimerForPresentation() <=
            kReleaseCounterWindowDuration;
    const bool releaseCounterCueVisible =
        dualCounterCueVisible ||
        (!chargeWeakPointFailedThisAction_ &&
         (kind == ActionKind::Smash || kind == ActionKind::Sweep) &&
         (preReleaseCounterCueVisible || activeReleaseCounterCueVisible));
    const bool badSlashCueVisible =
        (kind == ActionKind::Smash || kind == ActionKind::Sweep ||
         kind == ActionKind::Shot) &&
        !chargeDirectionVisible && !releaseCounterCueVisible &&
        (actionStep == ActionStep::Charge || actionStep == ActionStep::Hold ||
         actionStep == ActionStep::Active);

    if (dualCounterCueVisible && enemyCueParticleTimer_ <= 0.0f) {
        const float yaw = enemy_.GetTelegraphYaw();
        const XMFLOAT3 forward = {std::sinf(yaw), 0.12f, std::cosf(yaw)};
        auto emitDualCue = [&](const XMFLOAT3 &basePos,
                               const XMFLOAT4 &sparkColor,
                               const XMFLOAT4 &flashColor,
                               SwordCounterAxis axis) {
            XMFLOAT3 cuePos = basePos;
            cuePos.y += 0.42f;
            sparkParticles_.EmitBurst(cuePos, 112, 0.90f,
                                      GPUParticleSystem::BurstStyle::Sparks,
                                      sparkColor, forward, 4.15f);
            sparkParticles_.EmitBurst(
                cuePos, 124, 1.20f, GPUParticleSystem::BurstStyle::SlashLine,
                sparkColor, CounterAxisParticleDirection(axis), 0.72f);
            smokeParticles_.EmitBurst(cuePos, 7, 0.82f,
                                      GPUParticleSystem::BurstStyle::Flash,
                                      flashColor, forward, 0.58f);
        };

        const bool handIsGreen = enemy_.IsDualCounterHandStage();
        const SwordCounterAxis greenAxis =
            RequiredVisualDualCounterAxis(handIsGreen);
        const SwordCounterAxis redAxis =
            RequiredVisualDualCounterAxis(!handIsGreen);
        const XMFLOAT3 greenPos =
            handIsGreen ? enemy_.GetLeftHandTransform().position
                        : enemy_.GetRightHandTransform().position;
        const XMFLOAT3 redPos =
            handIsGreen ? enemy_.GetRightHandTransform().position
                        : enemy_.GetLeftHandTransform().position;
        emitDualCue(greenPos, {0.22f, 1.0f, 0.34f, 0.92f},
                    {0.18f, 1.0f, 0.28f, 0.78f}, greenAxis);
        emitDualCue(redPos, {1.0f, 0.08f, 0.04f, 0.92f},
                    {1.0f, 0.05f, 0.03f, 0.78f}, redAxis);
        enemyCueParticleTimer_ = 0.070f;
    } else if ((releaseCounterCueVisible || badSlashCueVisible) &&
        enemyCueParticleTimer_ <= 0.0f) {
        const float yaw = enemy_.GetTelegraphYaw();
        const XMFLOAT3 forward = {std::sinf(yaw), 0.12f, std::cosf(yaw)};
        XMFLOAT3 cuePos = enemyPos;
        cuePos.x += forward.x * 1.18f;
        cuePos.y += 1.28f;
        cuePos.z += forward.z * 1.18f;
        if (kind == ActionKind::Shot) {
            cuePos = enemy_.IsDualCounterHandStage()
                         ? enemy_.GetLeftHandTransform().position
                         : enemy_.GetRightHandTransform().position;
            cuePos.y += 0.42f;
        }
        const XMFLOAT4 cueColor =
            releaseCounterCueVisible ? XMFLOAT4{0.22f, 1.0f, 0.34f, 0.92f}
                                     : XMFLOAT4{1.0f, 0.08f, 0.04f, 0.92f};
        const float cuePower =
            dualCounterCueVisible
                ? 1.0f
                : activeReleaseCounterCueVisible
                ? 1.0f
                : std::clamp(0.64f + releaseAnticipation * 0.28f, 0.64f,
                             0.92f);
        const uint32_t sparkCount =
            releaseCounterCueVisible
                ? ((activeReleaseCounterCueVisible || dualCounterCueVisible)
                       ? 112u
                       : 76u)
                : 88u;
        const float sparkRadius =
            releaseCounterCueVisible ? 0.90f * cuePower : 0.98f;
        const float sparkSpeed =
            releaseCounterCueVisible ? 4.15f * cuePower : 4.35f;
        sparkParticles_.EmitBurst(cuePos, sparkCount, sparkRadius,
                                  GPUParticleSystem::BurstStyle::Sparks,
                                  cueColor, forward, sparkSpeed);
        const bool showCounterAxisLine =
            kind == ActionKind::Smash || kind == ActionKind::Sweep ||
            kind == ActionKind::Shot;
        if ((releaseCounterCueVisible || badSlashCueVisible) &&
            showCounterAxisLine) {
            const SwordCounterAxis cueAxis =
                kind == ActionKind::Shot
                    ? RequiredVisualDualCounterAxis(
                          enemy_.IsDualCounterHandStage())
                    : RequiredVisualCounterAxisForAction(kind);
            sparkParticles_.EmitBurst(
                cuePos, releaseCounterCueVisible ? 132u : 116u,
                releaseCounterCueVisible ? 1.22f : 1.12f,
                GPUParticleSystem::BurstStyle::SlashLine, cueColor,
                CounterAxisParticleDirection(cueAxis), 0.74f);
        }
        const XMFLOAT4 flashColor =
            releaseCounterCueVisible ? XMFLOAT4{0.18f, 1.0f, 0.28f, 0.78f}
                                     : XMFLOAT4{1.0f, 0.05f, 0.03f, 0.78f};
        smokeParticles_.EmitBurst(cuePos,
                                  releaseCounterCueVisible
                                      ? ((activeReleaseCounterCueVisible ||
                                          dualCounterCueVisible)
                                             ? 7
                                             : 4)
                                      : 5,
                                  releaseCounterCueVisible
                                      ? ((activeReleaseCounterCueVisible ||
                                          dualCounterCueVisible)
                                             ? 0.82f
                                             : 0.62f)
                                      : 0.70f,
                                  GPUParticleSystem::BurstStyle::Flash,
                                  flashColor, forward, 0.58f);
        enemyCueParticleTimer_ = releaseCounterCueVisible ? 0.070f : 0.085f;
    }

    if (enemySwordParticleTimer_ > 0.0f || ctx_->model == nullptr) {
        return;
    }

    XMFLOAT3 bladeRoot{};
    XMFLOAT3 bladeTip{};
    const XMFLOAT3 enemyBodyPos = enemy_.GetBodyTransform().position;
    if (Model *enemyModel = ctx_->model->GetModel(enemyModelId_)) {
        if (TryGetEnemySwordMeshSegment(*enemyModel, enemy_.GetVisualTransform(),
                                        enemyBodyPos, bladeRoot, bladeTip)) {
            const XMFLOAT3 bladeCenter = Lerp(bladeRoot, bladeTip, 0.56f);
            float dirX = bladeTip.x - bladeRoot.x;
            float dirY = bladeTip.y - bladeRoot.y;
            const float dirLen = std::sqrt(dirX * dirX + dirY * dirY);
            if (dirLen > 0.0001f) {
                dirX /= dirLen;
                dirY /= dirLen;
            } else {
                dirX = 1.0f;
                dirY = 0.0f;
            }

            const bool attackActive = actionStep == ActionStep::Active;
            const uint32_t count = attackActive ? 76u : 42u;
            const float radius = attackActive ? 0.78f : 0.52f;
            const float speed = attackActive ? 0.62f : 0.34f;
            const XMFLOAT4 bladeColor =
                kind == ActionKind::Sweep
                    ? XMFLOAT4{0.34f, 0.92f, 1.0f, 0.90f}
                    : XMFLOAT4{1.0f, 0.76f, 0.20f, 0.90f};
            explosionParticles_.EmitBurst(
                bladeCenter, count, radius,
                GPUParticleSystem::BurstStyle::SlashLine, bladeColor,
                {dirX, dirY, 0.0f}, speed);
            enemySwordParticleTimer_ = attackActive ? 0.024f : 0.040f;
        }
    }
}

bool GameScene::IsChargeWeakPointFocusActive() const {
    return !chargeWeakPointBroken_ &&
           enemy_.GetChargeWeakPointTimeLimitForPresentation() > 0.0f;
}

void GameScene::UpdateChargeWeakPointFocus(float deltaTime) {
    const float target = IsChargeWeakPointFocusActive() ? 1.0f : 0.0f;
    const float speed = target > chargeWeakPointFocusRatio_
                            ? chargeWeakPointFocusInSpeed_
                            : chargeWeakPointFocusOutSpeed_;
    const float alpha = std::clamp(speed * deltaTime, 0.0f, 1.0f);
    chargeWeakPointFocusRatio_ +=
        (target - chargeWeakPointFocusRatio_) * alpha;

    if (ctx_ == nullptr || ctx_->postEffectRenderer == nullptr) {
        return;
    }

    const float focus = chargeWeakPointFocusRatio_;
    if (focus <= 0.001f) {
        ctx_->postEffectRenderer->SetSceneDimStrength(0.0f);
        return;
    }

    ctx_->postEffectRenderer->SetRadialBlurCenter(0.5f, 0.48f);
    ctx_->postEffectRenderer->SetRadialBlurSampleCount(20);
    ctx_->postEffectRenderer->SetRadialBlurStrength(0.030f * focus);
    ctx_->postEffectRenderer->SetVignettingStrength(0.20f + 0.72f * focus);
    ctx_->postEffectRenderer->SetSceneDimStrength(0.42f * focus);
}

void GameScene::DrawOverlay() {
    hud_.Draw(*ctx_);
    DrawChargeWeakPointTimeGauge();
    DrawJoyConTutorial();
}

void GameScene::BeginVictorySequence() {
    battleResultRequested_ = true;
    victorySequenceActive_ = true;
    victorySequenceTimer_ = 0.0f;
    victoryClearTime_ = battleElapsedTime_;
    victoryFinalExplosionEmitted_ = false;
    victoryEnemyStartPos_ = enemy_.GetTransform().position;
    counterCinematicActive_ = false;
    SetEnemyAnimationFrozen(false);

    if (ctx_ != nullptr && ctx_->postEffectRenderer != nullptr) {
        ctx_->postEffectRenderer->SetVignettingEnabled(true);
        ctx_->postEffectRenderer->SetVignettingStrength(0.58f);
        ctx_->postEffectRenderer->SetRadialBlurCenter(0.5f, 0.48f);
        ctx_->postEffectRenderer->SetRadialBlurSampleCount(24);
        ctx_->postEffectRenderer->SetRadialBlurStrength(0.055f);
        ctx_->postEffectRenderer->SetSceneDimStrength(0.10f);
    }

    const XMFLOAT3 enemyPos = enemy_.GetTransform().position;
    sparkParticles_.EmitBurst(
        {enemyPos.x, enemyPos.y + 1.45f, enemyPos.z}, 220, 1.85f,
        GPUParticleSystem::BurstStyle::Flash, {1.0f, 0.96f, 0.82f, 0.95f},
        {0.0f, 1.0f, 0.0f}, 0.72f);
    explosionParticles_.EmitBurst(
        {enemyPos.x, enemyPos.y + 1.25f, enemyPos.z}, 96, 1.35f,
        GPUParticleSystem::BurstStyle::Explosion,
        {1.0f, 0.84f, 0.32f, 0.72f}, {0.0f, 1.0f, 0.0f}, 1.15f);
}

void GameScene::UpdateVictorySequence(float deltaTime) {
    victorySequenceTimer_ += deltaTime;
    const float ratio =
        std::clamp(victorySequenceTimer_ / victorySequenceDuration_, 0.0f,
                   1.0f);

    if (ctx_ != nullptr && ctx_->postEffectRenderer != nullptr) {
        const float stepped = std::floor(ratio * 14.0f) / 14.0f;
        const float blur = (1.0f - stepped) * 0.070f;
        ctx_->postEffectRenderer->SetRadialBlurStrength(blur);
        ctx_->postEffectRenderer->SetSceneDimStrength(0.10f + stepped * 0.18f);
    }

    if (!victoryFinalExplosionEmitted_ && victorySequenceTimer_ >= 3.90f) {
        victoryFinalExplosionEmitted_ = true;
        const XMFLOAT3 enemyPos = enemy_.GetTransform().position;
        if (soundsLoaded_ && ctx_ != nullptr && ctx_->sound != nullptr) {
            ctx_->sound->Play(explosionSoundId_);
        }
        explosionParticles_.EmitBurst(
            {enemyPos.x, enemyPos.y + 1.05f, enemyPos.z}, 3200, 8.40f,
            GPUParticleSystem::BurstStyle::Explosion,
            {1.0f, 0.64f, 0.08f, 1.0f}, {0.0f, 1.0f, 0.0f}, 5.80f);
        sparkParticles_.EmitBurst(
            {enemyPos.x, enemyPos.y + 1.22f, enemyPos.z}, 3600, 9.20f,
            GPUParticleSystem::BurstStyle::Sparks,
            {1.0f, 0.98f, 0.58f, 1.0f}, {0.0f, 1.0f, 0.0f}, 12.4f);
        smokeParticles_.EmitBurst(
            {enemyPos.x, enemyPos.y + 1.12f, enemyPos.z}, 680, 6.80f,
            GPUParticleSystem::BurstStyle::Flash,
            {1.0f, 0.94f, 0.70f, 1.0f}, {0.0f, 1.0f, 0.0f}, 1.55f);
    }

    if (victorySequenceTimer_ >= victorySequenceDuration_) {
        victorySequenceActive_ = false;
        if (ctx_ != nullptr && ctx_->postEffectRenderer != nullptr) {
            ctx_->postEffectRenderer->SetRadialBlurStrength(0.0f);
            ctx_->postEffectRenderer->SetSceneDimStrength(0.0f);
        }
        sceneManager_->ChangeScene(std::make_unique<BattleResultScene>(
            BattleResultScene::ResultKind::Clear, victoryClearTime_,
            selectedWeaponType_));
    }
}

void GameScene::DrawVictoryFlash() {
    if (!victorySequenceActive_ || ctx_ == nullptr || ctx_->sprite == nullptr ||
        ctx_->winApp == nullptr) {
        return;
    }

    const float introPulse =
        victorySequenceTimer_ < 0.82f
            ? 0.56f + 0.44f * std::sin(victorySequenceTimer_ * 12.0f)
            : 0.0f;
    const float earlyFlash =
        (std::max)(0.0f, 1.0f - victorySequenceTimer_ / 0.88f);
    const auto flashPulse = [&](float center, float width, float peak) {
        return (std::max)(
            0.0f, (1.0f - std::fabs(victorySequenceTimer_ - center) / width) *
                      peak);
    };
    const float fallFlash =
        (std::max)(flashPulse(1.35f, 0.20f, 0.70f),
                   flashPulse(2.25f, 0.22f, 0.80f));
    const float preExplosionFlash = flashPulse(3.58f, 0.42f, 1.0f);
    const float blink =
        (std::max)(introPulse * introPulse,
                   (std::max)(fallFlash, preExplosionFlash));
    const float finalFlash =
        victoryFinalExplosionEmitted_
            ? (std::max)(
                  0.0f,
                  1.0f - (victorySequenceTimer_ - 3.90f) / 0.52f)
            : 0.0f;
    const float alpha =
        std::clamp((std::max)(earlyFlash, (std::max)(blink, finalFlash)),
                   0.0f, 1.0f);
    if (alpha <= 0.01f) {
        return;
    }

    Sprite flash{};
    flash.textureId = 0;
    flash.position = {0.0f, 0.0f};
    flash.size = {static_cast<float>(ctx_->winApp->GetWidth()),
                  static_cast<float>(ctx_->winApp->GetHeight())};
    flash.color = {1.0f, 1.0f, 1.0f, alpha};

    ctx_->sprite->PreDraw();
    ctx_->sprite->DrawSprite(flash);
    ctx_->sprite->PostDraw();
}

void GameScene::DrawChargeWeakPointTimeGauge() {
#ifndef IMGUI_DISABLED
    const float limit = enemy_.GetChargeWeakPointTimeLimitForPresentation();
    if (limit <= 0.0001f || chargeWeakPointBroken_) {
        return;
    }

    const float remaining =
        enemy_.GetChargeWeakPointTimeRemainingForPresentation();
    const float ratio = std::clamp(remaining / limit, 0.0f, 1.0f);
    const float focus = std::clamp(chargeWeakPointFocusRatio_, 0.0f, 1.0f);
    const float alpha = std::clamp(0.16f + focus * 0.34f, 0.0f, 0.50f);

    const float screenW = static_cast<float>(ctx_->winApp->GetWidth());
    const float screenH = static_cast<float>(ctx_->winApp->GetHeight());
    const ImVec2 center(screenW * 0.5f, screenH * 0.47f);
    const float radius = std::clamp(screenH * 0.34f, 190.0f, 290.0f);
    const float thickness = std::clamp(radius * 0.075f, 14.0f, 22.0f);

    ImDrawList *drawList = ImGui::GetForegroundDrawList();
    drawList->AddCircleFilled(
        center, radius + thickness * 0.72f,
        ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.08f * alpha)), 96);
    drawList->AddCircle(
        center, radius + thickness * 0.40f,
        ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.14f * alpha)), 96,
        2.0f);
    drawList->AddCircle(
        center, radius,
        ImGui::GetColorU32(ImVec4(0.06f, 0.07f, 0.08f, 0.28f * alpha)), 96,
        thickness);

    constexpr int kSegments = 96;
    const int litSegments =
        static_cast<int>(std::ceil(static_cast<float>(kSegments) * ratio));
    const ImU32 progressColor =
        ratio < 0.28f
            ? ImGui::GetColorU32(ImVec4(1.0f, 0.18f, 0.08f, 0.62f * alpha))
            : ImGui::GetColorU32(ImVec4(0.20f, 0.92f, 1.0f, 0.58f * alpha));
    const float startAngle = -kPi * 0.5f;
    const float elapsedAngle = (1.0f - ratio) * kPi * 2.0f;
    const float progressStartAngle = startAngle + elapsedAngle;
    ImVec2 prev(center.x + std::cos(progressStartAngle) * radius,
                center.y + std::sin(progressStartAngle) * radius);
    for (int i = 1; i <= litSegments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kSegments);
        const float angle = progressStartAngle + kPi * 2.0f * t;
        const ImVec2 current(center.x + std::cos(angle) * radius,
                             center.y + std::sin(angle) * radius);
        drawList->AddLine(prev, current, progressColor, thickness);
        prev = current;
    }
#endif
}

void GameScene::DrawJoyConTutorial() {
#ifndef IMGUI_DISABLED
    if (!showJoyConTutorial_) {
        return;
    }

    const ActionKind kind = enemy_.GetActionKind();
    const ActionStep step = enemy_.GetActionStep();
    constexpr float kReleaseCounterWindowDuration = 0.62f;
    const bool chargeDirectionVisible =
        !chargeWeakPointBroken_ &&
        enemy_.GetChargeWeakPointTimeLimitForPresentation() > 0.0f;
    const float releaseAnticipation = enemy_.GetReleaseAnticipationRatio();
    const bool preReleaseCounterCueVisible =
        !chargeDirectionVisible && step != ActionStep::Active &&
        releaseAnticipation > 0.0f;
    const bool activeReleaseCounterCueVisible =
        step == ActionStep::Active &&
        enemy_.GetActionTimerForPresentation() <=
            kReleaseCounterWindowDuration;
    const bool counterCueVisible =
        !chargeWeakPointFailedThisAction_ &&
        (kind == ActionKind::Smash || kind == ActionKind::Sweep) &&
        (preReleaseCounterCueVisible || activeReleaseCounterCueVisible);
    const bool redCueVisible =
        (kind == ActionKind::Smash || kind == ActionKind::Sweep ||
         kind == ActionKind::Shot) &&
        !chargeDirectionVisible && !counterCueVisible &&
        (step == ActionStep::Charge || step == ActionStep::Hold ||
         step == ActionStep::Active);
    const bool joyConMode = player_.UsesJoyConControls();

    auto axisText = [](SwordCounterAxis axis) {
        switch (axis) {
        case SwordCounterAxis::Vertical:
            return "VERTICAL slash";
        case SwordCounterAxis::Horizontal:
            return "HORIZONTAL slash";
        default:
            return "slash";
        }
    };
    auto coloredLine = [](const ImVec4 &color, const char *label,
                          const char *text) {
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextWrapped("%s", text);
    };

    const char *mainTitle = "WATCH THE BOSS";
    const char *mainText = "Stay ready. Do not swing until a clear cue appears.";
    ImVec4 mainColor{0.90f, 0.92f, 0.96f, 1.0f};
    if (chargeDirectionVisible) {
        mainTitle = "FLOW LINE";
        mainText = "Slash along the GREEN line. Same angle, either direction is OK.";
        mainColor = {0.24f, 1.0f, 0.36f, 1.0f};
    } else if (counterCueVisible) {
        mainTitle = "COUNTER NOW";
        const SwordCounterAxis axis = RequiredVisualCounterAxisForAction(kind);
        mainText = axisText(axis);
        mainColor = {0.24f, 1.0f, 0.36f, 1.0f};
    } else if (redCueVisible) {
        mainTitle = "DO NOT SLASH";
        mainText = "RED means danger. Swinging now triggers an uncounterable hit.";
        mainColor = {1.0f, 0.18f, 0.10f, 1.0f};
    } else if (counterCinematicActive_) {
        mainTitle = "PARRY HIT";
        mainText = "Time is slow. Make one clean follow-up slash.";
        mainColor = {0.36f, 0.88f, 1.0f, 1.0f};
    }

    ImGui::SetNextWindowPos(ImVec2(22.0f, 92.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(520.0f, 378.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.84f);
    ImGui::Begin("Battle Tutorial", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

    ImGui::Text("BATTLE TUTORIAL  (%s)", joyConMode ? "JOY-CON" : "MOUSE");
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Text, mainColor);
    ImGui::TextUnformatted(mainTitle);
    ImGui::PopStyleColor();
    ImGui::TextWrapped("%s", mainText);
    ImGui::Spacing();

    ImGui::TextUnformatted("CUES");
    coloredLine({0.24f, 1.0f, 0.36f, 1.0f}, "GREEN",
                "slash now. Match the line direction for counters.");
    coloredLine({1.0f, 0.18f, 0.10f, 1.0f}, "RED",
                "do not slash. Wait for the boss to commit.");
    coloredLine({0.36f, 0.88f, 1.0f, 1.0f}, "SLOW",
                "after a parry, take the free follow-up hit.");
    ImGui::Spacing();

    ImGui::TextUnformatted("DIRECTION RULE");
    ImGui::BulletText("Vertical boss slash / vertical line: vertical slash.");
    ImGui::BulletText("Horizontal boss slash / horizontal line: horizontal slash.");
    ImGui::BulletText("Opposite direction on the same line still counts.");
    ImGui::Spacing();

    ImGui::TextUnformatted("CONTROLS");
    if (joyConMode) {
        ImGui::BulletText("Swing Joy-Con: slash.");
        ImGui::BulletText("C: calibrate sensors.");
        ImGui::BulletText("ZR: reset right Joy-Con pose.");
        ImGui::BulletText("R: reset both Joy-Con poses.");
        ImGui::BulletText("S / stick click: dodge.");
    } else {
        ImGui::BulletText("Hold Left Mouse Button and move: slash.");
        ImGui::BulletText("Release Left Mouse Button: sword returns front.");
        ImGui::BulletText("Space: dodge.");
    }
    ImGui::Separator();
    ImGui::Text("F1: hide / show tutorial");

    ImGui::End();
#endif
}

void GameScene::DrawEnemyWeaponTrail() {
    if (enemyWeaponTrailModelId_ == 0) {
        return;
    }

    const ActionKind kind = enemy_.GetActionKind();
    const ActionStep step = enemy_.GetActionStep();
    const bool isTrailStep = step == ActionStep::Active;
    const bool shouldRecordCurrent =
        (kind == ActionKind::Smash || kind == ActionKind::Sweep) && isTrailStep;

    float drawDelta = sceneLightTime_ - enemyWeaponTrailLastDrawTime_;
    enemyWeaponTrailLastDrawTime_ = sceneLightTime_;
    drawDelta = std::clamp(drawDelta, 0.0f, 0.05f);

    constexpr float kGhostLifetime = 0.22f;
    bool hasGhost = false;
    for (EnemyWeaponTrailSample &sample : enemyWeaponTrailSamples_) {
        if (!sample.active) {
            continue;
        }
        sample.age += drawDelta;
        if (sample.age >= kGhostLifetime) {
            sample.active = false;
        } else {
            hasGhost = true;
        }
    }

    if (!shouldRecordCurrent && !hasGhost) {
        return;
    }

    const XMFLOAT3 cameraPos = camera_.GetPosition();

    auto makeTrailGeometry = [&](const XMFLOAT3 &root, const XMFLOAT3 &tip,
                                 ActionKind sampleKind, float thickness,
                                 float extraLength, XMFLOAT3 &outCenter,
                                 float &outYaw, float &outRoll,
                                 float &outLength,
                                 float &outThickness) {
        outCenter = Lerp(root, tip, 0.55f);
        float toCameraX = cameraPos.x - outCenter.x;
        float toCameraZ = cameraPos.z - outCenter.z;
        float toCameraLen =
            std::sqrt(toCameraX * toCameraX + toCameraZ * toCameraZ);
        if (toCameraLen < 0.0001f) {
            toCameraLen = 1.0f;
        }
        toCameraX /= toCameraLen;
        toCameraZ /= toCameraLen;
        outCenter.x += toCameraX * 0.10f;
        outCenter.z += toCameraZ * 0.10f;

        outYaw = BillboardYawToCamera(outCenter, cameraPos);
        const float cameraRightX = std::cosf(outYaw);
        const float cameraRightZ = -std::sinf(outYaw);
        const float bladeX =
            (tip.x - root.x) * cameraRightX + (tip.z - root.z) * cameraRightZ;
        const float bladeY = tip.y - root.y;
        const float projectedLen =
            std::sqrt(bladeX * bladeX + bladeY * bladeY);
        const float fallbackRoll =
            sampleKind == ActionKind::Smash ? (kPi * 0.5f) : 0.0f;
        outRoll =
            projectedLen > 0.24f ? std::atan2f(bladeY, bladeX) : fallbackRoll;
        const float baseLength =
            sampleKind == ActionKind::Smash ? 2.25f : 2.85f;
        const float maxLength =
            sampleKind == ActionKind::Smash ? 4.15f : 4.55f;
        outLength = std::clamp((std::max)(baseLength, projectedLen * 1.10f) +
                                   extraLength,
                               baseLength, maxLength);
        outThickness = thickness;
    };

    auto drawTrailPlane = [&](const XMFLOAT3 &position, float planeYaw,
                              float roll, const XMFLOAT3 &scale,
                              const XMFLOAT4 &color, float intensity,
                              float noise) {
        Transform tf{};
        tf.position = position;
        tf.rotation = MakeQuat(0.0f, planeYaw, roll);
        tf.scale = scale;

        ModelDrawEffect effect{};
        effect.enabled = true;
        effect.additiveBlend = true;
        effect.disableCulling = true;
        effect.color = color;
        effect.intensity = intensity;
        effect.fresnelPower = 0.65f;
        effect.noiseAmount = noise;
        effect.time = sceneLightTime_;
        ctx_->model->SetDrawEffect(effect);
        ctx_->model->Draw(enemyWeaponTrailModelId_, tf, camera_);
    };

    for (const EnemyWeaponTrailSample &sample : enemyWeaponTrailSamples_) {
        if (!sample.active) {
            continue;
        }

        const float fade = 1.0f - std::clamp(sample.age / kGhostLifetime, 0.0f,
                                             1.0f);
        XMFLOAT3 ghostCenter{};
        float ghostYaw = 0.0f;
        float ghostRoll = 0.0f;
        float ghostLength = 0.0f;
        float ghostThickness = 0.0f;
        makeTrailGeometry(sample.root, sample.tip, sample.kind,
                          sample.thickness, fade * 0.18f, ghostCenter,
                          ghostYaw, ghostRoll, ghostLength, ghostThickness);

        XMFLOAT4 ghostColor = sample.color;
        ghostColor.w *= 0.34f * fade;
        drawTrailPlane(ghostCenter, ghostYaw, ghostRoll,
                       {ghostLength * (0.94f + 0.08f * fade),
                        ghostThickness * (0.70f + 0.30f * fade), 1.0f},
                       ghostColor, 0.74f * fade, 0.04f);
    }

    if (!shouldRecordCurrent) {
        ctx_->model->ClearDrawEffect();
        enemyWeaponTrailSampleTimer_ = 0.0f;
        enemyWeaponTrailLastKind_ = ActionKind::None;
        enemyWeaponTrailLastStep_ = ActionStep::None;
        return;
    }

    const XMFLOAT3 handPos = enemy_.GetRightHandTransform().position;
    const XMFLOAT3 enemyBodyPos = enemy_.GetBodyTransform().position;
    const float yaw = enemy_.GetTelegraphYaw();
    const float forwardX = std::sinf(yaw);
    const float forwardZ = std::cosf(yaw);
    const float rightX = std::cosf(yaw);
    const float rightZ = -std::sinf(yaw);

    const float pulse = 0.5f + 0.5f * std::sinf(sceneLightTime_ * 22.0f);
    const float activeBoost = step == ActionStep::Active ? 1.0f : 0.0f;
    const float holdBoost = step == ActionStep::Hold ? 1.0f : 0.0f;

    XMFLOAT3 bladeRoot = handPos;
    XMFLOAT3 bladeTip =
        kind == ActionKind::Smash
            ? XMFLOAT3{handPos.x - forwardX * 0.25f, handPos.y + 1.75f,
                       handPos.z - forwardZ * 0.25f}
            : XMFLOAT3{handPos.x - rightX * 1.85f, handPos.y + 0.14f,
                       handPos.z - rightZ * 1.85f};

    if (ctx_ != nullptr && ctx_->model != nullptr) {
        if (Model *enemyModel = ctx_->model->GetModel(enemyModelId_)) {
            TryGetEnemySwordMeshSegment(*enemyModel, enemy_.GetVisualTransform(),
                                        enemyBodyPos, bladeRoot, bladeTip);
        }
    }

    XMVECTOR rootV = XMLoadFloat3(&bladeRoot);
    XMVECTOR tipV = XMLoadFloat3(&bladeTip);
    XMVECTOR bladeV = tipV - rootV;
    float bladeLen = 0.0f;
    XMStoreFloat(&bladeLen, XMVector3Length(bladeV));
    if (bladeLen > 0.001f) {
        const XMVECTOR dir = bladeV / bladeLen;
        rootV += dir * 0.10f;
        tipV += dir * (0.48f + activeBoost * 0.20f);
        XMStoreFloat3(&bladeRoot, rootV);
        XMStoreFloat3(&bladeTip, tipV);
    }

    XMFLOAT3 trailCenter{};
    float billboardYaw = 0.0f;
    float trailRoll = 0.0f;
    float trailLength = 0.0f;
    float trailThickness = 0.0f;
    const float currentThickness =
        (kind == ActionKind::Smash ? 0.32f : 0.38f) + activeBoost * 0.10f;
    makeTrailGeometry(bladeRoot, bladeTip, kind, currentThickness,
                      activeBoost * 0.55f + holdBoost * 0.18f, trailCenter,
                      billboardYaw, trailRoll, trailLength, trailThickness);
    float toCameraX = cameraPos.x - trailCenter.x;
    float toCameraZ = cameraPos.z - trailCenter.z;
    float toCameraLen =
        std::sqrt(toCameraX * toCameraX + toCameraZ * toCameraZ);
    if (toCameraLen < 0.0001f) {
        toCameraLen = 1.0f;
    }
    toCameraX /= toCameraLen;
    toCameraZ /= toCameraLen;

    enemyWeaponTrailSampleTimer_ += drawDelta;
    if (enemyWeaponTrailLastKind_ != kind || enemyWeaponTrailLastStep_ != step) {
        enemyWeaponTrailSampleTimer_ = 99.0f;
    }
    const float sampleInterval = step == ActionStep::Active ? 0.032f : 0.065f;
    if (enemyWeaponTrailSampleTimer_ >= sampleInterval) {
        const XMFLOAT4 sampleColor =
            kind == ActionKind::Smash
                ? (enemy_.GetActionId() == ActionId::DelaySmash
                       ? XMFLOAT4{1.0f, 0.24f, 0.04f, 0.74f}
                       : XMFLOAT4{1.0f, 0.70f, 0.18f, 0.70f})
                : XMFLOAT4{0.10f, 0.86f, 1.0f, 0.72f};
        EnemyWeaponTrailSample &sample =
            enemyWeaponTrailSamples_[enemyWeaponTrailSampleCursor_ %
                                     enemyWeaponTrailSamples_.size()];
        sample.root = bladeRoot;
        sample.tip = bladeTip;
        sample.color = sampleColor;
        sample.kind = kind;
        sample.thickness = trailThickness;
        sample.age = 0.0f;
        sample.active = true;
        ++enemyWeaponTrailSampleCursor_;
        enemyWeaponTrailSampleTimer_ = 0.0f;
    }
    enemyWeaponTrailLastKind_ = kind;
    enemyWeaponTrailLastStep_ = step;

    if (kind == ActionKind::Smash) {
        const XMFLOAT4 trailColor =
            enemy_.GetActionId() == ActionId::DelaySmash
                ? XMFLOAT4{1.0f, 0.24f, 0.04f, 0.74f}
                : XMFLOAT4{1.0f, 0.70f, 0.18f, 0.70f};
        drawTrailPlane({trailCenter.x + toCameraX * 0.035f,
                        trailCenter.y + 0.010f,
                        trailCenter.z + toCameraZ * 0.035f},
                       billboardYaw, trailRoll,
                       {trailLength * 0.90f, trailThickness, 1.0f},
                       trailColor, 1.12f + 0.22f * pulse + activeBoost * 0.50f,
                       0.02f);
    } else if (kind == ActionKind::Sweep) {
        drawTrailPlane({trailCenter.x + toCameraX * 0.035f,
                        trailCenter.y + 0.010f,
                        trailCenter.z + toCameraZ * 0.035f},
                       billboardYaw, trailRoll,
                       {trailLength * 0.90f, trailThickness, 1.0f},
                       {0.10f, 0.86f, 1.0f, 0.72f},
                       1.18f + 0.22f * pulse + activeBoost * 0.55f, 0.02f);
    }

    ctx_->model->ClearDrawEffect();
}

void GameScene::DrawChargeWeakPoint() {
    const ActionKind actionKind = enemy_.GetActionKind();
    const ActionId actionId = enemy_.GetActionId();
    const ActionStep actionStep = enemy_.GetActionStep();
    const bool isChargeWeakPointVisible =
        !chargeWeakPointBroken_ &&
        ((actionKind == ActionKind::Smash && actionId == ActionId::DelaySmash &&
          actionStep == ActionStep::Charge) ||
         ((actionKind == ActionKind::Smash || actionKind == ActionKind::Sweep) &&
          actionStep == ActionStep::Hold));
    if (!isChargeWeakPointVisible || chargeWeakPointModelId_ == 0) {
        return;
    }

    const XMFLOAT3 &enemyPos = enemy_.GetTransform().position;
    const XMFLOAT3 &cameraPos = camera_.GetPosition();
    float toCameraX = cameraPos.x - enemyPos.x;
    float toCameraZ = cameraPos.z - enemyPos.z;
    float toCameraLen = std::sqrt(toCameraX * toCameraX + toCameraZ * toCameraZ);
    if (toCameraLen < 0.0001f) {
        toCameraLen = 1.0f;
    }
    toCameraX /= toCameraLen;
    toCameraZ /= toCameraLen;
    const float pulse = 0.5f + 0.5f * std::sinf(sceneLightTime_ * 24.0f);
    const float yaw = BillboardYawToCamera(enemyPos, cameraPos);

    Transform outer{};
    outer.position = {enemyPos.x + toCameraX * 0.72f, enemyPos.y + 1.62f,
                      enemyPos.z + toCameraZ * 0.72f};
    outer.rotation = MakeQuat(0.0f, yaw, 0.0f);
    outer.scale = {1.20f + 0.28f * pulse, 1.20f + 0.28f * pulse, 1.0f};

    ModelDrawEffect outerEffect{};
    outerEffect.enabled = true;
    outerEffect.additiveBlend = true;
    outerEffect.disableCulling = true;
    outerEffect.color = {1.0f, 0.34f, 0.04f, 0.58f};
    outerEffect.intensity = 0.42f + 0.18f * pulse;
    outerEffect.fresnelPower = 1.2f;
    outerEffect.noiseAmount = 0.18f;
    outerEffect.time = sceneLightTime_;
    ctx_->model->SetDrawEffect(outerEffect);
    ctx_->model->Draw(chargeWeakPointModelId_, outer, camera_);

    ctx_->model->ClearDrawEffect();
}

void GameScene::DrawArena() {
    ModelManager *model = ctx_->model;

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
    model->Draw(arenaCenterDiskModelId_, centerDisk, camera_);

    for (int i = 0; i < 16; ++i) {
        const float angle = static_cast<float>(i) * kPi * 0.125f;
        const float x = std::sinf(angle) * 3.7f;
        const float z = std::cosf(angle) * 3.7f;

        Transform spoke{};
        spoke.position = {x, 0.008f, z};
        spoke.rotation = MakeQuat(-kPi * 0.5f, angle, 0.0f);
        spoke.scale = {0.075f, 4.8f, 1.0f};
        model->Draw(arenaSpokeModelId_, spoke, camera_);
    }

    Transform innerRing{};
    innerRing.position = {0.0f, 0.012f, 0.0f};
    innerRing.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
    model->Draw(arenaInnerRingModelId_, innerRing, camera_);

    Transform outerRing{};
    outerRing.position = {0.0f, 0.018f, 0.0f};
    outerRing.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
    model->Draw(arenaOuterRingModelId_, outerRing, camera_);

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
}

void GameScene::DrawEnemyFocusMarker() {
    if (enemyFocusRingModelId_ == 0) {
        return;
    }

    const XMFLOAT3 &enemyPos = enemy_.GetTransform().position;
    const ActionStep actionStep = enemy_.GetActionStep();
    const bool isAttackCue =
        actionStep == ActionStep::Charge || actionStep == ActionStep::Hold ||
        actionStep == ActionStep::Active;
    const float pulse = 0.5f + 0.5f * std::sinf(sceneLightTime_ * 10.0f);

    Transform marker{};
    marker.position = {enemyPos.x, 0.034f, enemyPos.z};
    marker.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
    const float scale = isAttackCue ? 1.08f + 0.08f * pulse : 0.96f;
    marker.scale = {scale, scale, 1.0f};

    ModelDrawEffect effect{};
    effect.enabled = true;
    effect.additiveBlend = true;
    effect.disableCulling = true;
    effect.color = isAttackCue ? XMFLOAT4{1.0f, 0.84f, 0.24f, 0.88f}
                               : XMFLOAT4{0.84f, 0.96f, 1.0f, 0.62f};
    effect.intensity = isAttackCue ? 0.58f + 0.24f * pulse : 0.30f;
    effect.fresnelPower = 1.0f;
    effect.noiseAmount = 0.02f;
    effect.time = sceneLightTime_;
    ctx_->model->SetDrawEffect(effect);
    ctx_->model->Draw(enemyFocusRingModelId_, marker, camera_);
    ctx_->model->ClearDrawEffect();
}
