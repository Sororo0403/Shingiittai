#include "GameScene.h"
#include "BattleResultScene.h"
#include "DirectXCommon.h"
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

using namespace DirectX;

namespace {
constexpr float kPi = 3.14159265f;
constexpr float kBladeClashWinGuardBreakLead = 0.52f;
constexpr float kBladeClashWinGuardBreakImpactTime = 0.24f;
constexpr float kBladeClashWinActionSlow = 0.95f;
constexpr float kBladeClashGuardBreakSlowStart = 0.21f;
constexpr float kBladeClashGuardBreakSlowEnd = 0.36f;
constexpr float kBladeClashGuardBreakSlowScale = 0.18f;
constexpr float kBladeClashGuardBreakRecoilDistance = 0.72f;
constexpr float kBladeClashGuardBreakDrop = 0.16f;
constexpr float kBladeClashGuardBreakLift = 0.32f;
constexpr float kBladeClashGuardBreakPose = 0.48f;

struct SharedBattleModels {
    bool initialized = false;
    uint32_t arenaNoiseTextureId = 0;
    uint32_t arenaFloorModelId = 0;
    uint32_t arenaLowPolyTerrainModelId = 0;
    uint32_t arenaDistantTerrainModelId = 0;
    uint32_t arenaHazardSpireModelId = 0;
    uint32_t arenaHazardGlowRingModelId = 0;
    uint32_t arenaCityTowerModelId = 0;
    uint32_t arenaCityWindowModelId = 0;
    uint32_t arenaGiantBodyModelId = 0;
    uint32_t arenaGiantHeadModelId = 0;
    uint32_t arenaCenterDiskModelId = 0;
    uint32_t arenaSpokeModelId = 0;
    uint32_t arenaInnerRingModelId = 0;
    uint32_t arenaOuterRingModelId = 0;
    uint32_t arenaColumnModelId = 0;
    uint32_t arenaColumnCapModelId = 0;
    uint32_t arenaDomeModelId = 0;
    uint32_t arenaBarrierRingModelId = 0;
    uint32_t enemyFocusRingModelId = 0;
    uint32_t enemyWeaponTrailModelId = 0;
    uint32_t chargeWeakPointModelId = 0;
    uint32_t chargeWeakPointBackplateModelId = 0;
    uint32_t chargeWeakPointSlashModelId = 0;
};

SharedBattleModels gSharedBattleModels;

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

float SmoothStep01(float value) {
    const float t = std::clamp(value, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
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
    case ActionKind::BladeClash:
    case ActionKind::Wave:
    case ActionKind::Laser:
    case ActionKind::Cage:
        return 0.48f;
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

SwordCounterAxis RequiredVisualCounterAxisForAction(ActionKind kind,
                                                    ActionKind farFollowupKind) {
    switch (kind) {
    case ActionKind::Smash:
        return SwordCounterAxis::Vertical;
    case ActionKind::Sweep:
    case ActionKind::BladeClash:
        return SwordCounterAxis::Horizontal;
    case ActionKind::Laser:
        return farFollowupKind == ActionKind::Sweep ? SwordCounterAxis::Horizontal
                                                    : SwordCounterAxis::Vertical;
    default:
        return SwordCounterAxis::None;
    }
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

    model->textureId = rustTextureId;
    for (ModelSubMesh &subMesh : model->subMeshes) {
        subMesh.textureId = rustTextureId;

        Material material = modelManager->GetMaterial(subMesh.materialId);
        material.enableTexture = 1;
        material.color = {0.68f, 0.62f, 0.50f, 1.0f};
        XMStoreFloat4x4(&material.uvTransform,
                        XMMatrixTranspose(XMMatrixIdentity()));
        material.reflectionStrength = 0.085f;
        material.reflectionFresnelStrength = 0.030f;
        material.reflectionRoughness = 0.82f;
        material.enableDissolve = 0;
        material.dissolveEdgeColor = {0.68f, 0.24f, 0.08f, 0.46f};
        modelManager->SetMaterial(subMesh.materialId, material);
    }
}

} // namespace

void GameScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    ctx_->dxCommon->ResetClearColor();
    ctx_->postEffectRenderer->SetColorMode(PostEffectRenderer::ColorMode::None);
    ctx_->postEffectRenderer->SetVignettingShape(11.0f, 1.15f);
    ctx_->postEffectRenderer->SetVignettingStrength(0.20f);
    ctx_->postEffectRenderer->SetVignettingEnabled(true);
    combatFeedback_.Initialize(ctx_->postEffectRenderer);

    float aspect = static_cast<float>(ctx_->winApp->GetWidth()) /
                   static_cast<float>(ctx_->winApp->GetHeight());

    camera_.Initialize(aspect);
    camera_.SetMode(CameraMode::LookAt);
    camera_.UpdateMatrices();
    camera_.SetPerspectiveFovDeg(currentFovDeg_);

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
                                 {{0.22f, 0.31f, 0.56f, 0.98f},
                                  {0.11f, 0.17f, 0.32f, 0.98f},
                                  {0.30f, 0.24f, 0.20f, 0.98f}},
                                 0.12f, 0.06f, 0.78f);
    ApplyWeatheredMetalMaterials(model, swordModel, worldRustTextureId,
                                 {{0.34f, 0.46f, 0.74f, 1.0f},
                                  {0.13f, 0.23f, 0.46f, 1.0f},
                                  {0.34f, 0.25f, 0.21f, 1.0f}},
                                 0.24f, 0.12f, 0.64f);
    ApplyRustedRobotMaterials(model, enemyModel, enemyRustTextureId);
    ApplyWeatheredMetalMaterials(model, bulletModel, worldRustTextureId,
                                 {{0.95f, 0.72f, 0.36f, 1.0f},
                                  {0.70f, 0.78f, 0.76f, 1.0f},
                                  {0.48f, 0.24f, 0.12f, 1.0f}},
                                 0.76f, 0.58f, 0.26f);
    if (!gSharedBattleModels.initialized) {
        gSharedBattleModels.arenaNoiseTextureId = arenaStoneTextureId;
        gSharedBattleModels.arenaFloorModelId = model->CreatePlane(
            arenaStoneTextureId,
        MakeArenaMaterial({0.070f, 0.085f, 0.105f, 1.0f}, true, 0.025f,
                          0.84f));
        gSharedBattleModels.arenaLowPolyTerrainModelId =
            model->CreateLowPolyTerrain(
                arenaStoneTextureId,
                MakeArenaMaterial({0.075f, 0.10f, 0.13f, 1.0f}, true, 0.016f,
                                  0.80f),
                42, 78.0f, 1.25f, 15.0f, 0x4107u);
        gSharedBattleModels.arenaDistantTerrainModelId =
            model->CreateLowPolyTerrain(
                arenaStoneTextureId,
                MakeArenaMaterial({0.12f, 0.18f, 0.22f, 1.0f}, true, 0.02f,
                                  0.88f),
                18, 58.0f, 5.6f, 0.0f, 0x671Du);
        gSharedBattleModels.arenaHazardSpireModelId = model->CreateCylinder(
            arenaStoneTextureId,
            MakeArenaMaterial({0.10f, 0.15f, 0.15f, 1.0f}, true, 0.025f,
                              0.88f),
            7, 0.04f, 0.92f, 8.8f);
        gSharedBattleModels.arenaHazardGlowRingModelId = model->CreateRing(
            0, MakeArenaMaterial({0.55f, 0.88f, 0.96f, 0.42f}, false, 0.0f,
                                 0.46f),
            48, 2.45f, 0.34f);
        gSharedBattleModels.arenaCityTowerModelId = model->CreateBox(
            arenaStoneTextureId,
            MakeArenaMaterial({0.18f, 0.24f, 0.29f, 1.0f}, true, 0.025f,
                              0.76f),
            1.0f, 1.0f, 1.0f);
        gSharedBattleModels.arenaCityWindowModelId = model->CreatePlane(
            0, MakeArenaMaterial({0.72f, 0.90f, 0.96f, 0.58f}, false, 0.0f,
                                 0.40f));
        gSharedBattleModels.arenaGiantBodyModelId = model->CreateCylinder(
            arenaStoneTextureId,
            MakeArenaMaterial({0.055f, 0.10f, 0.11f, 1.0f}, true, 0.012f,
                              0.92f),
            9, 0.78f, 1.18f, 5.8f);
        gSharedBattleModels.arenaGiantHeadModelId = model->CreateCylinder(
            arenaStoneTextureId,
            MakeArenaMaterial({0.05f, 0.09f, 0.10f, 1.0f}, true, 0.01f,
                              0.94f),
            8, 0.92f, 1.05f, 1.15f);
        gSharedBattleModels.arenaCenterDiskModelId = model->CreateRing(
            arenaStoneTextureId,
            MakeArenaMaterial({0.72f, 0.58f, 0.30f, 1.0f}, false, 0.12f,
                              0.30f),
            96, 1.95f, 0.0f);
        gSharedBattleModels.arenaSpokeModelId = model->CreatePlane(
            arenaStoneTextureId,
        MakeArenaMaterial({0.28f, 0.21f, 0.12f, 1.0f}, false, 0.06f,
                          0.58f));
        gSharedBattleModels.arenaInnerRingModelId = model->CreateRing(
            arenaStoneTextureId,
            MakeArenaMaterial({0.64f, 0.60f, 0.44f, 1.0f}, false, 0.12f,
                              0.32f),
            96, 4.9f, 4.35f);
        gSharedBattleModels.arenaOuterRingModelId = model->CreateRing(
            arenaStoneTextureId,
            MakeArenaMaterial({0.68f, 0.28f, 0.18f, 1.0f}, false, 0.10f,
                              0.38f),
            128, 12.3f, 11.6f);
        gSharedBattleModels.arenaColumnModelId = model->CreateCylinder(
            arenaStoneTextureId,
            MakeArenaMaterial({0.22f, 0.26f, 0.30f, 1.0f}, true, 0.045f,
                              0.68f),
            24, 0.26f, 0.38f, 5.4f);
        gSharedBattleModels.arenaColumnCapModelId = model->CreateCylinder(
            arenaStoneTextureId,
            MakeArenaMaterial({0.62f, 0.30f, 0.18f, 1.0f}, false, 0.10f,
                              0.38f),
            32, 0.68f, 0.78f, 0.24f);
        gSharedBattleModels.arenaDomeModelId = model->CreateCylinder(
            arenaStoneTextureId,
            MakeArenaMaterial({0.09f, 0.14f, 0.28f, 0.78f}, true, 0.00f,
                              0.72f),
            128, 4.5f, 13.5f, 8.8f);
        gSharedBattleModels.arenaBarrierRingModelId = model->CreateRing(
            arenaStoneTextureId,
            MakeArenaMaterial({0.24f, 0.30f, 0.46f, 0.42f}, false, 0.00f,
                              0.34f),
            128, 13.1f, 12.9f);
        gSharedBattleModels.enemyFocusRingModelId = model->CreateRing(
            0, MakeArenaMaterial({0.92f, 0.98f, 1.0f, 0.72f}, false, 0.0f,
                                 0.42f),
            96, 1.85f, 1.58f);
        gSharedBattleModels.enemyWeaponTrailModelId = model->CreatePlane(
            0, MakeArenaMaterial({1.0f, 0.92f, 0.62f, 0.82f}, false, 0.0f,
                                 0.28f));
        gSharedBattleModels.chargeWeakPointModelId = model->CreatePlane(
            0, MakeArenaMaterial({1.0f, 0.96f, 0.78f, 0.92f}, false, 0.02f,
                                 0.20f));
        gSharedBattleModels.chargeWeakPointBackplateModelId =
            model->CreatePlane(
                0, MakeArenaMaterial({0.006f, 0.012f, 0.010f, 0.80f}, false,
                                     0.00f, 0.86f));
        gSharedBattleModels.chargeWeakPointSlashModelId = model->CreatePlane(
            0, MakeArenaMaterial({0.34f, 1.0f, 0.38f, 0.96f}, false, 0.04f,
                                 0.18f));
        gSharedBattleModels.initialized = true;
    }

    arenaNoiseTextureId_ = gSharedBattleModels.arenaNoiseTextureId;
    arenaFloorModelId_ = gSharedBattleModels.arenaFloorModelId;
    arenaLowPolyTerrainModelId_ =
        gSharedBattleModels.arenaLowPolyTerrainModelId;
    arenaDistantTerrainModelId_ =
        gSharedBattleModels.arenaDistantTerrainModelId;
    arenaHazardSpireModelId_ = gSharedBattleModels.arenaHazardSpireModelId;
    arenaHazardGlowRingModelId_ =
        gSharedBattleModels.arenaHazardGlowRingModelId;
    arenaCityTowerModelId_ = gSharedBattleModels.arenaCityTowerModelId;
    arenaCityWindowModelId_ = gSharedBattleModels.arenaCityWindowModelId;
    arenaGiantBodyModelId_ = gSharedBattleModels.arenaGiantBodyModelId;
    arenaGiantHeadModelId_ = gSharedBattleModels.arenaGiantHeadModelId;
    arenaCenterDiskModelId_ = gSharedBattleModels.arenaCenterDiskModelId;
    arenaSpokeModelId_ = gSharedBattleModels.arenaSpokeModelId;
    arenaInnerRingModelId_ = gSharedBattleModels.arenaInnerRingModelId;
    arenaOuterRingModelId_ = gSharedBattleModels.arenaOuterRingModelId;
    arenaColumnModelId_ = gSharedBattleModels.arenaColumnModelId;
    arenaColumnCapModelId_ = gSharedBattleModels.arenaColumnCapModelId;
    arenaDomeModelId_ = gSharedBattleModels.arenaDomeModelId;
    arenaBarrierRingModelId_ = gSharedBattleModels.arenaBarrierRingModelId;
    enemyFocusRingModelId_ = gSharedBattleModels.enemyFocusRingModelId;
    enemyWeaponTrailModelId_ = gSharedBattleModels.enemyWeaponTrailModelId;
    chargeWeakPointModelId_ = gSharedBattleModels.chargeWeakPointModelId;
    chargeWeakPointBackplateModelId_ =
        gSharedBattleModels.chargeWeakPointBackplateModelId;
    chargeWeakPointSlashModelId_ =
        gSharedBattleModels.chargeWeakPointSlashModelId;
    sparkParticles_.Initialize(dx, ctx_->srv, texture, particleTextureId_, 4096);
    sparkParticles_.SetEmission(1, 1000.0f);
    sparkParticles_.SetEmitterRadius(0.08f);
    explosionParticles_.Initialize(dx, ctx_->srv, texture, particleTextureId_,
                                   4096);
    explosionParticles_.SetEmission(1, 1000.0f);
    explosionParticles_.SetEmitterRadius(0.25f);
    smokeParticles_.Initialize(dx, ctx_->srv, texture, particleTextureId_, 2048);
    smokeParticles_.SetEmission(1, 1000.0f);
    smokeParticles_.SetEmitterRadius(0.40f);

    swordFlashParticles_.Initialize(dx, ctx_->srv, texture, particleTextureId_,
                                    384);
    swordFlashParticles_.SetEmission(1, 1000.0f);
    swordFlashParticles_.SetEmitterRadius(0.06f);

    swordTrailRenderer_.Initialize(dx);
    swordTrailRenderer_.Reset();
    swordSlashArcRenderer_.Initialize(dx);
    swordSlashArcRenderer_.Reset();
    prevSwordSlashStates_.fill(false);
    bladeClashPreviousSlashStates_.fill(false);
    dx->EndUpload();

    texture->ReleaseUploadBuffers();

    player_.Initialize(playerModel, swordModel);
    player_.SetInputCalibration(inputCalibration_);
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
    SyncEnemyAnimation();
    UpdateSceneLighting();
    battleElapsedTime_ = 0.0f;
    battleIntroActive_ = true;
    battleIntroTimer_ = 0.0f;
    battleIntroRevealEmitted_ = false;
    phaseTransitionWasActive_ = false;
    phaseTransitionReleaseEmitted_ = false;
    phaseTransitionLoopTimer_ = 0.0f;
    titleDemoTimer_ = 0.0f;
    titleDemoCounterTimer_ = 1.15f;
    battleResultRequested_ = false;
    victorySequenceActive_ = false;
    victorySequenceTimer_ = 0.0f;
    victoryClearTime_ = 0.0f;
    defeatSequenceActive_ = false;
    defeatSequenceTimer_ = 0.0f;
    defeatImpactEmitted_ = false;
    player_.SetDefeatPoseRatio(0.0f);
    counterCinematicActive_ = false;
    counterCinematicTimer_ = 0.0f;
    bladeClashActive_ = false;
    bladeClashGauge_ = 0.0f;
    bladeClashTimer_ = 0.0f;
    bladeClashCameraPush_ = 0.0f;
    bladeClashImpactPulse_ = 0.0f;
    bladeClashEnemySurgeTimer_ = 0.0f;
    bladeClashChainTimer_ = 0.0f;
    bladeClashSlashChain_ = 0;
    enemyLastStandPrimed_ = false;
    bladeClashFinal_ = false;
    bladeClashFinalBarrageStep_ = 0;
    bladeClashFinishActive_ = false;
    bladeClashFinishPlayerWon_ = false;
    bladeClashFinishImpactEmitted_ = false;
    bladeClashFinishGuardBreakEmitted_ = false;
    bladeClashFinishSkidEmitted_ = false;
    bladeClashFinishWallImpactEmitted_ = false;
    bladeClashFinishPendingEnemyTransition_ = false;
    bladeClashFinishTimer_ = 0.0f;
    bladeClashFinishCenter_ = {0.0f, 0.0f, 0.0f};
    bladeClashFinishPlayerStart_ = {0.0f, 0.0f, 0.0f};
    bladeClashFinishPlayerEnd_ = {0.0f, 0.0f, 0.0f};
    bladeClashFinishEnemyStart_ = {0.0f, 0.0f, 0.0f};
    bladeClashFinishEnemyEnd_ = {0.0f, 0.0f, 0.0f};
    enemyAnimationFrozen_ = false;
    chargeWeakPointActionKind_ = ActionKind::None;
    failedChargeWeakPointActionKind_ = ActionKind::None;
    chargeWeakPointActionSerial_ = 0;
    failedChargeWeakPointActionSerial_ = 0;
    chargeWeakPointBroken_ = false;
    chargeWeakPointFailedThisAction_ = false;
    enemyRedPunishUncounterable_ = false;
    chargeWeakPointSlashCount_ = 0;
    previousChargeWeakPointSlashStates_.fill(false);
    previousSwordSoundStates_.fill(false);
    enemyCueParticleTimer_ = 0.0f;
    enemyWeakPointParticleTimer_ = 0.0f;
    enemySwordParticleTimer_ = 0.0f;
    hud_.Initialize(*ctx_);
    enemy_.FaceTargetImmediately(player_.GetTransform().position);
    ApplyEnemyIntroDissolve(0.0f);
}

