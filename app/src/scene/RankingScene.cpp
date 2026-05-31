#include "RankingScene.h"
#include "AssetManager.h"
#include "AppSceneServices.h"
#include "Input.h"
#include "SceneManager.h"
#include "Sprite.h"
#include "SpriteManager.h"
#include "TextureManager.h"
#include "TutorialSelectScene.h"
#include "WeaponSelectScene.h"
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
constexpr float kIntroDuration = 1.10f;
constexpr float kBackgroundRevealDuration = 0.84f;
constexpr float kContentFadeDelay = 0.48f;
constexpr float kContentFadeDuration = 0.50f;
constexpr float kTransitionDuration = 0.16f;
constexpr size_t kRankingDrawCount = 5;

XMFLOAT4 Color(float r, float g, float b, float a = 1.0f) {
    return {r, g, b, a};
}

float SmoothStep(float t) { return t * t * (3.0f - 2.0f * t); }

float Smooth01(float t) {
    return SmoothStep(std::clamp(t, 0.0f, 1.0f));
}

std::filesystem::path RankingPath() {
    return AssetManager::GetAssetRoot() / L"save" / L"ranking.tsv";
}

InputControlType RankingModeFromToken(const std::string &token) {
    return token == "hand" ? InputControlType::Hand
                           : InputControlType::KeyboardMouse;
}
} // namespace

RankingScene::RankingScene(ReturnTarget returnTarget)
    : returnTarget_(returnTarget) {}

void RankingScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    AppSceneServices::RequestHandTrackingStop();
    AppSceneServices::StartMenuBgm(ctx);
    introTimer_ = 0.0f;
    transitionTimer_ = 0.0f;
    returnRequested_ = false;

    backgroundScene_ =
        std::make_unique<GameScene>(GameScene::Mode::BackgroundOnly);
    backgroundScene_->Initialize(ctx);

    rankingTitleLabel_ =
        LoadTextureImage(L"app/resources/ui/result/text/ranking_title.png");
    rankHeaderLabel_ =
        LoadTextureImage(L"app/resources/ui/result/text/ranking_rank.png");
    scoreHeaderLabel_ =
        LoadTextureImage(L"app/resources/ui/result/text/ranking_score.png");
    timeHeaderLabel_ =
        LoadTextureImage(L"app/resources/ui/result/text/ranking_time.png");
    difficultyHeaderLabel_ =
        LoadTextureImage(L"app/resources/ui/result/text/ranking_difficulty.png");
    modeKbmLabel_ =
        LoadTextureImage(L"app/resources/ui/weapon_select/text/input_kbm.png");
    modeHandLabel_ =
        LoadTextureImage(L"app/resources/ui/weapon_select/text/input_hand.png");
    for (int i = 0; i < 10; ++i) {
        digitImages_[static_cast<size_t>(i)] =
            LoadTextureImage(L"app/resources/ui/result/mplus/glyphs/char_" +
                             std::to_wstring(i) + L".png");
    }
    colonImage_ =
        LoadTextureImage(L"app/resources/ui/result/mplus/glyphs/char_colon.png");
    dotImage_ =
        LoadTextureImage(L"app/resources/ui/result/mplus/glyphs/char_dot.png");
    dashImage_ =
        LoadTextureImage(L"app/resources/ui/result/mplus/glyphs/char_dash.png");
    secondImage_ =
        LoadTextureImage(L"app/resources/ui/result/mplus/glyphs/char_s.png");

    LoadRanking();
}

