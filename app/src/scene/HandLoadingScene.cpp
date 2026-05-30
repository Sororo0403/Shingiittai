#include "HandLoadingScene.h"
#include "AppSceneServices.h"
#include "GameScene.h"
#include "PostEffectManager.h"
#include "SceneManager.h"
#include "Sprite.h"
#include "SpriteManager.h"
#include "WinApp.h"
#include <algorithm>
#include <cstring>
#include <memory>

using namespace DirectX;

namespace {
constexpr uint16_t kPreviewPort = 5006;
constexpr float kPreviewStaleSeconds = 0.75f;

XMFLOAT4 Color(float r, float g, float b, float a = 1.0f) {
    return {r, g, b, a};
}
} // namespace

HandLoadingScene::HandLoadingScene(
    const SwordInputCalibration &inputCalibration, float difficulty)
    : inputCalibration_(inputCalibration), difficulty_(difficulty) {}

HandLoadingScene::HandLoadingScene(
    const SwordInputCalibration &inputCalibration,
    GameScene::Mode destinationMode)
    : inputCalibration_(inputCalibration), destinationMode_(destinationMode) {}

void HandLoadingScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    handTrackingStartRequested_ = false;
    RequestHandTrackingStartOnce();
    previewReceiver_.Initialize(ctx_->rendering.texture, kPreviewPort);

    if (ctx_->rendering.postEffectManager != nullptr) {
        ctx_->rendering.postEffectManager->SetBaseProfile(PostProcessProfile{});
    }
}

void HandLoadingScene::Update() {
    RequestHandTrackingStartOnce();
    previewReceiver_.Update(ctx_->frame.deltaTime);
    if (previewReceiver_.HasFreshFrame(kPreviewStaleSeconds)) {
        if (destinationMode_ == GameScene::Mode::Gameplay) {
            sceneManager_->ChangeScene(
                std::make_unique<GameScene>(inputCalibration_, difficulty_));
        } else {
            sceneManager_->ChangeScene(
                std::make_unique<GameScene>(inputCalibration_,
                                            destinationMode_));
        }
    }
}

void HandLoadingScene::Draw() {
    const float screenWidth =
        static_cast<float>(ctx_->systems.winApp->GetWidth());
    const float screenHeight =
        static_cast<float>(ctx_->systems.winApp->GetHeight());

    ctx_->rendering.sprite->PreDraw();
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
             Color(0.0f, 0.0f, 0.0f));
    ctx_->rendering.sprite->PostDraw();
}

void HandLoadingScene::DrawPostProcessOverlay() {
    const float screenWidth =
        static_cast<float>(ctx_->systems.winApp->GetWidth());
    const float screenHeight =
        static_cast<float>(ctx_->systems.winApp->GetHeight());

    ctx_->rendering.sprite->PreDraw(true);
    DrawFacingInstruction(screenWidth, screenHeight);
    DrawLoadingText(screenWidth, screenHeight);
    ctx_->rendering.sprite->PostDraw();
}

bool HandLoadingScene::RequestHandTrackingStartOnce() {
    if (handTrackingStartRequested_ ||
        !AppSceneServices::HasHandTrackingStart()) {
        return handTrackingStartRequested_;
    }

    handTrackingStartRequested_ = AppSceneServices::RequestHandTrackingStart();
    return handTrackingStartRequested_;
}

bool HandLoadingScene::IsHandTrackingReady() const {
    return !AppSceneServices::HasHandTrackingReady() ||
           AppSceneServices::IsHandTrackingReady();
}

void HandLoadingScene::DrawRect(float x, float y, float w, float h,
                                const XMFLOAT4 &color) {
    Sprite sprite{};
    sprite.position = {x, y};
    sprite.size = {w, h};
    sprite.color = color;
    sprite.textureId = 0;
    ctx_->rendering.sprite->DrawSprite(sprite);
}

void HandLoadingScene::DrawLoadingText(float screenWidth, float screenHeight) {
    constexpr const char *kText = "NOWLOADING";
    const float scale = std::clamp(screenWidth / 1700.0f, 0.68f, 0.92f);
    const float padding = std::clamp(screenWidth * 0.030f, 28.0f, 52.0f);
    const float textWidth = MeasureBlockText(kText, scale);
    const float textHeight = 41.0f * scale;
    DrawBlockText(kText, screenWidth - padding - textWidth,
                  screenHeight - padding - textHeight, scale,
                  Color(1.0f, 1.0f, 1.0f, 0.92f));
}

void HandLoadingScene::DrawFacingInstruction(float screenWidth,
                                             float screenHeight) {
    constexpr const char *kText = "FACE CAMERA";
    const float scale = std::clamp(screenWidth / 1250.0f, 0.78f, 1.25f);
    const float textWidth = MeasureBlockText(kText, scale);
    DrawBlockText(kText, (screenWidth - textWidth) * 0.5f,
                  screenHeight * 0.42f, scale,
                  Color(1.0f, 1.0f, 1.0f, 0.96f));
}

