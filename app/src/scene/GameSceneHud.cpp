#include "GameSceneHud.h"
#include "SpriteManager.h"
#include "WinApp.h"
#include <algorithm>

void GameSceneHud::Initialize(const SceneContext &ctx) {
    (void)ctx;
    playerHpRate_ = 1.0f;
    bossHpRate_ = 1.0f;
}

void GameSceneHud::Update(const SceneContext &ctx, float playerHp,
                          float enemyHp, float enemyMaxHp) {
    (void)ctx;

    playerHpRate_ = std::clamp(playerHp / kPlayerHpMax, 0.0f, 1.0f);
    bossHpRate_ =
        enemyMaxHp > 0.0f ? std::clamp(enemyHp / enemyMaxHp, 0.0f, 1.0f)
                          : 0.0f;
}

void GameSceneHud::Draw(const SceneContext &ctx) {
    const float screenWidth =
        ctx.winApp ? static_cast<float>(ctx.winApp->GetWidth()) : 1280.0f;
    const float screenHeight =
        ctx.winApp ? static_cast<float>(ctx.winApp->GetHeight()) : 720.0f;

    const float bossW = (std::min)(kBossHpBarMaxWidth, screenWidth * 0.62f);
    const float bossX = (screenWidth - bossW) * 0.5f;
    const float bossY = 34.0f;

    const float playerW = (std::min)(kPlayerHpBarMaxWidth, screenWidth * 0.34f);
    const float playerX = screenWidth - playerW - 36.0f;
    const float playerY = screenHeight - 58.0f;

    ctx.sprite->PreDraw();

    DrawBar(ctx, bossX, bossY, bossW, 18.0f, bossHpRate_,
            {0.95f, 0.12f, 0.08f, 0.96f}, {1.0f, 0.58f, 0.22f, 0.92f});
    DrawBar(ctx, playerX, playerY, playerW, 16.0f, playerHpRate_,
            {0.12f, 0.88f, 0.42f, 0.96f}, {0.74f, 1.0f, 0.72f, 0.88f});

    ctx.sprite->PostDraw();
}

void GameSceneHud::DrawRect(const SceneContext &ctx, float x, float y, float w,
                            float h, const DirectX::XMFLOAT4 &color) {
    Sprite sprite{};
    sprite.textureId = 0;
    sprite.position = {x, y};
    sprite.size = {w, h};
    sprite.color = color;
    ctx.sprite->DrawSprite(sprite);
}

void GameSceneHud::DrawBar(const SceneContext &ctx, float x, float y, float w,
                           float h, float rate,
                           const DirectX::XMFLOAT4 &fill,
                           const DirectX::XMFLOAT4 &accent) {
    const float clampedRate = std::clamp(rate, 0.0f, 1.0f);
    const float frame = 3.0f;
    const float fillW = (std::max)(0.0f, (w - frame * 2.0f) * clampedRate);

    DrawRect(ctx, x - 8.0f, y - 7.0f, w + 16.0f, h + 14.0f,
             {0.015f, 0.018f, 0.025f, 0.72f});
    DrawRect(ctx, x - 3.0f, y - 3.0f, w + 6.0f, h + 6.0f,
             {0.80f, 0.70f, 0.52f, 0.34f});
    DrawRect(ctx, x, y, w, h, {0.035f, 0.040f, 0.052f, 0.96f});
    DrawRect(ctx, x + frame, y + frame, w - frame * 2.0f, h - frame * 2.0f,
             {0.09f, 0.095f, 0.11f, 0.90f});

    if (fillW > 0.0f) {
        DrawRect(ctx, x + frame, y + frame, fillW, h - frame * 2.0f, fill);
        DrawRect(ctx, x + frame, y + frame, fillW,
                 (std::max)(2.0f, (h - frame * 2.0f) * 0.30f), accent);
        DrawRect(ctx, x + frame + fillW - 2.0f, y + 1.0f, 2.0f, h - 2.0f,
                 {1.0f, 0.96f, 0.76f, 0.64f});
    }
}
