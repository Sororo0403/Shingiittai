#pragma once

#include "Camera.h"
#include <cstdint>

class ModelManager;
class TextureManager;

/// <summary>
/// バトルアリーナを構成するモデルとテクスチャの識別子を保持する
/// </summary>
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

/// <summary>
/// アリーナ描画に必要なモデルを遅延生成し、共有識別子を返す
/// </summary>
const BattleArenaModelIds &EnsureBattleArenaModels(ModelManager *model,
                                                   TextureManager *texture);

/// <summary>
/// 指定した構築進捗と表示モードでバトルアリーナを描画する
/// </summary>
void DrawBattleArena(ModelManager *model, const Camera &camera,
                     const BattleArenaModelIds &ids, float time,
                     float floorBuild = 1.0f, float tileBuild = 1.0f,
                     float lineBuild = 1.0f, float distantBuild = 1.0f,
                     bool tutorialBackgroundMode = false,
                     bool backgroundOnlyMode = false);