float HandLoadingScene::MeasureBlockText(const char *text, float scale) const {
    if (text == nullptr) {
        return 0.0f;
    }

    constexpr float kGlyphWidth = 29.0f;
    constexpr float kGlyphGap = 8.0f;
    float width = 0.0f;
    const size_t length = std::strlen(text);
    for (size_t i = 0; i < length; ++i) {
        width += kGlyphWidth;
        if (i + 1 < length) {
            width += kGlyphGap;
        }
    }
    return width * scale;
}

void HandLoadingScene::DrawBlockText(const char *text, float x, float y,
                                     float scale, const XMFLOAT4 &color) {
    if (text == nullptr) {
        return;
    }

    constexpr float kGlyphWidth = 29.0f;
    constexpr float kGlyphGap = 8.0f;
    const size_t length = std::strlen(text);
    for (size_t i = 0; i < length; ++i) {
        DrawBlockGlyph(text[i], x, y, scale, color);
        x += (kGlyphWidth + kGlyphGap) * scale;
    }
}

void HandLoadingScene::DrawBlockGlyph(char glyph, float x, float y,
                                      float scale, const XMFLOAT4 &color) {
    const char *rows[7] = {};
    switch (glyph) {
    case 'A':
        rows[0] = "01110"; rows[1] = "10001"; rows[2] = "10001";
        rows[3] = "11111"; rows[4] = "10001"; rows[5] = "10001";
        rows[6] = "10001"; break;
    case 'C':
        rows[0] = "01111"; rows[1] = "10000"; rows[2] = "10000";
        rows[3] = "10000"; rows[4] = "10000"; rows[5] = "10000";
        rows[6] = "01111"; break;
    case 'D':
        rows[0] = "11110"; rows[1] = "10001"; rows[2] = "10001";
        rows[3] = "10001"; rows[4] = "10001"; rows[5] = "10001";
        rows[6] = "11110"; break;
    case 'E':
        rows[0] = "11111"; rows[1] = "10000"; rows[2] = "10000";
        rows[3] = "11110"; rows[4] = "10000"; rows[5] = "10000";
        rows[6] = "11111"; break;
    case 'F':
        rows[0] = "11111"; rows[1] = "10000"; rows[2] = "10000";
        rows[3] = "11110"; rows[4] = "10000"; rows[5] = "10000";
        rows[6] = "10000"; break;
    case 'G':
        rows[0] = "01110"; rows[1] = "10001"; rows[2] = "10000";
        rows[3] = "10111"; rows[4] = "10001"; rows[5] = "10001";
        rows[6] = "01110"; break;
    case 'I':
        rows[0] = "11111"; rows[1] = "00100"; rows[2] = "00100";
        rows[3] = "00100"; rows[4] = "00100"; rows[5] = "00100";
        rows[6] = "11111"; break;
    case 'L':
        rows[0] = "10000"; rows[1] = "10000"; rows[2] = "10000";
        rows[3] = "10000"; rows[4] = "10000"; rows[5] = "10000";
        rows[6] = "11111"; break;
    case 'M':
        rows[0] = "10001"; rows[1] = "11011"; rows[2] = "10101";
        rows[3] = "10101"; rows[4] = "10001"; rows[5] = "10001";
        rows[6] = "10001"; break;
    case 'N':
        rows[0] = "10001"; rows[1] = "11001"; rows[2] = "10101";
        rows[3] = "10011"; rows[4] = "10001"; rows[5] = "10001";
        rows[6] = "10001"; break;
    case 'O':
        rows[0] = "01110"; rows[1] = "10001"; rows[2] = "10001";
        rows[3] = "10001"; rows[4] = "10001"; rows[5] = "10001";
        rows[6] = "01110"; break;
    case 'R':
        rows[0] = "11110"; rows[1] = "10001"; rows[2] = "10001";
        rows[3] = "11110"; rows[4] = "10100"; rows[5] = "10010";
        rows[6] = "10001"; break;
    case 'W':
        rows[0] = "10001"; rows[1] = "10001"; rows[2] = "10001";
        rows[3] = "10101"; rows[4] = "10101"; rows[5] = "10101";
        rows[6] = "01010"; break;
    default:
        return;
    }

    const float cell = 5.0f * scale;
    const float gap = 1.0f * scale;
    for (int row = 0; row < 7; ++row) {
        for (int col = 0; col < 5; ++col) {
            if (rows[row][col] != '1') {
                continue;
            }
            DrawRect(x + static_cast<float>(col) * (cell + gap),
                     y + static_cast<float>(row) * (cell + gap),
                     cell, cell, color);
        }
    }
}
