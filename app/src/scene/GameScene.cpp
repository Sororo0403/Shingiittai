#include <WinSock2.h>
#include <WS2tcpip.h>
#include "GameScene.h"
#include "AppSceneServices.h"
#include "compat/ParticleCompat.h"
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
#include <DirectXTex.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <sstream>
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
constexpr uint16_t kPreviewPort = 5006;
constexpr float kDebugPreviewStaleSeconds = 0.75f;

SOCKET ToSocket(uintptr_t value) {
    return static_cast<SOCKET>(value);
}

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
    material.reflectionFresnelStrength = reflection * 0.42f;
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

uint32_t Hash2D(uint32_t x, uint32_t y, uint32_t seed) {
    uint32_t h = x * 374761393u + y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13u)) * 1274126177u;
    return h ^ (h >> 16u);
}

uint32_t CreateProceduralTexture(TextureManager *texture, uint32_t width,
                                 uint32_t height,
                                 const XMFLOAT3 &baseColor,
                                 const XMFLOAT3 &accentColor, uint32_t seed,
                                 float grainStrength) {
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4u);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint32_t h = Hash2D(x / 3u, y / 3u, seed);
            const float noise = static_cast<float>(h & 255u) / 255.0f;
            const float streak =
                static_cast<float>(Hash2D(x / 19u, y / 7u, seed + 17u) & 255u) /
                255.0f;
            const float t = std::clamp(noise * grainStrength +
                                           streak * (1.0f - grainStrength),
                                       0.0f, 1.0f);
            const float fine =
                static_cast<float>(Hash2D(x, y, seed + 31u) & 63u) / 255.0f;
            XMFLOAT3 color{
                std::clamp(baseColor.x + (accentColor.x - baseColor.x) * t + fine,
                           0.0f, 1.0f),
                std::clamp(baseColor.y + (accentColor.y - baseColor.y) * t + fine,
                           0.0f, 1.0f),
                std::clamp(baseColor.z + (accentColor.z - baseColor.z) * t + fine,
                           0.0f, 1.0f),
            };
            const size_t index = (static_cast<size_t>(y) * width + x) * 4u;
            pixels[index + 0] = static_cast<uint8_t>(color.x * 255.0f);
            pixels[index + 1] = static_cast<uint8_t>(color.y * 255.0f);
            pixels[index + 2] = static_cast<uint8_t>(color.z * 255.0f);
            pixels[index + 3] = 255u;
        }
    }
    return texture->CreateFromRgbaPixels(width, height, pixels.data());
}

uint32_t AppCreateRustedMetalTexture(TextureManager *texture, uint32_t width,
                                     uint32_t height) {
    return CreateProceduralTexture(texture, width, height,
                                   {0.23f, 0.22f, 0.20f},
                                   {0.70f, 0.30f, 0.12f}, 0x914Au, 0.62f);
}

uint32_t AppCreateArenaStoneTexture(TextureManager *texture, uint32_t width,
                                    uint32_t height) {
    return CreateProceduralTexture(texture, width, height,
                                   {0.07f, 0.08f, 0.09f},
                                   {0.22f, 0.24f, 0.22f}, 0x51C3u, 0.48f);
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
        material.baseColorTextureId = rustTextureId;
        material.color = palette[colorIndex % palette.size()];
        material.reflectionStrength = reflection;
        material.reflectionFresnelStrength = fresnel;
        material.reflectionRoughness = roughness;
        material.enableDissolve = 0.0f;
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
        material.baseColorTextureId = rustTextureId;
        material.color = {0.68f, 0.62f, 0.50f, 1.0f};
        XMStoreFloat4x4(&material.uvTransform,
                        XMMatrixTranspose(XMMatrixIdentity()));
        material.reflectionStrength = 0.085f;
        material.reflectionFresnelStrength = 0.030f;
        material.reflectionRoughness = 0.82f;
        material.enableDissolve = 0.0f;
        material.dissolveEdgeColor = {0.68f, 0.24f, 0.08f, 0.46f};
        modelManager->SetMaterial(subMesh.materialId, material);
    }
}

} // namespace

GameScene::~GameScene() { CloseDebugPreviewSocket(); }

void GameScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    ctx_->rendering.dxCommon->ResetClearColor();
    ctx_->rendering.postEffectRenderer->SetColorMode(PostEffectRenderer::ColorMode::None);
    ctx_->rendering.postEffectRenderer->SetVignettingShape(11.0f, 1.15f);
    ctx_->rendering.postEffectRenderer->SetVignettingStrength(0.20f);
    ctx_->rendering.postEffectRenderer->SetVignettingEnabled(true);
    combatFeedback_.Initialize(ctx_->rendering.postEffectRenderer);

    float aspect = static_cast<float>(ctx_->systems.winApp->GetWidth()) /
                   static_cast<float>(ctx_->systems.winApp->GetHeight());

    camera_.Initialize(aspect);    camera_.UpdateMatrices();
    camera_.SetPerspectiveFovDeg(currentFovDeg_);

    debugPreviewFrame_ = {};
    debugPreviewFrame_.rgbaPixels.resize(
        static_cast<size_t>(debugPreviewFrame_.width) *
        static_cast<size_t>(debugPreviewFrame_.height) * 4u);
    debugPreviewFrame_.textureId = ctx_->rendering.texture->CreateFromRgbaPixels(
        debugPreviewFrame_.width, debugPreviewFrame_.height,
        debugPreviewFrame_.rgbaPixels.data());
    debugPreviewJpegBuffer_.clear();
    debugPreviewChunkReceived_.clear();
    debugPreviewFrameId_ = 0;
    debugPreviewReceivedChunks_ = 0;

    DirectXCommon *dx = ctx_->rendering.dxCommon;
    ModelManager *model = ctx_->rendering.model;
    TextureManager *texture = ctx_->rendering.texture;

    uint32_t playerModel =
        model->Load(L"app/resources/models/player/player.glb");
    uint32_t swordModel = model->Load(L"app/resources/models/player/sword.glb");
    uint32_t enemyModel = model->Load(L"app/resources/models/boss/boss.gltf");
    uint32_t bulletModel =
        ctx_->rendering.model->Load(L"app/resources/models/bullet/bullet.obj");
    particleTextureId_ = texture->Load(L"app/resources/sprites/smoke.png");
    const uint32_t enemyRustTextureId =
        AppCreateRustedMetalTexture(texture, 512, 512);
    const uint32_t worldRustTextureId =
        AppCreateRustedMetalTexture(texture, 768, 768);
    const uint32_t arenaStoneTextureId =
        AppCreateArenaStoneTexture(texture, 1024, 1024);
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
            model->CreatePlane(
                arenaStoneTextureId,
                MakeArenaMaterial({0.075f, 0.10f, 0.13f, 1.0f}, true, 0.016f,
                                  0.80f));
        gSharedBattleModels.arenaDistantTerrainModelId =
            model->CreatePlane(
                arenaStoneTextureId,
                MakeArenaMaterial({0.12f, 0.18f, 0.22f, 1.0f}, true, 0.02f,
                                  0.88f));
        gSharedBattleModels.arenaHazardSpireModelId = model->CreateCylinder(
            arenaStoneTextureId,
            MakeArenaMaterial({0.10f, 0.15f, 0.15f, 1.0f}, true, 0.025f,
                              0.88f),
            7, 0.04f, 0.92f, 8.8f);
        gSharedBattleModels.arenaHazardGlowRingModelId = model->CreateRing(
            0, MakeArenaMaterial({0.55f, 0.88f, 0.96f, 0.42f}, false, 0.0f,
                                 0.46f),
            48, 2.45f, 0.34f);
        gSharedBattleModels.arenaCityTowerModelId = model->CreateCylinder(
            arenaStoneTextureId,
            MakeArenaMaterial({0.18f, 0.24f, 0.29f, 1.0f}, true, 0.025f,
                              0.76f),
            4, 0.72f, 0.72f, 1.0f);
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
    sparkParticles_.Initialize(dx, ctx_->rendering.srv, texture, particleTextureId_, 4096);
    sparkParticles_.SetEmission(1, 1000.0f);
    sparkParticles_.SetEmitterRadius(0.08f);
    explosionParticles_.Initialize(dx, ctx_->rendering.srv, texture, particleTextureId_,
                                   4096);
    explosionParticles_.SetEmission(1, 1000.0f);
    explosionParticles_.SetEmitterRadius(0.25f);
    smokeParticles_.Initialize(dx, ctx_->rendering.srv, texture, particleTextureId_, 2048);
    smokeParticles_.SetEmission(1, 1000.0f);
    smokeParticles_.SetEmitterRadius(0.40f);

    swordFlashParticles_.Initialize(dx, ctx_->rendering.srv, texture, particleTextureId_,
                                    384);
    swordFlashParticles_.SetEmission(1, 1000.0f);
    swordFlashParticles_.SetEmitterRadius(0.06f);

    swordTrailRenderer_.Initialize(dx);
    swordTrailRenderer_.Reset();
    swordSlashArcRenderer_.Initialize(dx);
    swordSlashArcRenderer_.Reset();
    prevSwordSlashStates_.fill(false);
    bladeClashPreviousSlashStates_.fill(false);

    player_.Initialize(playerModel, swordModel);
    player_.SetInputCalibration(inputCalibration_);
    playerModelId_ = playerModel;
    swordModelId_ = swordModel;
    enemy_.Initialize(enemyModel, bulletModel);
    enemyModelId_ = enemyModel;
    enemyProjectileModelId_ = bulletModel;
    if (ctx_->systems.sound != nullptr) {
        slashSoundId_ =
            ctx_->systems.sound->Load(L"app/resources/sounds/slash_hero.wav");
        enemyReleaseSoundId_ =
            ctx_->systems.sound->Load(L"app/resources/sounds/enemy_release_snap.wav");
        hitSoundId_ =
            ctx_->systems.sound->Load(L"app/resources/sounds/hit_impact.wav");
        counterSoundId_ =
            ctx_->systems.sound->Load(L"app/resources/sounds/counter_burst.wav");
        damageSoundId_ =
            ctx_->systems.sound->Load(L"app/resources/sounds/damage_heavy.wav");
        explosionSoundId_ =
            ctx_->systems.sound->Load(L"app/resources/sounds/explosion_boss.wav");
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
    titleDemoPhaseTimer_ = 0.0f;
    titleDemoPhase_ = 0;
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
    handTrackingStartRequested_ = false;
    player_.SetCameraSwordSlashSuppressed(false);
    if (inputCalibration_.controlType == InputControlType::Hand) {
        if (AppSceneServices::HasHandTrackingStart()) {
            AppSceneServices::RequestHandTrackingStart();
            handTrackingStartRequested_ = true;
        }
    }
    hud_.Initialize(*ctx_);
    enemy_.FaceTargetImmediately(player_.GetTransform().position);
    ApplyEnemyIntroDissolve(0.0f);
}

