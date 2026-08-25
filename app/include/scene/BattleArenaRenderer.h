#pragma once

#include "Camera.h"
#include <cstdint>

class ModelManager;
class TextureManager;

struct BattleArenaModelIds {
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
    uint32_t arenaTutorialSpokeModelId = 0;
    uint32_t arenaInnerRingModelId = 0;
    uint32_t arenaOuterRingModelId = 0;
    uint32_t arenaColumnModelId = 0;
    uint32_t arenaColumnCapModelId = 0;
    uint32_t arenaDomeModelId = 0;
    uint32_t arenaBarrierRingModelId = 0;
    uint32_t chargeWeakPointModelId = 0;
};

const BattleArenaModelIds &EnsureBattleArenaModels(ModelManager *model,
                                                   TextureManager *texture);

void DrawBattleArena(ModelManager *model, const Camera &camera,
                     const BattleArenaModelIds &ids, float time,
                     float floorBuild = 1.0f, float tileBuild = 1.0f,
                     float lineBuild = 1.0f, float distantBuild = 1.0f,
                     bool tutorialBackgroundMode = false,
                     bool backgroundOnlyMode = false);
