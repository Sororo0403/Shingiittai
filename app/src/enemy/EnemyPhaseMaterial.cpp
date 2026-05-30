#include "enemy/EnemyPhaseMaterial.h"

#include "Material.h"
#include "Model.h"
#include "ModelManager.h"
#include "TextureManager.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace {
uint32_t Hash2D(uint32_t x, uint32_t y, uint32_t seed) {
    uint32_t h = x * 374761393u + y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13u)) * 1274126177u;
    return h ^ (h >> 16u);
}

float SmoothStep01(float value) {
    const float t = std::clamp(value, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

XMFLOAT4 LerpColor(const XMFLOAT4 &from, const XMFLOAT4 &to, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return {from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t,
            from.z + (to.z - from.z) * t, from.w + (to.w - from.w) * t};
}

std::vector<uint8_t>
CreateProceduralTexturePixels(uint32_t width, uint32_t height,
                              const XMFLOAT3 &baseColor,
                              const XMFLOAT3 &accentColor, uint32_t seed,
                              float grainStrength,
                              bool isotropicPattern = false) {
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4u);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint32_t h = Hash2D(x / 3u, y / 3u, seed);
            const float noise = static_cast<float>(h & 255u) / 255.0f;
            float pattern = 0.0f;
            if (isotropicPattern) {
                const float broad =
                    static_cast<float>(Hash2D(x / 13u, y / 13u, seed + 17u) &
                                       255u) /
                    255.0f;
                const float mid =
                    static_cast<float>(Hash2D(x / 7u, y / 7u, seed + 23u) &
                                       255u) /
                    255.0f;
                const float remaining = (std::max)(1.0f - grainStrength, 0.0f);
                pattern = broad * remaining * 0.58f + mid * remaining * 0.42f;
            } else {
                pattern =
                    static_cast<float>(
                        Hash2D(x / 19u, y / 7u, seed + 17u) & 255u) /
                    255.0f * (1.0f - grainStrength);
            }
            const float t =
                std::clamp(noise * grainStrength + pattern, 0.0f, 1.0f);
            const float fine =
                static_cast<float>(Hash2D(x, y, seed + 31u) & 63u) / 255.0f;
            const XMFLOAT3 color{
                std::clamp(baseColor.x + (accentColor.x - baseColor.x) * t +
                               fine,
                           0.0f, 1.0f),
                std::clamp(baseColor.y + (accentColor.y - baseColor.y) * t +
                               fine,
                           0.0f, 1.0f),
                std::clamp(baseColor.z + (accentColor.z - baseColor.z) * t +
                               fine,
                           0.0f, 1.0f),
            };
            const size_t index = (static_cast<size_t>(y) * width + x) * 4u;
            pixels[index + 0] = static_cast<uint8_t>(color.x * 255.0f);
            pixels[index + 1] = static_cast<uint8_t>(color.y * 255.0f);
            pixels[index + 2] = static_cast<uint8_t>(color.z * 255.0f);
            pixels[index + 3] = 255u;
        }
    }
    return pixels;
}

std::vector<uint8_t> CreateSmoothMetalTexturePixels(
    uint32_t width, uint32_t height, const XMFLOAT3 &baseColor,
    const XMFLOAT3 &highlightColor) {
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4u);
    constexpr uint32_t kBrushSeed = 0xC1E4u;
    for (uint32_t y = 0; y < height; ++y) {
        const float v = height > 1u ? static_cast<float>(y) /
                                          static_cast<float>(height - 1u)
                                    : 0.0f;
        const float sheen = 0.12f + 0.11f * std::sinf(v * 3.14159265f * 2.0f -
                                                      0.45f);
        for (uint32_t x = 0; x < width; ++x) {
            const size_t index = (static_cast<size_t>(y) * width + x) * 4u;
            const float wideColumn =
                static_cast<float>(Hash2D(x / 19u, 0u, kBrushSeed) & 255u) /
                255.0f;
            const float narrowColumn =
                static_cast<float>(Hash2D(x / 5u, 3u, kBrushSeed + 11u) &
                                   255u) /
                255.0f;
            const float scratchNoise =
                static_cast<float>(Hash2D(x, y / 23u, kBrushSeed + 29u) &
                                   255u) /
                255.0f;
            const float darkGroove =
                scratchNoise > 0.88f ? (scratchNoise - 0.88f) * 1.65f : 0.0f;
            const float brightEdge =
                scratchNoise < 0.06f ? (0.06f - scratchNoise) * 1.95f : 0.0f;
            const float brush =
                (wideColumn - 0.5f) * 0.18f +
                (narrowColumn - 0.5f) * 0.13f - darkGroove + brightEdge;
            const float t = std::clamp(sheen + brush, 0.0f, 1.0f);
            const XMFLOAT3 color{
                baseColor.x + (highlightColor.x - baseColor.x) * t,
                baseColor.y + (highlightColor.y - baseColor.y) * t,
                baseColor.z + (highlightColor.z - baseColor.z) * t};
            pixels[index + 0] = static_cast<uint8_t>(
                std::clamp(color.x, 0.0f, 1.0f) * 255.0f);
            pixels[index + 1] = static_cast<uint8_t>(
                std::clamp(color.y, 0.0f, 1.0f) * 255.0f);
            pixels[index + 2] = static_cast<uint8_t>(
                std::clamp(color.z, 0.0f, 1.0f) * 255.0f);
            pixels[index + 3] = 255u;
        }
    }
    return pixels;
}