void GameScene::Update() {
    Input *input = ctx_->systems.input;
    const float baseDeltaTime = ctx_->frame.deltaTime;
    if (runMode_ == RunMode::Play &&
        inputCalibration_.controlType == InputControlType::Hand) {
        UpdateDebugCameraPreview(baseDeltaTime);
    }
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
        ctx_->rendering.model->UpdateAnimation(playerModelId_, baseDeltaTime * 0.08f);
        ctx_->rendering.model->UpdateAnimation(enemyModelId_, baseDeltaTime * 0.002f);
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
        ctx_->rendering.model->UpdateAnimation(playerModelId_, baseDeltaTime * 0.035f);
        ctx_->rendering.model->UpdateAnimation(enemyModelId_, baseDeltaTime * 0.28f);
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
            EmitParticleBurst(explosionParticles_, 
                breakCenter, 92, 0.86f,
                AppParticleBurstStyle::Explosion,
                {1.0f, 0.86f, 0.42f, 0.72f},
                {-bladeClashDirection_.x, 0.16f, -bladeClashDirection_.z},
                1.72f);
            EmitParticleBurst(sparkParticles_, 
                breakCenter, 128, 0.72f, AppParticleBurstStyle::Sparks,
                {1.0f, 0.92f, 0.48f, 0.88f},
                {bladeClashDirection_.z, 0.18f, -bladeClashDirection_.x},
                2.25f);
            EmitParticleBurst(swordFlashParticles_, 
                breakCenter, 10, 0.58f, AppParticleBurstStyle::Flash,
                {1.0f, 1.0f, 0.82f, 0.66f},
                {-bladeClashDirection_.x, 0.0f, -bladeClashDirection_.z},
                0.34f);
            EmitParticleBurst(smokeParticles_, 
                breakCenter, 24, 0.62f, AppParticleBurstStyle::Smoke,
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
            // constexpr float clashLossDamage = 25.0f;
            XMFLOAT3 sweepCenter = player_.GetTransform().position;
            sweepCenter.y += 1.02f;
            EmitParticleBurst(explosionParticles_, 
                sweepCenter, 92, 1.24f,
                AppParticleBurstStyle::SlashLine,
                {1.0f, 0.28f, 0.06f, 0.96f},
                {bladeClashDirection_.z, -0.04f, -bladeClashDirection_.x},
                1.92f);
            EmitParticleBurst(swordFlashParticles_, 
                sweepCenter, 14, 0.92f, AppParticleBurstStyle::Flash,
                {1.0f, 0.56f, 0.16f, 0.82f},
                {-bladeClashDirection_.x, 0.0f, -bladeClashDirection_.z},
                0.48f);
            EmitParticleBurst(sparkParticles_, 
                sweepCenter, 46, 0.42f, AppParticleBurstStyle::Sparks,
                {1.0f, 0.46f, 0.12f, 0.82f},
                {-bladeClashDirection_.x, 0.08f, -bladeClashDirection_.z},
                1.48f);
            EmitParticleBurst(smokeParticles_, 
                sweepCenter, 18, 0.44f, AppParticleBurstStyle::Smoke,
                {0.42f, 0.31f, 0.24f, 0.44f},
                {-bladeClashDirection_.x, 0.04f, -bladeClashDirection_.z},
                0.62f);
            const float appliedDamage = player_.TakeDamage(0.0f);
            // const float appliedDamage =
            //     player_.TakeDamage(clashLossDamage);
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
            EmitParticleBurst(explosionParticles_, 
                skidCenter, 36, 0.56f,
                AppParticleBurstStyle::SlashLine,
                {1.0f, 0.42f, 0.10f, 0.38f},
                {bladeClashDirection_.z, 0.02f, -bladeClashDirection_.x},
                0.92f);
            EmitParticleBurst(smokeParticles_, 
                skidCenter, 38, 0.64f, AppParticleBurstStyle::Smoke,
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
            EmitParticleBurst(explosionParticles_, 
                crashCenter, 110, 0.96f,
                AppParticleBurstStyle::Explosion,
                {1.0f, 0.36f, 0.10f, 0.78f},
                {-bladeClashDirection_.x, 0.12f, -bladeClashDirection_.z},
                2.35f);
            EmitParticleBurst(sparkParticles_, 
                crashCenter, 72, 0.58f, AppParticleBurstStyle::Sparks,
                {1.0f, 0.56f, 0.16f, 0.86f},
                {bladeClashDirection_.z, 0.10f, -bladeClashDirection_.x},
                2.10f);
            EmitParticleBurst(swordFlashParticles_, 
                crashCenter, 12, 0.82f, AppParticleBurstStyle::Flash,
                {1.0f, 0.82f, 0.48f, 0.58f},
                {-bladeClashDirection_.x, 0.0f, -bladeClashDirection_.z},
                0.40f);
            XMFLOAT3 dustCenter = crashCenter;
            dustCenter.y -= 0.52f;
            EmitParticleBurst(smokeParticles_, 
                dustCenter, 50, 0.94f, AppParticleBurstStyle::Smoke,
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
                EmitParticleBurst(smokeParticles_, 
                    trailCenter, 4, 0.34f, AppParticleBurstStyle::Smoke,
                    {0.34f, 0.29f, 0.24f, 0.30f},
                    {-bladeClashDirection_.x, 0.04f, -bladeClashDirection_.z},
                    0.48f);
                EmitParticleBurst(sparkParticles_, 
                    trailCenter, 3, 0.16f, AppParticleBurstStyle::Sparks,
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
            EmitParticleBurst(explosionParticles_, 
                cutCenter, 104, 0.86f,
                AppParticleBurstStyle::SlashLine,
                {1.0f, 0.90f, 0.58f, 0.66f},
                {bladeClashDirection_.z, 0.12f, -bladeClashDirection_.x},
                1.72f);
            EmitParticleBurst(explosionParticles_, 
                cutCenter, 42, 0.68f,
                AppParticleBurstStyle::Explosion,
                {1.0f, 0.78f, 0.34f, 0.50f},
                {bladeClashDirection_.x, 0.16f, bladeClashDirection_.z},
                1.18f);
            EmitParticleBurst(swordFlashParticles_, 
                cutCenter, 8, 0.42f, AppParticleBurstStyle::Flash,
                {1.0f, 0.96f, 0.80f, 0.56f},
                {bladeClashDirection_.z, 0.0f, -bladeClashDirection_.x},
                0.26f);
            EmitParticleBurst(sparkParticles_, 
                cutCenter, 46, 0.26f, AppParticleBurstStyle::Sparks,
                {1.0f, 0.82f, 0.28f, 0.58f},
                {bladeClashDirection_.z, 0.16f, -bladeClashDirection_.x},
                1.08f);
            EmitParticleBurst(smokeParticles_, 
                cutCenter, 18, 0.52f, AppParticleBurstStyle::Smoke,
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
            EmitParticleBurst(swordFlashParticles_, 
                pressureCenter, 7, 0.38f, AppParticleBurstStyle::Flash,
                {1.0f, 1.0f, 0.88f, 0.46f}, bladeClashDirection_, 0.24f);
            EmitParticleBurst(explosionParticles_, 
                pressureCenter, 70, 0.72f,
                AppParticleBurstStyle::SpiritSparkle,
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
            EmitParticleBurst(explosionParticles_, 
                blastCenter, 270, 1.26f,
                AppParticleBurstStyle::Explosion,
                {1.0f, 0.96f, 0.70f, 0.88f}, bladeClashDirection_, 2.65f);
            EmitParticleBurst(sparkParticles_, 
                blastCenter, 170, 0.76f, AppParticleBurstStyle::Sparks,
                {1.0f, 0.66f, 0.14f, 0.86f}, bladeClashDirection_, 2.42f);
            EmitParticleBurst(swordFlashParticles_, 
                blastCenter, 14, 0.68f, AppParticleBurstStyle::Flash,
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
            EmitParticleBurst(explosionParticles_, 
                skidCenter, 112, 0.82f,
                AppParticleBurstStyle::Explosion,
                {1.0f, 0.58f, 0.14f, 0.66f},
                {bladeClashDirection_.x, 0.22f, bladeClashDirection_.z},
                1.62f);
            EmitParticleBurst(sparkParticles_, 
                skidCenter, 88, 0.54f, AppParticleBurstStyle::Sparks,
                {1.0f, 0.72f, 0.18f, 0.84f},
                {bladeClashDirection_.z, 0.10f, -bladeClashDirection_.x},
                1.42f);
            EmitParticleBurst(smokeParticles_, 
                skidCenter, 86, 1.02f, AppParticleBurstStyle::Smoke,
                {0.42f, 0.34f, 0.26f, 0.58f}, bladeClashDirection_, 1.26f);
        }
        if (bladeClashFinishPlayerWon_ && bladeClashFinal_ &&
            !bladeClashFinishWallImpactEmitted_ &&
            bladeClashWinActionTimer >= 2.28f) {
            bladeClashFinishWallImpactEmitted_ = true;
            XMFLOAT3 crashCenter = enemy_.GetTransform().position;
            crashCenter.y += 0.42f;
            EmitParticleBurst(explosionParticles_, 
                crashCenter, 240, 1.36f,
                AppParticleBurstStyle::Explosion,
                {1.0f, 0.48f, 0.10f, 0.78f}, bladeClashDirection_, 2.70f);
            EmitParticleBurst(sparkParticles_, 
                crashCenter, 150, 0.86f, AppParticleBurstStyle::Sparks,
                {1.0f, 0.76f, 0.22f, 0.86f},
                {-bladeClashDirection_.x, 0.12f, -bladeClashDirection_.z},
                2.10f);
            EmitParticleBurst(smokeParticles_, 
                crashCenter, 124, 1.36f, AppParticleBurstStyle::Smoke,
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
            EmitParticleBurst(explosionParticles_, 
                skidCenter, 78, 0.92f,
                AppParticleBurstStyle::SlashLine,
                {1.0f, 0.78f, 0.32f, 0.58f},
                {bladeClashDirection_.z, 0.02f, -bladeClashDirection_.x},
                1.46f);
            EmitParticleBurst(sparkParticles_, 
                skidCenter, 48, 0.46f, AppParticleBurstStyle::Sparks,
                {1.0f, 0.70f, 0.20f, 0.70f},
                {-bladeClashDirection_.x, 0.04f, -bladeClashDirection_.z},
                1.08f);
            EmitParticleBurst(smokeParticles_, 
                skidCenter, 52, 0.94f, AppParticleBurstStyle::Smoke,
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

    ctx_->rendering.model->UpdateAnimation(playerModelId_, playerDeltaTime);

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
                EmitParticleBurst(smokeParticles_, 
                    burstCenter, 5, 0.30f, AppParticleBurstStyle::Smoke,
                    {0.34f, 0.29f, 0.24f, 0.24f},
                    {-bladeClashDirection_.x, 0.06f, -bladeClashDirection_.z},
                    0.52f);
                EmitParticleBurst(sparkParticles_, 
                    burstCenter, 4, 0.14f, AppParticleBurstStyle::Sparks,
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
        player_.SetCameraSwordSlashSuppressed(false);
        player_.Update(input, playerDeltaTime, enemy_.GetTransform().position,
                       cameraYaw_, forceRangedReflectMove, baseDeltaTime,
                       suppressLookAt);
        if (lockPlayerPositionForFarLaser) {
            player_.LockPosition(farLaserLockedPlayerPos);
        }
    }
    UpdateSwordVfx(baseDeltaTime);
    if (soundsLoaded_ && ctx_->systems.sound != nullptr) {
        const auto slashStates = player_.GetSwordSlashStates();
        for (size_t i = 0; i < slashStates.size(); ++i) {
            if (slashStates[i] && !previousSwordSoundStates_[i]) {
                ctx_->systems.sound->Play(slashSoundId_);
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
                if (soundsLoaded_ && ctx_->systems.sound != nullptr) {
                    ctx_->systems.sound->Play(enemyReleaseSoundId_);
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
            ctx_->rendering.model->UpdateAnimation(enemyModelId_, enemyAnimationDeltaTime);
        }
    }
    ApplyEnemyProceduralAnimation();

    UpdateBattleCamera();
    camera_.UpdateMatrices();

    if (runMode_ == RunMode::TitleDemo) {
        UpdateTitleDemo(baseDeltaTime);
    } else {
        player_.SetCameraSwordSlashSuppressed(false);
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

void GameScene::UpdateDebugCameraPreview(float deltaTime) {
    debugPreviewFrame_.staleTimer += deltaTime;
    ReceiveDebugPreviewPackets();
}

bool GameScene::EnsureDebugPreviewSocket() {
    if (debugPreviewSocketReady_) {
        return true;
    }

    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return false;
    }

    SOCKET udpSocket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udpSocket == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(kPreviewPort);
    if (bind(udpSocket, reinterpret_cast<sockaddr *>(&address),
             sizeof(address)) == SOCKET_ERROR) {
        closesocket(udpSocket);
        WSACleanup();
        return false;
    }

    u_long nonBlocking = 1;
    if (ioctlsocket(udpSocket, FIONBIO, &nonBlocking) == SOCKET_ERROR) {
        closesocket(udpSocket);
        WSACleanup();
        return false;
    }

    debugPreviewSocket_ = static_cast<uintptr_t>(udpSocket);
    debugPreviewSocketReady_ = true;
    return true;
}

void GameScene::CloseDebugPreviewSocket() {
    if (debugPreviewSocketReady_) {
        closesocket(ToSocket(debugPreviewSocket_));
        WSACleanup();
    }
    debugPreviewSocket_ = UINTPTR_MAX;
    debugPreviewSocketReady_ = false;
}

void GameScene::ReceiveDebugPreviewPackets() {
    if (!EnsureDebugPreviewSocket()) {
        return;
    }

    std::array<uint8_t, 1600> buffer{};
    for (;;) {
        sockaddr_in from{};
        int fromLength = sizeof(from);
        const int bytes = recvfrom(ToSocket(debugPreviewSocket_),
                                   reinterpret_cast<char *>(buffer.data()),
                                   static_cast<int>(buffer.size()), 0,
                                   reinterpret_cast<sockaddr *>(&from),
                                   &fromLength);
        if (bytes == SOCKET_ERROR) {
            return;
        }
        HandleDebugPreviewPacket(buffer.data(), bytes);
    }
}

void GameScene::HandleDebugPreviewPacket(const uint8_t *data, int bytes) {
    const uint8_t *newline = static_cast<const uint8_t *>(
        std::memchr(data, '\n', static_cast<size_t>(bytes)));
    if (newline == nullptr) {
        return;
    }

    const std::string header(reinterpret_cast<const char *>(data),
                             reinterpret_cast<const char *>(newline));
    std::istringstream stream(header);
    std::string magic;
    uint32_t frameId = 0;
    size_t chunkIndex = 0;
    size_t chunkCount = 0;
    size_t totalSize = 0;
    if (!(stream >> magic >> frameId >> chunkIndex >> chunkCount >> totalSize) ||
        magic != "SGCAM" || chunkCount == 0 || chunkIndex >= chunkCount ||
        totalSize == 0 || totalSize > 1024u * 1024u) {
        return;
    }

    const uint8_t *payload = newline + 1;
    const size_t payloadSize = static_cast<size_t>(data + bytes - payload);
    const size_t offset = chunkIndex * 1150u;
    if (offset >= totalSize || payloadSize > totalSize - offset) {
        return;
    }

    if (frameId != debugPreviewFrameId_ ||
        debugPreviewChunkReceived_.size() != chunkCount ||
        debugPreviewJpegBuffer_.size() != totalSize) {
        debugPreviewFrameId_ = frameId;
        debugPreviewJpegBuffer_.assign(totalSize, 0u);
        debugPreviewChunkReceived_.assign(chunkCount, false);
        debugPreviewReceivedChunks_ = 0;
    }

    if (!debugPreviewChunkReceived_[chunkIndex]) {
        std::memcpy(debugPreviewJpegBuffer_.data() + offset, payload,
                    payloadSize);
        debugPreviewChunkReceived_[chunkIndex] = true;
        ++debugPreviewReceivedChunks_;
    }

    if (debugPreviewReceivedChunks_ == debugPreviewChunkReceived_.size()) {
        DecodeDebugPreviewJpeg(debugPreviewJpegBuffer_);
    }
}

void GameScene::DecodeDebugPreviewJpeg(
    const std::vector<uint8_t> &jpegData) {
    if (jpegData.empty()) {
        return;
    }

    DirectX::ScratchImage scratch;
    DirectX::TexMetadata metadata{};
    HRESULT hr = DirectX::LoadFromWICMemory(
        jpegData.data(), jpegData.size(), DirectX::WIC_FLAGS_FORCE_RGB,
        &metadata, scratch);
    if (FAILED(hr)) {
        return;
    }

    DirectX::ScratchImage converted;
    const DirectX::Image *image = scratch.GetImage(0, 0, 0);
    if (image != nullptr && image->format != DXGI_FORMAT_R8G8B8A8_UNORM) {
        hr = DirectX::Convert(*image, DXGI_FORMAT_R8G8B8A8_UNORM,
                              DirectX::TEX_FILTER_DEFAULT, 0.0f, converted);
        if (FAILED(hr)) {
            return;
        }
        image = converted.GetImage(0, 0, 0);
    }

    if (image == nullptr || image->pixels == nullptr || image->width == 0 ||
        image->height == 0 || image->width != debugPreviewFrame_.width ||
        image->height != debugPreviewFrame_.height) {
        return;
    }

    const size_t rowBytes = static_cast<size_t>(debugPreviewFrame_.width) * 4u;
    const size_t imageBytes =
        rowBytes * static_cast<size_t>(debugPreviewFrame_.height);
    if (debugPreviewFrame_.rgbaPixels.size() != imageBytes) {
        debugPreviewFrame_.rgbaPixels.resize(imageBytes);
    }

    for (uint32_t y = 0; y < debugPreviewFrame_.height; ++y) {
        std::memcpy(debugPreviewFrame_.rgbaPixels.data() + rowBytes * y,
                    image->pixels + image->rowPitch * y, rowBytes);
    }
    debugPreviewFrame_.valid = true;
    debugPreviewFrame_.dirty = true;
    debugPreviewFrame_.staleTimer = 0.0f;
}

void GameScene::UploadDebugPreviewTextureIfNeeded() {
    if (!debugPreviewFrame_.dirty || !debugPreviewFrame_.valid ||
        debugPreviewFrame_.rgbaPixels.empty()) {
        return;
    }

    ctx_->rendering.texture->UpdateTexture2D(
        debugPreviewFrame_.textureId, debugPreviewFrame_.rgbaPixels.data(),
        static_cast<size_t>(debugPreviewFrame_.width) * 4u);
    debugPreviewFrame_.dirty = false;
}

void GameScene::DrawDebugCameraPreview() {
    if (ctx_ == nullptr || ctx_->rendering.sprite == nullptr || ctx_->rendering.texture == nullptr ||
        ctx_->systems.winApp == nullptr) {
        return;
    }

    UploadDebugPreviewTextureIfNeeded();
    constexpr float kMargin = 16.0f;
    constexpr float kPreviewWidth = 192.0f;
    const float previewAspect =
        static_cast<float>(debugPreviewFrame_.width) /
        static_cast<float>((std::max)(debugPreviewFrame_.height, 1u));
    const float previewHeight = kPreviewWidth / previewAspect;
    const bool fresh = debugPreviewFrame_.valid &&
                       debugPreviewFrame_.staleTimer <= kDebugPreviewStaleSeconds;
    const float alpha = fresh ? 0.88f : 0.34f;

    auto drawRect = [&](float x, float y, float w, float h,
                        const XMFLOAT4 &color) {
        Sprite sprite{};
        sprite.textureId = 0;
        sprite.position = {x, y};
        sprite.size = {w, h};
        sprite.color = color;
        ctx_->rendering.sprite->DrawSprite(sprite);
    };

    ctx_->rendering.sprite->PreDraw();
    drawRect(kMargin - 4.0f, kMargin - 4.0f, kPreviewWidth + 8.0f,
             previewHeight + 8.0f, {0.0f, 0.0f, 0.0f, 0.52f});

    if (debugPreviewFrame_.valid) {
        Sprite preview{};
        preview.textureId = debugPreviewFrame_.textureId;
        preview.position = {kMargin, kMargin};
        preview.size = {kPreviewWidth, previewHeight};
        preview.color = {1.0f, 1.0f, 1.0f, alpha};
        ctx_->rendering.sprite->DrawSprite(preview);
    }

    const XMFLOAT4 border =
        fresh ? XMFLOAT4{0.32f, 0.72f, 1.0f, 0.62f}
              : XMFLOAT4{0.90f, 0.72f, 0.22f, 0.50f};
    drawRect(kMargin, kMargin, kPreviewWidth, 2.0f, border);
    drawRect(kMargin, kMargin + previewHeight - 2.0f, kPreviewWidth, 2.0f,
             border);
    drawRect(kMargin, kMargin, 2.0f, previewHeight, border);
    drawRect(kMargin + kPreviewWidth - 2.0f, kMargin, 2.0f, previewHeight,
             border);
    ctx_->rendering.sprite->PostDraw();
}

void GameScene::Draw() {
    ctx_->rendering.model->PreDraw();
    const bool bladeClashWinFinish =
        bladeClashFinishActive_ && bladeClashFinishPlayerWon_;
    if (bladeClashWinFinish) {
        DrawBladeClashFinishBackdrop();
    } else {
        DrawArena();
    }
    const float playerVisualScale = bladeClashWinFinish ? 0.68f : 1.0f;
    const float enemyVisualScale = bladeClashWinFinish ? 1.32f : 1.0f;
    player_.Draw(ctx_->rendering.model, camera_,
                 !playerViewCamera_ || bladeClashFinishActive_,
                 bladeClashFinishActive_, playerVisualScale);
    if (!(victorySequenceActive_ && victoryFinalExplosionEmitted_)) {
        enemy_.Draw(ctx_->rendering.model, camera_, enemyVisualScale);
    }
    ctx_->rendering.model->PostDraw();
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
    DrawTitleDemoFlash();
}

void GameScene::DispatchCombatFeedback(const CombatFeedbackEvent &event) {
    combatFeedback_.PushEvent(event);
    EmitCombatParticles(event);
    if (event.type == CombatFeedbackEventType::PlayerSlashHit) {
        DirectX::XMFLOAT2 slashDirection{0.0f, 0.0f};
        const auto swords = player_.GetSwords();
        if (event.swordIndex < swords.size() && swords[event.swordIndex]) {
            slashDirection = swords[event.swordIndex]->GetSlashDirection();
        }
        swordSlashArcRenderer_.EmitHitLine(event.position, event.direction,
                                           camera_, event.power,
                                           slashDirection);
    }
    if (!soundsLoaded_ || ctx_ == nullptr || ctx_->systems.sound == nullptr) {
        return;
    }

    switch (event.type) {
    case CombatFeedbackEventType::CounterSuccess:
    case CombatFeedbackEventType::BladeClashGuardBreak:
        ctx_->systems.sound->Play(counterSoundId_);
        break;
    case CombatFeedbackEventType::PlayerSlashHit:
    case CombatFeedbackEventType::BladeClashPierce:
        ctx_->systems.sound->Play(hitSoundId_);
        break;
    case CombatFeedbackEventType::PlayerDamaged:
        ctx_->systems.sound->Play(damageSoundId_);
        break;
    case CombatFeedbackEventType::ProjectileReflect:
        ctx_->systems.sound->Play(counterSoundId_);
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
        EmitParticleBurst(sparkParticles_, position,
                                  static_cast<uint32_t>(34.0f + power * 12.0f),
                                  0.050f, AppParticleBurstStyle::Sparks,
                                  {0.94f, 0.97f, 1.00f, 0.76f}, direction,
                                  2.35f + power * 0.38f);
        EmitParticleBurst(swordFlashParticles_, 
            position, static_cast<uint32_t>(14.0f + power * 4.0f),
            0.088f + power * 0.012f, AppParticleBurstStyle::Flash,
            {1.0f, 0.98f, 1.00f, 0.72f}, direction, 0.38f + power * 0.05f);
        EmitParticleBurst(explosionParticles_, 
            position, static_cast<uint32_t>(54.0f + power * 12.0f),
            0.18f + power * 0.016f, AppParticleBurstStyle::SlashLine,
            {0.94f, 0.90f, 1.00f, 0.82f}, direction, 2.18f + power * 0.30f);
        EmitParticleBurst(smokeParticles_, 
            position, static_cast<uint32_t>(6.0f + power * 2.0f),
            0.13f + power * 0.012f, AppParticleBurstStyle::Smoke,
            {0.96f, 0.97f, 1.00f, 0.16f}, direction, 1.26f + power * 0.12f);
        break;
    case CombatFeedbackEventType::BladeClashPierce:
        EmitParticleBurst(sparkParticles_, position,
                                  static_cast<uint32_t>(64.0f + power * 28.0f),
                                  0.13f, AppParticleBurstStyle::Sparks,
                                  {1.00f, 0.68f, 0.28f, 1.0f}, direction,
                                  1.55f + power * 0.38f);
        break;
    case CombatFeedbackEventType::PlayerGuard:
        EmitParticleBurst(sparkParticles_, position, 92, 0.20f,
                                  AppParticleBurstStyle::Sparks,
                                  {1.00f, 0.74f, 0.36f, 1.0f}, direction, 1.95f);
        EmitParticleBurst(explosionParticles_, position, 14, 0.16f,
                                      AppParticleBurstStyle::Explosion,
                                      {0.92f, 0.50f, 0.22f, 1.0f}, direction,
                                      0.72f);
        EmitParticleBurst(smokeParticles_, position, 10, 0.24f,
                                  AppParticleBurstStyle::Smoke,
                                  {0.42f, 0.36f, 0.31f, 1.0f}, direction, 0.48f);
        break;
    case CombatFeedbackEventType::PlayerDamaged:
        EmitParticleBurst(sparkParticles_, position, 110, 0.24f,
                                  AppParticleBurstStyle::Sparks,
                                  {1.00f, 0.52f, 0.20f, 1.0f}, direction, 2.15f);
        EmitParticleBurst(explosionParticles_, position, 42, 0.30f,
                                      AppParticleBurstStyle::Explosion,
                                      {1.00f, 0.30f, 0.10f, 1.0f}, direction,
                                      1.15f);
        EmitParticleBurst(smokeParticles_, position, 34, 0.40f,
                                  AppParticleBurstStyle::Smoke,
                                  {0.48f, 0.36f, 0.28f, 1.0f}, direction, 0.78f);
        break;
    case CombatFeedbackEventType::CounterSuccess:
    case CombatFeedbackEventType::BladeClashGuardBreak:
        EmitParticleBurst(sparkParticles_, position, 170, 0.34f,
                                  AppParticleBurstStyle::Sparks,
                                  {1.00f, 0.76f, 0.26f, 1.0f}, direction, 2.1f);
        EmitParticleBurst(explosionParticles_, position, 78, 0.46f,
                                      AppParticleBurstStyle::Explosion,
                                      {1.00f, 0.46f, 0.12f, 1.0f}, direction,
                                      1.48f);
        EmitParticleBurst(smokeParticles_, position, 54, 0.56f,
                                  AppParticleBurstStyle::Smoke,
                                  {0.36f, 0.31f, 0.27f, 1.0f}, direction, 0.92f);
        break;
    case CombatFeedbackEventType::ProjectileReflect:
        EmitParticleBurst(sparkParticles_, position, 118, 0.24f,
                                  AppParticleBurstStyle::Sparks,
                                  {0.96f, 0.86f, 0.64f, 1.0f}, direction, 2.15f);
        EmitParticleBurst(explosionParticles_, position, 46, 0.30f,
                                      AppParticleBurstStyle::Explosion,
                                      {0.68f, 0.78f, 0.72f, 1.0f}, direction,
                                      1.12f);
        EmitParticleBurst(smokeParticles_, position, 24, 0.36f,
                                  AppParticleBurstStyle::Smoke,
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
            EmitParticleBurst(explosionParticles_, 
                origin, 150, 2.10f,
                AppParticleBurstStyle::SpiritSparkle,
                {1.0f, 1.0f, 0.96f, 0.72f}, forward, 1.68f);
            break;
        case ActionKind::Sweep:
            EmitParticleBurst(explosionParticles_, 
                origin, 158, 2.18f,
                AppParticleBurstStyle::SpiritSparkle,
                {1.0f, 1.0f, 0.96f, 0.70f}, forward, 1.58f);
            break;
        case ActionKind::BladeClash:
            EmitParticleBurst(explosionParticles_, 
                origin, 56, 1.05f,
                AppParticleBurstStyle::SpiritSparkle,
                {0.94f, 1.0f, 0.96f, 0.50f}, forward, 0.92f);
            EmitParticleBurst(smokeParticles_, 
                origin, 2, 0.34f, AppParticleBurstStyle::Flash,
                {0.92f, 1.0f, 0.96f, 0.22f}, forward, 0.22f);
            break;
        case ActionKind::Wave:
            EmitParticleBurst(explosionParticles_, 
                origin, 130, 2.02f,
                AppParticleBurstStyle::SpiritSparkle,
                {0.96f, 1.0f, 0.96f, 0.68f}, forward, 1.48f);
            break;
        case ActionKind::Laser:
            EmitParticleBurst(explosionParticles_, 
                origin, 150, 2.24f,
                AppParticleBurstStyle::SpiritSparkle,
                {0.58f, 0.96f, 1.0f, 0.72f}, forward, 1.72f);
            EmitParticleBurst(sparkParticles_, 
                origin, 96, 0.52f, AppParticleBurstStyle::SlashLine,
                {0.70f, 1.0f, 1.0f, 0.86f}, forward, 1.05f);
            break;
        case ActionKind::Cage: {
            XMFLOAT3 cageOrigin = player_.GetTransform().position;
            cageOrigin.y += 0.72f;
            EmitParticleBurst(explosionParticles_, 
                cageOrigin, 120, 1.70f,
                AppParticleBurstStyle::SpiritSparkle,
                {0.54f, 1.0f, 0.94f, 0.66f}, {0.0f, 1.0f, 0.0f}, 1.35f);
            EmitParticleBurst(sparkParticles_, 
                cageOrigin, 64, 0.34f, AppParticleBurstStyle::SlashLine,
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
        EmitParticleBurst(sparkParticles_, cuePos, 112, 1.46f,
                                  AppParticleBurstStyle::SlashLine,
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
            EmitParticleBurst(sparkParticles_, cuePos, 28, 0.34f,
                                      AppParticleBurstStyle::Sparks,
                                      sparkColor, forward, 1.85f);
            EmitParticleBurst(smokeParticles_, cuePos, 2, 0.30f,
                                      AppParticleBurstStyle::Flash,
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
        EmitParticleBurst(sparkParticles_, cuePos, sparkCount, sparkRadius,
                                  AppParticleBurstStyle::Sparks,
                                  cueColor, forward, sparkSpeed);
        const bool showCounterAxisLine =
            kind == ActionKind::Smash || kind == ActionKind::Sweep ||
            kind == ActionKind::Laser || phase2BladeClashStandbyCueVisible;
        if ((releaseCounterCueVisible || badSlashCueVisible) &&
            showCounterAxisLine) {
            const SwordCounterAxis cueAxis =
                RequiredVisualCounterAxisForAction(
                    kind, enemy_.GetFarLaserFollowupKind());
            EmitParticleBurst(sparkParticles_, 
                cuePos, releaseCounterCueVisible ? 104u : 116u,
                releaseCounterCueVisible ? 1.26f : 1.34f,
                AppParticleBurstStyle::SlashLine, cueColor,
                CounterAxisParticleDirection(cueAxis), 0.96f);
            if (badSlashCueVisible) {
                EmitParticleBurst(sparkParticles_, 
                    cuePos, 46u, 0.74f,
                    AppParticleBurstStyle::SlashLine,
                    {1.0f, 0.16f, 0.10f, 0.82f},
                    CounterAxisParticleDirection(cueAxis), 0.58f);
            }
        }
        const XMFLOAT4 flashColor =
            releaseCounterCueVisible ? XMFLOAT4{0.18f, 1.0f, 0.28f, 0.78f}
                                     : XMFLOAT4{1.0f, 0.04f, 0.10f, 0.88f};
        EmitParticleBurst(smokeParticles_, cuePos,
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
                                  AppParticleBurstStyle::Flash,
                                  flashColor, forward, 0.58f);
        enemyCueParticleTimer_ =
            kind == ActionKind::BladeClash ? 0.160f
                                     : releaseCounterCueVisible ? 0.110f : 0.125f;
    }

    const bool drawEnemySwordAfterimages = false;
    if (!drawEnemySwordAfterimages) {
        return;
    }

    if (enemySwordParticleTimer_ > 0.0f || ctx_->rendering.model == nullptr) {
        return;
    }

    XMFLOAT3 bladeRoot{};
    XMFLOAT3 bladeTip{};
    const XMFLOAT3 enemyBodyPos = enemy_.GetBodyTransform().position;
    if (Model *enemyModel = ctx_->rendering.model->GetModel(enemyModelId_)) {
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
            EmitParticleBurst(explosionParticles_, 
                bladeCenter, count, radius,
                AppParticleBurstStyle::SlashLine, bladeColor,
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

    if (ctx_ == nullptr || ctx_->rendering.postEffectRenderer == nullptr) {
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
        ctx_->rendering.postEffectRenderer->SetRadialBlurCenter(0.5f, 0.48f);
        ctx_->rendering.postEffectRenderer->SetRadialBlurSampleCount(18);
        ctx_->rendering.postEffectRenderer->SetRadialBlurStrength(radialBlurStrength);
        ctx_->rendering.postEffectRenderer->SetVignettingEnabled(true);
        ctx_->rendering.postEffectRenderer->SetVignettingShape(vignetteScale,
                                                     vignettePower);
        ctx_->rendering.postEffectRenderer->SetVignettingStrength(vignetteStrength);
        ctx_->rendering.postEffectRenderer->SetSceneDimStrength(sceneDimStrength);
        return;
    }

    const float focus = chargeWeakPointFocusRatio_;
    if (focus <= 0.001f) {
        ctx_->rendering.postEffectRenderer->SetVignettingShape(11.0f, 1.15f);
        ctx_->rendering.postEffectRenderer->SetSceneDimStrength(0.0f);
        return;
    }

    ctx_->rendering.postEffectRenderer->SetRadialBlurCenter(0.5f, 0.48f);
    ctx_->rendering.postEffectRenderer->SetRadialBlurSampleCount(20);
    ctx_->rendering.postEffectRenderer->SetRadialBlurStrength(0.030f * focus);
    ctx_->rendering.postEffectRenderer->SetVignettingShape(11.0f, 1.15f);
    ctx_->rendering.postEffectRenderer->SetVignettingStrength(0.20f + 0.72f * focus);
    ctx_->rendering.postEffectRenderer->SetSceneDimStrength(0.42f * focus);
}

void GameScene::DrawTransparent() {
    if (runMode_ == RunMode::TitleDemo) {
        return;
    }
    if (battleIntroActive_) {
        if (inputCalibration_.controlType == InputControlType::Hand) {
            DrawDebugCameraPreview();
        }
        return;
    }
    hud_.Draw(*ctx_);
    if (inputCalibration_.controlType == InputControlType::Hand) {
        DrawDebugCameraPreview();
    }
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
    if (ctx_ != nullptr && ctx_->rendering.postEffectRenderer != nullptr) {
        ctx_->rendering.postEffectRenderer->SetVignettingEnabled(true);
        ctx_->rendering.postEffectRenderer->SetVignettingStrength(0.30f + 0.42f * hold);
        ctx_->rendering.postEffectRenderer->SetRadialBlurCenter(0.5f, 0.48f);
        ctx_->rendering.postEffectRenderer->SetRadialBlurSampleCount(20);
        ctx_->rendering.postEffectRenderer->SetRadialBlurStrength(
            0.010f + 0.026f * hold + 0.036f * release);
        ctx_->rendering.postEffectRenderer->SetSceneDimStrength(0.10f + 0.18f * hold);
    }

    enemy_.Update(BuildPlayerCombatObservation(), deltaTime);
    ctx_->rendering.model->UpdateAnimation(playerModelId_, deltaTime * 0.025f);
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
        if (ctx_ != nullptr && ctx_->rendering.postEffectRenderer != nullptr) {
            ctx_->rendering.postEffectRenderer->SetRadialBlurStrength(0.0f);
            ctx_->rendering.postEffectRenderer->SetSceneDimStrength(0.0f);
            ctx_->rendering.postEffectRenderer->SetVignettingStrength(0.24f);
        }
    }
}

void GameScene::EmitPhaseTransitionStartEffects() {
    const XMFLOAT3 enemyPos = enemy_.GetTransform().position;
    const XMFLOAT3 origin{enemyPos.x, enemyPos.y + 1.18f, enemyPos.z};
    EmitParticleBurst(smokeParticles_, origin, 54, 1.00f,
                              AppParticleBurstStyle::Flash,
                              {1.0f, 0.52f, 0.12f, 0.86f},
                              {0.0f, 1.0f, 0.0f}, 0.62f);
    EmitParticleBurst(sparkParticles_, origin, 150, 1.45f,
                              AppParticleBurstStyle::Sparks,
                              {1.0f, 0.72f, 0.24f, 0.96f},
                              {0.0f, 1.0f, 0.0f}, 3.2f);
    if (soundsLoaded_ && ctx_ != nullptr && ctx_->systems.sound != nullptr) {
        ctx_->systems.sound->Play(enemyReleaseSoundId_);
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
    EmitParticleBurst(sparkParticles_, origin, sparkCount, 0.58f + 0.46f * hold,
                              AppParticleBurstStyle::Sparks,
                              {1.0f, 0.28f, 0.04f, 0.86f},
                              {0.0f, 1.0f, 0.0f}, 2.4f + 2.0f * hold);
    if (hold > 0.35f) {
        EmitParticleBurst(smokeParticles_, origin, 3, 0.58f,
                                  AppParticleBurstStyle::Flash,
                                  {1.0f, 0.20f, 0.04f, 0.52f},
                                  {0.0f, 1.0f, 0.0f}, 0.38f);
    }
}

void GameScene::EmitPhaseTransitionReleaseEffects() {
    const XMFLOAT3 enemyPos = enemy_.GetTransform().position;
    const XMFLOAT3 origin{enemyPos.x, enemyPos.y + 1.30f, enemyPos.z};
    EmitParticleBurst(explosionParticles_, origin, 110, 1.38f,
                                  AppParticleBurstStyle::Explosion,
                                  {1.0f, 0.30f, 0.05f, 0.92f},
                                  {0.0f, 1.0f, 0.0f}, 3.0f);
    EmitParticleBurst(sparkParticles_, origin, 230, 1.85f,
                              AppParticleBurstStyle::Sparks,
                              {1.0f, 0.86f, 0.30f, 1.0f},
                              {0.0f, 1.0f, 0.0f}, 7.0f);
    EmitParticleBurst(smokeParticles_, origin, 52, 1.42f,
                              AppParticleBurstStyle::Flash,
                              {1.0f, 0.58f, 0.12f, 0.76f},
                              {0.0f, 1.0f, 0.0f}, 1.0f);
    if (soundsLoaded_ && ctx_ != nullptr && ctx_->systems.sound != nullptr) {
        ctx_->systems.sound->Play(explosionSoundId_);
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
    if (ctx_ != nullptr && ctx_->rendering.postEffectRenderer != nullptr) {
        ctx_->rendering.postEffectRenderer->SetVignettingEnabled(true);
        ctx_->rendering.postEffectRenderer->SetVignettingStrength(0.16f + 0.06f * ratio);
        ctx_->rendering.postEffectRenderer->SetRadialBlurCenter(0.5f, 0.48f);
        ctx_->rendering.postEffectRenderer->SetRadialBlurSampleCount(18);
        ctx_->rendering.postEffectRenderer->SetRadialBlurStrength(0.035f * (1.0f - ratio));
        ctx_->rendering.postEffectRenderer->SetSceneDimStrength(0.0f);
    }
    if (!battleIntroRevealEmitted_ && battleIntroTimer_ >= 2.36f) {
        battleIntroRevealEmitted_ = true;
        const XMFLOAT3 enemyPos = enemy_.GetTransform().position;
        EmitParticleBurst(swordFlashParticles_, 
            {enemyPos.x, enemyPos.y + 1.45f, enemyPos.z}, 8, 0.38f,
            AppParticleBurstStyle::Flash,
            {1.0f, 0.98f, 0.88f, 0.82f}, {0.0f, 1.0f, 0.0f}, 0.22f);
        EmitParticleBurst(explosionParticles_, 
            {enemyPos.x, enemyPos.y + 1.30f, enemyPos.z}, 156, 2.12f,
            AppParticleBurstStyle::SpiritSparkle,
            {1.0f, 1.0f, 0.96f, 0.68f}, {0.0f, 1.0f, 0.0f}, 1.52f);
        if (soundsLoaded_ && ctx_ != nullptr && ctx_->systems.sound != nullptr) {
            ctx_->systems.sound->Play(enemyReleaseSoundId_);
        }
    }

    ctx_->rendering.model->UpdateAnimation(playerModelId_, deltaTime * 0.04f);
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
        if (ctx_ != nullptr && ctx_->rendering.postEffectRenderer != nullptr) {
            ctx_->rendering.postEffectRenderer->SetRadialBlurStrength(0.0f);
            ctx_->rendering.postEffectRenderer->SetSceneDimStrength(0.0f);
            ctx_->rendering.postEffectRenderer->SetVignettingStrength(0.20f);
        }
    }
}

void GameScene::ApplyEnemyIntroDissolve(float revealRatio) {
    if (ctx_ == nullptr || ctx_->rendering.model == nullptr || enemyModelId_ == 0) {
        return;
    }
    Model *model = ctx_->rendering.model->GetModel(enemyModelId_);
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
    const float alpha = SmoothStep01(reveal);
    for (ModelSubMesh &subMesh : model->subMeshes) {
        Material material = ctx_->rendering.model->GetMaterial(subMesh.materialId);
        material.enableDissolve = dissolveActive ? 1.0f : 0.0f;
        material.dissolveThreshold = threshold;
        material.dissolveEdgeWidth = 0.11f + 0.11f * (1.0f - reveal);
        material.dissolveEdgeColor = {1.0f, 0.70f, 0.30f, 0.70f};
        material.color.w = dissolveActive ? alpha : 1.0f;
        material.blendMode = dissolveActive
                                 ? static_cast<int32_t>(BlendMode::Transparent)
                                 : static_cast<int32_t>(BlendMode::Opaque);
        material.depthWrite = dissolveActive ? 0 : 1;
        ctx_->rendering.model->SetMaterial(subMesh.materialId, material);
    }
}

void GameScene::UpdateTitleDemo(float deltaTime) {
    titleDemoTimer_ += deltaTime;
    titleDemoPhaseTimer_ += deltaTime;
    constexpr float kIdleBeforeShowcase = 5.35f;

    auto applyTitleDemoPost = [&]() {
        if (ctx_ == nullptr || ctx_->rendering.postEffectRenderer == nullptr) {
            return;
        }

        const float cueFlash =
            titleDemoPhase_ == 1
                ? std::clamp(1.0f - titleDemoPhaseTimer_ / 0.52f, 0.0f, 1.0f)
                : 0.0f;
        ctx_->rendering.postEffectRenderer->SetColorMode(PostEffectRenderer::ColorMode::None);
        ctx_->rendering.postEffectRenderer->SetRadialBlurCenter(0.5f, 0.48f);
        ctx_->rendering.postEffectRenderer->SetRadialBlurSampleCount(24);
        ctx_->rendering.postEffectRenderer->SetRadialBlurStrength(
            0.018f + 0.048f * cueFlash +
            (bladeClashActive_ ? 0.030f * bladeClashImpactPulse_ : 0.0f));
        ctx_->rendering.postEffectRenderer->SetVignettingEnabled(true);
        ctx_->rendering.postEffectRenderer->SetVignettingShape(8.6f, 1.24f);
        ctx_->rendering.postEffectRenderer->SetVignettingStrength(
            0.24f + 0.38f * cueFlash +
            (bladeClashActive_ ? 0.16f * std::abs(bladeClashGauge_) : 0.0f));
        ctx_->rendering.postEffectRenderer->SetSceneDimStrength(
            0.05f + 0.18f * cueFlash);
    };

    if (titleDemoPhase_ == 0 && titleDemoPhaseTimer_ < kIdleBeforeShowcase) {
        applyTitleDemoPost();
        return;
    }

    if (titleDemoPhase_ == 0) {
        titleDemoPhase_ = 1;
        titleDemoPhaseTimer_ = 0.0f;
        titleDemoCounterTimer_ = 0.0f;
        applyTitleDemoPost();
        return;
    }

    if (titleDemoPhase_ == 1 && titleDemoPhaseTimer_ >= 0.52f) {
        BeginTitleDemoBladeClash();
        titleDemoPhase_ = 2;
        titleDemoPhaseTimer_ = 0.0f;
        titleDemoCounterTimer_ = 0.06f;
    }

    if (titleDemoPhase_ == 2 && bladeClashActive_) {
        titleDemoCounterTimer_ -= deltaTime;
        if (titleDemoCounterTimer_ <= 0.0f) {
            titleDemoCounterTimer_ = 0.16f;
            bladeClashGauge_ =
                std::clamp(bladeClashGauge_ + 0.40f, -1.1f, 1.1f);
            bladeClashCameraPush_ =
                (std::min)(bladeClashCameraPush_ + 0.72f, 1.0f);
            bladeClashImpactPulse_ = 1.0f;
            player_.NotifyCounterSuccess(1);

            XMFLOAT3 strikeCenter = bladeClashCenter_;
            strikeCenter.y += 0.08f;
            EmitParticleBurst(sparkParticles_, 
                strikeCenter, 92, 0.26f, AppParticleBurstStyle::Sparks,
                {1.0f, 0.72f, 0.24f, 0.86f}, bladeClashDirection_, 2.40f);
            EmitParticleBurst(swordFlashParticles_, 
                strikeCenter, 10, 0.28f, AppParticleBurstStyle::Flash,
                {1.0f, 1.0f, 0.84f, 0.72f}, bladeClashDirection_, 0.34f);

            CombatFeedbackEvent feedback{};
            feedback.type = CombatFeedbackEventType::CounterSuccess;
            feedback.position = strikeCenter;
            feedback.direction = bladeClashDirection_;
            feedback.power = 10.0f;
            feedback.swordIndex = 1;
            DispatchCombatFeedback(feedback);
        }
        UpdateBladeClash(deltaTime);
        applyTitleDemoPost();
        return;
    }

    if (titleDemoPhase_ == 2 && bladeClashFinishActive_) {
        applyTitleDemoPost();
        return;
    }

    if (titleDemoPhase_ == 2 && !bladeClashActive_ &&
        !bladeClashFinishActive_) {
        titleDemoPhase_ = 3;
        titleDemoPhaseTimer_ = 0.0f;
    }

    if (titleDemoPhase_ == 3) {
        applyTitleDemoPost();
        if (titleDemoPhaseTimer_ >= 1.35f) {
            ResetTitleDemoShowcase();
        }
        return;
    }
    applyTitleDemoPost();
}

void GameScene::BeginTitleDemoBladeClash() {
    if (bladeClashActive_ || bladeClashFinishActive_) {
        return;
    }

    const XMFLOAT3 playerPos = {-1.35f, 0.0f, 0.70f};
    const XMFLOAT3 enemyPos = {0.95f, 0.0f, 3.10f};
    player_.LockPosition(playerPos);
    enemy_.SetCinematicTransform(enemyPos, 0.0f);
    enemy_.FaceTargetImmediately(playerPos);
    player_.SetYaw(std::atan2f(enemyPos.x - playerPos.x,
                               enemyPos.z - playerPos.z));
    enemyLastStandPrimed_ = true;
    counterCinematicActive_ = false;
    counterCinematicTimer_ = 0.0f;
    SetEnemyAnimationFrozen(false);
    BeginBladeClash(1, true);
}

void GameScene::ResetTitleDemoShowcase() {
    player_.Initialize(playerModelId_, swordModelId_);
    player_.SetInputCalibration(inputCalibration_);
    enemy_.Initialize(enemyModelId_, enemyProjectileModelId_);
    enemy_.FaceTargetImmediately(player_.GetTransform().position);
    player_.SetDefeatPoseRatio(0.0f);
    player_.SetBladeClashPose(false);
    counterCinematicActive_ = false;
    counterCinematicTimer_ = 0.0f;
    SetEnemyAnimationFrozen(false);
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
    bladeClashPreviousSlashStates_.fill(false);
    previousChargeWeakPointSlashStates_.fill(false);
    previousSwordSoundStates_.fill(false);
    playerHitCooldown_ = 0.0f;
    enemyHitCooldown_ = 0.0f;
    swordTrailRenderer_.Reset();
    swordSlashArcRenderer_.Reset();
    titleDemoTimer_ = 0.0f;
    titleDemoCounterTimer_ = 0.90f;
    titleDemoPhaseTimer_ = 0.0f;
    titleDemoPhase_ = 0;
    if (ctx_ != nullptr && ctx_->rendering.postEffectRenderer != nullptr) {
        ctx_->rendering.postEffectRenderer->SetColorMode(PostEffectRenderer::ColorMode::None);
        ctx_->rendering.postEffectRenderer->SetRadialBlurStrength(0.0f);
        ctx_->rendering.postEffectRenderer->SetSceneDimStrength(0.0f);
        ctx_->rendering.postEffectRenderer->SetVignettingShape(11.0f, 1.15f);
        ctx_->rendering.postEffectRenderer->SetVignettingStrength(0.20f);
    }
    SyncEnemyAnimation();
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

    if (ctx_ != nullptr && ctx_->rendering.postEffectRenderer != nullptr) {
        ctx_->rendering.postEffectRenderer->SetVignettingEnabled(true);
        ctx_->rendering.postEffectRenderer->SetVignettingStrength(0.58f);
        ctx_->rendering.postEffectRenderer->SetRadialBlurCenter(0.5f, 0.48f);
        ctx_->rendering.postEffectRenderer->SetRadialBlurSampleCount(24);
        ctx_->rendering.postEffectRenderer->SetRadialBlurStrength(0.055f);
        ctx_->rendering.postEffectRenderer->SetSceneDimStrength(0.10f);
    }

    const XMFLOAT3 enemyPos = enemy_.GetTransform().position;
    EmitParticleBurst(sparkParticles_, 
        {enemyPos.x, enemyPos.y + 1.45f, enemyPos.z}, 220, 1.85f,
        AppParticleBurstStyle::Flash, {1.0f, 0.96f, 0.82f, 0.95f},
        {0.0f, 1.0f, 0.0f}, 0.72f);
    EmitParticleBurst(explosionParticles_, 
        {enemyPos.x, enemyPos.y + 1.25f, enemyPos.z}, 96, 1.35f,
        AppParticleBurstStyle::Explosion,
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

    if (ctx_ != nullptr && ctx_->rendering.postEffectRenderer != nullptr) {
        ctx_->rendering.postEffectRenderer->SetVignettingEnabled(true);
        ctx_->rendering.postEffectRenderer->SetVignettingStrength(0.62f);
        ctx_->rendering.postEffectRenderer->SetRadialBlurCenter(0.5f, 0.54f);
        ctx_->rendering.postEffectRenderer->SetRadialBlurSampleCount(22);
        ctx_->rendering.postEffectRenderer->SetRadialBlurStrength(0.040f);
        ctx_->rendering.postEffectRenderer->SetSceneDimStrength(0.18f);
    }

    const XMFLOAT3 playerPos = player_.GetTransform().position;
    EmitParticleBurst(explosionParticles_, 
        {playerPos.x, playerPos.y + 1.05f, playerPos.z}, 180, 1.55f,
        AppParticleBurstStyle::Explosion,
        {1.0f, 0.12f, 0.05f, 0.82f}, {0.0f, 1.0f, 0.0f}, 2.2f);
    EmitParticleBurst(sparkParticles_, 
        {playerPos.x, playerPos.y + 1.18f, playerPos.z}, 260, 1.35f,
        AppParticleBurstStyle::Sparks,
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

    if (ctx_ != nullptr && ctx_->rendering.postEffectRenderer != nullptr) {
        const float ratio =
            std::clamp(defeatSequenceTimer_ / defeatSequenceDuration_, 0.0f,
                       1.0f);
        ctx_->rendering.postEffectRenderer->SetRadialBlurStrength(0.040f *
                                                        (1.0f - ratio));
        ctx_->rendering.postEffectRenderer->SetSceneDimStrength(0.18f + 0.22f * ratio);
    }

    if (!defeatImpactEmitted_ && defeatSequenceTimer_ >= 1.58f) {
        defeatImpactEmitted_ = true;
        const XMFLOAT3 playerPos = player_.GetTransform().position;
        EmitParticleBurst(smokeParticles_, 
            {playerPos.x, playerPos.y + 0.28f, playerPos.z}, 160, 2.15f,
            AppParticleBurstStyle::Smoke,
            {0.18f, 0.17f, 0.16f, 0.88f}, {0.0f, 1.0f, 0.0f}, 0.85f);
        EmitParticleBurst(sparkParticles_, 
            {playerPos.x, playerPos.y + 0.42f, playerPos.z}, 180, 1.05f,
            AppParticleBurstStyle::Sparks,
            {1.0f, 0.18f, 0.08f, 0.86f}, {0.0f, 1.0f, 0.0f}, 4.2f);
    }

    if (defeatSequenceTimer_ >= defeatSequenceDuration_) {
        defeatSequenceActive_ = false;
        player_.SetDefeatPoseRatio(0.0f);
        if (ctx_ != nullptr && ctx_->rendering.postEffectRenderer != nullptr) {
            ctx_->rendering.postEffectRenderer->SetRadialBlurStrength(0.0f);
            ctx_->rendering.postEffectRenderer->SetSceneDimStrength(0.0f);
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

    if (ctx_ != nullptr && ctx_->rendering.postEffectRenderer != nullptr) {
        const float stepped = std::floor(ratio * 14.0f) / 14.0f;
        const float blur = (1.0f - stepped) * 0.070f;
        ctx_->rendering.postEffectRenderer->SetRadialBlurStrength(blur);
        ctx_->rendering.postEffectRenderer->SetSceneDimStrength(0.10f + stepped * 0.18f);
    }

    if (!victoryFinalExplosionEmitted_ && victorySequenceTimer_ >= 3.90f) {
        victoryFinalExplosionEmitted_ = true;
        const XMFLOAT3 enemyPos = enemy_.GetTransform().position;
        if (soundsLoaded_ && ctx_ != nullptr && ctx_->systems.sound != nullptr) {
            ctx_->systems.sound->Play(explosionSoundId_);
        }
        EmitParticleBurst(explosionParticles_, 
            {enemyPos.x, enemyPos.y + 1.05f, enemyPos.z}, 3200, 8.40f,
            AppParticleBurstStyle::Explosion,
            {1.0f, 0.64f, 0.08f, 1.0f}, {0.0f, 1.0f, 0.0f}, 5.80f);
        EmitParticleBurst(sparkParticles_, 
            {enemyPos.x, enemyPos.y + 1.22f, enemyPos.z}, 3600, 9.20f,
            AppParticleBurstStyle::Sparks,
            {1.0f, 0.98f, 0.58f, 1.0f}, {0.0f, 1.0f, 0.0f}, 12.4f);
        EmitParticleBurst(smokeParticles_, 
            {enemyPos.x, enemyPos.y + 1.12f, enemyPos.z}, 680, 6.80f,
            AppParticleBurstStyle::Flash,
            {1.0f, 0.94f, 0.70f, 1.0f}, {0.0f, 1.0f, 0.0f}, 1.55f);
    }

    if (victorySequenceTimer_ >= victorySequenceDuration_) {
        victorySequenceActive_ = false;
        if (ctx_ != nullptr && ctx_->rendering.postEffectRenderer != nullptr) {
            ctx_->rendering.postEffectRenderer->SetRadialBlurStrength(0.0f);
            ctx_->rendering.postEffectRenderer->SetSceneDimStrength(0.0f);
        }
        sceneManager_->ChangeScene(std::make_unique<BattleResultScene>(
            BattleResultScene::ResultKind::Clear, victoryClearTime_,
            inputCalibration_));
    }
}

void GameScene::DrawBladeClashFinishFrame() {
    if (!bladeClashFinishActive_ || !bladeClashFinishPlayerWon_ ||
        ctx_ == nullptr || ctx_->rendering.sprite == nullptr || ctx_->systems.winApp == nullptr) {
        return;
    }

    const float screenW = static_cast<float>(ctx_->systems.winApp->GetWidth());
    const float screenH = static_cast<float>(ctx_->systems.winApp->GetHeight());
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
        ctx_->rendering.sprite->DrawSprite(sprite);
    };

    ctx_->rendering.sprite->PreDraw();
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
    ctx_->rendering.sprite->PostDraw();
}

void GameScene::DrawTitleDemoFlash() {
    if (runMode_ != RunMode::TitleDemo || titleDemoPhase_ != 1 ||
        ctx_ == nullptr || ctx_->rendering.sprite == nullptr || ctx_->systems.winApp == nullptr) {
        return;
    }

    const float flash =
        std::clamp(1.0f - titleDemoPhaseTimer_ / 0.52f, 0.0f, 1.0f);
    if (flash <= 0.01f) {
        return;
    }

    Sprite sprite{};
    sprite.textureId = 0;
    sprite.position = {0.0f, 0.0f};
    sprite.size = {static_cast<float>(ctx_->systems.winApp->GetWidth()),
                   static_cast<float>(ctx_->systems.winApp->GetHeight())};
    sprite.color = {1.0f, 1.0f, 1.0f, flash};

    ctx_->rendering.sprite->PreDraw();
    ctx_->rendering.sprite->DrawSprite(sprite);
    ctx_->rendering.sprite->PostDraw();
}

void GameScene::DrawVictoryFlash() {
    if (!victorySequenceActive_ || ctx_ == nullptr || ctx_->rendering.sprite == nullptr ||
        ctx_->systems.winApp == nullptr) {
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
    flash.size = {static_cast<float>(ctx_->systems.winApp->GetWidth()),
                  static_cast<float>(ctx_->systems.winApp->GetHeight())};
    flash.color = {1.0f, 1.0f, 1.0f, alpha};

    ctx_->rendering.sprite->PreDraw();
    ctx_->rendering.sprite->DrawSprite(flash);
    ctx_->rendering.sprite->PostDraw();
}

void GameScene::DrawDefeatFlash() {
    if (!defeatSequenceActive_ || ctx_ == nullptr || ctx_->rendering.sprite == nullptr ||
        ctx_->systems.winApp == nullptr) {
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
    const float w = static_cast<float>(ctx_->systems.winApp->GetWidth());
    const float h = static_cast<float>(ctx_->systems.winApp->GetHeight());

    ctx_->rendering.sprite->PreDraw();
    if (red > 0.01f) {
        Sprite flash{};
        flash.textureId = 0;
        flash.position = {0.0f, 0.0f};
        flash.size = {w, h};
        flash.color = {1.0f, 0.06f, 0.02f, red};
        ctx_->rendering.sprite->DrawSprite(flash);
    }
    if (black > 0.01f) {
        Sprite fade{};
        fade.textureId = 0;
        fade.position = {0.0f, 0.0f};
        fade.size = {w, h};
        fade.color = {0.0f, 0.0f, 0.0f, black};
        ctx_->rendering.sprite->DrawSprite(fade);
    }
    ctx_->rendering.sprite->PostDraw();
}

void GameScene::DrawBattleIntroFlash() {
    if (!battleIntroActive_ || ctx_ == nullptr || ctx_->rendering.sprite == nullptr ||
        ctx_->systems.winApp == nullptr) {
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

    const float w = static_cast<float>(ctx_->systems.winApp->GetWidth());
    const float h = static_cast<float>(ctx_->systems.winApp->GetHeight());
    ctx_->rendering.sprite->PreDraw();
    Sprite flash{};
    flash.textureId = 0;
    flash.position = {0.0f, 0.0f};
    flash.size = {w, h};
    flash.color = {1.0f, 0.985f, 0.93f, alpha};
    ctx_->rendering.sprite->DrawSprite(flash);
    if (appearFlash > 0.01f) {
        Sprite flare{};
        flare.textureId = 0;
        flare.position = {w * 0.15f, h * 0.39f};
        flare.size = {w * 0.70f, h * 0.18f};
        flare.color = {1.0f, 1.0f, 1.0f, appearFlash * 0.16f};
        ctx_->rendering.sprite->DrawSprite(flare);
    }
    ctx_->rendering.sprite->PostDraw();
}

void GameScene::DrawChargeWeakPointTimeGauge() {
    const float limit = enemy_.GetChargeWeakPointTimeLimitForPresentation();
    if (limit <= 0.0001f || chargeWeakPointBroken_ || ctx_ == nullptr ||
        ctx_->rendering.sprite == nullptr || ctx_->systems.winApp == nullptr) {
        return;
    }

    const float remaining =
        enemy_.GetChargeWeakPointTimeRemainingForPresentation();
    const float ratio = std::clamp(remaining / limit, 0.0f, 1.0f);
    const float focus = std::clamp(chargeWeakPointFocusRatio_, 0.0f, 1.0f);
    const float alpha = std::clamp(0.16f + focus * 0.34f, 0.0f, 0.50f);

    const float screenW = static_cast<float>(ctx_->systems.winApp->GetWidth());
    const float screenH = static_cast<float>(ctx_->systems.winApp->GetHeight());
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
        ctx_->rendering.sprite->DrawSprite(sprite);
    };

    ctx_->rendering.sprite->PreDraw();
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
    ctx_->rendering.sprite->PostDraw();
}

void GameScene::DrawBladeClashGauge() {
    if (!bladeClashActive_ || ctx_ == nullptr || ctx_->rendering.sprite == nullptr ||
        ctx_->systems.winApp == nullptr) {
        return;
    }

    const float screenW = static_cast<float>(ctx_->systems.winApp->GetWidth());
    const float screenH = static_cast<float>(ctx_->systems.winApp->GetHeight());
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
        ctx_->rendering.sprite->DrawSprite(sprite);
    };

    ctx_->rendering.sprite->PreDraw();
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
    ctx_->rendering.sprite->PostDraw();
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
        ctx_->rendering.model->SetDrawEffect(effect);
        ctx_->rendering.model->Draw(enemyWeaponTrailModelId_, tf, camera_);
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
        ctx_->rendering.model->ClearDrawEffect();
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
        ctx_->rendering.model != nullptr) {
        if (Model *enemyModel = ctx_->rendering.model->GetModel(enemyModelId_)) {
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

    ctx_->rendering.model->ClearDrawEffect();
}

void GameScene::DrawChargeWeakPoint() {
}

void GameScene::DrawBladeClashFinishBackdrop() {
    DrawArena();
}

void GameScene::DrawDistantHazardBackdrop() {
    ModelManager *model = ctx_->rendering.model;
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
    std::vector<Transform> cityTowers;
    cityTowers.reserve(4u * 3u * 17u);
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
                cityTowers.push_back(tower);
            }
        }
    }
    if (!cityTowers.empty()) {
        model->DrawInstanced(arenaCityTowerModelId_, cityTowers.data(),
                             static_cast<uint32_t>(cityTowers.size()), camera_);
    }
    model->ClearDrawEffect();

    ModelDrawEffect blockEffect = cityEffect;
    blockEffect.color = {0.07f, 0.10f, 0.13f, 0.92f};
    blockEffect.intensity = 0.18f;
    model->SetDrawEffect(blockEffect);
    std::vector<Transform> cityBlocks;
    cityBlocks.reserve(23u);
    for (int i = 0; i < 15; ++i) {
        const float offset = static_cast<float>(i - 7);
        Transform wall{};
        wall.position = {offset * 1.72f, -0.76f,
                         72.0f + std::fabs(offset) * 0.42f};
        wall.rotation = MakeQuat(0.0f, 0.0f, 0.0f);
        wall.scale = {1.28f + 0.12f * static_cast<float>(i % 3),
                      5.3f + static_cast<float>((i * 5) % 5) * 0.72f,
                      1.08f};
        cityBlocks.push_back(wall);
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
            cityBlocks.push_back(brace);
        }
    }
    if (!cityBlocks.empty()) {
        model->DrawInstanced(arenaCityTowerModelId_, cityBlocks.data(),
                             static_cast<uint32_t>(cityBlocks.size()), camera_);
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
    std::vector<Transform> cityWindows;
    cityWindows.reserve(4u * 11u);
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
            cityWindows.push_back(panel);
        }
    }
    if (!cityWindows.empty()) {
        model->DrawInstanced(arenaCityWindowModelId_, cityWindows.data(),
                             static_cast<uint32_t>(cityWindows.size()), camera_);
    }
    model->ClearDrawEffect();
}

void GameScene::DrawArena() {
    ModelManager *model = ctx_->rendering.model;
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

    std::vector<Transform> fieldTiles;
    fieldTiles.reserve(480u);
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
            fieldTiles.push_back(tile);
        }
    }

    Transform center{};
    center.position = {0.0f, 0.012f, 0.0f};
    center.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
    center.scale = {4.8f, 4.8f, 1.0f};
    fieldTiles.push_back(center);

    for (int i = -13; i <= 13; ++i) {
        Transform laneX{};
        laneX.position = {0.0f, 0.016f,
                          static_cast<float>(i) * kLaneSpacing};
        laneX.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
        laneX.scale = {168.0f, i == 0 ? 0.080f : 0.034f, 1.0f};
        fieldTiles.push_back(laneX);

        Transform laneZ{};
        laneZ.position = {static_cast<float>(i) * kLaneSpacing, 0.017f,
                          0.0f};
        laneZ.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
        laneZ.scale = {i == 0 ? 0.080f : 0.034f, 168.0f, 1.0f};
        fieldTiles.push_back(laneZ);
    }
    if (!fieldTiles.empty()) {
        model->DrawInstanced(arenaSpokeModelId_, fieldTiles.data(),
                             static_cast<uint32_t>(fieldTiles.size()), camera_);
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
    std::vector<Transform> glowLines;
    glowLines.reserve(62u);
    for (int i = -15; i <= 15; ++i) {
        Transform lightX{};
        lightX.position = {0.0f, 0.034f,
                           static_cast<float>(i) * kPatternSpacing};
        lightX.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
        lightX.scale = {160.0f, 0.016f, 1.0f};
        glowLines.push_back(lightX);

        Transform lightZ{};
        lightZ.position = {static_cast<float>(i) * kPatternSpacing, 0.035f,
                           0.0f};
        lightZ.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
        lightZ.scale = {0.016f, 160.0f, 1.0f};
        glowLines.push_back(lightZ);
    }
    if (!glowLines.empty()) {
        model->DrawInstanced(arenaCityWindowModelId_, glowLines.data(),
                             static_cast<uint32_t>(glowLines.size()), camera_);
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
    ctx_->rendering.model->SetDrawEffect(effect);
    ctx_->rendering.model->Draw(enemyFocusRingModelId_, marker, camera_);
    ctx_->rendering.model->ClearDrawEffect();
}
