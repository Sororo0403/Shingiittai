#include "BattleResultScene.h"
#include "DirectXCommon.h"
#include "GameScene.h"
#include "Input.h"
#include "PostEffectRenderer.h"
#include "SceneManager.h"
#include "SpriteManager.h"
#include "TitleScene.h"
#include "TextureManager.h"
#include "WinApp.h"
#include <Xinput.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>

using namespace DirectX;

namespace {
constexpr const char *kRankingPath = "app/resources/result/clear_ranking.txt";

XMFLOAT4 Color(float r, float g, float b, float a = 1.0f) {
    return {r, g, b, a};
}

float SmoothStep(float t) { return t * t * (3.0f - 2.0f * t); }
} // namespace

BattleResultScene::BattleResultScene(ResultKind resultKind, float clearTime,
                                     PlayerWeaponType weaponType)
    : resultKind_(resultKind), weaponType_(weaponType),
      clearTime_((std::max)(0.0f, clearTime)) {}

void BattleResultScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    ctx_->postEffectRenderer->SetColorMode(PostEffectRenderer::ColorMode::None);
    ctx_->postEffectRenderer->SetVignettingEnabled(true);
    ctx_->postEffectRenderer->SetVignettingStrength(0.30f);
    ctx_->postEffectRenderer->SetSceneDimStrength(0.0f);
    ctx_->postEffectRenderer->SetRadialBlurStrength(0.0f);

    ctx_->dxCommon->BeginUpload();
    clearTitle_ = LoadTextureImage(L"app/resources/result/clear_title.png");
    gameOverTitle_ =
        LoadTextureImage(L"app/resources/result/game_over_title.png");
    clearTimeLabel_ =
        LoadTextureImage(L"app/resources/result/clear_time.png");
    rankingLabel_ = LoadTextureImage(L"app/resources/result/ranking.png");
    newRecordLabel_ =
        LoadTextureImage(L"app/resources/result/new_record.png");
    noClearTimeLabel_ =
        LoadTextureImage(L"app/resources/result/no_clear_time.png");
    retryLabel_ = LoadTextureImage(L"app/resources/result/retry.png");
    menuLabel_ = LoadTextureImage(L"app/resources/result/menu.png");
    for (int i = 0; i < 10; ++i) {
        digitImages_[static_cast<size_t>(i)] =
            LoadTextureImage(L"app/resources/result/char_" +
                             std::to_wstring(i) + L".png");
    }
    for (int i = 0; i < kMaxRanking; ++i) {
        rankImages_[static_cast<size_t>(i)] =
            LoadTextureImage(L"app/resources/result/rank_" +
                             std::to_wstring(i + 1) + L".png");
    }
    colonImage_ = LoadTextureImage(L"app/resources/result/char_colon.png");
    dotImage_ = LoadTextureImage(L"app/resources/result/char_dot.png");
    dashImage_ = LoadTextureImage(L"app/resources/result/char_dash.png");
    secondImage_ = LoadTextureImage(L"app/resources/result/char_s.png");
    ctx_->dxCommon->EndUpload();
    ctx_->texture->ReleaseUploadBuffers();

    LoadRanking();
    if (resultKind_ == ResultKind::Clear) {
        RegisterClearTime();
    }
}

void BattleResultScene::Update() {
    sceneTime_ += ctx_->deltaTime;
    Input *input = ctx_->input;

    const bool retry =
        input->IsKeyTrigger(DIK_RETURN) || input->IsKeyTrigger(DIK_SPACE) ||
        input->IsMouseTrigger(0) ||
        (input->IsGamepadConnected() &&
         input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_A));
    if (retry) {
        sceneManager_->ChangeScene(std::make_unique<GameScene>(weaponType_));
        return;
    }

    const bool menu =
        input->IsKeyTrigger(DIK_TAB) ||
        (input->IsGamepadConnected() &&
         input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_B));
    if (menu) {
        sceneManager_->ChangeScene(std::make_unique<TitleScene>());
    }
}

