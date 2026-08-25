#include "BattleArenaRenderer.h"
#include "Material.h"
#include "ModelManager.h"
#include "TextureManager.h"
#include "Transform.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <vector>

using namespace DirectX;

namespace {
constexpr float kPi = 3.14159265f;

BattleArenaModelIds gBattleArenaModels;
bool gBattleArenaModelsInitialized = false;

float SmoothStep01(float value) {
    const float t = std::clamp(value, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

XMFLOAT4 MakeQuat(float pitch, float yaw, float roll) {
    XMFLOAT4 q{};
    XMStoreFloat4(&q, XMQuaternionRotationRollPitchYaw(pitch, yaw, roll));
    return q;
}

uint32_t Hash2D(uint32_t x, uint32_t y, uint32_t seed) {
    uint32_t h = x * 374761393u + y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13u)) * 1274126177u;
    return h ^ (h >> 16u);
}

uint32_t CreateBattleArenaStoneTexture(TextureManager *texture, uint32_t width,
                                       uint32_t height) {
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4u);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint32_t h = Hash2D(x / 3u, y / 3u, 0x51C3u);
            const float noise = static_cast<float>(h & 255u) / 255.0f;
            const float pattern =
                static_cast<float>(Hash2D(x / 19u, y / 7u, 0x51C3u + 17u) &
                                   255u) /
                255.0f * 0.52f;
            const float t = std::clamp(noise * 0.48f + pattern, 0.0f, 1.0f);
            const float fine =
                static_cast<float>(Hash2D(x, y, 0x51C3u + 31u) & 63u) / 255.0f;
            const XMFLOAT3 base{0.07f, 0.08f, 0.09f};
            const XMFLOAT3 accent{0.22f, 0.24f, 0.22f};
            const XMFLOAT3 color{
                std::clamp(base.x + (accent.x - base.x) * t + fine, 0.0f,
                           1.0f),
                std::clamp(base.y + (accent.y - base.y) * t + fine, 0.0f,
                           1.0f),
                std::clamp(base.z + (accent.z - base.z) * t + fine, 0.0f,
                           1.0f),
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

Material MakeArenaMaterial(const XMFLOAT4 &color, bool useTexture = false,
                           float reflection = 0.34f, float roughness = 0.48f) {
    Material material{};
    material.color = color;
    material.enableTexture = useTexture ? 1 : 0;
    material.reflectionStrength = reflection;
    material.reflectionFresnelStrength = reflection * 0.42f;
    material.reflectionRoughness = roughness;
    return material;
}

std::vector<Transform> BuildFieldTiles(float tileBuild, float lineBuild) {
        std::vector<Transform> fieldTiles;
        fieldTiles.reserve(480u);
        for (int z = -14; z <= 14; ++z) {
            for (int x = -14; x <= 14; ++x) {
                if ((std::abs(x) + std::abs(z)) % 2 != 0) {
                    continue;
                }
                const float distance =
                    (std::fabs(static_cast<float>(x)) +
                     std::fabs(static_cast<float>(z))) /
                    28.0f;
                const float tileLocalBuild =
                    SmoothStep01((tileBuild - distance * 0.34f) / 0.46f);
                if (tileLocalBuild <= 0.0f) {
                    continue;
                }
                Transform tile{};
                tile.position = {static_cast<float>(x) * 3.55f, 0.004f,
                                 static_cast<float>(z) * 3.55f};
                tile.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
                tile.scale = {2.26f * tileLocalBuild, 2.26f * tileLocalBuild,
                              1.0f};
                fieldTiles.push_back(tile);
            }
        }

        if (tileBuild > 0.0f) {
            Transform center{};
            center.position = {0.0f, 0.012f, 0.0f};
            center.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
            center.scale = {4.8f * tileBuild, 4.8f * tileBuild, 1.0f};
            fieldTiles.push_back(center);
        }

        for (int i = -13; i <= 13; ++i) {
            const float laneLocalBuild =
                SmoothStep01((lineBuild -
                              std::fabs(static_cast<float>(i)) / 13.0f * 0.24f) /
                             0.56f);
            if (laneLocalBuild <= 0.0f) {
                continue;
            }
            Transform laneX{};
            laneX.position = {0.0f, 0.016f, static_cast<float>(i) * 4.25f};
            laneX.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
            laneX.scale = {168.0f * laneLocalBuild, i == 0 ? 0.080f : 0.034f,
                           1.0f};
            fieldTiles.push_back(laneX);

            Transform laneZ{};
            laneZ.position = {static_cast<float>(i) * 4.25f, 0.017f, 0.0f};
            laneZ.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
            laneZ.scale = {i == 0 ? 0.080f : 0.034f, 168.0f * laneLocalBuild,
                           1.0f};
            fieldTiles.push_back(laneZ);
        }
    return fieldTiles;
}

std::vector<Transform> BuildCityTowers(float buildProgress) {
    std::vector<Transform> towers;
    towers.reserve(size_t{4} * 3u * 17u);
    for (int side = 0; side < 4; ++side) {
        const bool alongX = side < 2;
        const float sign = (side == 0 || side == 2) ? 1.0f : -1.0f;
        for (int row = 0; row < 3; ++row) {
            for (int index = 0; index < 17; ++index) {
                const float lane = (static_cast<float>(index) - 8.0f) * 4.15f;
                const float depth = 48.0f + static_cast<float>(row) * 4.1f;
                const float height =
                    2.4f +
                    static_cast<float>((index * 5 + row * 7 + side) % 10) *
                        0.45f +
                    static_cast<float>(row) * 0.55f;
                const float order = static_cast<float>(row) * 0.12f +
                                    static_cast<float>(index) / 17.0f * 0.16f;
                const float progress =
                    SmoothStep01((buildProgress - order) / 0.34f);
                if (progress <= 0.0f) {
                    continue;
                }
                Transform tower{};
                tower.position =
                    alongX ? XMFLOAT3{lane, -0.68f, sign * depth + 8.0f}
                           : XMFLOAT3{sign * depth, -0.68f, lane + 8.0f};
                tower.rotation =
                    MakeQuat(0.0f, alongX ? 0.0f : kPi * 0.5f, 0.0f);
                tower.scale = {
                    0.62f + static_cast<float>((index + row) % 3) * 0.16f,
                    height * progress,
                    0.78f + static_cast<float>((index * 3 + row) % 2) * 0.24f};
                towers.push_back(tower);
            }
        }
    }
    return towers;
}

std::vector<Transform> BuildCityBlocks(float buildProgress) {
    std::vector<Transform> blocks;
    blocks.reserve(23u);
    for (int index = 0; index < 15; ++index) {
        const float offset = static_cast<float>(index - 7);
        const float progress = SmoothStep01(
            (buildProgress - static_cast<float>(index) * 0.012f) / 0.30f);
        if (progress <= 0.0f) {
            continue;
        }
        Transform wall{};
        wall.position = {offset * 1.72f, -0.76f,
                         72.0f + std::fabs(offset) * 0.42f};
        wall.rotation = MakeQuat(0.0f, 0.0f, 0.0f);
        wall.scale = {
            1.28f + 0.12f * static_cast<float>(index % 3),
            (5.3f + static_cast<float>((index * 5) % 5) * 0.72f) * progress,
            1.08f};
        blocks.push_back(wall);
    }
    for (int side = 0; side < 2; ++side) {
        const float sign = side == 0 ? -1.0f : 1.0f;
        for (int step = 0; step < 4; ++step) {
            const float progress = SmoothStep01(
                (buildProgress - (0.18f + static_cast<float>(step) * 0.08f)) /
                0.28f);
            if (progress <= 0.0f) {
                continue;
            }
            Transform brace{};
            brace.position = {sign * (9.2f + static_cast<float>(step) * 2.25f),
                              2.25f + static_cast<float>(step) * 0.62f,
                              69.4f + static_cast<float>(step) * 1.55f};
            brace.rotation = MakeQuat(0.0f, sign * 0.12f, 0.0f);
            brace.scale = {
                (2.8f - static_cast<float>(step) * 0.22f) * progress, 0.30f,
                0.78f};
            blocks.push_back(brace);
        }
    }
    return blocks;
}

std::vector<Transform> BuildCityWindows(float buildProgress) {
    std::vector<Transform> windows;
    windows.reserve(size_t{4} * 11u);
    for (int side = 0; side < 4; ++side) {
        const bool alongX = side < 2;
        const float sign = (side == 0 || side == 2) ? 1.0f : -1.0f;
        for (int index = 0; index < 11; ++index) {
            const float progress = SmoothStep01(
                (buildProgress - (0.32f + static_cast<float>(index) * 0.018f)) /
                0.34f);
            if (progress <= 0.0f) {
                continue;
            }
            const float lane = (static_cast<float>(index) - 5.0f) * 4.0f;
            Transform panel{};
            panel.position =
                alongX
                    ? XMFLOAT3{lane,
                               1.15f + static_cast<float>(index % 4) * 0.58f,
                               sign * 47.35f + 8.0f}
                    : XMFLOAT3{sign * 47.35f,
                               1.15f + static_cast<float>(index % 4) * 0.58f,
                               lane + 8.0f};
            panel.rotation =
                MakeQuat(0.0f, alongX ? 0.0f : kPi * 0.5f, 0.0f);
            panel.scale = {
                0.16f,
                (1.05f + static_cast<float>(index % 2) * 0.40f) * progress,
                1.0f};
            windows.push_back(panel);
        }
    }
    return windows;
}

void DrawDistantHazardBackdrop(ModelManager *model, const Camera &camera,
                               const BattleArenaModelIds &ids, float time,
                               float buildProgress) {
    buildProgress = SmoothStep01(buildProgress);
    if (buildProgress <= 0.0f) {
        return;
    }
    const float pulse = 0.5f + 0.5f * std::sinf(time * 1.8f);

    ModelDrawEffect cityEffect{};
    cityEffect.enabled = true;
    cityEffect.additiveBlend = false;
    cityEffect.disableCulling = true;
    cityEffect.color = {0.16f, 0.20f, 0.24f, 0.90f * buildProgress};
    cityEffect.intensity = 0.12f * buildProgress;
    cityEffect.fresnelPower = 0.95f;
    cityEffect.noiseAmount = 0.0f;
    cityEffect.time = time * 0.45f;
    model->SetDrawEffect(cityEffect);
    std::vector<Transform> cityTowers = BuildCityTowers(buildProgress);
    if (!cityTowers.empty()) {
        model->DrawInstanced(ids.arenaCityTowerModelId, cityTowers.data(),
                             static_cast<uint32_t>(cityTowers.size()), camera);
    }
    model->ClearDrawEffect();

    ModelDrawEffect blockEffect = cityEffect;
    blockEffect.color = {0.07f, 0.10f, 0.13f, 0.92f * buildProgress};
    blockEffect.intensity = 0.18f * buildProgress;
    model->SetDrawEffect(blockEffect);
    std::vector<Transform> cityBlocks = BuildCityBlocks(buildProgress);
    if (!cityBlocks.empty()) {
        model->DrawInstanced(ids.arenaCityTowerModelId, cityBlocks.data(),
                             static_cast<uint32_t>(cityBlocks.size()), camera);
    }
    model->ClearDrawEffect();

    ModelDrawEffect lightEffect{};
    lightEffect.enabled = true;
    lightEffect.additiveBlend = true;
    lightEffect.disableCulling = true;
    lightEffect.color = {0.96f, 0.58f, 0.28f, 0.18f * buildProgress};
    lightEffect.intensity = (0.035f + 0.025f * pulse) * buildProgress;
    lightEffect.fresnelPower = 0.8f;
    lightEffect.noiseAmount = 0.0f;
    lightEffect.time = time;
    model->SetDrawEffect(lightEffect);
    std::vector<Transform> cityWindows = BuildCityWindows(buildProgress);
    if (!cityWindows.empty()) {
        model->DrawInstanced(ids.arenaCityWindowModelId, cityWindows.data(),
                             static_cast<uint32_t>(cityWindows.size()), camera);
    }
    model->ClearDrawEffect();
}
} // namespace

const BattleArenaModelIds &EnsureBattleArenaModels(ModelManager *model,
                                                   TextureManager *texture) {
    if (gBattleArenaModelsInitialized) {
        return gBattleArenaModels;
    }

    const uint32_t arenaStoneTextureId =
        CreateBattleArenaStoneTexture(texture, 1024, 1024);
    gBattleArenaModels.arenaNoiseTextureId = arenaStoneTextureId;
    gBattleArenaModels.arenaFloorModelId =
        model->CreatePlane(arenaStoneTextureId,
                           MakeArenaMaterial({0.070f, 0.085f, 0.105f, 1.0f},
                                             true, 0.025f, 0.84f));
    gBattleArenaModels.arenaLowPolyTerrainModelId = model->CreatePlane(
        arenaStoneTextureId,
        MakeArenaMaterial({0.075f, 0.10f, 0.13f, 1.0f}, true, 0.016f, 0.80f));
    gBattleArenaModels.arenaDistantTerrainModelId = model->CreatePlane(
        arenaStoneTextureId,
        MakeArenaMaterial({0.12f, 0.18f, 0.22f, 1.0f}, true, 0.02f, 0.88f));
    gBattleArenaModels.arenaHazardSpireModelId = model->CreateCylinder(
        arenaStoneTextureId,
        MakeArenaMaterial({0.10f, 0.15f, 0.15f, 1.0f}, true, 0.025f, 0.88f),
        7, 0.04f, 0.92f, 8.8f);
    gBattleArenaModels.arenaHazardGlowRingModelId = model->CreateRing(
        0, MakeArenaMaterial({0.55f, 0.88f, 0.96f, 0.42f}, false, 0.0f, 0.46f),
        48, 2.45f, 0.34f);
    gBattleArenaModels.arenaCityTowerModelId = model->CreateCylinder(
        arenaStoneTextureId,
        MakeArenaMaterial({0.18f, 0.24f, 0.29f, 1.0f}, true, 0.025f, 0.76f),
        4, 0.72f, 0.72f, 1.0f);
    gBattleArenaModels.arenaCityWindowModelId = model->CreatePlane(
        0, MakeArenaMaterial({0.72f, 0.90f, 0.96f, 0.58f}, false, 0.0f, 0.40f));
    gBattleArenaModels.arenaGiantBodyModelId = model->CreateCylinder(
        arenaStoneTextureId,
        MakeArenaMaterial({0.055f, 0.10f, 0.11f, 1.0f}, true, 0.012f, 0.92f),
        9, 0.78f, 1.18f, 5.8f);
    gBattleArenaModels.arenaGiantHeadModelId = model->CreateCylinder(
        arenaStoneTextureId,
        MakeArenaMaterial({0.05f, 0.09f, 0.10f, 1.0f}, true, 0.01f, 0.94f),
        8, 0.92f, 1.05f, 1.15f);
    gBattleArenaModels.arenaCenterDiskModelId = model->CreateRing(
        arenaStoneTextureId,
        MakeArenaMaterial({0.72f, 0.58f, 0.30f, 1.0f}, false, 0.12f, 0.30f),
        96, 1.95f, 0.0f);
    gBattleArenaModels.arenaSpokeModelId = model->CreatePlane(
        arenaStoneTextureId,
        MakeArenaMaterial({0.28f, 0.21f, 0.12f, 1.0f}, false, 0.06f, 0.58f));
    gBattleArenaModels.arenaTutorialSpokeModelId = model->CreatePlane(
        arenaStoneTextureId, [] {
            Material material =
                MakeArenaMaterial({0.014f, 0.25f, 0.22f, 1.0f}, false, 0.045f,
                                  0.62f);
            material.cullMode = static_cast<int32_t>(MaterialCullMode::None);
            return material;
        }());
    gBattleArenaModels.arenaInnerRingModelId = model->CreateRing(
        arenaStoneTextureId,
        MakeArenaMaterial({0.64f, 0.60f, 0.44f, 1.0f}, false, 0.12f, 0.32f),
        96, 4.9f, 4.35f);
    gBattleArenaModels.arenaOuterRingModelId = model->CreateRing(
        arenaStoneTextureId,
        MakeArenaMaterial({0.68f, 0.28f, 0.18f, 1.0f}, false, 0.10f, 0.38f),
        128, 12.3f, 11.6f);
    gBattleArenaModels.arenaColumnModelId = model->CreateCylinder(
        arenaStoneTextureId,
        MakeArenaMaterial({0.22f, 0.26f, 0.30f, 1.0f}, true, 0.045f, 0.68f),
        24, 0.26f, 0.38f, 5.4f);
    gBattleArenaModels.arenaColumnCapModelId = model->CreateCylinder(
        arenaStoneTextureId,
        MakeArenaMaterial({0.62f, 0.30f, 0.18f, 1.0f}, false, 0.10f, 0.38f),
        32, 0.68f, 0.78f, 0.24f);
    gBattleArenaModels.arenaDomeModelId = model->CreateCylinder(
        arenaStoneTextureId,
        MakeArenaMaterial({0.09f, 0.14f, 0.28f, 0.78f}, true, 0.00f, 0.72f),
        128, 4.5f, 13.5f, 8.8f);
    gBattleArenaModels.arenaBarrierRingModelId = model->CreateRing(
        arenaStoneTextureId,
        MakeArenaMaterial({0.24f, 0.30f, 0.46f, 0.42f}, false, 0.00f, 0.34f),
        128, 13.1f, 12.9f);
    gBattleArenaModels.chargeWeakPointModelId = model->CreatePlane(
        0, MakeArenaMaterial({1.0f, 0.96f, 0.78f, 0.92f}, false, 0.02f,
                             0.20f));
    gBattleArenaModelsInitialized = true;
    return gBattleArenaModels;
}

void DrawBattleArena(ModelManager *model, const Camera &camera,
                     const BattleArenaModelIds &ids, float time,
                     float floorBuild, float tileBuild, float lineBuild,
                     float distantBuild, bool tutorialBackgroundMode,
                     bool backgroundOnlyMode) {
    const float pulse = 0.5f + 0.5f * std::sinf(time * 2.2f);
    constexpr float kPatternSpacing = 3.55f;

    if (!backgroundOnlyMode) {
        DrawDistantHazardBackdrop(model, camera, ids, time, distantBuild);
    }

    if (floorBuild > 0.0f) {
        Transform floor{};
        floor.position = {0.0f, -0.04f, 0.0f};
        floor.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
        floor.scale = {180.0f * floorBuild, 180.0f * floorBuild, 1.0f};
        model->Draw(ids.arenaFloorModelId, floor, camera);
    }

    if (!tutorialBackgroundMode) {
        ModelDrawEffect fieldEffect{};
        fieldEffect.enabled = true;
        fieldEffect.additiveBlend = false;
        fieldEffect.disableCulling = true;
        fieldEffect.color = {0.085f, 0.075f, 0.060f, 0.115f * tileBuild};
        fieldEffect.intensity = 0.004f * tileBuild;
        fieldEffect.fresnelPower = 0.7f;
        fieldEffect.noiseAmount = 0.0f;
        fieldEffect.baseDim = 0.0f;
        fieldEffect.time = time;
        model->SetDrawEffect(fieldEffect);
    }

    std::vector<Transform> fieldTiles =
        BuildFieldTiles(tileBuild, lineBuild);
    if (!fieldTiles.empty()) {
        const uint32_t spokeModelId = tutorialBackgroundMode
                                          ? ids.arenaTutorialSpokeModelId
                                          : ids.arenaSpokeModelId;
        model->DrawInstanced(spokeModelId, fieldTiles.data(),
                             static_cast<uint32_t>(fieldTiles.size()), camera);
    }
    model->ClearDrawEffect();

    if (!tutorialBackgroundMode) {
        ModelDrawEffect lineEffect{};
        lineEffect.enabled = true;
        lineEffect.additiveBlend = true;
        lineEffect.disableCulling = true;
        lineEffect.color = {0.22f, 0.10f, 0.045f, 0.026f * lineBuild};
        lineEffect.intensity = (0.0015f + 0.0015f * pulse) * lineBuild;
        lineEffect.fresnelPower = 0.75f;
        lineEffect.noiseAmount = 0.0f;
        lineEffect.time = time;
        model->SetDrawEffect(lineEffect);
        std::vector<Transform> glowLines;
        glowLines.reserve(62u);
        for (int i = -15; i <= 15; ++i) {
            const float glowBuild =
                SmoothStep01((lineBuild -
                              std::fabs(static_cast<float>(i)) / 15.0f *
                                  0.28f) /
                             0.52f);
            if (glowBuild <= 0.0f) {
                continue;
            }
            Transform lightX{};
            lightX.position = {0.0f, 0.034f,
                               static_cast<float>(i) * kPatternSpacing};
            lightX.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
            lightX.scale = {160.0f * glowBuild, 0.016f, 1.0f};
            glowLines.push_back(lightX);

            Transform lightZ{};
            lightZ.position = {static_cast<float>(i) * kPatternSpacing, 0.035f,
                               0.0f};
            lightZ.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
            lightZ.scale = {0.016f, 160.0f * glowBuild, 1.0f};
            glowLines.push_back(lightZ);
        }
        if (!glowLines.empty()) {
            model->DrawInstanced(ids.arenaCityWindowModelId, glowLines.data(),
                                 static_cast<uint32_t>(glowLines.size()),
                                 camera);
        }
        model->ClearDrawEffect();
    }

    if (backgroundOnlyMode) {
        DrawDistantHazardBackdrop(model, camera, ids, time, distantBuild);
    }
}