void GameScene::Update() {
    Input *input = ctx_->input;
    const float baseDeltaTime = ctx_->deltaTime;
    if (battleIntroActive_) {
        if (runMode_ == RunMode::TitleDemo) {
            player_.UpdateJoyConCalibrationInput(input, baseDeltaTime);
        }
        UpdateBattleIntro(baseDeltaTime);
        return;
    }
    if (victorySequenceActive_) {
        sceneLightTime_ += baseDeltaTime;
        combatFeedback_.Update(baseDeltaTime, sceneLightTime_);
        hud_.Update(*ctx_, player_.GetHP(), enemy_.GetHP(),
                    enemy_.GetMaxHP());
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
    if (defeatSequenceActive_) {
        sceneLightTime_ += baseDeltaTime;
        combatFeedback_.Update(baseDeltaTime, sceneLightTime_);
        UpdateDefeatSequence(baseDeltaTime);
        UpdateBattleCamera();
        camera_.UpdateMatrices();
        ctx_->model->UpdateAnimation(playerModelId_, baseDeltaTime * 0.035f);
        ctx_->model->UpdateAnimation(enemyModelId_, baseDeltaTime * 0.28f);
        ApplyEnemyProceduralAnimation();
        sparkParticles_.Update(baseDeltaTime);
        explosionParticles_.Update(baseDeltaTime);
        smokeParticles_.Update(baseDeltaTime);
        return;
    }
    if (enemy_.IsPhaseTransitionActive() && !bladeClashFinishActive_) {
        UpdatePhaseTransitionCinematic(baseDeltaTime);
        return;
    }
    phaseTransitionWasActive_ = false;
    phaseTransitionReleaseEmitted_ = false;

    if (runMode_ == RunMode::Play && input != nullptr &&
        input->IsKeyTrigger(DIK_F8) && !bladeClashActive_ &&
        !bladeClashFinishActive_ && !counterCinematicActive_) {
        if (enemy_.ForcePhase3PhantomWarpSkill()) {
            chargeWeakPointActionKind_ = ActionKind::None;
            chargeWeakPointActionSerial_ = 0;
            failedChargeWeakPointActionKind_ = ActionKind::None;
            failedChargeWeakPointActionSerial_ = 0;
            chargeWeakPointBroken_ = false;
            chargeWeakPointFailedThisAction_ = false;
            chargeWeakPointSlashCount_ = 0;
            previousChargeWeakPointSlashStates_.fill(false);
            enemyRedPunishUncounterable_ = false;
            SetEnemyAnimationFrozen(false);
            SyncEnemyAnimation();
        }
    }
    if (runMode_ == RunMode::Play && input != nullptr &&
        input->IsKeyTrigger(DIK_F9) && !bladeClashActive_ &&
        !bladeClashFinishActive_ && !counterCinematicActive_) {
        if (enemy_.ForceFarLaserSkill()) {
            chargeWeakPointActionKind_ = ActionKind::None;
            chargeWeakPointActionSerial_ = 0;
            failedChargeWeakPointActionKind_ = ActionKind::None;
            failedChargeWeakPointActionSerial_ = 0;
            chargeWeakPointBroken_ = false;
            chargeWeakPointFailedThisAction_ = false;
            chargeWeakPointSlashCount_ = 0;
            previousChargeWeakPointSlashStates_.fill(false);
            enemyRedPunishUncounterable_ = false;
            SetEnemyAnimationFrozen(false);
            SyncEnemyAnimation();
        }
    }

    combatFeedback_.Update(baseDeltaTime, sceneLightTime_);
    UpdateChargeWeakPointFocus(baseDeltaTime);
    if (bladeClashFinishActive_) {
        float bladeClashFinishDeltaTime = baseDeltaTime;
        if (bladeClashFinishPlayerWon_ &&
            bladeClashFinishTimer_ >= kBladeClashGuardBreakSlowStart &&
            bladeClashFinishTimer_ < kBladeClashGuardBreakSlowEnd) {
            const float slowT =
                std::clamp((bladeClashFinishTimer_ -
                            kBladeClashGuardBreakSlowStart) /
                               (kBladeClashGuardBreakSlowEnd -
                                kBladeClashGuardBreakSlowStart),
                           0.0f, 1.0f);
            const float snapHold = std::sinf(slowT * kPi);
            const float slowScale =
                1.0f - (1.0f - kBladeClashGuardBreakSlowScale) * snapHold;
            bladeClashFinishDeltaTime *= slowScale;
        }
        bladeClashFinishTimer_ += bladeClashFinishDeltaTime;
        const float bladeClashWinActionTimer =
            bladeClashFinishPlayerWon_
                ? (std::max)(0.0f, bladeClashFinishTimer_ -
                                        kBladeClashWinGuardBreakLead) /
                      kBladeClashWinActionSlow
                : bladeClashFinishTimer_;
        if (bladeClashFinishPlayerWon_ &&
            !bladeClashFinishGuardBreakEmitted_ &&
            bladeClashFinishTimer_ >= kBladeClashWinGuardBreakImpactTime) {
            bladeClashFinishGuardBreakEmitted_ = true;
            XMFLOAT3 breakCenter = enemy_.GetTransform().position;
            breakCenter.y += 1.22f;
            explosionParticles_.EmitBurst(
                breakCenter, 92, 0.86f,
                GPUParticleSystem::BurstStyle::Explosion,
                {1.0f, 0.86f, 0.42f, 0.72f},
                {-bladeClashDirection_.x, 0.16f, -bladeClashDirection_.z},
                1.72f);
            sparkParticles_.EmitBurst(
                breakCenter, 128, 0.72f, GPUParticleSystem::BurstStyle::Sparks,
                {1.0f, 0.92f, 0.48f, 0.88f},
                {bladeClashDirection_.z, 0.18f, -bladeClashDirection_.x},
                2.25f);
            swordFlashParticles_.EmitBurst(
                breakCenter, 10, 0.58f, GPUParticleSystem::BurstStyle::Flash,
                {1.0f, 1.0f, 0.82f, 0.66f},
                {-bladeClashDirection_.x, 0.0f, -bladeClashDirection_.z},
                0.34f);
            smokeParticles_.EmitBurst(
                breakCenter, 24, 0.62f, GPUParticleSystem::BurstStyle::Smoke,
                {0.36f, 0.32f, 0.26f, 0.42f},
                {-bladeClashDirection_.x, 0.06f, -bladeClashDirection_.z},
                0.76f);
            CombatFeedbackEvent guardBreakFeedback{};
            guardBreakFeedback.type =
                CombatFeedbackEventType::BladeClashGuardBreak;
            guardBreakFeedback.position = breakCenter;
            guardBreakFeedback.direction =
                {-bladeClashDirection_.x, 0.0f, -bladeClashDirection_.z};
            guardBreakFeedback.power = bladeClashFinal_ ? 16.0f : 10.5f;
            DispatchCombatFeedback(guardBreakFeedback);
        }
        if (!bladeClashFinishPlayerWon_ && !bladeClashFinishSkidEmitted_ &&
            bladeClashFinishTimer_ >= 0.78f) {
            bladeClashFinishSkidEmitted_ = true;
            bladeClashFinishImpactEmitted_ = true;
            constexpr float clashLossDamage = 25.0f;
            XMFLOAT3 sweepCenter = player_.GetTransform().position;
            sweepCenter.y += 1.02f;
            explosionParticles_.EmitBurst(
                sweepCenter, 92, 1.24f,
                GPUParticleSystem::BurstStyle::SlashLine,
                {1.0f, 0.28f, 0.06f, 0.96f},
                {bladeClashDirection_.z, -0.04f, -bladeClashDirection_.x},
                1.92f);
            swordFlashParticles_.EmitBurst(
                sweepCenter, 14, 0.92f, GPUParticleSystem::BurstStyle::Flash,
                {1.0f, 0.56f, 0.16f, 0.82f},
                {-bladeClashDirection_.x, 0.0f, -bladeClashDirection_.z},
                0.48f);
            sparkParticles_.EmitBurst(
                sweepCenter, 46, 0.42f, GPUParticleSystem::BurstStyle::Sparks,
                {1.0f, 0.46f, 0.12f, 0.82f},
                {-bladeClashDirection_.x, 0.08f, -bladeClashDirection_.z},
                1.48f);
            smokeParticles_.EmitBurst(
                sweepCenter, 18, 0.44f, GPUParticleSystem::BurstStyle::Smoke,
                {0.42f, 0.31f, 0.24f, 0.44f},
                {-bladeClashDirection_.x, 0.04f, -bladeClashDirection_.z},
                0.62f);
            const float appliedDamage =
                player_.TakeDamage(clashLossDamage);
            CombatFeedbackEvent lossFeedback{};
            if (appliedDamage > 0.0f) {
                lossFeedback.type = CombatFeedbackEventType::PlayerDamaged;
                lossFeedback.position = sweepCenter;
                lossFeedback.direction =
                    {-bladeClashDirection_.x, 0.0f, -bladeClashDirection_.z};
                lossFeedback.power = 12.0f;
                DispatchCombatFeedback(lossFeedback);
            }
            playerHitCooldown_ = 0.82f;
        }
        if (!bladeClashFinishPlayerWon_ && bladeClashFinishSkidEmitted_ &&
            bladeClashFinishTimer_ >= 1.24f && bladeClashFinishTimer_ < 1.28f) {
            XMFLOAT3 skidCenter = player_.GetTransform().position;
            skidCenter.y += 0.28f;
            explosionParticles_.EmitBurst(
                skidCenter, 36, 0.56f,
                GPUParticleSystem::BurstStyle::SlashLine,
                {1.0f, 0.42f, 0.10f, 0.38f},
                {bladeClashDirection_.z, 0.02f, -bladeClashDirection_.x},
                0.92f);
            smokeParticles_.EmitBurst(
                skidCenter, 38, 0.64f, GPUParticleSystem::BurstStyle::Smoke,
                {0.34f, 0.28f, 0.24f, 0.44f},
                {-bladeClashDirection_.x, 0.03f, -bladeClashDirection_.z},
                0.62f);
        }
        if (!bladeClashFinishPlayerWon_ && bladeClashFinishSkidEmitted_ &&
            !bladeClashFinishWallImpactEmitted_ &&
            bladeClashFinishTimer_ >= 1.58f) {
            bladeClashFinishWallImpactEmitted_ = true;
            XMFLOAT3 crashCenter = player_.GetTransform().position;
            crashCenter.y += 0.86f;
            explosionParticles_.EmitBurst(
                crashCenter, 110, 0.96f,
                GPUParticleSystem::BurstStyle::Explosion,
                {1.0f, 0.36f, 0.10f, 0.78f},
                {-bladeClashDirection_.x, 0.12f, -bladeClashDirection_.z},
                2.35f);
            sparkParticles_.EmitBurst(
                crashCenter, 72, 0.58f, GPUParticleSystem::BurstStyle::Sparks,
                {1.0f, 0.56f, 0.16f, 0.86f},
                {bladeClashDirection_.z, 0.10f, -bladeClashDirection_.x},
                2.10f);
            swordFlashParticles_.EmitBurst(
                crashCenter, 12, 0.82f, GPUParticleSystem::BurstStyle::Flash,
                {1.0f, 0.82f, 0.48f, 0.58f},
                {-bladeClashDirection_.x, 0.0f, -bladeClashDirection_.z},
                0.40f);
            XMFLOAT3 dustCenter = crashCenter;
            dustCenter.y -= 0.52f;
            smokeParticles_.EmitBurst(
                dustCenter, 50, 0.94f, GPUParticleSystem::BurstStyle::Smoke,
                {0.38f, 0.32f, 0.28f, 0.58f},
                {-bladeClashDirection_.x, 0.05f, -bladeClashDirection_.z},
                1.25f);
            CombatFeedbackEvent crashFeedback{};
            crashFeedback.type = CombatFeedbackEventType::PlayerDamaged;
            crashFeedback.position = crashCenter;
            crashFeedback.direction =
                {-bladeClashDirection_.x, 0.0f, -bladeClashDirection_.z};
            crashFeedback.power = 16.0f;
            DispatchCombatFeedback(crashFeedback);
        }
        if (bladeClashFinishPlayerWon_ && bladeClashFinal_) {
            const float leanT =
                std::clamp(bladeClashWinActionTimer / 0.36f, 0.0f, 1.0f);
            const float launchT =
                std::clamp((bladeClashWinActionTimer - 0.36f) / 0.82f, 0.0f,
                           1.0f);
            const float bounceT =
                std::clamp((bladeClashWinActionTimer - 1.10f) / 0.54f, 0.0f,
                           1.0f);
            const float slideT =
                std::clamp((bladeClashWinActionTimer - 1.64f) / 0.66f, 0.0f,
                           1.0f);
            const float settleT =
                std::clamp((bladeClashWinActionTimer - 2.30f) / 0.42f, 0.0f,
                           1.0f);
            const float leanEase = leanT * leanT * (3.0f - 2.0f * leanT);
            const float launchEase = 1.0f - std::pow(1.0f - launchT, 4.0f);
            const float bounceEase = 1.0f - std::pow(1.0f - bounceT, 3.0f);
            const float slideEase =
                1.0f - std::pow(1.0f - slideT, 3.0f);
            const float settleEase =
                settleT * settleT * (3.0f - 2.0f * settleT);
            const float travel =
                0.42f * leanEase + 10.40f * launchEase +
                4.70f * bounceEase + 5.25f * slideEase +
                0.43f * settleEase;
            const float firstArc = std::sinf(launchT * kPi) * 1.42f;
            const float secondArc = std::sinf(bounceT * kPi) * 0.72f;
            XMFLOAT3 enemyPos = {
                bladeClashFinishEnemyStart_.x + bladeClashDirection_.x * travel,
                bladeClashFinishEnemyStart_.y + firstArc + secondArc -
                    0.16f * slideEase - 0.24f * settleEase,
                bladeClashFinishEnemyStart_.z + bladeClashDirection_.z * travel};
            const float enemyYaw =
                std::atan2f(-bladeClashDirection_.x, -bladeClashDirection_.z) +
                std::sinf(bounceT * kPi) * 0.18f * (1.0f - slideEase);
            enemy_.SetCinematicTransform(enemyPos, enemyYaw);

            if (slideT > 0.02f && slideT < 0.96f) {
                XMFLOAT3 trailCenter = enemyPos;
                trailCenter.y += 0.18f;
                smokeParticles_.EmitBurst(
                    trailCenter, 4, 0.34f, GPUParticleSystem::BurstStyle::Smoke,
                    {0.34f, 0.29f, 0.24f, 0.30f},
                    {-bladeClashDirection_.x, 0.04f, -bladeClashDirection_.z},
                    0.48f);
                sparkParticles_.EmitBurst(
                    trailCenter, 3, 0.16f, GPUParticleSystem::BurstStyle::Sparks,
                    {1.0f, 0.66f, 0.18f, 0.36f},
                    {bladeClashDirection_.z, 0.02f, -bladeClashDirection_.x},
                    0.54f);
            }
        }
        if (bladeClashFinishPlayerWon_ && !bladeClashFinal_ &&
            !bladeClashFinishImpactEmitted_ &&
            bladeClashWinActionTimer >= 0.28f) {
            bladeClashFinishImpactEmitted_ = true;
            XMFLOAT3 cutCenter = enemy_.GetTransform().position;
            cutCenter.y += 1.18f;
            explosionParticles_.EmitBurst(
                cutCenter, 104, 0.86f,
                GPUParticleSystem::BurstStyle::SlashLine,
                {1.0f, 0.90f, 0.58f, 0.66f},
                {bladeClashDirection_.z, 0.12f, -bladeClashDirection_.x},
                1.72f);
            explosionParticles_.EmitBurst(
                cutCenter, 42, 0.68f,
                GPUParticleSystem::BurstStyle::Explosion,
                {1.0f, 0.78f, 0.34f, 0.50f},
                {bladeClashDirection_.x, 0.16f, bladeClashDirection_.z},
                1.18f);
            swordFlashParticles_.EmitBurst(
                cutCenter, 8, 0.42f, GPUParticleSystem::BurstStyle::Flash,
                {1.0f, 0.96f, 0.80f, 0.56f},
                {bladeClashDirection_.z, 0.0f, -bladeClashDirection_.x},
                0.26f);
            sparkParticles_.EmitBurst(
                cutCenter, 46, 0.26f, GPUParticleSystem::BurstStyle::Sparks,
                {1.0f, 0.82f, 0.28f, 0.58f},
                {bladeClashDirection_.z, 0.16f, -bladeClashDirection_.x},
                1.08f);
            smokeParticles_.EmitBurst(
                cutCenter, 18, 0.52f, GPUParticleSystem::BurstStyle::Smoke,
                {0.38f, 0.32f, 0.25f, 0.34f},
                {-bladeClashDirection_.x, 0.10f, -bladeClashDirection_.z},
                0.62f);
            CombatFeedbackEvent slashFeedback{};
            slashFeedback.type = CombatFeedbackEventType::BladeClashPierce;
            slashFeedback.position = cutCenter;
            slashFeedback.direction =
                {bladeClashDirection_.z, 0.0f, -bladeClashDirection_.x};
            slashFeedback.power = 9.5f;
            DispatchCombatFeedback(slashFeedback);
        }
        if (bladeClashFinishPlayerWon_ && bladeClashFinal_ &&
            bladeClashFinalBarrageStep_ == 0 &&
            bladeClashWinActionTimer >= 0.18f) {
            bladeClashFinalBarrageStep_ = 1;
            XMFLOAT3 pressureCenter = bladeClashCenter_;
            pressureCenter.y += 0.16f;
            swordFlashParticles_.EmitBurst(
                pressureCenter, 7, 0.38f, GPUParticleSystem::BurstStyle::Flash,
                {1.0f, 1.0f, 0.88f, 0.46f}, bladeClashDirection_, 0.24f);
            explosionParticles_.EmitBurst(
                pressureCenter, 70, 0.72f,
                GPUParticleSystem::BurstStyle::SpiritSparkle,
                {1.0f, 0.96f, 0.78f, 0.54f}, {0.0f, 1.0f, 0.0f}, 0.88f);
            CombatFeedbackEvent pressureFeedback{};
            pressureFeedback.type = CombatFeedbackEventType::CounterSuccess;
            pressureFeedback.position = pressureCenter;
            pressureFeedback.direction = bladeClashDirection_;
            pressureFeedback.power = 8.5f;
            DispatchCombatFeedback(pressureFeedback);
        }
        if (bladeClashFinishPlayerWon_ && bladeClashFinal_ &&
            !bladeClashFinishImpactEmitted_ &&
            bladeClashWinActionTimer >= 0.36f) {
            bladeClashFinishImpactEmitted_ = true;
            XMFLOAT3 blastCenter = enemy_.GetTransform().position;
            blastCenter.y += 1.10f;
            explosionParticles_.EmitBurst(
                blastCenter, 270, 1.26f,
                GPUParticleSystem::BurstStyle::Explosion,
                {1.0f, 0.96f, 0.70f, 0.88f}, bladeClashDirection_, 2.65f);
            sparkParticles_.EmitBurst(
                blastCenter, 170, 0.76f, GPUParticleSystem::BurstStyle::Sparks,
                {1.0f, 0.66f, 0.14f, 0.86f}, bladeClashDirection_, 2.42f);
            swordFlashParticles_.EmitBurst(
                blastCenter, 14, 0.68f, GPUParticleSystem::BurstStyle::Flash,
                {1.0f, 1.0f, 0.86f, 0.72f}, bladeClashDirection_, 0.48f);
            CombatFeedbackEvent blowbackFeedback{};
            blowbackFeedback.type = CombatFeedbackEventType::CounterSuccess;
            blowbackFeedback.position = blastCenter;
            blowbackFeedback.direction = bladeClashDirection_;
            blowbackFeedback.power = 22.0f;
            DispatchCombatFeedback(blowbackFeedback);
        }
        if (bladeClashFinishPlayerWon_ && bladeClashFinal_ &&
            !bladeClashFinishSkidEmitted_ &&
            bladeClashWinActionTimer >= 1.10f) {
            bladeClashFinishSkidEmitted_ = true;
            XMFLOAT3 skidCenter = enemy_.GetTransform().position;
            skidCenter.y += 0.22f;
            explosionParticles_.EmitBurst(
                skidCenter, 112, 0.82f,
                GPUParticleSystem::BurstStyle::Explosion,
                {1.0f, 0.58f, 0.14f, 0.66f},
                {bladeClashDirection_.x, 0.22f, bladeClashDirection_.z},
                1.62f);
            sparkParticles_.EmitBurst(
                skidCenter, 88, 0.54f, GPUParticleSystem::BurstStyle::Sparks,
                {1.0f, 0.72f, 0.18f, 0.84f},
                {bladeClashDirection_.z, 0.10f, -bladeClashDirection_.x},
                1.42f);
            smokeParticles_.EmitBurst(
                skidCenter, 86, 1.02f, GPUParticleSystem::BurstStyle::Smoke,
                {0.42f, 0.34f, 0.26f, 0.58f}, bladeClashDirection_, 1.26f);
        }
        if (bladeClashFinishPlayerWon_ && bladeClashFinal_ &&
            !bladeClashFinishWallImpactEmitted_ &&
            bladeClashWinActionTimer >= 2.28f) {
            bladeClashFinishWallImpactEmitted_ = true;
            XMFLOAT3 crashCenter = enemy_.GetTransform().position;
            crashCenter.y += 0.42f;
            explosionParticles_.EmitBurst(
                crashCenter, 240, 1.36f,
                GPUParticleSystem::BurstStyle::Explosion,
                {1.0f, 0.48f, 0.10f, 0.78f}, bladeClashDirection_, 2.70f);
            sparkParticles_.EmitBurst(
                crashCenter, 150, 0.86f, GPUParticleSystem::BurstStyle::Sparks,
                {1.0f, 0.76f, 0.22f, 0.86f},
                {-bladeClashDirection_.x, 0.12f, -bladeClashDirection_.z},
                2.10f);
            smokeParticles_.EmitBurst(
                crashCenter, 124, 1.36f, GPUParticleSystem::BurstStyle::Smoke,
                {0.36f, 0.30f, 0.26f, 0.62f}, bladeClashDirection_, 1.56f);
        }
        if (bladeClashFinishPlayerWon_ && !bladeClashFinal_ &&
            !bladeClashFinishSkidEmitted_ &&
            bladeClashWinActionTimer >= 0.76f) {
            bladeClashFinishSkidEmitted_ = true;
            XMFLOAT3 skidCenter = {
                enemy_.GetTransform().position.x + bladeClashDirection_.x * 6.8f,
                player_.GetTransform().position.y + 0.54f,
                enemy_.GetTransform().position.z + bladeClashDirection_.z * 6.8f};
            explosionParticles_.EmitBurst(
                skidCenter, 78, 0.92f,
                GPUParticleSystem::BurstStyle::SlashLine,
                {1.0f, 0.78f, 0.32f, 0.58f},
                {bladeClashDirection_.z, 0.02f, -bladeClashDirection_.x},
                1.46f);
            sparkParticles_.EmitBurst(
                skidCenter, 48, 0.46f, GPUParticleSystem::BurstStyle::Sparks,
                {1.0f, 0.70f, 0.20f, 0.70f},
                {-bladeClashDirection_.x, 0.04f, -bladeClashDirection_.z},
                1.08f);
            smokeParticles_.EmitBurst(
                skidCenter, 52, 0.94f, GPUParticleSystem::BurstStyle::Smoke,
                {0.36f, 0.30f, 0.25f, 0.52f},
                {bladeClashDirection_.x, 0.08f, bladeClashDirection_.z},
                1.08f);
        }
        if (bladeClashFinishTimer_ >= bladeClashFinishDuration_) {
            const bool shouldResolveEnemyTransition =
                bladeClashFinishPendingEnemyTransition_;
            const bool wasFinalBladeClash = bladeClashFinal_;
            bladeClashFinishActive_ = false;
            bladeClashFinal_ = false;
            bladeClashFinishPendingEnemyTransition_ = false;
            player_.SetBladeClashPose(false);
            player_.SetDefeatPoseRatio(0.0f);
            if (shouldResolveEnemyTransition) {
                enemy_.ResolveDeferredDamageTransitions();
                enemyHitCooldown_ = 0.22f;
            }
            if (!shouldResolveEnemyTransition && wasFinalBladeClash) {
                enemyHitCooldown_ = 0.35f;
            }
            if (player_.GetHP() > 0.0f && enemy_.GetHP() > 0.0f &&
                !enemy_.IsPhaseTransitionActive()) {
                enemy_.BeginBladeClashReturnWarp(BuildPlayerCombatObservation());
            }
        }
    }
    const float gameplayDeltaTime = baseDeltaTime * ComputeGameplayTimeScale();
    const bool chargeFocusHoldingEnemyAttack =
        chargeWeakPointFocusRatio_ > 0.001f && IsChargeWeakPointFocusActive();
    const float playerDeltaTime = gameplayDeltaTime;
    float enemyDeltaTime =
        bladeClashActive_
            ? 0.0f
            : counterCinematicActive_
            ? (baseDeltaTime *
               (std::min)(counterTimeScale_, ComputeGameplayTimeScale()))
            : (chargeFocusHoldingEnemyAttack ? baseDeltaTime
                                             : gameplayDeltaTime);
    if (enemy_.GetBossPhase() == BossPhase::Phase3 &&
        !enemy_.IsPhaseTransitionActive() && !bladeClashActive_ &&
        !counterCinematicActive_) {
        enemyDeltaTime *= 1.08f;
    }
    UpdateCamera(input);

    ctx_->model->UpdateAnimation(playerModelId_, playerDeltaTime);

    bool forceRangedReflectMove = false;
    for (const auto &wave : enemy_.GetWaves()) {
        if (wave.isAlive && !wave.isReflected) {
            forceRangedReflectMove = true;
            break;
        }
    }
    if (bladeClashFinishActive_ && bladeClashFinishPlayerWon_) {
        const float bladeClashWinActionTimer =
            (std::max)(0.0f, bladeClashFinishTimer_ -
                                  kBladeClashWinGuardBreakLead) /
            kBladeClashWinActionSlow;
        if (bladeClashWinActionTimer <= 0.0f) {
            const float guardT = std::clamp(
                bladeClashFinishTimer_ / kBladeClashWinGuardBreakLead, 0.0f,
                1.0f);
            const float brace = guardT < 0.10f ? guardT / 0.10f : 1.0f;
            const float strain =
                std::sinf(std::clamp(guardT / 0.42f, 0.0f,
                                     1.0f) *
                          kPi);
            const float collapseT = std::clamp(
                (bladeClashFinishTimer_ - kBladeClashWinGuardBreakImpactTime) /
                    (kBladeClashWinGuardBreakLead -
                     kBladeClashWinGuardBreakImpactTime),
                0.0f, 1.0f);
            const float collapseEase =
                1.0f - std::pow(1.0f - collapseT, 4.0f);
            const float snap =
                std::sinf(std::clamp(
                              (bladeClashFinishTimer_ -
                               kBladeClashWinGuardBreakImpactTime) /
                                  0.08f,
                              0.0f, 1.0f) *
                          kPi);
            const float finishYaw =
                std::atan2f(bladeClashDirection_.x, bladeClashDirection_.z);
            XMFLOAT3 bracePos = bladeClashFinishPlayerStart_;
            bracePos.x += bladeClashDirection_.x *
                          (0.07f * brace + 0.13f * strain);
            bracePos.z += bladeClashDirection_.z *
                          (0.07f * brace + 0.13f * strain);
            player_.SetCinematicBladeClashPose(
                bracePos, finishYaw,
                std::clamp(0.38f + 0.14f * strain + 0.10f * snap,
                           0.0f, 1.0f));
            player_.SetDefeatPoseRatio(0.0f);

            XMFLOAT3 enemyPos = {
                bladeClashFinishEnemyStart_.x +
                    bladeClashDirection_.x *
                        (0.13f * snap +
                         kBladeClashGuardBreakRecoilDistance * collapseEase),
                bladeClashFinishEnemyStart_.y + 0.09f * snap +
                    (kBladeClashGuardBreakLift -
                     kBladeClashGuardBreakDrop * 0.25f) *
                        collapseEase,
                bladeClashFinishEnemyStart_.z +
                    bladeClashDirection_.z *
                        (0.13f * snap +
                         kBladeClashGuardBreakRecoilDistance * collapseEase)};
            const float enemyYaw =
                std::atan2f(-bladeClashDirection_.x, -bladeClashDirection_.z);
            const float side = bladeClashDirection_.x >= 0.0f ? 1.0f : -1.0f;
            enemy_.SetCinematicTransform(enemyPos, enemyYaw,
                                         -kBladeClashGuardBreakPose *
                                             collapseEase,
                                         side *
                                             (0.085f * snap +
                                              0.090f * collapseEase));
        } else if (bladeClashFinal_) {
            const float braceT =
                std::clamp(bladeClashWinActionTimer / 0.36f, 0.0f, 1.0f);
            const float settleT =
                std::clamp((bladeClashWinActionTimer - 1.10f) / 0.70f, 0.0f,
                           1.0f);
            const float braceEase = 1.0f - std::pow(1.0f - braceT, 3.0f);
            const float settleEase = settleT * settleT * (3.0f - 2.0f * settleT);
            XMFLOAT3 finishPos = {
                bladeClashFinishPlayerStart_.x +
                    bladeClashDirection_.x * (0.74f * braceEase -
                                              0.34f * settleEase),
                bladeClashFinishPlayerStart_.y,
                bladeClashFinishPlayerStart_.z +
                    bladeClashDirection_.z * (0.74f * braceEase -
                                              0.34f * settleEase)};
            const float finishYaw =
                std::atan2f(bladeClashDirection_.x, bladeClashDirection_.z);
            player_.SetCinematicBladeClashPose(
                finishPos, finishYaw,
                std::clamp(0.84f - 0.20f * settleEase, 0.0f, 1.0f));
            player_.SetDefeatPoseRatio(0.0f);
        } else {
            const float windupT =
                std::clamp(bladeClashWinActionTimer / 0.12f, 0.0f, 1.0f);
            const float cutT = std::clamp(
                (bladeClashWinActionTimer - 0.03f) / 0.18f, 0.0f, 1.0f);
            const float slideT = std::clamp(
                (bladeClashWinActionTimer - 0.11f) / 0.44f, 0.0f, 1.0f);
            const float settleT = std::clamp(
                (bladeClashWinActionTimer - 0.50f) / 0.34f, 0.0f, 1.0f);
            const float windupEase =
                windupT * windupT * (3.0f - 2.0f * windupT);
            const float cutEase = 1.0f - std::pow(1.0f - cutT, 4.0f);
            const float slideEase = 1.0f - std::pow(1.0f - slideT, 2.0f);
            const float settleEase =
                settleT * settleT * (3.0f - 2.0f * settleT);
            const float dashEase =
                std::clamp(0.86f * cutEase + 0.24f * slideEase, 0.0f, 1.0f);
            XMFLOAT3 dashPos = Lerp(bladeClashFinishPlayerStart_,
                                    bladeClashFinishPlayerEnd_, dashEase);
            const float anticipation =
                std::sinf(windupEase * kPi) * (1.0f - cutEase);
            const float lift =
                std::sinf(cutT * kPi) * 0.10f * (1.0f - settleEase);
            dashPos.x -= bladeClashDirection_.x * 0.42f * anticipation;
            dashPos.y += lift;
            dashPos.z -= bladeClashDirection_.z * 0.42f * anticipation;
            player_.LockPosition(dashPos);

            const float slashPose =
                std::sinf(std::clamp((bladeClashWinActionTimer - 0.06f) / 0.38f,
                                     0.0f, 1.0f) *
                          kPi);
            const float finishYaw =
                std::atan2f(bladeClashDirection_.x, bladeClashDirection_.z);
            player_.SetCinematicBladeClashPose(
                dashPos, finishYaw,
                std::clamp(0.58f + 0.42f * slashPose - 0.16f * settleEase,
                           0.0f, 1.0f));

            const float enemyHitT = std::clamp(
                (bladeClashWinActionTimer - 0.08f) / 0.14f, 0.0f, 1.0f);
            const float enemyBreakT = std::clamp(
                (bladeClashWinActionTimer - 0.18f) / 0.38f, 0.0f, 1.0f);
            const float enemySlamT = std::clamp(
                (bladeClashWinActionTimer - 0.48f) / 0.38f, 0.0f, 1.0f);
            const float enemySlideT = std::clamp(
                (bladeClashWinActionTimer - 0.78f) / 0.52f, 0.0f, 1.0f);
            const float enemyHitEase =
                1.0f - std::pow(1.0f - enemyHitT, 5.0f);
            const float enemyBreakEase =
                enemyBreakT * enemyBreakT * (3.0f - 2.0f * enemyBreakT);
            const float enemySlamEase =
                1.0f - std::pow(1.0f - enemySlamT, 4.0f);
            const float enemySlideEase =
                1.0f - std::pow(1.0f - enemySlideT, 2.0f);
            const float hitPop = std::sinf(enemyHitT * kPi);
            const float breakArc = std::sinf(enemyBreakT * kPi);
            const float slamArc = std::sinf(enemySlamT * kPi);
            const float recoil =
                kBladeClashGuardBreakRecoilDistance +
                1.12f * enemyHitEase + 3.45f * enemyBreakEase +
                5.90f * enemySlamEase + 1.95f * enemySlideEase;
            const float side =
                bladeClashDirection_.x >= 0.0f ? 1.0f : -1.0f;
            const XMFLOAT3 right = {
                bladeClashDirection_.z, 0.0f, -bladeClashDirection_.x};
            const float sideDrift =
                side * (0.42f * hitPop + 0.92f * enemySlamEase -
                        0.18f * enemySlideEase);
            XMFLOAT3 enemyPos = {
                bladeClashFinishEnemyStart_.x + bladeClashDirection_.x * recoil +
                    right.x * sideDrift,
                bladeClashFinishEnemyStart_.y +
                    kBladeClashGuardBreakLift * (1.0f - 0.22f * enemySlamEase) -
                    kBladeClashGuardBreakDrop + hitPop * 0.50f +
                    breakArc * 1.12f + slamArc * 0.58f -
                    0.82f * enemySlideEase,
                bladeClashFinishEnemyStart_.z + bladeClashDirection_.z * recoil +
                    right.z * sideDrift};
            const float enemyYaw =
                std::atan2f(-bladeClashDirection_.x, -bladeClashDirection_.z);
            const float twistYaw =
                enemyYaw +
                side * (0.46f * hitPop + 0.98f * enemySlamEase -
                        0.22f * enemySlideEase);
            const float pitch =
                -0.18f * kBladeClashGuardBreakPose - 0.28f * hitPop -
                0.90f * enemySlamEase + 0.18f * enemySlideEase;
            const float roll =
                side * (0.36f * hitPop + 0.92f * enemySlamEase -
                        0.20f * enemySlideEase);
            enemy_.SetCinematicTransform(enemyPos, twistYaw, pitch, roll);

            if (enemySlamT > 0.02f && enemySlideT < 0.82f) {
                XMFLOAT3 burstCenter = enemyPos;
                burstCenter.y += 0.26f;
                smokeParticles_.EmitBurst(
                    burstCenter, 5, 0.30f, GPUParticleSystem::BurstStyle::Smoke,
                    {0.34f, 0.29f, 0.24f, 0.24f},
                    {-bladeClashDirection_.x, 0.06f, -bladeClashDirection_.z},
                    0.52f);
                sparkParticles_.EmitBurst(
                    burstCenter, 4, 0.14f, GPUParticleSystem::BurstStyle::Sparks,
                    {1.0f, 0.66f, 0.18f, 0.32f},
                    {right.x * side, 0.08f, right.z * side}, 0.64f);
            }
        }
    } else if (bladeClashFinishActive_) {
        const float leanT =
            std::clamp(bladeClashFinishTimer_ / 0.78f, 0.0f, 1.0f);
        const float recoilT =
            std::clamp((bladeClashFinishTimer_ - 0.78f) / 0.36f, 0.0f, 1.0f);
        const float slideT =
            std::clamp((bladeClashFinishTimer_ - 1.12f) / 0.52f, 0.0f, 1.0f);
        const float leanEase = leanT * leanT * (3.0f - 2.0f * leanT);
        const float recoilEase = 1.0f - std::pow(1.0f - recoilT, 3.0f);
        const float slideEase =
            slideT < 0.18f
                ? 0.06f *
                      std::pow(std::clamp(slideT / 0.18f, 0.0f, 1.0f), 2.0f)
                : slideT < 0.74f
                      ? 0.06f +
                            0.88f *
                                (1.0f -
                                 std::pow(1.0f -
                                              std::clamp((slideT - 0.18f) /
                                                             0.56f,
                                                         0.0f, 1.0f),
                                          5.0f))
                      : 0.94f +
                            0.06f *
                                (std::clamp((slideT - 0.74f) / 0.26f, 0.0f,
                                            1.0f) *
                                 std::clamp((slideT - 0.74f) / 0.26f, 0.0f,
                                            1.0f) *
                                 (3.0f -
                                  2.0f *
                                      std::clamp((slideT - 0.74f) / 0.26f,
                                                 0.0f, 1.0f)));
        const float retreat =
            0.24f * leanEase + 0.56f * recoilEase +
            (15.80f - 0.80f) * slideEase;
        XMFLOAT3 lossPos = {
            bladeClashFinishPlayerStart_.x - bladeClashDirection_.x * retreat,
            bladeClashFinishPlayerStart_.y,
            bladeClashFinishPlayerStart_.z - bladeClashDirection_.z * retreat};
        lossPos.y += std::sin(recoilT * kPi) * 0.16f;
        lossPos.y += std::sin(slideT * kPi) * 0.92f;
        player_.LockPosition(lossPos);
        if (bladeClashFinishSkidEmitted_) {
            const float faceEnemyYaw =
                std::atan2f(bladeClashDirection_.x, bladeClashDirection_.z);
            const float awayYaw =
                std::atan2f(-bladeClashDirection_.x, -bladeClashDirection_.z);
            player_.SetYaw(slideT > 0.001f ? awayYaw : faceEnemyYaw);
            player_.SetBladeClashPose(false);
            player_.SetDefeatPoseRatio(
                std::clamp(0.20f + 0.58f * recoilEase + 0.32f * slideEase,
                           0.0f, 1.0f));
        } else {
            player_.SetDefeatPoseRatio(0.0f);
            player_.SetBladeClashPose(true, 0.0f);
        }
    } else {
        const bool suppressLookAt =
            enemy_.ShouldSuppressPhase3PhantomBehindLookAt();
        const bool lockPlayerPositionForFarLaser =
            enemy_.ShouldLockPlayerForFarLaserSkill();
        const XMFLOAT3 farLaserLockedPlayerPos = player_.GetTransform().position;
        player_.Update(input, playerDeltaTime, enemy_.GetTransform().position,
                       cameraYaw_, forceRangedReflectMove, baseDeltaTime,
                       suppressLookAt);
        if (lockPlayerPositionForFarLaser) {
            player_.LockPosition(farLaserLockedPlayerPos);
        }
    }
    UpdateSwordVfx(baseDeltaTime);
    if (soundsLoaded_ && ctx_->sound != nullptr) {
        const auto slashStates = player_.GetSwordSlashStates();
        for (size_t i = 0; i < slashStates.size(); ++i) {
            if (slashStates[i] && !previousSwordSoundStates_[i]) {
                ctx_->sound->Play(slashSoundId_);
            }
        }
        previousSwordSoundStates_ = slashStates;
    }
    hud_.Update(*ctx_, player_.GetHP(), enemy_.GetHP(), enemy_.GetMaxHP());
    sceneLightTime_ += baseDeltaTime;
    battleElapsedTime_ += baseDeltaTime;

    if (!bladeClashFinishActive_) {
        const ActionKind previousEnemyActionKind = enemy_.GetActionKind();
        const ActionStep previousEnemyActionStep = enemy_.GetActionStep();
        enemy_.Update(BuildPlayerCombatObservation(), enemyDeltaTime);
        const ActionKind currentEnemyActionKind = enemy_.GetActionKind();
        const ActionStep currentEnemyActionStep = enemy_.GetActionStep();
        if (currentEnemyActionKind != previousEnemyActionKind ||
            currentEnemyActionStep != previousEnemyActionStep) {
            EmitEnemyActionParticles(currentEnemyActionKind,
                                     currentEnemyActionStep);
            const bool isEnemyAttackRelease =
                (currentEnemyActionKind == ActionKind::Smash ||
                 currentEnemyActionKind == ActionKind::Sweep ||
                 currentEnemyActionKind == ActionKind::BladeClash ||
                 currentEnemyActionKind == ActionKind::Wave ||
                 currentEnemyActionKind == ActionKind::Cage) &&
                currentEnemyActionStep == ActionStep::Active;
            if (isEnemyAttackRelease) {
                if (soundsLoaded_ && ctx_->sound != nullptr) {
                    ctx_->sound->Play(enemyReleaseSoundId_);
                }
            }
        }
    }
    ApplyEnemyCageConstraint(gameplayDeltaTime);
    UpdateSceneLighting();

    SyncEnemyAnimation();
    const bool bladeClashFinishWinAnim =
        bladeClashFinishActive_ && bladeClashFinishPlayerWon_;
    SetEnemyAnimationFrozen(
        counterCinematicActive_ ||
        (bladeClashFinishActive_ && bladeClashFinishPlayerWon_ &&
         !bladeClashFinishWinAnim));
    if (!enemyAnimationFrozen_) {
        float enemyAnimationDeltaTime = enemyDeltaTime;
        const ActionKind enemyActionKind = enemy_.GetActionKind();
        const ActionStep enemyActionStep = enemy_.GetActionStep();
        const bool bladeClashStartupAnim =
            enemyActionKind == ActionKind::BladeClash &&
            enemyActionStep == ActionStep::Charge;
        const bool phase3GuardCounterAnim =
            enemy_.IsPhase3GuardCounterGuarding();
        const bool cageCastAnim =
            enemyActionKind == ActionKind::Cage &&
            (enemyActionStep == ActionStep::Charge ||
             enemyActionStep == ActionStep::Active);
        if (bladeClashActive_ || bladeClashFinishWinAnim ||
            bladeClashStartupAnim || phase3GuardCounterAnim) {
            UpdateBladeClashEnemyAnimation(baseDeltaTime);
            if (bladeClashStartupAnim || phase3GuardCounterAnim) {
                enemyAnimationDeltaTime = 0.0f;
            }
        } else if (cageCastAnim) {
            UpdateCageEnemyAnimation(baseDeltaTime);
            enemyAnimationDeltaTime = 0.0f;
        } else if (enemyActionKind == ActionKind::Smash ||
            enemyActionKind == ActionKind::Sweep ||
            enemyActionKind == ActionKind::BladeClash ||
            enemyActionKind == ActionKind::Wave ||
            enemyActionKind == ActionKind::Cage) {
            if (IsChargeStanceSettled(enemyActionKind, enemyActionStep,
                                      enemy_.GetActionTimerForPresentation())) {
                enemyAnimationDeltaTime *= 0.035f;
            } else if (enemyActionStep == ActionStep::Active) {
                enemyAnimationDeltaTime *= 4.6f;
            } else if (enemyActionStep == ActionStep::Recovery) {
                enemyAnimationDeltaTime *= 0.72f;
            }
        }
        if (!bladeClashActive_) {
            ctx_->model->UpdateAnimation(enemyModelId_, enemyAnimationDeltaTime);
        }
    }
    ApplyEnemyProceduralAnimation();

    UpdateBattleCamera();
    camera_.UpdateMatrices();

    if (runMode_ == RunMode::TitleDemo) {
        UpdateTitleDemo(baseDeltaTime);
    } else {
        UpdateCombat(gameplayDeltaTime);
    }
    if (enemy_.IsPhaseTransitionActive() && !phaseTransitionWasActive_) {
        phaseTransitionWasActive_ = true;
        phaseTransitionReleaseEmitted_ = false;
        phaseTransitionLoopTimer_ = 0.0f;
        EmitPhaseTransitionStartEffects();
    }
    if (runMode_ == RunMode::Play && !battleResultRequested_ &&
        !bladeClashFinishActive_) {
        if (enemy_.GetHP() <= 0.0f) {
            BeginVictorySequence();
            return;
        }
        if (player_.GetHP() <= 0.0f) {
            BeginDefeatSequence();
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
    swordFlashParticles_.Update(baseDeltaTime);
}

void GameScene::Draw() {
    ctx_->model->PreDraw();

    const bool bladeClashWinFinish =
        bladeClashFinishActive_ && bladeClashFinishPlayerWon_;
    if (bladeClashWinFinish) {
        DrawBladeClashFinishBackdrop();
    } else {
        DrawArena();
    }
    const float playerVisualScale = bladeClashWinFinish ? 0.68f : 1.0f;
    const float enemyVisualScale = bladeClashWinFinish ? 1.32f : 1.0f;
    player_.Draw(ctx_->model, camera_,
                 !playerViewCamera_ || bladeClashFinishActive_,
                 bladeClashFinishActive_, playerVisualScale);
    if (!(victorySequenceActive_ && victoryFinalExplosionEmitted_)) {
        enemy_.Draw(ctx_->model, camera_, enemyVisualScale);
    }
    ctx_->model->PostDraw();
    swordTrailRenderer_.Draw(camera_);
    swordSlashArcRenderer_.Draw(camera_);

    if (!bladeClashWinFinish) {
        smokeParticles_.Draw(camera_);
        sparkParticles_.Draw(camera_);
        explosionParticles_.Draw(camera_);
        swordFlashParticles_.Draw(camera_);
    }
    DrawVictoryFlash();
    DrawDefeatFlash();
    DrawBattleIntroFlash();
}

void GameScene::DispatchCombatFeedback(const CombatFeedbackEvent &event) {
    combatFeedback_.PushEvent(event);
    EmitCombatParticles(event);
    if (event.type == CombatFeedbackEventType::PlayerSlashHit ||
        event.type == CombatFeedbackEventType::CounterSuccess) {
        DirectX::XMFLOAT2 slashDirection{0.0f, 0.0f};
        const auto swords = player_.GetSwords();
        if (event.swordIndex < swords.size() && swords[event.swordIndex]) {
            slashDirection = swords[event.swordIndex]->GetSlashDirection();
        }
        if (event.type == CombatFeedbackEventType::CounterSuccess) {
            swordSlashArcRenderer_.EmitParryLine(event.position, event.direction,
                                                 camera_, event.power,
                                                 slashDirection);
        } else {
            swordSlashArcRenderer_.EmitHitLine(event.position, event.direction,
                                               camera_, event.power,
                                               slashDirection);
        }
    }
    if (!soundsLoaded_ || ctx_ == nullptr || ctx_->sound == nullptr) {
        return;
    }

    switch (event.type) {
    case CombatFeedbackEventType::CounterSuccess:
    case CombatFeedbackEventType::BladeClashGuardBreak:
        ctx_->sound->Play(counterSoundId_);
        break;
    case CombatFeedbackEventType::PlayerSlashHit:
    case CombatFeedbackEventType::BladeClashPierce:
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

void GameScene::UpdateSwordVfx(float deltaTime) {
    swordTrailRenderer_.Update(player_, deltaTime);

    const auto slashStates = player_.GetSwordSlashStates();

    for (size_t i = 0; i < Player::kSwordCount; ++i) {
        prevSwordSlashStates_[i] = slashStates[i];
    }

    swordSlashArcRenderer_.Update(deltaTime);
}

void GameScene::EmitCombatParticles(const CombatFeedbackEvent &event) {
    XMFLOAT3 position = event.position;
    position.y += 0.08f;

    const XMFLOAT3 direction = event.direction;
    const float power = (std::max)(0.6f, event.power);

    switch (event.type) {
    case CombatFeedbackEventType::PlayerSlashHit:
        sparkParticles_.EmitBurst(position,
                                  static_cast<uint32_t>(34.0f + power * 12.0f),
                                  0.050f, GPUParticleSystem::BurstStyle::Sparks,
                                  {0.94f, 0.97f, 1.00f, 0.76f}, direction,
                                  2.35f + power * 0.38f);
        swordFlashParticles_.EmitBurst(
            position, static_cast<uint32_t>(14.0f + power * 4.0f),
            0.088f + power * 0.012f, GPUParticleSystem::BurstStyle::Flash,
            {1.0f, 0.98f, 1.00f, 0.72f}, direction, 0.38f + power * 0.05f);
        explosionParticles_.EmitBurst(
            position, static_cast<uint32_t>(54.0f + power * 12.0f),
            0.18f + power * 0.016f, GPUParticleSystem::BurstStyle::SlashLine,
            {0.94f, 0.90f, 1.00f, 0.82f}, direction, 2.18f + power * 0.30f);
        smokeParticles_.EmitBurst(
            position, static_cast<uint32_t>(6.0f + power * 2.0f),
            0.13f + power * 0.012f, GPUParticleSystem::BurstStyle::Smoke,
            {0.96f, 0.97f, 1.00f, 0.16f}, direction, 1.26f + power * 0.12f);
        break;
    case CombatFeedbackEventType::BladeClashPierce:
        sparkParticles_.EmitBurst(position,
                                  static_cast<uint32_t>(64.0f + power * 28.0f),
                                  0.13f, GPUParticleSystem::BurstStyle::Sparks,
                                  {1.00f, 0.68f, 0.28f, 1.0f}, direction,
                                  1.55f + power * 0.38f);
        break;
    case CombatFeedbackEventType::PlayerGuard:
        sparkParticles_.EmitBurst(position, 92, 0.20f,
                                  GPUParticleSystem::BurstStyle::Sparks,
                                  {1.00f, 0.74f, 0.36f, 1.0f}, direction, 1.95f);
        explosionParticles_.EmitBurst(position, 14, 0.16f,
                                      GPUParticleSystem::BurstStyle::Explosion,
                                      {0.92f, 0.50f, 0.22f, 1.0f}, direction,
                                      0.72f);
        smokeParticles_.EmitBurst(position, 10, 0.24f,
                                  GPUParticleSystem::BurstStyle::Smoke,
                                  {0.42f, 0.36f, 0.31f, 1.0f}, direction, 0.48f);
        break;
    case CombatFeedbackEventType::PlayerDamaged:
        sparkParticles_.EmitBurst(position, 110, 0.24f,
                                  GPUParticleSystem::BurstStyle::Sparks,
                                  {1.00f, 0.52f, 0.20f, 1.0f}, direction, 2.15f);
        explosionParticles_.EmitBurst(position, 42, 0.30f,
                                      GPUParticleSystem::BurstStyle::Explosion,
                                      {1.00f, 0.30f, 0.10f, 1.0f}, direction,
                                      1.15f);
        smokeParticles_.EmitBurst(position, 34, 0.40f,
                                  GPUParticleSystem::BurstStyle::Smoke,
                                  {0.48f, 0.36f, 0.28f, 1.0f}, direction, 0.78f);
        break;
    case CombatFeedbackEventType::CounterSuccess:
        swordFlashParticles_.EmitBurst(
            position, static_cast<uint32_t>(18.0f + power * 5.0f),
            0.24f + power * 0.030f, GPUParticleSystem::BurstStyle::Flash,
            {0.88f, 1.00f, 1.00f, 0.92f}, direction, 0.62f + power * 0.06f);
        sparkParticles_.EmitBurst(position,
                                  static_cast<uint32_t>(154.0f + power * 24.0f),
                                  0.42f + power * 0.035f,
                                  GPUParticleSystem::BurstStyle::Sparks,
                                  {0.04f, 0.72f, 1.00f, 0.96f}, direction,
                                  2.55f + power * 0.30f);
        explosionParticles_.EmitBurst(
            position, static_cast<uint32_t>(62.0f + power * 14.0f),
            0.48f + power * 0.030f, GPUParticleSystem::BurstStyle::SlashLine,
            {0.74f, 0.14f, 1.00f, 0.88f}, direction, 1.80f + power * 0.18f);
        explosionParticles_.EmitBurst(
            position, static_cast<uint32_t>(70.0f + power * 12.0f),
            0.58f + power * 0.035f, GPUParticleSystem::BurstStyle::SpiritSparkle,
            {0.28f, 0.92f, 1.00f, 0.76f}, direction, 1.62f + power * 0.20f);
        smokeParticles_.EmitBurst(position, 12, 0.34f,
                                  GPUParticleSystem::BurstStyle::Flash,
                                  {0.52f, 0.22f, 1.00f, 0.24f}, direction,
                                  0.48f);
        break;
    case CombatFeedbackEventType::BladeClashGuardBreak:
        sparkParticles_.EmitBurst(position, 170, 0.34f,
                                  GPUParticleSystem::BurstStyle::Sparks,
                                  {1.00f, 0.76f, 0.26f, 1.0f}, direction, 2.1f);
        explosionParticles_.EmitBurst(position, 78, 0.46f,
                                      GPUParticleSystem::BurstStyle::Explosion,
                                      {1.00f, 0.46f, 0.12f, 1.0f}, direction,
                                      1.48f);
        smokeParticles_.EmitBurst(position, 54, 0.56f,
                                  GPUParticleSystem::BurstStyle::Smoke,
                                  {0.36f, 0.31f, 0.27f, 1.0f}, direction, 0.92f);
        break;
    case CombatFeedbackEventType::ProjectileReflect:
        sparkParticles_.EmitBurst(position, 118, 0.24f,
                                  GPUParticleSystem::BurstStyle::Sparks,
                                  {0.96f, 0.86f, 0.64f, 1.0f}, direction, 2.15f);
        explosionParticles_.EmitBurst(position, 46, 0.30f,
                                      GPUParticleSystem::BurstStyle::Explosion,
                                      {0.68f, 0.78f, 0.72f, 1.0f}, direction,
                                      1.12f);
        smokeParticles_.EmitBurst(position, 24, 0.36f,
                                  GPUParticleSystem::BurstStyle::Smoke,
                                  {0.38f, 0.34f, 0.31f, 1.0f}, direction, 0.66f);
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
            explosionParticles_.EmitBurst(
                origin, 150, 2.10f,
                GPUParticleSystem::BurstStyle::SpiritSparkle,
                {1.0f, 1.0f, 0.96f, 0.72f}, forward, 1.68f);
            break;
        case ActionKind::Sweep:
            explosionParticles_.EmitBurst(
                origin, 158, 2.18f,
                GPUParticleSystem::BurstStyle::SpiritSparkle,
                {1.0f, 1.0f, 0.96f, 0.70f}, forward, 1.58f);
            break;
        case ActionKind::BladeClash:
            explosionParticles_.EmitBurst(
                origin, 56, 1.05f,
                GPUParticleSystem::BurstStyle::SpiritSparkle,
                {0.94f, 1.0f, 0.96f, 0.50f}, forward, 0.92f);
            smokeParticles_.EmitBurst(
                origin, 2, 0.34f, GPUParticleSystem::BurstStyle::Flash,
                {0.92f, 1.0f, 0.96f, 0.22f}, forward, 0.22f);
            break;
        case ActionKind::Wave:
            explosionParticles_.EmitBurst(
                origin, 130, 2.02f,
                GPUParticleSystem::BurstStyle::SpiritSparkle,
                {0.96f, 1.0f, 0.96f, 0.68f}, forward, 1.48f);
            break;
        case ActionKind::Laser:
            explosionParticles_.EmitBurst(
                origin, 150, 2.24f,
                GPUParticleSystem::BurstStyle::SpiritSparkle,
                {0.58f, 0.96f, 1.0f, 0.72f}, forward, 1.72f);
            sparkParticles_.EmitBurst(
                origin, 96, 0.52f, GPUParticleSystem::BurstStyle::SlashLine,
                {0.70f, 1.0f, 1.0f, 0.86f}, forward, 1.05f);
            break;
        case ActionKind::Cage: {
            XMFLOAT3 cageOrigin = player_.GetTransform().position;
            cageOrigin.y += 0.72f;
            explosionParticles_.EmitBurst(
                cageOrigin, 120, 1.70f,
                GPUParticleSystem::BurstStyle::SpiritSparkle,
                {0.54f, 1.0f, 0.94f, 0.66f}, {0.0f, 1.0f, 0.0f}, 1.35f);
            sparkParticles_.EmitBurst(
                cageOrigin, 64, 0.34f, GPUParticleSystem::BurstStyle::SlashLine,
                {1.0f, 0.94f, 0.54f, 0.78f}, {1.0f, 0.0f, 0.0f}, 0.82f);
            break;
        }
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

        const XMFLOAT4 cueColor{0.34f, 1.0f, 0.38f, 1.0f};
        sparkParticles_.EmitBurst(cuePos, 112, 1.46f,
                                  GPUParticleSystem::BurstStyle::SlashLine,
                                  cueColor, {slashDir.x, slashDir.y, 0.0f},
                                  1.06f);
        enemyWeakPointParticleTimer_ = 0.085f;
    }

    constexpr float kReleaseCounterWindowDuration = 0.62f;
    const float releaseAnticipation = enemy_.GetReleaseAnticipationRatio();
    const bool dualCounterCueVisible =
        kind == ActionKind::BladeClash && enemy_.IsBladeClashWindow();
    const bool phase2BladeClashStandbyCueVisible =
        kind == ActionKind::BladeClash && enemy_.IsPhase2BladeClashStandby();
    const bool preReleaseCounterCueVisible =
        !chargeDirectionVisible && actionStep != ActionStep::Active &&
        releaseAnticipation > 0.0f;
    const bool activeReleaseCounterCueVisible =
        actionStep == ActionStep::Active &&
        enemy_.GetActionTimerForPresentation() <=
            kReleaseCounterWindowDuration;
    const bool farLaserCueVisible =
        kind == ActionKind::Laser &&
        (actionStep == ActionStep::Charge || actionStep == ActionStep::Active);
    const bool phase3GuardCounterCueVisible =
        enemy_.IsPhase3GuardCounterActive() &&
        (kind == ActionKind::Smash || kind == ActionKind::Sweep) &&
        (actionStep == ActionStep::Charge ||
         actionStep == ActionStep::Active);
    const bool releaseCounterCueVisible =
        dualCounterCueVisible || phase2BladeClashStandbyCueVisible ||
        farLaserCueVisible ||
        (!chargeWeakPointFailedThisAction_ &&
         (kind == ActionKind::Smash || kind == ActionKind::Sweep) &&
         (phase3GuardCounterCueVisible || preReleaseCounterCueVisible ||
          activeReleaseCounterCueVisible));
    const bool badSlashCueVisible =
        (kind == ActionKind::Smash || kind == ActionKind::Sweep ||
         kind == ActionKind::BladeClash || kind == ActionKind::Laser) &&
        !chargeDirectionVisible && !releaseCounterCueVisible &&
        (actionStep == ActionStep::Charge || actionStep == ActionStep::Hold ||
         actionStep == ActionStep::Active);

    if (dualCounterCueVisible && enemyCueParticleTimer_ <= 0.0f) {
        const float yaw = enemy_.GetTelegraphYaw();
        const XMFLOAT3 forward = {std::sinf(yaw), 0.12f, std::cosf(yaw)};
        auto emitDualCue = [&](const XMFLOAT3 &basePos,
                               const XMFLOAT4 &sparkColor,
                               const XMFLOAT4 &flashColor) {
            XMFLOAT3 cuePos = basePos;
            cuePos.y += 0.42f;
            sparkParticles_.EmitBurst(cuePos, 28, 0.34f,
                                      GPUParticleSystem::BurstStyle::Sparks,
                                      sparkColor, forward, 1.85f);
            smokeParticles_.EmitBurst(cuePos, 2, 0.30f,
                                      GPUParticleSystem::BurstStyle::Flash,
                                      flashColor, forward, 0.28f);
        };

        const XMFLOAT3 cueBase = enemy_.GetBodyTransform().position;
        emitDualCue(cueBase, {0.22f, 1.0f, 0.34f, 0.92f},
                    {0.18f, 1.0f, 0.28f, 0.78f});
        enemyCueParticleTimer_ = 0.130f;
    } else if ((releaseCounterCueVisible || badSlashCueVisible) &&
        enemyCueParticleTimer_ <= 0.0f) {
        const float yaw = enemy_.GetTelegraphYaw();
        const XMFLOAT3 forward = {std::sinf(yaw), 0.12f, std::cosf(yaw)};
        XMFLOAT3 cuePos = enemyPos;
        cuePos.x += forward.x * 1.18f;
        cuePos.y += 1.28f;
        cuePos.z += forward.z * 1.18f;
        if (kind == ActionKind::BladeClash) {
            cuePos = enemy_.GetBodyTransform().position;
            cuePos.y += 0.42f;
        }
        const XMFLOAT4 cueColor =
            releaseCounterCueVisible ? XMFLOAT4{0.22f, 1.0f, 0.34f, 0.92f}
                                     : XMFLOAT4{1.0f, 0.02f, 0.12f, 0.98f};
        const float cuePower =
            dualCounterCueVisible
                ? 1.0f
                : (activeReleaseCounterCueVisible ||
                   phase3GuardCounterCueVisible)
                ? 1.0f
                : std::clamp(0.64f + releaseAnticipation * 0.28f, 0.64f,
                             0.92f);
        const uint32_t sparkCount =
            kind == ActionKind::BladeClash
                ? 30u
                : releaseCounterCueVisible
                ? ((activeReleaseCounterCueVisible ||
                    phase3GuardCounterCueVisible || dualCounterCueVisible)
                       ? 72u
                       : 52u)
                : 76u;
        const float sparkRadius =
            kind == ActionKind::BladeClash
                ? 0.34f
                : releaseCounterCueVisible ? 0.90f * cuePower : 1.18f;
        const float sparkSpeed =
            kind == ActionKind::BladeClash
                ? 1.65f
                : releaseCounterCueVisible ? 4.15f * cuePower : 4.70f;
        sparkParticles_.EmitBurst(cuePos, sparkCount, sparkRadius,
                                  GPUParticleSystem::BurstStyle::Sparks,
                                  cueColor, forward, sparkSpeed);
        const bool showCounterAxisLine =
            kind == ActionKind::Smash || kind == ActionKind::Sweep ||
            kind == ActionKind::Laser || phase2BladeClashStandbyCueVisible;
        if ((releaseCounterCueVisible || badSlashCueVisible) &&
            showCounterAxisLine) {
            const SwordCounterAxis cueAxis =
                RequiredVisualCounterAxisForAction(
                    kind, enemy_.GetFarLaserFollowupKind());
            sparkParticles_.EmitBurst(
                cuePos, releaseCounterCueVisible ? 104u : 116u,
                releaseCounterCueVisible ? 1.26f : 1.34f,
                GPUParticleSystem::BurstStyle::SlashLine, cueColor,
                CounterAxisParticleDirection(cueAxis), 0.96f);
            if (badSlashCueVisible) {
                sparkParticles_.EmitBurst(
                    cuePos, 46u, 0.74f,
                    GPUParticleSystem::BurstStyle::SlashLine,
                    {1.0f, 0.16f, 0.10f, 0.82f},
                    CounterAxisParticleDirection(cueAxis), 0.58f);
            }
        }
        const XMFLOAT4 flashColor =
            releaseCounterCueVisible ? XMFLOAT4{0.18f, 1.0f, 0.28f, 0.78f}
                                     : XMFLOAT4{1.0f, 0.04f, 0.10f, 0.88f};
        smokeParticles_.EmitBurst(cuePos,
                                  kind == ActionKind::BladeClash
                                      ? 2
                                      :
                                  releaseCounterCueVisible
                                      ? ((activeReleaseCounterCueVisible ||
                                          phase3GuardCounterCueVisible ||
                                          dualCounterCueVisible)
                                             ? 7
                                             : 4)
                                      : 5,
                                  kind == ActionKind::BladeClash
                                      ? 0.30f
                                      :
                                  releaseCounterCueVisible
                                      ? ((activeReleaseCounterCueVisible ||
                                          phase3GuardCounterCueVisible ||
                                          dualCounterCueVisible)
                                             ? 0.82f
                                             : 0.62f)
                                      : 0.70f,
                                  GPUParticleSystem::BurstStyle::Flash,
                                  flashColor, forward, 0.58f);
        enemyCueParticleTimer_ =
            kind == ActionKind::BladeClash ? 0.160f
                                     : releaseCounterCueVisible ? 0.110f : 0.125f;
    }

    const bool drawEnemySwordAfterimages = false;
    if (!drawEnemySwordAfterimages) {
        return;
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

    if (bladeClashFinishActive_) {
        const float ratio =
            bladeClashFinishDuration_ > 0.0001f
                ? std::clamp(bladeClashFinishTimer_ / bladeClashFinishDuration_,
                             0.0f, 1.0f)
                : 1.0f;
        const float hold = 1.0f - std::clamp((ratio - 0.76f) / 0.24f,
                                             0.0f, 1.0f);
        float radialBlurStrength =
            bladeClashFinishPlayerWon_ ? 0.018f * hold : 0.020f * hold;
        float vignetteStrength = bladeClashFinishPlayerWon_ ? 0.22f : 0.46f;
        float vignetteScale = 11.0f;
        float vignettePower = 1.15f;
        float sceneDimStrength =
            bladeClashFinishPlayerWon_ ? 0.03f * hold : 0.16f * hold;
        if (bladeClashFinishPlayerWon_) {
            const float guardBreakInT = std::clamp(
                (bladeClashFinishTimer_ -
                 (kBladeClashWinGuardBreakImpactTime - 0.035f)) /
                    0.16f,
                0.0f, 1.0f);
            const float guardBreakHold = 1.0f - std::clamp(
                (bladeClashFinishTimer_ - kBladeClashWinGuardBreakImpactTime) /
                    0.36f,
                0.0f, 1.0f);
            const float guardBreakPulse =
                std::sinf(guardBreakInT * kPi) * 0.10f +
                guardBreakHold * 0.28f;
            radialBlurStrength =
                (std::max)(radialBlurStrength, 0.024f * guardBreakHold);
            vignetteStrength =
                std::clamp((std::max)(vignetteStrength,
                                      0.26f + guardBreakPulse),
                           0.0f, 0.68f);
            vignetteScale = 8.2f;
            vignettePower = 1.25f;
            sceneDimStrength =
                (std::max)(sceneDimStrength, 0.06f * guardBreakHold);

            const float pierceTimer =
                (std::max)(0.0f, bladeClashFinishTimer_ -
                                      kBladeClashWinGuardBreakLead) /
                kBladeClashWinActionSlow;
            const float pierceT =
                std::clamp((pierceTimer - 0.24f) / 0.18f, 0.0f, 1.0f);
            const float pierceHold =
                pierceTimer >= 0.24f
                    ? 1.0f - std::clamp((pierceTimer - 0.32f) / 0.30f, 0.0f,
                                        1.0f)
                    : 0.0f;
            const float piercePulse =
                std::sinf(pierceT * kPi) * 0.020f + pierceHold * 0.026f;
            radialBlurStrength =
                (std::max)(radialBlurStrength, piercePulse);
        }
        ctx_->postEffectRenderer->SetRadialBlurCenter(0.5f, 0.48f);
        ctx_->postEffectRenderer->SetRadialBlurSampleCount(18);
        ctx_->postEffectRenderer->SetRadialBlurStrength(radialBlurStrength);
        ctx_->postEffectRenderer->SetVignettingEnabled(true);
        ctx_->postEffectRenderer->SetVignettingShape(vignetteScale,
                                                     vignettePower);
        ctx_->postEffectRenderer->SetVignettingStrength(vignetteStrength);
        ctx_->postEffectRenderer->SetSceneDimStrength(sceneDimStrength);
        return;
    }

    const float focus = chargeWeakPointFocusRatio_;
    if (focus <= 0.001f) {
        ctx_->postEffectRenderer->SetVignettingShape(11.0f, 1.15f);
        ctx_->postEffectRenderer->SetSceneDimStrength(0.0f);
        return;
    }

    ctx_->postEffectRenderer->SetRadialBlurCenter(0.5f, 0.48f);
    ctx_->postEffectRenderer->SetRadialBlurSampleCount(20);
    ctx_->postEffectRenderer->SetRadialBlurStrength(0.030f * focus);
    ctx_->postEffectRenderer->SetVignettingShape(11.0f, 1.15f);
    ctx_->postEffectRenderer->SetVignettingStrength(0.20f + 0.72f * focus);
    ctx_->postEffectRenderer->SetSceneDimStrength(0.42f * focus);
}

void GameScene::DrawOverlay() {
    if (runMode_ == RunMode::TitleDemo || battleIntroActive_) {
        return;
    }
    hud_.Draw(*ctx_);
    DrawChargeWeakPointTimeGauge();
    DrawBladeClashGauge();
}

void GameScene::UpdatePhaseTransitionCinematic(float deltaTime) {
    if (!phaseTransitionWasActive_) {
        phaseTransitionWasActive_ = true;
        phaseTransitionReleaseEmitted_ = false;
        phaseTransitionLoopTimer_ = 0.0f;
        EmitPhaseTransitionStartEffects();
    }

    sceneLightTime_ += deltaTime;
    combatFeedback_.Update(deltaTime, sceneLightTime_);
    UpdateChargeWeakPointFocus(deltaTime);

    const float ratio = enemy_.GetPhaseTransitionRatio();
    constexpr float kReleaseStart = 0.88f;
    constexpr float kReleaseDuration = 0.05f;
    const float charge = SmoothStep01(ratio / kReleaseStart);
    const float release = SmoothStep01((ratio - kReleaseStart) / kReleaseDuration);
    const float hold = charge * (1.0f - release);
    if (ctx_ != nullptr && ctx_->postEffectRenderer != nullptr) {
        ctx_->postEffectRenderer->SetVignettingEnabled(true);
        ctx_->postEffectRenderer->SetVignettingStrength(0.30f + 0.42f * hold);
        ctx_->postEffectRenderer->SetRadialBlurCenter(0.5f, 0.48f);
        ctx_->postEffectRenderer->SetRadialBlurSampleCount(20);
        ctx_->postEffectRenderer->SetRadialBlurStrength(
            0.010f + 0.026f * hold + 0.036f * release);
        ctx_->postEffectRenderer->SetSceneDimStrength(0.10f + 0.18f * hold);
    }

    enemy_.Update(BuildPlayerCombatObservation(), deltaTime);
    ctx_->model->UpdateAnimation(playerModelId_, deltaTime * 0.025f);
    UpdatePhaseTransitionEnemyAnimation(deltaTime);
    ApplyEnemyProceduralAnimation();
    UpdateSceneLighting();
    hud_.Update(*ctx_, player_.GetHP(), enemy_.GetHP(), enemy_.GetMaxHP());
    UpdateBattleCamera();
    camera_.UpdateMatrices();

    EmitPhaseTransitionLoopEffects(deltaTime);
    if (!phaseTransitionReleaseEmitted_ && ratio >= kReleaseStart) {
        phaseTransitionReleaseEmitted_ = true;
        EmitPhaseTransitionReleaseEffects();
    }

    sparkParticles_.Update(deltaTime);
    explosionParticles_.Update(deltaTime);
    smokeParticles_.Update(deltaTime);
    swordFlashParticles_.Update(deltaTime);

    if (!enemy_.IsPhaseTransitionActive()) {
        phaseTransitionWasActive_ = false;
        phaseTransitionReleaseEmitted_ = false;
        if (ctx_ != nullptr && ctx_->postEffectRenderer != nullptr) {
            ctx_->postEffectRenderer->SetRadialBlurStrength(0.0f);
            ctx_->postEffectRenderer->SetSceneDimStrength(0.0f);
            ctx_->postEffectRenderer->SetVignettingStrength(0.24f);
        }
    }
}

void GameScene::EmitPhaseTransitionStartEffects() {
    const XMFLOAT3 enemyPos = enemy_.GetTransform().position;
    const XMFLOAT3 origin{enemyPos.x, enemyPos.y + 1.18f, enemyPos.z};
    smokeParticles_.EmitBurst(origin, 54, 1.00f,
                              GPUParticleSystem::BurstStyle::Flash,
                              {1.0f, 0.52f, 0.12f, 0.86f},
                              {0.0f, 1.0f, 0.0f}, 0.62f);
    sparkParticles_.EmitBurst(origin, 150, 1.45f,
                              GPUParticleSystem::BurstStyle::Sparks,
                              {1.0f, 0.72f, 0.24f, 0.96f},
                              {0.0f, 1.0f, 0.0f}, 3.2f);
    if (soundsLoaded_ && ctx_ != nullptr && ctx_->sound != nullptr) {
        ctx_->sound->Play(enemyReleaseSoundId_);
    }
}

void GameScene::EmitPhaseTransitionLoopEffects(float deltaTime) {
    phaseTransitionLoopTimer_ -= deltaTime;
    if (phaseTransitionLoopTimer_ > 0.0f) {
        return;
    }
    phaseTransitionLoopTimer_ = 0.105f;

    const float ratio = enemy_.GetPhaseTransitionRatio();
    constexpr float kReleaseStart = 0.88f;
    constexpr float kReleaseDuration = 0.05f;
    const float charge = SmoothStep01(ratio / kReleaseStart);
    const float release = SmoothStep01((ratio - kReleaseStart) / kReleaseDuration);
    const float hold = charge * (1.0f - release);
    const XMFLOAT3 enemyPos = enemy_.GetTransform().position;
    const XMFLOAT3 origin{enemyPos.x, enemyPos.y + 1.28f, enemyPos.z};
    const uint32_t sparkCount =
        static_cast<uint32_t>(28.0f + 44.0f * hold);
    sparkParticles_.EmitBurst(origin, sparkCount, 0.58f + 0.46f * hold,
                              GPUParticleSystem::BurstStyle::Sparks,
                              {1.0f, 0.28f, 0.04f, 0.86f},
                              {0.0f, 1.0f, 0.0f}, 2.4f + 2.0f * hold);
    if (hold > 0.35f) {
        smokeParticles_.EmitBurst(origin, 3, 0.58f,
                                  GPUParticleSystem::BurstStyle::Flash,
                                  {1.0f, 0.20f, 0.04f, 0.52f},
                                  {0.0f, 1.0f, 0.0f}, 0.38f);
    }
}

void GameScene::EmitPhaseTransitionReleaseEffects() {
    const XMFLOAT3 enemyPos = enemy_.GetTransform().position;
    const XMFLOAT3 origin{enemyPos.x, enemyPos.y + 1.30f, enemyPos.z};
    explosionParticles_.EmitBurst(origin, 110, 1.38f,
                                  GPUParticleSystem::BurstStyle::Explosion,
                                  {1.0f, 0.30f, 0.05f, 0.92f},
                                  {0.0f, 1.0f, 0.0f}, 3.0f);
    sparkParticles_.EmitBurst(origin, 230, 1.85f,
                              GPUParticleSystem::BurstStyle::Sparks,
                              {1.0f, 0.86f, 0.30f, 1.0f},
                              {0.0f, 1.0f, 0.0f}, 7.0f);
    smokeParticles_.EmitBurst(origin, 52, 1.42f,
                              GPUParticleSystem::BurstStyle::Flash,
                              {1.0f, 0.58f, 0.12f, 0.76f},
                              {0.0f, 1.0f, 0.0f}, 1.0f);
    if (soundsLoaded_ && ctx_ != nullptr && ctx_->sound != nullptr) {
        ctx_->sound->Play(explosionSoundId_);
    }
}

void GameScene::UpdateBattleIntro(float deltaTime) {
    battleIntroTimer_ += deltaTime;
    sceneLightTime_ += deltaTime;
    combatFeedback_.Update(deltaTime, sceneLightTime_);

    const float ratio =
        std::clamp(battleIntroTimer_ / battleIntroDuration_, 0.0f, 1.0f);
    const float dissolveProgress =
        std::clamp((battleIntroTimer_ - 0.32f) / 2.02f, 0.0f, 1.0f);
    const float reveal = SmoothStep01(dissolveProgress);
    ApplyEnemyIntroDissolve(reveal);
    if (ctx_ != nullptr && ctx_->postEffectRenderer != nullptr) {
        ctx_->postEffectRenderer->SetVignettingEnabled(true);
        ctx_->postEffectRenderer->SetVignettingStrength(0.16f + 0.06f * ratio);
        ctx_->postEffectRenderer->SetRadialBlurCenter(0.5f, 0.48f);
        ctx_->postEffectRenderer->SetRadialBlurSampleCount(18);
        ctx_->postEffectRenderer->SetRadialBlurStrength(0.035f * (1.0f - ratio));
        ctx_->postEffectRenderer->SetSceneDimStrength(0.0f);
    }
    if (ctx_ != nullptr && ctx_->dxCommon != nullptr) {
        const float whiteBg =
            SmoothStep01((battleIntroTimer_ - 2.34f) / 0.38f);
        const XMFLOAT4 darkBg = {0.040f, 0.050f, 0.088f, 1.0f};
        const XMFLOAT4 paperWhite = {0.34f, 0.365f, 0.390f, 1.0f};
        ctx_->dxCommon->SetClearColor(
            {darkBg.x + (paperWhite.x - darkBg.x) * whiteBg,
             darkBg.y + (paperWhite.y - darkBg.y) * whiteBg,
             darkBg.z + (paperWhite.z - darkBg.z) * whiteBg, 1.0f});
    }

    if (!battleIntroRevealEmitted_ && battleIntroTimer_ >= 2.36f) {
        battleIntroRevealEmitted_ = true;
        const XMFLOAT3 enemyPos = enemy_.GetTransform().position;
        swordFlashParticles_.EmitBurst(
            {enemyPos.x, enemyPos.y + 1.45f, enemyPos.z}, 8, 0.38f,
            GPUParticleSystem::BurstStyle::Flash,
            {1.0f, 0.98f, 0.88f, 0.82f}, {0.0f, 1.0f, 0.0f}, 0.22f);
        explosionParticles_.EmitBurst(
            {enemyPos.x, enemyPos.y + 1.30f, enemyPos.z}, 156, 2.12f,
            GPUParticleSystem::BurstStyle::SpiritSparkle,
            {1.0f, 1.0f, 0.96f, 0.68f}, {0.0f, 1.0f, 0.0f}, 1.52f);
        if (soundsLoaded_ && ctx_ != nullptr && ctx_->sound != nullptr) {
            ctx_->sound->Play(enemyReleaseSoundId_);
        }
    }

    ctx_->model->UpdateAnimation(playerModelId_, deltaTime * 0.04f);
    enemy_.FaceTargetImmediately(player_.GetTransform().position);
    UpdateBattleIntroEnemyAnimation(deltaTime);
    ApplyEnemyProceduralAnimation();
    UpdateSceneLighting();
    UpdateBattleCamera();
    camera_.UpdateMatrices();
    sparkParticles_.Update(deltaTime);
    explosionParticles_.Update(deltaTime);
    smokeParticles_.Update(deltaTime);
    swordFlashParticles_.Update(deltaTime);

    if (battleIntroTimer_ >= battleIntroDuration_) {
        battleIntroActive_ = false;
        battleIntroTimer_ = battleIntroDuration_;
        ApplyEnemyIntroDissolve(1.0f);
        if (ctx_ != nullptr && ctx_->dxCommon != nullptr) {
            ctx_->dxCommon->SetClearColor({0.34f, 0.365f, 0.390f, 1.0f});
        }
        if (ctx_ != nullptr && ctx_->postEffectRenderer != nullptr) {
            ctx_->postEffectRenderer->SetRadialBlurStrength(0.0f);
            ctx_->postEffectRenderer->SetSceneDimStrength(0.0f);
            ctx_->postEffectRenderer->SetVignettingStrength(0.20f);
        }
    }
}

void GameScene::ApplyEnemyIntroDissolve(float revealRatio) {
    if (ctx_ == nullptr || ctx_->model == nullptr || enemyModelId_ == 0) {
        return;
    }
    Model *model = ctx_->model->GetModel(enemyModelId_);
    if (model == nullptr) {
        return;
    }

    const float reveal = std::clamp(revealRatio, 0.0f, 1.0f);
    const bool dissolveActive = battleIntroActive_ && reveal < 0.995f;
    const float liquidRipple =
        dissolveActive ? 0.045f * std::sinf(battleIntroTimer_ * 18.0f) *
                             (1.0f - reveal)
                       : 0.0f;
    const float threshold =
        std::clamp(1.08f - reveal * 1.18f + liquidRipple, 0.0f, 1.0f);
    for (ModelSubMesh &subMesh : model->subMeshes) {
        Material material = ctx_->model->GetMaterial(subMesh.materialId);
        material.enableDissolve = dissolveActive ? 1 : 0;
        material.dissolveThreshold = threshold;
        material.dissolveEdgeWidth = 0.11f + 0.11f * (1.0f - reveal);
        material.dissolveEdgeColor = {1.0f, 0.70f, 0.30f, 0.70f};
        ctx_->model->SetMaterial(subMesh.materialId, material);
    }
}

void GameScene::UpdateTitleDemo(float deltaTime) {
    titleDemoTimer_ += deltaTime;
    titleDemoCounterTimer_ -= deltaTime;
    if (titleDemoCounterTimer_ > 0.0f) {
        return;
    }

    titleDemoCounterTimer_ = 1.35f + 0.55f *
        (0.5f + 0.5f * std::sinf(titleDemoTimer_ * 1.7f));
    const XMFLOAT3 enemyPos = enemy_.GetTransform().position;
    const XMFLOAT3 playerPos = player_.GetTransform().position;
    enemy_.NotifyCountered(0.82f);
    if (enemy_.GetHP() > 45.0f) {
        enemy_.TakeDamage(3.0f);
    }
    player_.NotifyCounterSuccess(1);

    CombatFeedbackEvent feedback{};
    feedback.type = CombatFeedbackEventType::CounterSuccess;
    feedback.position = enemyPos;
    feedback.position.y += 1.15f;
    feedback.direction = {enemyPos.x - playerPos.x, 0.0f,
                          enemyPos.z - playerPos.z};
    feedback.power = 2.2f;
    feedback.swordIndex = 1;
    DispatchCombatFeedback(feedback);
    counterCinematicActive_ = true;
    counterCinematicTimer_ = 0.34f;
    SetEnemyAnimationFrozen(true);

    if (titleDemoTimer_ > 28.0f) {
        titleDemoTimer_ = 0.0f;
    }
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
    if (ctx_ != nullptr) {
        hud_.Update(*ctx_, player_.GetHP(), enemy_.GetHP(),
                    enemy_.GetMaxHP());
    }

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

void GameScene::BeginDefeatSequence() {
    battleResultRequested_ = true;
    defeatSequenceActive_ = true;
    defeatSequenceTimer_ = 0.0f;
    defeatImpactEmitted_ = false;
    counterCinematicActive_ = false;
    SetEnemyAnimationFrozen(false);
    player_.SetDefeatPoseRatio(0.0f);

    if (ctx_ != nullptr && ctx_->postEffectRenderer != nullptr) {
        ctx_->postEffectRenderer->SetVignettingEnabled(true);
        ctx_->postEffectRenderer->SetVignettingStrength(0.62f);
        ctx_->postEffectRenderer->SetRadialBlurCenter(0.5f, 0.54f);
        ctx_->postEffectRenderer->SetRadialBlurSampleCount(22);
        ctx_->postEffectRenderer->SetRadialBlurStrength(0.040f);
        ctx_->postEffectRenderer->SetSceneDimStrength(0.18f);
    }

    const XMFLOAT3 playerPos = player_.GetTransform().position;
    explosionParticles_.EmitBurst(
        {playerPos.x, playerPos.y + 1.05f, playerPos.z}, 180, 1.55f,
        GPUParticleSystem::BurstStyle::Explosion,
        {1.0f, 0.12f, 0.05f, 0.82f}, {0.0f, 1.0f, 0.0f}, 2.2f);
    sparkParticles_.EmitBurst(
        {playerPos.x, playerPos.y + 1.18f, playerPos.z}, 260, 1.35f,
        GPUParticleSystem::BurstStyle::Sparks,
        {1.0f, 0.32f, 0.16f, 0.92f}, {0.0f, 1.0f, 0.0f}, 5.2f);
}

void GameScene::UpdateDefeatSequence(float deltaTime) {
    defeatSequenceTimer_ += deltaTime;
    const float fallStart = 0.34f;
    const float fallDuration = 1.22f;
    const float fallRatio =
        std::clamp((defeatSequenceTimer_ - fallStart) / fallDuration, 0.0f,
                   1.0f);
    player_.SetDefeatPoseRatio(fallRatio);

    if (ctx_ != nullptr && ctx_->postEffectRenderer != nullptr) {
        const float ratio =
            std::clamp(defeatSequenceTimer_ / defeatSequenceDuration_, 0.0f,
                       1.0f);
        ctx_->postEffectRenderer->SetRadialBlurStrength(0.040f *
                                                        (1.0f - ratio));
        ctx_->postEffectRenderer->SetSceneDimStrength(0.18f + 0.22f * ratio);
    }

    if (!defeatImpactEmitted_ && defeatSequenceTimer_ >= 1.58f) {
        defeatImpactEmitted_ = true;
        const XMFLOAT3 playerPos = player_.GetTransform().position;
        smokeParticles_.EmitBurst(
            {playerPos.x, playerPos.y + 0.28f, playerPos.z}, 160, 2.15f,
            GPUParticleSystem::BurstStyle::Smoke,
            {0.18f, 0.17f, 0.16f, 0.88f}, {0.0f, 1.0f, 0.0f}, 0.85f);
        sparkParticles_.EmitBurst(
            {playerPos.x, playerPos.y + 0.42f, playerPos.z}, 180, 1.05f,
            GPUParticleSystem::BurstStyle::Sparks,
            {1.0f, 0.18f, 0.08f, 0.86f}, {0.0f, 1.0f, 0.0f}, 4.2f);
    }

    if (defeatSequenceTimer_ >= defeatSequenceDuration_) {
        defeatSequenceActive_ = false;
        player_.SetDefeatPoseRatio(0.0f);
        if (ctx_ != nullptr && ctx_->postEffectRenderer != nullptr) {
            ctx_->postEffectRenderer->SetRadialBlurStrength(0.0f);
            ctx_->postEffectRenderer->SetSceneDimStrength(0.0f);
        }
        sceneManager_->ChangeScene(std::make_unique<BattleResultScene>(
            BattleResultScene::ResultKind::GameOver, battleElapsedTime_,
            inputCalibration_));
    }
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
            inputCalibration_));
    }
}

void GameScene::DrawBladeClashFinishFrame() {
    if (!bladeClashFinishActive_ || !bladeClashFinishPlayerWon_ ||
        ctx_ == nullptr || ctx_->sprite == nullptr || ctx_->winApp == nullptr) {
        return;
    }

    const float screenW = static_cast<float>(ctx_->winApp->GetWidth());
    const float screenH = static_cast<float>(ctx_->winApp->GetHeight());
    if (screenW <= 1.0f || screenH <= 1.0f) {
        return;
    }

    const float ratio =
        bladeClashFinishDuration_ > 0.0001f
            ? std::clamp(bladeClashFinishTimer_ / bladeClashFinishDuration_,
                         0.0f, 1.0f)
            : 1.0f;
    const float fadeIn = std::clamp(bladeClashFinishTimer_ / 0.16f, 0.0f, 1.0f);
    const float fadeOut = 1.0f - std::clamp((ratio - 0.90f) / 0.10f, 0.0f, 1.0f);
    const float alpha = fadeIn * fadeOut;
    if (alpha <= 0.01f) {
        return;
    }

    const auto pulse = [&](float center, float width, float peak) {
        return (std::max)(
            0.0f,
            (1.0f - std::fabs(bladeClashFinishTimer_ - center) / width) * peak);
    };
    const float flash =
        std::clamp((std::max)(pulse(0.28f, 0.16f, 1.0f),
                              (std::max)(pulse(0.56f, 0.20f, 0.72f),
                                         pulse(1.18f, 0.26f, 0.44f))),
                   0.0f, 1.0f);
    const float shimmer =
        0.72f + 0.28f * std::sinf(sceneLightTime_ * 22.0f);
    const float barH = std::clamp(screenH * 0.085f, 52.0f, 78.0f);
    const float lineH = std::clamp(screenH * 0.006f, 4.0f, 7.0f);

    auto drawSprite = [&](float x, float y, float w, float h,
                          const XMFLOAT4 &color) {
        Sprite sprite{};
        sprite.textureId = 0;
        sprite.position = {x, y};
        sprite.size = {w, h};
        sprite.color = color;
        ctx_->sprite->DrawSprite(sprite);
    };

    ctx_->sprite->PreDraw();
    drawSprite(0.0f, 0.0f, screenW, barH,
               {0.0f, 0.0f, 0.0f, 0.30f * alpha});
    drawSprite(0.0f, screenH - barH, screenW, barH,
               {0.0f, 0.0f, 0.0f, 0.30f * alpha});
    drawSprite(0.0f, barH - lineH, screenW, lineH,
               {1.0f, 0.88f, 0.52f, (0.34f + 0.20f * flash) * alpha});
    drawSprite(0.0f, screenH - barH, screenW, lineH,
               {1.0f, 0.78f, 0.22f, (0.52f + 0.24f * flash) * alpha});

    const float cornerW = std::clamp(screenW * 0.13f, 120.0f, 190.0f);
    const float cornerH = std::clamp(screenH * 0.018f, 12.0f, 18.0f);
    drawSprite(28.0f, barH + 8.0f, cornerW, cornerH,
               {1.0f, 0.88f, 0.56f, 0.28f * shimmer * alpha});
    drawSprite(screenW - 28.0f - cornerW, screenH - barH - 8.0f - cornerH,
               cornerW, cornerH,
               {1.0f, 0.88f, 0.56f, 0.24f * shimmer * alpha});

    if (flash > 0.01f) {
        drawSprite(0.0f, 0.0f, screenW, screenH,
                   {1.0f, 0.95f, 0.72f, 0.10f * flash * alpha});
        drawSprite(screenW * 0.13f, screenH * 0.47f, screenW * 0.74f,
                   std::clamp(screenH * 0.010f, 7.0f, 12.0f),
                   {1.0f, 1.0f, 1.0f, 0.34f * flash * alpha});
        drawSprite(screenW * 0.19f, screenH * 0.515f, screenW * 0.62f,
                   std::clamp(screenH * 0.005f, 4.0f, 7.0f),
                   {1.0f, 0.72f, 0.28f, 0.22f * flash * alpha});
    }
    ctx_->sprite->PostDraw();
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

void GameScene::DrawDefeatFlash() {
    if (!defeatSequenceActive_ || ctx_ == nullptr || ctx_->sprite == nullptr ||
        ctx_->winApp == nullptr) {
        return;
    }

    const auto pulse = [&](float center, float width, float peak) {
        return (std::max)(
            0.0f, (1.0f - std::fabs(defeatSequenceTimer_ - center) / width) *
                      peak);
    };
    const float red =
        (std::max)(pulse(0.08f, 0.22f, 0.72f), pulse(1.58f, 0.30f, 0.36f));
    const float black =
        std::clamp((defeatSequenceTimer_ - 1.55f) /
                       (defeatSequenceDuration_ - 1.55f),
                   0.0f, 0.72f);
    const float w = static_cast<float>(ctx_->winApp->GetWidth());
    const float h = static_cast<float>(ctx_->winApp->GetHeight());

    ctx_->sprite->PreDraw();
    if (red > 0.01f) {
        Sprite flash{};
        flash.textureId = 0;
        flash.position = {0.0f, 0.0f};
        flash.size = {w, h};
        flash.color = {1.0f, 0.06f, 0.02f, red};
        ctx_->sprite->DrawSprite(flash);
    }
    if (black > 0.01f) {
        Sprite fade{};
        fade.textureId = 0;
        fade.position = {0.0f, 0.0f};
        fade.size = {w, h};
        fade.color = {0.0f, 0.0f, 0.0f, black};
        ctx_->sprite->DrawSprite(fade);
    }
    ctx_->sprite->PostDraw();
}

void GameScene::DrawBattleIntroFlash() {
    if (!battleIntroActive_ || ctx_ == nullptr || ctx_->sprite == nullptr ||
        ctx_->winApp == nullptr) {
        return;
    }

    const auto flashPulse = [&](float center, float width, float peak) {
        return (std::max)(
            0.0f, (1.0f - std::fabs(battleIntroTimer_ - center) / width) *
                      peak);
    };
    const float appearFlash = flashPulse(2.36f, 0.24f, 0.58f);
    const float afterGlow = flashPulse(2.68f, 0.52f, 0.22f);
    const float alpha = std::clamp((std::max)(appearFlash, afterGlow), 0.0f,
                                   0.62f);
    if (alpha <= 0.01f) {
        return;
    }

    const float w = static_cast<float>(ctx_->winApp->GetWidth());
    const float h = static_cast<float>(ctx_->winApp->GetHeight());
    ctx_->sprite->PreDraw();
    Sprite flash{};
    flash.textureId = 0;
    flash.position = {0.0f, 0.0f};
    flash.size = {w, h};
    flash.color = {1.0f, 0.985f, 0.93f, alpha};
    ctx_->sprite->DrawSprite(flash);
    if (appearFlash > 0.01f) {
        Sprite flare{};
        flare.textureId = 0;
        flare.position = {w * 0.15f, h * 0.39f};
        flare.size = {w * 0.70f, h * 0.18f};
        flare.color = {1.0f, 1.0f, 1.0f, appearFlash * 0.16f};
        ctx_->sprite->DrawSprite(flare);
    }
    ctx_->sprite->PostDraw();
}

void GameScene::DrawChargeWeakPointTimeGauge() {
    const float limit = enemy_.GetChargeWeakPointTimeLimitForPresentation();
    if (limit <= 0.0001f || chargeWeakPointBroken_ || ctx_ == nullptr ||
        ctx_->sprite == nullptr || ctx_->winApp == nullptr) {
        return;
    }

    const float remaining =
        enemy_.GetChargeWeakPointTimeRemainingForPresentation();
    const float ratio = std::clamp(remaining / limit, 0.0f, 1.0f);
    const float focus = std::clamp(chargeWeakPointFocusRatio_, 0.0f, 1.0f);
    const float alpha = std::clamp(0.16f + focus * 0.34f, 0.0f, 0.50f);

    const float screenW = static_cast<float>(ctx_->winApp->GetWidth());
    const float screenH = static_cast<float>(ctx_->winApp->GetHeight());
    const XMFLOAT2 center(screenW * 0.5f, screenH * 0.47f);
    const float radius = std::clamp(screenH * 0.34f, 190.0f, 290.0f);
    const float thickness = std::clamp(radius * 0.075f, 14.0f, 22.0f);

    constexpr int kSegments = 96;
    const int litSegments =
        static_cast<int>(std::ceil(static_cast<float>(kSegments) * ratio));
    const XMFLOAT4 progressColor =
        ratio < 0.28f
            ? XMFLOAT4{1.0f, 0.18f, 0.08f, 0.62f * alpha}
            : XMFLOAT4{0.20f, 0.92f, 1.0f, 0.58f * alpha};
    const float startAngle = -kPi * 0.5f;
    const float elapsedAngle = (1.0f - ratio) * kPi * 2.0f;
    const float progressStartAngle = startAngle + elapsedAngle;

    auto drawBlock = [&](float x, float y, float size,
                         const XMFLOAT4 &color) {
        Sprite sprite{};
        sprite.textureId = 0;
        sprite.position = {x - size * 0.5f, y - size * 0.5f};
        sprite.size = {size, size};
        sprite.color = color;
        ctx_->sprite->DrawSprite(sprite);
    };

    ctx_->sprite->PreDraw();
    for (int i = 0; i < kSegments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kSegments);
        const float angle = startAngle + kPi * 2.0f * t;
        const float x = center.x + std::cos(angle) * radius;
        const float y = center.y + std::sin(angle) * radius;
        drawBlock(x, y, thickness * 0.72f,
                  {0.02f, 0.03f, 0.035f, 0.20f * alpha});
    }
    for (int i = 0; i < kSegments; i += 6) {
        const float t = static_cast<float>(i) / static_cast<float>(kSegments);
        const float angle = startAngle + kPi * 2.0f * t;
        drawBlock(center.x + std::cos(angle) * (radius + thickness * 0.55f),
                  center.y + std::sin(angle) * (radius + thickness * 0.55f),
                  thickness * 0.38f,
                  {1.0f, 1.0f, 1.0f, 0.13f * alpha});
    }
    for (int i = 0; i < litSegments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kSegments);
        const float angle = progressStartAngle + kPi * 2.0f * t;
        const float pulse = 0.84f + 0.16f * std::sinf(sceneLightTime_ * 12.0f +
                                                      static_cast<float>(i) * 0.17f);
        XMFLOAT4 color = progressColor;
        color.w *= pulse;
        drawBlock(center.x + std::cos(angle) * radius,
                  center.y + std::sin(angle) * radius, thickness, color);
    }
    ctx_->sprite->PostDraw();
}

void GameScene::DrawBladeClashGauge() {
    if (!bladeClashActive_ || ctx_ == nullptr || ctx_->sprite == nullptr ||
        ctx_->winApp == nullptr) {
        return;
    }

    const float screenW = static_cast<float>(ctx_->winApp->GetWidth());
    const float screenH = static_cast<float>(ctx_->winApp->GetHeight());
    const float centerX = screenW * 0.5f;
    const float centerY = screenH * 0.52f;
    const float width = std::clamp(screenW * 0.36f, 360.0f, 620.0f);
    const float height = 30.0f;
    const float progress = std::clamp((bladeClashGauge_ + 1.0f) * 0.5f,
                                      0.0f, 1.0f);
    const float timeRatio =
        bladeClashDuration_ > 0.0001f
            ? std::clamp(bladeClashTimer_ / bladeClashDuration_, 0.0f, 1.0f)
            : 0.0f;
    const float impact = std::clamp(bladeClashImpactPulse_, 0.0f, 1.0f);
    const float urgent = timeRatio < 0.25f ? (1.0f - timeRatio / 0.25f) : 0.0f;
    const float pulse =
        0.78f + 0.22f * std::sinf(sceneLightTime_ * (22.0f + 18.0f * urgent));

    auto drawSprite = [&](float x, float y, float w, float h,
                          const XMFLOAT4 &color) {
        Sprite sprite{};
        sprite.textureId = 0;
        sprite.position = {x, y};
        sprite.size = {w, h};
        sprite.color = color;
        ctx_->sprite->DrawSprite(sprite);
    };

    ctx_->sprite->PreDraw();
    const XMFLOAT4 frameColor =
        bladeClashFinal_ ? XMFLOAT4{0.30f, 0.08f, 0.02f, 0.84f}
                         : XMFLOAT4{0.02f, 0.025f, 0.030f, 0.72f};
    const XMFLOAT4 enemyBase =
        bladeClashFinal_ ? XMFLOAT4{0.24f, 0.02f, 0.01f, 0.92f}
                         : XMFLOAT4{0.12f, 0.05f, 0.05f, 0.88f};
    const XMFLOAT4 playerBase =
        bladeClashFinal_ ? XMFLOAT4{0.05f, 0.10f, 0.20f, 0.92f}
                         : XMFLOAT4{0.04f, 0.12f, 0.16f, 0.88f};
    const XMFLOAT4 playerFill =
        bladeClashFinal_
            ? XMFLOAT4{1.0f, 0.92f, 0.44f, 0.30f + 0.24f * impact}
            : XMFLOAT4{0.12f, 0.86f, 0.92f, 0.22f + 0.18f * impact};
    const XMFLOAT4 enemyFill =
        bladeClashFinal_
            ? XMFLOAT4{1.0f, 0.10f, 0.02f, 0.18f + 0.18f * urgent}
            : XMFLOAT4{1.0f, 0.18f, 0.05f, 0.10f + 0.10f * urgent};

    drawSprite(centerX - width * 0.5f - 8.0f, centerY - height * 0.5f - 8.0f,
               width + 16.0f, height + 16.0f,
               frameColor);
    drawSprite(centerX - width * 0.5f, centerY - height * 0.5f, width, height,
               enemyBase);
    drawSprite(centerX, centerY - height * 0.5f, width * 0.5f, height,
               playerBase);
    drawSprite(centerX - width * 0.5f, centerY - height * 0.5f,
               width * progress, height, playerFill);
    drawSprite(centerX - width * 0.5f + width * progress,
               centerY - height * 0.5f,
               width * (1.0f - progress), height, enemyFill);
    for (int i = 1; i < 8; ++i) {
        const float tickX = centerX - width * 0.5f +
                            width * static_cast<float>(i) / 8.0f;
        drawSprite(tickX - 1.0f, centerY - height * 0.5f, 2.0f, height,
                   {1.0f, 1.0f, 1.0f, i == 4 ? 0.30f : 0.13f});
    }

    const float markerX = centerX - width * 0.5f + width * progress;
    const float markerW = 16.0f + 10.0f * impact;
    drawSprite(markerX - markerW * 0.5f, centerY - height * 0.5f - 14.0f,
               markerW, height + 28.0f,
               bladeClashFinal_
                   ? XMFLOAT4{1.0f, 0.92f, 0.46f, 0.95f * pulse}
                   : XMFLOAT4{0.90f, 1.0f, 1.0f, 0.90f * pulse});
    drawSprite(markerX - markerW * 0.5f - 7.0f,
               centerY - height * 0.5f - 6.0f,
               markerW + 14.0f, height + 12.0f,
               bladeClashFinal_
                   ? XMFLOAT4{1.0f, 0.34f, 0.08f, 0.16f * impact}
                   : XMFLOAT4{0.40f, 0.95f, 1.0f, 0.10f * impact});
    drawSprite(centerX - 2.0f, centerY - height * 0.5f - 10.0f, 4.0f,
               height + 20.0f, {1.0f, 1.0f, 1.0f, 0.34f});

    const float timeW = width * timeRatio;
    drawSprite(centerX - width * 0.5f, centerY + height * 0.5f + 10.0f,
               width, 6.0f, {0.02f, 0.02f, 0.02f, 0.70f});
    drawSprite(centerX - width * 0.5f, centerY + height * 0.5f + 10.0f,
               timeW, 6.0f,
               bladeClashFinal_
                   ? (timeRatio < 0.28f
                          ? XMFLOAT4{1.0f, 0.08f, 0.02f, 0.98f}
                          : XMFLOAT4{1.0f, 0.78f, 0.22f, 0.96f})
                   : (timeRatio < 0.28f
                          ? XMFLOAT4{1.0f, 0.14f, 0.06f, 0.92f}
                          : XMFLOAT4{0.94f, 0.82f, 0.30f, 0.90f}));
    ctx_->sprite->PostDraw();
}

void GameScene::DrawEnemyWeaponTrail() {
    if (enemyWeaponTrailModelId_ == 0) {
        return;
    }

    const ActionKind kind = enemy_.GetActionKind();
    const ActionStep step = enemy_.GetActionStep();
    const bool isTrailStep =
        step == ActionStep::Active ||
        (kind == ActionKind::BladeClash && step == ActionStep::Charge);
    const bool shouldRecordCurrent =
        (kind == ActionKind::Smash || kind == ActionKind::Sweep ||
         kind == ActionKind::BladeClash) &&
        isTrailStep;

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
            sampleKind == ActionKind::Smash ? (kPi * 0.5f)
            : sampleKind == ActionKind::BladeClash ? 0.18f
                                                    : 0.0f;
        outRoll =
            projectedLen > 0.24f ? std::atan2f(bladeY, bladeX) : fallbackRoll;
        const float baseLength =
            sampleKind == ActionKind::Smash ? 2.25f
            : sampleKind == ActionKind::BladeClash ? 3.10f
                                                    : 2.85f;
        const float maxLength =
            sampleKind == ActionKind::Smash ? 4.15f
            : sampleKind == ActionKind::BladeClash ? 4.85f
                                                    : 4.55f;
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
    XMFLOAT3 bladeTip{};
    if (kind == ActionKind::Smash) {
        bladeTip = {handPos.x - forwardX * 0.25f, handPos.y + 1.75f,
                    handPos.z - forwardZ * 0.25f};
    } else if (kind == ActionKind::BladeClash) {
        const XMFLOAT3 bodyPos = enemy_.GetBodyTransform().position;
        bladeRoot = {bodyPos.x + forwardX * 0.86f + rightX * 1.35f,
                     handPos.y + 0.10f,
                     bodyPos.z + forwardZ * 0.86f + rightZ * 1.35f};
        bladeTip = {bodyPos.x + forwardX * 0.86f - rightX * 1.45f,
                    bladeRoot.y + 0.01f,
                    bodyPos.z + forwardZ * 0.86f - rightZ * 1.45f};
    } else {
        bladeTip = {handPos.x - rightX * 1.85f, handPos.y + 0.14f,
                    handPos.z - rightZ * 1.85f};
    }

    if (kind != ActionKind::BladeClash && ctx_ != nullptr &&
        ctx_->model != nullptr) {
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
        tipV += dir *
                (kind == ActionKind::BladeClash
                     ? (0.68f + activeBoost * 0.34f)
                     : (0.48f + activeBoost * 0.20f));
        XMStoreFloat3(&bladeRoot, rootV);
        XMStoreFloat3(&bladeTip, tipV);
    }

    XMFLOAT3 trailCenter{};
    float billboardYaw = 0.0f;
    float trailRoll = 0.0f;
    float trailLength = 0.0f;
    float trailThickness = 0.0f;
    const float currentThickness =
        (kind == ActionKind::Smash ? 0.32f
         : kind == ActionKind::BladeClash ? 0.52f
                                          : 0.38f) +
        activeBoost * 0.12f;
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
                ? XMFLOAT4{1.0f, 0.70f, 0.18f, 0.70f}
            : kind == ActionKind::BladeClash
                ? XMFLOAT4{0.48f, 1.0f, 0.74f, 0.86f}
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
        drawTrailPlane({trailCenter.x + toCameraX * 0.035f,
                        trailCenter.y + 0.010f,
                        trailCenter.z + toCameraZ * 0.035f},
                       billboardYaw, trailRoll,
                       {trailLength * 0.90f, trailThickness, 1.0f},
                       {1.0f, 0.70f, 0.18f, 0.70f},
                       1.12f + 0.22f * pulse + activeBoost * 0.50f, 0.02f);
    } else if (kind == ActionKind::Sweep) {
        drawTrailPlane({trailCenter.x + toCameraX * 0.035f,
                        trailCenter.y + 0.010f,
                        trailCenter.z + toCameraZ * 0.035f},
                       billboardYaw, trailRoll,
                       {trailLength * 0.90f, trailThickness, 1.0f},
                       {0.10f, 0.86f, 1.0f, 0.72f},
                       1.18f + 0.22f * pulse + activeBoost * 0.55f, 0.02f);
    } else if (kind == ActionKind::BladeClash) {
        const float clashGlow = 0.5f + 0.5f * std::sinf(sceneLightTime_ * 30.0f);
        drawTrailPlane({trailCenter.x + toCameraX * 0.040f,
                        trailCenter.y + 0.014f,
                        trailCenter.z + toCameraZ * 0.040f},
                       billboardYaw, trailRoll,
                       {trailLength * 0.98f, trailThickness, 1.0f},
                       {0.50f, 1.0f, 0.72f, 0.84f},
                       1.55f + 0.34f * clashGlow + activeBoost * 0.45f, 0.035f);
        drawTrailPlane({trailCenter.x + toCameraX * 0.055f,
                        trailCenter.y + 0.018f,
                        trailCenter.z + toCameraZ * 0.055f},
                       billboardYaw, trailRoll,
                       {trailLength * 0.72f, trailThickness * 0.42f, 1.0f},
                       {1.0f, 0.92f, 0.58f, 0.62f},
                       1.02f + 0.28f * clashGlow, 0.012f);
    }

    ctx_->model->ClearDrawEffect();
}

void GameScene::DrawChargeWeakPoint() {
}

void GameScene::DrawBladeClashFinishBackdrop() {
    DrawArena();
}

void GameScene::DrawDistantHazardBackdrop() {
    ModelManager *model = ctx_->model;
    const float pulse = 0.5f + 0.5f * std::sinf(sceneLightTime_ * 1.8f);

    ModelDrawEffect cityEffect{};
    cityEffect.enabled = true;
    cityEffect.additiveBlend = false;
    cityEffect.disableCulling = true;
    cityEffect.color = {0.16f, 0.20f, 0.24f, 0.90f};
    cityEffect.intensity = 0.12f;
    cityEffect.fresnelPower = 0.95f;
    cityEffect.noiseAmount = 0.0f;
    cityEffect.time = sceneLightTime_ * 0.45f;
    model->SetDrawEffect(cityEffect);
    for (int side = 0; side < 4; ++side) {
        const bool alongX = side < 2;
        const float sign = (side == 0 || side == 2) ? 1.0f : -1.0f;
        for (int row = 0; row < 3; ++row) {
            for (int i = 0; i < 17; ++i) {
                const float lane = (static_cast<float>(i) - 8.0f) * 4.15f;
                const float depthLane = 48.0f + static_cast<float>(row) * 4.1f;
                const float height =
                    2.4f + static_cast<float>((i * 5 + row * 7 + side) % 10) *
                               0.45f +
                    static_cast<float>(row) * 0.55f;
                Transform tower{};
                tower.position = alongX
                                     ? XMFLOAT3{lane, -0.68f,
                                               sign * depthLane + 8.0f}
                                     : XMFLOAT3{sign * depthLane, -0.68f,
                                               lane + 8.0f};
                tower.rotation =
                    MakeQuat(0.0f, alongX ? 0.0f : kPi * 0.5f, 0.0f);
                tower.scale = {0.62f + static_cast<float>((i + row) % 3) * 0.16f,
                               height,
                               0.78f + static_cast<float>((i * 3 + row) % 2) *
                                           0.24f};
                model->Draw(arenaCityTowerModelId_, tower, camera_);
            }
        }
    }
    model->ClearDrawEffect();

    ModelDrawEffect blockEffect = cityEffect;
    blockEffect.color = {0.07f, 0.10f, 0.13f, 0.92f};
    blockEffect.intensity = 0.18f;
    model->SetDrawEffect(blockEffect);
    for (int i = 0; i < 15; ++i) {
        const float offset = static_cast<float>(i - 7);
        Transform wall{};
        wall.position = {offset * 1.72f, -0.76f,
                         72.0f + std::fabs(offset) * 0.42f};
        wall.rotation = MakeQuat(0.0f, 0.0f, 0.0f);
        wall.scale = {1.28f + 0.12f * static_cast<float>(i % 3),
                      5.3f + static_cast<float>((i * 5) % 5) * 0.72f,
                      1.08f};
        model->Draw(arenaCityTowerModelId_, wall, camera_);
    }
    for (int side = 0; side < 2; ++side) {
        const float sign = side == 0 ? -1.0f : 1.0f;
        for (int step = 0; step < 4; ++step) {
            Transform brace{};
            brace.position = {sign * (9.2f + static_cast<float>(step) * 2.25f),
                              2.25f + static_cast<float>(step) * 0.62f,
                              69.4f + static_cast<float>(step) * 1.55f};
            brace.rotation = MakeQuat(0.0f, sign * 0.12f, 0.0f);
            brace.scale = {2.8f - static_cast<float>(step) * 0.22f, 0.30f,
                           0.78f};
            model->Draw(arenaCityTowerModelId_, brace, camera_);
        }
    }
    model->ClearDrawEffect();

    ModelDrawEffect lightEffect{};
    lightEffect.enabled = true;
    lightEffect.additiveBlend = true;
    lightEffect.disableCulling = true;
    lightEffect.color = {0.96f, 0.58f, 0.28f, 0.18f};
    lightEffect.intensity = 0.035f + 0.025f * pulse;
    lightEffect.fresnelPower = 0.8f;
    lightEffect.noiseAmount = 0.0f;
    lightEffect.time = sceneLightTime_;
    model->SetDrawEffect(lightEffect);
    for (int side = 0; side < 4; ++side) {
        const bool alongX = side < 2;
        const float sign = (side == 0 || side == 2) ? 1.0f : -1.0f;
        for (int i = 0; i < 11; ++i) {
            const float lane = (static_cast<float>(i) - 5.0f) * 4.0f;
            Transform panel{};
            panel.position = alongX
                                 ? XMFLOAT3{lane, 1.15f + static_cast<float>(i % 4) * 0.58f,
                                           sign * 47.35f + 8.0f}
                                 : XMFLOAT3{sign * 47.35f,
                                           1.15f + static_cast<float>(i % 4) * 0.58f,
                                           lane + 8.0f};
            panel.rotation =
                MakeQuat(0.0f, alongX ? 0.0f : kPi * 0.5f, 0.0f);
            panel.scale = {0.16f, 1.05f + static_cast<float>(i % 2) * 0.40f,
                           1.0f};
            model->Draw(arenaCityWindowModelId_, panel, camera_);
        }
    }
    model->ClearDrawEffect();
}

void GameScene::DrawArena() {
    ModelManager *model = ctx_->model;
    const float pulse = 0.5f + 0.5f * std::sinf(sceneLightTime_ * 2.2f);
    constexpr float kPatternSpacing = 3.55f;
    constexpr float kLaneSpacing = 4.25f;

    DrawDistantHazardBackdrop();

    Transform floor{};
    floor.position = {0.0f, -0.04f, 0.0f};
    floor.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
    floor.scale = {180.0f, 180.0f, 1.0f};
    model->Draw(arenaFloorModelId_, floor, camera_);

    ModelDrawEffect fieldEffect{};
    fieldEffect.enabled = true;
    fieldEffect.additiveBlend = false;
    fieldEffect.disableCulling = true;
    fieldEffect.color = {0.085f, 0.075f, 0.060f, 0.115f};
    fieldEffect.intensity = 0.004f;
    fieldEffect.fresnelPower = 0.7f;
    fieldEffect.noiseAmount = 0.0f;
    fieldEffect.time = sceneLightTime_;
    model->SetDrawEffect(fieldEffect);

    for (int z = -14; z <= 14; ++z) {
        for (int x = -14; x <= 14; ++x) {
            if ((std::abs(x) + std::abs(z)) % 2 != 0) {
                continue;
            }
            Transform tile{};
            tile.position = {static_cast<float>(x) * kPatternSpacing, 0.004f,
                             static_cast<float>(z) * kPatternSpacing};
            tile.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
            tile.scale = {2.26f, 2.26f, 1.0f};
            model->Draw(arenaSpokeModelId_, tile, camera_);
        }
    }

    Transform center{};
    center.position = {0.0f, 0.012f, 0.0f};
    center.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
    center.scale = {4.8f, 4.8f, 1.0f};
    model->Draw(arenaSpokeModelId_, center, camera_);

    for (int i = -13; i <= 13; ++i) {
        Transform laneX{};
        laneX.position = {0.0f, 0.016f,
                          static_cast<float>(i) * kLaneSpacing};
        laneX.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
        laneX.scale = {168.0f, i == 0 ? 0.080f : 0.034f, 1.0f};
        model->Draw(arenaSpokeModelId_, laneX, camera_);

        Transform laneZ{};
        laneZ.position = {static_cast<float>(i) * kLaneSpacing, 0.017f,
                          0.0f};
        laneZ.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
        laneZ.scale = {i == 0 ? 0.080f : 0.034f, 168.0f, 1.0f};
        model->Draw(arenaSpokeModelId_, laneZ, camera_);
    }
    model->ClearDrawEffect();

    ModelDrawEffect lineEffect{};
    lineEffect.enabled = true;
    lineEffect.additiveBlend = true;
    lineEffect.disableCulling = true;
    lineEffect.color = {0.22f, 0.10f, 0.045f, 0.026f};
    lineEffect.intensity = 0.0015f + 0.0015f * pulse;
    lineEffect.fresnelPower = 0.75f;
    lineEffect.noiseAmount = 0.0f;
    lineEffect.time = sceneLightTime_;
    model->SetDrawEffect(lineEffect);
    for (int i = -15; i <= 15; ++i) {
        Transform lightX{};
        lightX.position = {0.0f, 0.034f,
                           static_cast<float>(i) * kPatternSpacing};
        lightX.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
        lightX.scale = {160.0f, 0.016f, 1.0f};
        model->Draw(arenaCityWindowModelId_, lightX, camera_);

        Transform lightZ{};
        lightZ.position = {static_cast<float>(i) * kPatternSpacing, 0.035f,
                           0.0f};
        lightZ.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
        lightZ.scale = {0.016f, 160.0f, 1.0f};
        model->Draw(arenaCityWindowModelId_, lightZ, camera_);
    }
    model->ClearDrawEffect();
}

void GameScene::DrawEnemyFocusMarker() {
    if (enemyFocusRingModelId_ == 0) {
        return;
    }

    const XMFLOAT3 &enemyPos = enemy_.GetTransform().position;
    const ActionStep actionStep = enemy_.GetActionStep();
    const bool isAttackCue =
        battleIntroActive_ ||
        actionStep == ActionStep::Charge || actionStep == ActionStep::Hold ||
        actionStep == ActionStep::Active;
    const float pulse = 0.5f + 0.5f * std::sinf(sceneLightTime_ * 10.0f);
    const float introThreat = battleIntroActive_
                                  ? SmoothStep01(battleIntroTimer_ / 2.36f)
                                  : 0.0f;

    Transform marker{};
    marker.position = {enemyPos.x, 0.034f, enemyPos.z};
    marker.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
    const float introScale = 1.24f + 0.30f * introThreat + 0.10f * pulse;
    const float scale =
        battleIntroActive_ ? introScale : (isAttackCue ? 1.08f + 0.08f * pulse : 0.96f);
    marker.scale = {scale, scale, 1.0f};

    ModelDrawEffect effect{};
    effect.enabled = true;
    effect.additiveBlend = true;
    effect.disableCulling = true;
    effect.color =
        battleIntroActive_
            ? XMFLOAT4{1.0f, 0.48f, 0.08f, 0.78f}
            : (isAttackCue ? XMFLOAT4{1.0f, 0.84f, 0.24f, 0.88f}
                           : XMFLOAT4{0.84f, 0.96f, 1.0f, 0.62f});
    effect.intensity =
        battleIntroActive_ ? 0.42f + 0.34f * introThreat + 0.18f * pulse
                           : (isAttackCue ? 0.58f + 0.24f * pulse : 0.30f);
    effect.fresnelPower = 1.0f;
    effect.noiseAmount = 0.02f;
    effect.time = sceneLightTime_;
    ctx_->model->SetDrawEffect(effect);
    ctx_->model->Draw(enemyFocusRingModelId_, marker, camera_);
    ctx_->model->ClearDrawEffect();
}