void BattleResultScene::Draw() {
    const float w = static_cast<float>(ctx_->winApp->GetWidth());
    const float h = static_cast<float>(ctx_->winApp->GetHeight());

    ctx_->sprite->PreDraw();
    DrawBackground(w, h);
    if (resultKind_ == ResultKind::Clear) {
        DrawClear(w, h);
    } else {
        DrawGameOver(w, h);
    }
    ctx_->sprite->PostDraw();
}

void BattleResultScene::DrawOverlay() {}

BattleResultScene::Image
BattleResultScene::LoadTextureImage(const std::wstring &path) {
    Image image{};
    image.textureId = ctx_->texture->Load(path);
    image.width = static_cast<float>(ctx_->texture->GetWidth(image.textureId));
    image.height =
        static_cast<float>(ctx_->texture->GetHeight(image.textureId));
    return image;
}

void BattleResultScene::LoadRanking() {
    ranking_.clear();
    std::ifstream file(kRankingPath);
    float value = 0.0f;
    while (file >> value) {
        if (value > 0.0f) {
            ranking_.push_back(value);
        }
    }
    std::sort(ranking_.begin(), ranking_.end());
    if (ranking_.size() > static_cast<size_t>(kMaxRanking)) {
        ranking_.resize(kMaxRanking);
    }
}

void BattleResultScene::SaveRanking() const {
    std::filesystem::create_directories("app/resources/result");
    std::ofstream file(kRankingPath, std::ios::trunc);
    file << std::fixed << std::setprecision(3);
    for (float value : ranking_) {
        file << value << '\n';
    }
}

void BattleResultScene::RegisterClearTime() {
    if (registered_) {
        return;
    }
    registered_ = true;
    ranking_.push_back(clearTime_);
    std::sort(ranking_.begin(), ranking_.end());
    const auto it = std::find(ranking_.begin(), ranking_.end(), clearTime_);
    if (it != ranking_.end()) {
        newRecordIndex_ = static_cast<int>(std::distance(ranking_.begin(), it));
    }
    if (ranking_.size() > static_cast<size_t>(kMaxRanking)) {
        ranking_.resize(kMaxRanking);
    }
    newRecord_ = newRecordIndex_ >= 0 && newRecordIndex_ < kMaxRanking;
    SaveRanking();
}

void BattleResultScene::DrawBackground(float screenWidth, float screenHeight) {
    const float pulse = 0.5f + 0.5f * std::sin(sceneTime_ * 2.2f);
    const XMFLOAT4 base =
        resultKind_ == ResultKind::Clear
            ? Color(0.015f, 0.045f + pulse * 0.010f, 0.050f, 1.0f)
            : Color(0.055f + pulse * 0.010f, 0.018f, 0.020f, 1.0f);
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight, base);
    DrawRect(screenWidth * 0.08f, screenHeight * 0.16f, screenWidth * 0.84f,
             screenHeight * 0.68f, Color(0.02f, 0.024f, 0.026f, 0.86f));
    DrawRect(screenWidth * 0.08f, screenHeight * 0.16f, screenWidth * 0.84f,
             4.0f,
             resultKind_ == ResultKind::Clear
                 ? Color(1.0f, 0.82f, 0.26f, 0.88f)
                 : Color(1.0f, 0.20f, 0.18f, 0.80f));
}

void BattleResultScene::DrawClear(float screenWidth, float screenHeight) {
    const float cx = screenWidth * 0.5f;
    DrawImage(clearTitle_, cx - clearTitle_.width * 0.5f, screenHeight * 0.12f);
    DrawImage(clearTimeLabel_, cx - clearTimeLabel_.width * 0.82f * 0.5f,
              screenHeight * 0.32f, 0.82f);
    DrawTextLine(FormatTime(clearTime_), cx, screenHeight * 0.40f, 1.15f);

    if (newRecord_) {
        DrawImage(newRecordLabel_, cx - newRecordLabel_.width * 0.80f * 0.5f,
                  screenHeight * 0.505f, 0.80f,
                  0.74f + 0.26f * SmoothStep(0.5f + 0.5f *
                                             std::sin(sceneTime_ * 7.0f)));
    }

    DrawImage(rankingLabel_, screenWidth * 0.26f, screenHeight * 0.60f, 0.72f);
    const float rowY = screenHeight * 0.66f;
    for (int i = 0; i < kMaxRanking; ++i) {
        const float y = rowY + static_cast<float>(i) * 42.0f;
        const float alpha =
            i == newRecordIndex_ ? 0.75f + 0.25f * std::sin(sceneTime_ * 8.0f)
                                 : 1.0f;
        DrawImage(rankImages_[static_cast<size_t>(i)], screenWidth * 0.38f, y,
                  0.70f, alpha);
        if (i < static_cast<int>(ranking_.size())) {
            DrawTextLine(FormatTime(ranking_[static_cast<size_t>(i)]),
                         screenWidth * 0.60f, y - 2.0f, 0.72f, alpha);
        } else {
            DrawTextLine("--:--.--s", screenWidth * 0.60f, y - 2.0f, 0.72f,
                         0.45f);
        }
    }

    DrawImage(retryLabel_, screenWidth * 0.30f, screenHeight * 0.90f, 0.78f);
    DrawImage(menuLabel_, screenWidth * 0.58f, screenHeight * 0.90f, 0.78f);
}

