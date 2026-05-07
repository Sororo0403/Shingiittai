#include "GameSceneHud.h"
#include "SpriteManager.h"

void GameSceneHud::Initialize(const SceneContext &ctx) {
    (void)ctx;

    playerHpBackSprite_.textureId = 0;
    playerHpBackSprite_.position = {856.0f, 656.0f};
    playerHpBackSprite_.size = {kPlayerHpBarMaxWidth + 8.0f, 48.0f};
    playerHpBackSprite_.color = {0.2f, 0.2f, 0.2f, 1.0f};

    playerHpSprite_.textureId = 0;
    playerHpSprite_.position = {860.0f, 660.0f};
    playerHpSprite_.size = {kPlayerHpBarMaxWidth, 40.0f};
    playerHpSprite_.color = {0.0f, 1.0f, 0.0f, 1.0f};

    bossHpBackSprite_.textureId = 0;
    bossHpBackSprite_.position = {246.0f, 56.0f};
    bossHpBackSprite_.size = {kBossHpBarMaxWidth + 8.0f, 48.0f};
    bossHpBackSprite_.color = {0.2f, 0.2f, 0.2f, 1.0f};

    bossHpSprite_.textureId = 0;
    bossHpSprite_.position = {250.0f, 60.0f};
    bossHpSprite_.size = {kBossHpBarMaxWidth, 40.0f};
    bossHpSprite_.color = {1.0f, 0.0f, 0.0f, 1.0f};
}

void GameSceneHud::Update(const SceneContext &ctx, float playerHp,
                          float enemyHp) {
    (void)ctx;

    float playerHpRate = playerHp / kPlayerHpMax;
    if (playerHpRate < 0.0f) {
        playerHpRate = 0.0f;
    }

    if (playerHpRate > 1.0f) {
        playerHpRate = 1.0f;
    }

    playerHpSprite_.size.x = kPlayerHpBarMaxWidth * playerHpRate;

    float bossHpRate = enemyHp / kBossHpMax;
    if (bossHpRate < 0.0f) {
        bossHpRate = 0.0f;
    }

    if (bossHpRate > 1.0f) {
        bossHpRate = 1.0f;
    }

    bossHpSprite_.size.x = kBossHpBarMaxWidth * bossHpRate;
}

void GameSceneHud::Draw(const SceneContext &ctx) {
    ctx.sprite->PreDraw();
    ctx.sprite->DrawSprite(playerHpBackSprite_);
    ctx.sprite->DrawSprite(playerHpSprite_);

    ctx.sprite->DrawSprite(bossHpBackSprite_);
    ctx.sprite->DrawSprite(bossHpSprite_);
    ctx.sprite->PostDraw();
}