void RankingScene::Update() {
    introTimer_ = (std::min)(introTimer_ + ctx_->frame.deltaTime,
                             kIntroDuration + 0.2f);
    if (backgroundScene_) {
        backgroundScene_->Update();
    }

    if (returnRequested_) {
        transitionTimer_ += ctx_->frame.deltaTime;
        if (transitionTimer_ >= kTransitionDuration) {
            sceneManager_->ChangeScene(CreateReturnScene());
        }
        return;
    }

    Input *input = ctx_->systems.input;
    if (input != nullptr) {
        const bool gamepad = input->IsGamepadConnected();
        if (input->IsKeyTrigger(DIK_A) || input->IsKeyTrigger(DIK_LEFT) ||
            (gamepad &&
             input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_DPAD_LEFT))) {
            ChangeControlType(InputControlType::KeyboardMouse);
        }
        if (input->IsKeyTrigger(DIK_D) || input->IsKeyTrigger(DIK_RIGHT) ||
            (gamepad &&
             input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_DPAD_RIGHT))) {
            ChangeControlType(InputControlType::Hand);
        }
    }

    if (input != nullptr &&
        (input->IsKeyTrigger(DIK_ESCAPE) || input->IsKeyTrigger(DIK_SPACE))) {
        AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Cancel);
        BeginReturn();
    }
}

void RankingScene::Draw() {
    const float screenWidth =
        static_cast<float>(ctx_->systems.winApp->GetWidth());
    const float screenHeight =
        static_cast<float>(ctx_->systems.winApp->GetHeight());

    if (backgroundScene_) {
        backgroundScene_->Draw();
    }

    ctx_->rendering.sprite->PreDraw();
    DrawOverlay(screenWidth, screenHeight);
    DrawRanking(screenWidth, screenHeight);
    DrawTransition(screenWidth, screenHeight);
    ctx_->rendering.sprite->PostDraw();
}

RankingScene::Image RankingScene::LoadTextureImage(const std::wstring &path) {
    Image image{};
    image.textureId = ctx_->rendering.texture->Load(path);
    image.width =
        static_cast<float>(ctx_->rendering.texture->GetWidth(image.textureId));
    image.height =
        static_cast<float>(ctx_->rendering.texture->GetHeight(image.textureId));
    return image;
}

void RankingScene::LoadRanking() {
    rankingEntries_.clear();

    std::ifstream file(RankingPath(), std::ios::binary);
    if (!file) {
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream stream(line);
        RankingEntry entry{};
        if (line.empty() || line[0] == '#') {
            continue;
        }

        std::string mode;
        if (stream >> mode >> entry.score >> entry.difficulty >>
            entry.clearTime) {
            entry.controlType = RankingModeFromToken(mode);
        } else {
            stream.clear();
            stream.str(line);
            if (!(stream >> entry.score >> entry.difficulty >>
                  entry.clearTime)) {
                continue;
            }
            entry.controlType = InputControlType::KeyboardMouse;
        }

        if (entry.controlType == selectedControlType_) {
            entry.score = (std::max)(0, entry.score);
            entry.difficulty = std::clamp(entry.difficulty, 0.0f, 9.0f);
            entry.clearTime = (std::max)(0.0f, entry.clearTime);
            rankingEntries_.push_back(entry);
        }
    }

    std::sort(rankingEntries_.begin(), rankingEntries_.end(),
              [](const RankingEntry &a, const RankingEntry &b) {
                  return a.score > b.score;
              });
    if (rankingEntries_.size() > kRankingDrawCount) {
        rankingEntries_.resize(kRankingDrawCount);
    }
}

void RankingScene::BeginReturn() {
    returnRequested_ = true;
    transitionTimer_ = 0.0f;
}

void RankingScene::ChangeControlType(InputControlType controlType) {
    if (selectedControlType_ == controlType) {
        return;
    }
    selectedControlType_ = controlType;
    LoadRanking();
    AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Select);
}

void RankingScene::DrawOverlay(float screenWidth, float screenHeight) {
    const float backgroundReveal =
        Smooth01(introTimer_ / kBackgroundRevealDuration);
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
             Color(0.0f, 0.0f, 0.0f,
                   0.72f + (1.0f - backgroundReveal) * 0.22f));
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight * 0.20f,
             Color(0.0f, 0.0f, 0.0f, 0.32f * backgroundReveal));
    DrawRect(0.0f, screenHeight * 0.80f, screenWidth, screenHeight * 0.20f,
             Color(0.0f, 0.0f, 0.0f, 0.36f * backgroundReveal));
}