void BattleResultScene::DrawGameOver(float screenWidth, float screenHeight) {
    const float cx = screenWidth * 0.5f;
    DrawImage(gameOverTitle_, cx - gameOverTitle_.width * 0.5f,
              screenHeight * 0.22f);
    DrawImage(noClearTimeLabel_,
              cx - noClearTimeLabel_.width * 0.86f * 0.5f,
              screenHeight * 0.47f, 0.86f);
    DrawImage(retryLabel_, screenWidth * 0.30f, screenHeight * 0.70f, 0.88f);
    DrawImage(menuLabel_, screenWidth * 0.58f, screenHeight * 0.70f, 0.88f);
}

void BattleResultScene::DrawImage(const Image &image, float x, float y,
                                  float scale, float alpha) {
    Sprite sprite{};
    sprite.textureId = image.textureId;
    sprite.position = {x, y};
    sprite.size = {image.width * scale, image.height * scale};
    sprite.color = {1.0f, 1.0f, 1.0f, alpha};
    ctx_->sprite->DrawSprite(sprite);
}

void BattleResultScene::DrawRect(float x, float y, float w, float h,
                                 const XMFLOAT4 &color) {
    Sprite sprite{};
    sprite.textureId = 0;
    sprite.position = {x, y};
    sprite.size = {w, h};
    sprite.color = color;
    ctx_->sprite->DrawSprite(sprite);
}

void BattleResultScene::DrawTextLine(const std::string &text, float centerX,
                                     float y, float scale, float alpha) {
    float x = centerX - MeasureTextLine(text, scale) * 0.5f;
    for (char c : text) {
        if (c == ' ') {
            x += 18.0f * scale;
            continue;
        }
        const Image *image = FindCharImage(c);
        if (image == nullptr) {
            continue;
        }
        DrawImage(*image, x, y, scale, alpha);
        x += image->width * scale - 4.0f * scale;
    }
}

float BattleResultScene::MeasureTextLine(const std::string &text,
                                         float scale) const {
    float width = 0.0f;
    for (char c : text) {
        if (c == ' ') {
            width += 18.0f * scale;
            continue;
        }
        const Image *image = FindCharImage(c);
        if (image != nullptr) {
            width += image->width * scale - 4.0f * scale;
        }
    }
    return (std::max)(0.0f, width);
}

const BattleResultScene::Image *BattleResultScene::FindCharImage(char c) const {
    if (c >= '0' && c <= '9') {
        return &digitImages_[static_cast<size_t>(c - '0')];
    }
    if (c == ':') {
        return &colonImage_;
    }
    if (c == '.') {
        return &dotImage_;
    }
    if (c == '-') {
        return &dashImage_;
    }
    if (c == 's' || c == 'S') {
        return &secondImage_;
    }
    return nullptr;
}

std::string BattleResultScene::FormatTime(float seconds) const {
    const int centiseconds =
        static_cast<int>(std::round((std::max)(0.0f, seconds) * 100.0f));
    const int minutes = centiseconds / 6000;
    const int sec = (centiseconds / 100) % 60;
    const int centi = centiseconds % 100;
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << minutes << ':'
        << std::setw(2) << sec << '.' << std::setw(2) << centi << 's';
    return oss.str();
}
