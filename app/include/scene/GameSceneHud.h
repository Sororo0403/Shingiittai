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
    Sprite playerHpSprite_{};
    Sprite playerHpBackSprite_{};
    Sprite bossHpSprite_{};
    Sprite bossHpBackSprite_{};

    static constexpr float kPlayerHpMax = 100.0f;
    static constexpr float kPlayerHpBarMaxWidth = 400.0f;
    static constexpr float kBossHpMax = 1000.0f;
    static constexpr float kBossHpBarMaxWidth = 800.0f;
};