void RankingScene::DrawRanking(float screenWidth, float screenHeight) {
    const float intro =
        Smooth01((introTimer_ - kContentFadeDelay) / kContentFadeDuration);
    if (intro <= 0.0f) {
        return;
    }
    const float panelW = std::clamp(screenWidth * 0.55f, 620.0f, 860.0f);
    const float panelH = std::clamp(screenHeight * 0.70f, 440.0f, 620.0f);
    const float x = (screenWidth - panelW) * 0.5f;
    const float y =
        (screenHeight - panelH) * 0.5f + (1.0f - intro) * 30.0f;

    DrawRect(x + 12.0f, y + 14.0f, panelW, panelH,
             Color(0.0f, 0.0f, 0.0f, 0.44f * intro));
    DrawRect(x, y, panelW, panelH,
             Color(0.010f, 0.013f, 0.018f, 0.92f * intro));
    DrawRect(x + panelW * 0.05f, y + panelH * 0.245f, panelW * 0.90f,
             panelH * 0.60f, Color(0.0f, 0.0f, 0.0f, 0.24f * intro));
    DrawFrame(x, y, panelW, panelH, 2.0f,
              Color(0.95f, 0.72f, 0.28f, 0.82f * intro));

    const float titleScale =
        std::clamp((panelW * 0.42f) /
                       (std::max)(rankingTitleLabel_.width, 1.0f),
                   0.48f, 0.78f);
    const float titleW = rankingTitleLabel_.width * titleScale;
    DrawImage(rankingTitleLabel_, x + (panelW - titleW) * 0.5f,
              y + panelH * 0.085f, titleScale, 0.94f * intro);

    const float tabY = y + panelH * 0.185f;
    const float tabW = panelW * 0.22f;
    const float tabH = panelH * 0.045f;
    const float tabGap = panelW * 0.025f;
    const float tabStartX = x + (panelW - tabW * 2.0f - tabGap) * 0.5f;
    const Image *modeLabels[2] = {&modeKbmLabel_, &modeHandLabel_};
    const InputControlType modes[2] = {InputControlType::KeyboardMouse,
                                       InputControlType::Hand};
    for (int i = 0; i < 2; ++i) {
        const bool selected = selectedControlType_ == modes[i];
        const float tabX = tabStartX + static_cast<float>(i) * (tabW + tabGap);
        DrawRect(tabX, tabY, tabW, tabH,
                 selected ? Color(0.18f, 0.13f, 0.055f, 0.88f * intro)
                          : Color(0.040f, 0.046f, 0.058f, 0.72f * intro));
        DrawFrame(tabX, tabY, tabW, tabH, selected ? 2.0f : 1.0f,
                  selected ? Color(1.0f, 0.78f, 0.34f, 0.88f * intro)
                           : Color(0.62f, 0.66f, 0.72f, 0.32f * intro));
        const Image &label = *modeLabels[i];
        const float labelScale =
            (std::min)({(tabW * 0.72f) / (std::max)(label.width, 1.0f),
                        (tabH * 0.56f) / (std::max)(label.height, 1.0f),
                        0.62f});
        const float labelW = label.width * labelScale;
        const float labelH = label.height * labelScale;
        DrawImage(label, tabX + (tabW - labelW) * 0.5f,
                  tabY + (tabH - labelH) * 0.5f, labelScale,
                  (selected ? 0.92f : 0.62f) * intro);
    }

    const float contentLeft = x + panelW * 0.08f;
    const float contentRight = x + panelW * 0.92f;
    const float contentW = contentRight - contentLeft;
    const float columnGap = panelW * 0.035f;
    const float rankColumnW = MeasureTextLine("10:", 1.0f);
    const float scoreColumnW = MeasureTextLine("000000", 1.0f);
    const float timeColumnW = MeasureTextLine("99:59.99s", 1.0f);
    const float difficultyColumnW = MeasureTextLine("9.0", 1.0f);
    const float nominalRowWidth =
        rankColumnW + scoreColumnW + timeColumnW + difficultyColumnW +
        columnGap * 3.0f;
    const float rowScale =
        std::clamp(contentW / (std::max)(nominalRowWidth, 1.0f), 0.42f, 0.62f);
    const float gap = columnGap * rowScale;
    const float scoreRight =
        contentLeft + rankColumnW * rowScale + gap + scoreColumnW * rowScale;
    const float timeRight =
        scoreRight + gap + timeColumnW * rowScale;
    const float difficultyRight =
        timeRight + gap + difficultyColumnW * rowScale;
    const float rankX = contentLeft;
    const float headerY = y + panelH * 0.245f;
    const float rowStartY = y + panelH * 0.335f;
    const float rowGap = panelH * 0.070f;
    const float headerScale = std::clamp(rowScale * 0.86f, 0.34f, 0.50f);
    DrawImage(rankHeaderLabel_, rankX, headerY, headerScale, 0.72f * intro);
    DrawImage(scoreHeaderLabel_,
              scoreRight - scoreHeaderLabel_.width * headerScale, headerY,
              headerScale, 0.72f * intro);
    DrawImage(timeHeaderLabel_, timeRight - timeHeaderLabel_.width * headerScale,
              headerY, headerScale, 0.72f * intro);
    DrawImage(difficultyHeaderLabel_,
              difficultyRight - difficultyHeaderLabel_.width * headerScale,
              headerY, headerScale, 0.72f * intro);
    DrawRect(contentLeft - panelW * 0.020f, y + panelH * 0.305f,
             contentW + panelW * 0.040f, 1.0f,
             Color(0.95f, 0.72f, 0.28f, 0.30f * intro));
    const float tableTop = y + panelH * 0.245f;
    const float tableBottom = rowStartY + rowGap * 4.72f;
    const float lineAlpha = 0.22f * intro;
    DrawRect(contentLeft + rankColumnW * rowScale + gap * 0.50f, tableTop,
             1.0f, tableBottom - tableTop,
             Color(0.95f, 0.72f, 0.28f, lineAlpha));
    DrawRect(scoreRight + gap * 0.50f, tableTop, 1.0f,
             tableBottom - tableTop, Color(0.95f, 0.72f, 0.28f, lineAlpha));
    DrawRect(timeRight + gap * 0.50f, tableTop, 1.0f,
             tableBottom - tableTop, Color(0.95f, 0.72f, 0.28f, lineAlpha));
    if (rankingEntries_.empty()) {
        DrawTextLineLeft("--", x + panelW * 0.45f, rowStartY + rowGap * 2.0f,
                         rowScale, 0.70f * intro);
        return;
    }

    const size_t rows = (std::min)(rankingEntries_.size(), kRankingDrawCount);
    for (size_t i = 0; i < rows; ++i) {
        const RankingEntry &entry = rankingEntries_[i];
        const float rowY = rowStartY + static_cast<float>(i) * rowGap;
        DrawRect(contentLeft - panelW * 0.020f, rowY - rowGap * 0.12f,
                 contentW + panelW * 0.040f, rowGap * 0.84f,
                 Color(1.0f, 1.0f, 1.0f,
                       (i % 2 == 0 ? 0.030f : 0.015f) * intro));
        std::ostringstream row;
        row << (i + 1) << ":";
        DrawTextLineLeft(row.str(), contentLeft, rowY, rowScale,
                         0.96f * intro);
        DrawTextLineRight(FormatScore(entry.score), scoreRight, rowY, rowScale,
                          0.98f * intro);
        DrawTextLineRight(FormatTime(entry.clearTime), timeRight, rowY,
                          rowScale, 1.0f * intro);
        DrawTextLineRight(FormatDifficulty(entry.difficulty), difficultyRight,
                          rowY, rowScale, 0.94f * intro);
    }
}

