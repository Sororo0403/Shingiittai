#pragma once

#include "Enemy.h"
#include <DirectXMath.h>
#include <cstdint>
#include <vector>

class ModelManager;
class TextureManager;

struct EnemyPhaseMaterialSet {
    static constexpr uint32_t kTextureSize = 512;

    uint32_t rustTextureId = 0;
    uint32_t cleanMetalTextureId = 0;
    uint32_t goldMetalTextureId = 0;
    uint32_t blendTextureId = 0;
    uint32_t currentTextureId = 0;

    std::vector<uint8_t> rustPixels;
    std::vector<uint8_t> cleanMetalPixels;
    std::vector<uint8_t> goldMetalPixels;
    std::vector<uint8_t> blendPixels;
};

void InitializeEnemyPhaseMaterialSet(TextureManager *texture,
                                     EnemyPhaseMaterialSet &materials);

uint32_t GetEnemyPhaseTextureId(const EnemyPhaseMaterialSet &materials);

void ApplyEnemyPhaseMaterial(ModelManager *modelManager,
                             TextureManager *textureManager, uint32_t modelId,
                             EnemyPhaseMaterialSet &materials,
                             BossPhase phase, bool phaseTransitionActive,
                             float transitionRatio);