void BlendTexturePixels(const std::vector<uint8_t> &from,
                        const std::vector<uint8_t> &to,
                        std::vector<uint8_t> &out, float t) {
    const size_t count = (std::min)(from.size(), to.size());
    out.resize(count);
    t = std::clamp(t, 0.0f, 1.0f);
    for (size_t i = 0; i < count; ++i) {
        const float value =
            static_cast<float>(from[i]) +
            (static_cast<float>(to[i]) - static_cast<float>(from[i])) * t;
        out[i] = static_cast<uint8_t>(std::clamp(value, 0.0f, 255.0f));
    }
}

struct EnemyPhaseMaterialProfile {
    uint32_t textureId = 0;
    std::vector<XMFLOAT4> palette{};
    float reflection = 0.0f;
    float fresnel = 0.0f;
    float roughness = 0.0f;
};

EnemyPhaseMaterialProfile LerpEnemyPhaseProfile(
    const EnemyPhaseMaterialProfile &from,
    const EnemyPhaseMaterialProfile &to, float t) {
    t = SmoothStep01(t);
    EnemyPhaseMaterialProfile profile = to;
    profile.reflection = from.reflection + (to.reflection - from.reflection) * t;
    profile.fresnel = from.fresnel + (to.fresnel - from.fresnel) * t;
    profile.roughness = from.roughness + (to.roughness - from.roughness) * t;

    const size_t paletteSize = (std::max)(from.palette.size(), to.palette.size());
    profile.palette.clear();
    profile.palette.reserve(paletteSize);
    for (size_t i = 0; i < paletteSize; ++i) {
        const XMFLOAT4 fromColor =
            from.palette.empty() ? XMFLOAT4{1.0f, 1.0f, 1.0f, 1.0f}
                                 : from.palette[i % from.palette.size()];
        const XMFLOAT4 toColor =
            to.palette.empty() ? XMFLOAT4{1.0f, 1.0f, 1.0f, 1.0f}
                               : to.palette[i % to.palette.size()];
        profile.palette.push_back(LerpColor(fromColor, toColor, t));
    }
    return profile;
}

void ApplyEnemyPhaseMaterialProfile(ModelManager *modelManager, uint32_t modelId,
                                    const EnemyPhaseMaterialProfile &profile) {
    if (modelManager == nullptr || profile.palette.empty()) {
        return;
    }

    Model *model = modelManager->GetModel(modelId);
    if (model == nullptr) {
        return;
    }

    model->textureId = profile.textureId;
    size_t colorIndex = 0;
    for (ModelSubMesh &subMesh : model->subMeshes) {
        subMesh.textureId = profile.textureId;

        Material material = modelManager->GetMaterial(subMesh.materialId);
        material.enableTexture = 1;
        material.baseColorTextureId = profile.textureId;
        material.color = profile.palette[colorIndex % profile.palette.size()];
        material.color.w = 1.0f;
        XMStoreFloat4x4(&material.uvTransform,
                        XMMatrixTranspose(XMMatrixIdentity()));
        material.reflectionStrength = profile.reflection;
        material.reflectionFresnelStrength = profile.fresnel;
        material.reflectionRoughness = profile.roughness;
        material.roughness = profile.roughness;
        material.blendMode = static_cast<int32_t>(BlendMode::Opaque);
        material.depthWrite = 1;
        material.enableDissolve = 0.0f;
        modelManager->SetMaterial(subMesh.materialId, material);
        ++colorIndex;
    }
}
} // namespace