void RankingScene::DrawTransition(float screenWidth, float screenHeight) {
    const float introFade = 1.0f - Smooth01(introTimer_ / kIntroDuration);
    if (introFade > 0.0f) {
        DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
                 Color(0.0f, 0.0f, 0.0f, introFade));
    }

    if (!returnRequested_) {
        return;
    }
    const float t =
        std::clamp(transitionTimer_ / kTransitionDuration, 0.0f, 1.0f);
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
             Color(0.0f, 0.0f, 0.0f, SmoothStep(t)));
}

void RankingScene::DrawRect(float x, float y, float w, float h,
                            const XMFLOAT4 &color) {
    Sprite sprite{};
    sprite.position = {x, y};
    sprite.size = {w, h};
    sprite.color = color;
    sprite.textureId = 0;
    ctx_->rendering.sprite->DrawSprite(sprite);
}

void RankingScene::DrawFrame(float x, float y, float w, float h,
                             float thickness, const XMFLOAT4 &color) {
    DrawRect(x, y, w, thickness, color);
    DrawRect(x, y + h - thickness, w, thickness, color);
    DrawRect(x, y, thickness, h, color);
    DrawRect(x + w - thickness, y, thickness, h, color);
}

void RankingScene::DrawImage(const Image &image, float x, float y, float scale,
                             float alpha) {
    if (image.width <= 0.0f || image.height <= 0.0f) {
        return;
    }

    Sprite sprite{};
    sprite.position = {x, y};
    sprite.size = {image.width * scale, image.height * scale};
    sprite.color = {1.0f, 1.0f, 1.0f, alpha};
    sprite.textureId = image.textureId;
    ctx_->rendering.sprite->DrawSprite(sprite);
}

