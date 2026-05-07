#include "GameSceneHud.h"
#include "DirectXCommon.h"
#include "SpriteManager.h"
#include "TextureManager.h"

void GameSceneHud::Initialize(const SceneContext &ctx) {
    DirectXCommon *dx = ctx.dxCommon;
    TextureManager *texture = ctx.texture;
    SpriteManager *sprite = ctx.sprite;

    dx->BeginUpload();
    playerHpSpriteId_ = sprite->Create(L"resources/texture/white64x64.png");
    playerHpBackSpriteId_ = sprite->Create(L"resources/texture/white64x64.png");

    bossHpSpriteId_ = sprite->Create(L"resources/texture/white64x64.png");
    bossHpBackSpriteId_ = sprite->Create(L"resources/texture/white64x64.png");
    dx->EndUpload();

    texture->ReleaseUploadBuffers();

    // PlayerのHPバー
    auto &playerHpBackSprite = sprite->GetSprite(playerHpBackSpriteId_);
    playerHpBackSprite.position = {856.0f, 656.0f};
    playerHpBackSprite.size = {kPlayerHpBarMaxWidth + 8.0f, 48.0f};
    playerHpBackSprite.color = {0.2f, 0.2f, 0.2f, 1.0f};

    auto &playerHpSprite = sprite->GetSprite(playerHpSpriteId_);
    playerHpSprite.position = {860.0f, 660.0f};
    playerHpSprite.size = {kPlayerHpBarMaxWidth, 40.0f};
    playerHpSprite.color = {0.0f, 1.0f, 0.0f, 1.0f};

    // BossのHPバー
    auto &bossHpBackSprite = sprite->GetSprite(bossHpBackSpriteId_);
    bossHpBackSprite.position = {246.0f, 56.0f};
    bossHpBackSprite.size = {kBossHpBarMaxWidth + 8.0f, 48.0f};
    bossHpBackSprite.color = {0.2f, 0.2f, 0.2f, 1.0f};

    auto &bossHpSprite = sprite->GetSprite(bossHpSpriteId_);
    bossHpSprite.position = {250.0f, 60.0f};
    bossHpSprite.size = {kBossHpBarMaxWidth, 40.0f};
    bossHpSprite.color = {1.0f, 0.0f, 0.0f, 1.0f};
}

void GameSceneHud::Update(const SceneContext &ctx, float playerHp,
                          float enemyHp) {
    // プレイヤーHPの更新処理
    float playerHpRate = playerHp / kPlayerHpMax;
    if (playerHpRate < 0.0f) {
        playerHpRate = 0.0f;
    }

    if (playerHpRate > 1.0f) {
        playerHpRate = 1.0f;
    }

    auto &playerHpSprite = ctx.sprite->GetSprite(playerHpSpriteId_);
    playerHpSprite.size.x = kPlayerHpBarMaxWidth * playerHpRate;

    // ボスHPの更新処理
    float bossHpRate = enemyHp / kBossHpMax;
    if (bossHpRate < 0.0f) {
        bossHpRate = 0.0f;
    }

    if (bossHpRate > 1.0f) {
        bossHpRate = 1.0f;
    }

    auto &bossHpSprite = ctx.sprite->GetSprite(bossHpSpriteId_);
    bossHpSprite.size.x = kBossHpBarMaxWidth * bossHpRate;
}

void GameSceneHud::Draw(const SceneContext &ctx) {
    ctx.sprite->PreDraw();
    ctx.sprite->Draw(playerHpBackSpriteId_);
    ctx.sprite->Draw(playerHpSpriteId_);

    ctx.sprite->Draw(bossHpBackSpriteId_);
    ctx.sprite->Draw(bossHpSpriteId_);
    ctx.sprite->PostDraw();
}