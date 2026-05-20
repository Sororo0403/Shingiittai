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
    void Update(const SceneContext &ctx, float playerHp, float enemyHp,
                float enemyMaxHp);
    void Draw(const SceneContext &ctx);

  private:
    void DrawRect(const SceneContext &ctx, float x, float y, float w, float h,
                  const DirectX::XMFLOAT4 &color);
    void DrawBar(const SceneContext &ctx, float x, float y, float w, float h,
                 float rate, const DirectX::XMFLOAT4 &fill,
                 const DirectX::XMFLOAT4 &accent);

    float playerHpRate_ = 1.0f;
    float bossHpRate_ = 1.0f;

    static constexpr float kPlayerHpMax = 100.0f;
    static constexpr float kPlayerHpBarMaxWidth = 400.0f;
    static constexpr float kBossHpBarMaxWidth = 800.0f;
};
