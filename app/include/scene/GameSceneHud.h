#pragma once
#include "Enemy.h"
#include "Player.h"
#include <SceneContext.h>
#include <Sprite.h>
#include <cstdint>
#include <string>

class GameSceneHud {
  public:
    void Initialize(const SceneContext &ctx);
    void Update(const SceneContext &ctx, float playerHp, float enemyHp);
    void Draw(const SceneContext &ctx);

  private:
    // UI
    uint32_t playerHpSpriteId_ = 0;
    uint32_t playerHpBackSpriteId_ = 0;

    uint32_t bossHpSpriteId_ = 0;
    uint32_t bossHpBackSpriteId_ = 0;

    static constexpr float kPlayerHpMax = 100.0f;
    static constexpr float kPlayerHpBarMaxWidth = 400.0f;
    static constexpr float kBossHpMax = 1000.0f;
    static constexpr float kBossHpBarMaxWidth = 800.0f;
};