void RankingScene::DrawTextLineLeft(const std::string &text, float x, float y,
                                    float scale, float alpha) {
    float cursorX = x;
    for (char c : text) {
        if (c == ' ') {
            cursorX += 18.0f * scale;
            continue;
        }

        const Image *image = FindCharImage(c);
        if (image == nullptr || image->width <= 0.0f) {
            cursorX += 18.0f * scale;
            continue;
        }

        DrawImage(*image, cursorX, y, scale, alpha);
        cursorX += (image->width + 5.0f) * scale;
    }
}

void RankingScene::DrawTextLineRight(const std::string &text, float rightX,
                                     float y, float scale, float alpha) {
    DrawTextLineLeft(text, rightX - MeasureTextLine(text, scale), y, scale,
                     alpha);
}

float RankingScene::MeasureTextLine(const std::string &text,
                                    float scale) const {
    float width = 0.0f;
    for (char c : text) {
        if (c == ' ') {
            width += 18.0f * scale;
            continue;
        }
        const Image *image = FindCharImage(c);
        width += ((image != nullptr) ? image->width : 18.0f) * scale;
        width += 5.0f * scale;
    }
    return width;
}

const RankingScene::Image *RankingScene::FindCharImage(char c) const {
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

std::string RankingScene::FormatTime(float seconds) const {
    const int centiseconds =
        static_cast<int>(std::round((std::max)(0.0f, seconds) * 100.0f));
    const int minutes = centiseconds / 6000;
    const int sec = (centiseconds / 100) % 60;
    const int centi = centiseconds % 100;

    std::ostringstream stream;
    stream << std::setfill('0') << std::setw(2) << minutes << ':'
           << std::setw(2) << sec << '.' << std::setw(2) << centi << 's';
    return stream.str();
}

std::string RankingScene::FormatDifficulty(float difficulty) const {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(1)
           << std::clamp(difficulty, 0.0f, 9.0f);
    return stream.str();
}

std::string RankingScene::FormatScore(int score) const {
    std::ostringstream stream;
    stream << std::setw(6) << std::setfill('0') << (std::max)(score, 0);
    return stream.str();
}

std::unique_ptr<BaseScene> RankingScene::CreateReturnScene() const {
    switch (returnTarget_) {
    case ReturnTarget::TutorialSelect:
        return std::make_unique<TutorialSelectScene>();
    case ReturnTarget::WeaponSelect:
    default:
        return std::make_unique<WeaponSelectScene>();
    }
}