void InitializeEnemyPhaseMaterialSet(TextureManager *texture,
                                     EnemyPhaseMaterialSet &materials) {
    if (texture == nullptr || materials.rustTextureId != 0) {
        return;
    }

    constexpr uint32_t size = EnemyPhaseMaterialSet::kTextureSize;
    materials.rustPixels =
        CreateProceduralTexturePixels(size, size, {0.23f, 0.22f, 0.20f},
                                      {0.70f, 0.30f, 0.12f}, 0x914Au, 0.62f,
                                      true);
    materials.cleanMetalPixels =
        CreateSmoothMetalTexturePixels(size, size, {0.31f, 0.33f, 0.32f},
                                       {0.50f, 0.51f, 0.46f});
    materials.goldMetalPixels =
        CreateProceduralTexturePixels(size, size, {0.31f, 0.22f, 0.08f},
                                      {0.56f, 0.41f, 0.16f}, 0xB05Du, 0.38f,
                                      true);
    materials.blendPixels = materials.rustPixels;

    materials.rustTextureId =
        texture->CreateFromRgbaPixels(size, size, materials.rustPixels.data());
    materials.cleanMetalTextureId = texture->CreateFromRgbaPixels(
        size, size, materials.cleanMetalPixels.data());
    materials.goldMetalTextureId =
        texture->CreateFromRgbaPixels(size, size, materials.goldMetalPixels.data());
    materials.blendTextureId =
        texture->CreateFromRgbaPixels(size, size, materials.blendPixels.data());
    materials.currentTextureId = materials.rustTextureId;
}

uint32_t GetEnemyPhaseTextureId(const EnemyPhaseMaterialSet &materials) {
    if (materials.currentTextureId != 0) {
        return materials.currentTextureId;
    }
    if (materials.rustTextureId != 0) {
        return materials.rustTextureId;
    }
    return materials.blendTextureId;
}

void ApplyEnemyPhaseMaterial(ModelManager *modelManager,
                             TextureManager *textureManager, uint32_t modelId,
                             EnemyPhaseMaterialSet &materials,
                             BossPhase phase, bool phaseTransitionActive,
                             float transitionRatio) {
    if (modelManager == nullptr || modelId == 0) {
        return;
    }
    InitializeEnemyPhaseMaterialSet(textureManager, materials);
    if (materials.rustTextureId == 0 || materials.cleanMetalTextureId == 0 ||
        materials.goldMetalTextureId == 0 || materials.blendTextureId == 0) {
        return;
    }

    const EnemyPhaseMaterialProfile rustProfile{
        materials.rustTextureId,
        {{0.35f, 0.33f, 0.28f, 1.0f},
         {0.30f, 0.29f, 0.25f, 1.0f},
         {0.42f, 0.34f, 0.26f, 1.0f}},
        0.045f,
        0.012f,
        0.94f};
    const EnemyPhaseMaterialProfile cleanMetalProfile{
        materials.cleanMetalTextureId,
        {{0.46f, 0.48f, 0.45f, 1.0f},
         {0.31f, 0.34f, 0.34f, 1.0f},
         {0.55f, 0.53f, 0.47f, 1.0f}},
        0.30f,
        0.12f,
        0.38f};
    const EnemyPhaseMaterialProfile goldProfile{
        materials.goldMetalTextureId,
        {{0.50f, 0.36f, 0.13f, 1.0f},
         {0.34f, 0.24f, 0.09f, 1.0f},
         {0.62f, 0.46f, 0.18f, 1.0f}},
        0.20f,
        0.08f,
        0.54f};

    EnemyPhaseMaterialProfile profile = rustProfile;
    const std::vector<uint8_t> *blendFromPixels = nullptr;
    const std::vector<uint8_t> *blendToPixels = nullptr;
    if (phase == BossPhase::Phase2) {
        profile =
            LerpEnemyPhaseProfile(rustProfile, cleanMetalProfile, transitionRatio);
        if (phaseTransitionActive) {
            blendFromPixels = &materials.rustPixels;
            blendToPixels = &materials.cleanMetalPixels;
            profile.textureId = materials.blendTextureId;
        } else {
            profile.textureId = materials.cleanMetalTextureId;
        }
    } else if (phase == BossPhase::Phase3) {
        profile =
            LerpEnemyPhaseProfile(cleanMetalProfile, goldProfile, transitionRatio);
        if (phaseTransitionActive) {
            blendFromPixels = &materials.cleanMetalPixels;
            blendToPixels = &materials.goldMetalPixels;
            profile.textureId = materials.blendTextureId;
        } else {
            profile.textureId = materials.goldMetalTextureId;
        }
    }

    if (blendFromPixels != nullptr && blendToPixels != nullptr &&
        textureManager != nullptr) {
        BlendTexturePixels(*blendFromPixels, *blendToPixels,
                           materials.blendPixels, SmoothStep01(transitionRatio));
        textureManager->UpdateTexture2D(materials.blendTextureId,
                                        materials.blendPixels.data(),
                                        EnemyPhaseMaterialSet::kTextureSize * 4u);
    }

    materials.currentTextureId = profile.textureId;
    ApplyEnemyPhaseMaterialProfile(modelManager, modelId, profile);
}
