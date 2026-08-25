#include "HandLoadingScene.h"
#include "AppSceneServices.h"
#include "DifficultyCauldronScene.h"
#include "GameScene.h"
#include "PostEffectManager.h"
#include "SceneManager.h"
#include "Sprite.h"
#include "SpriteManager.h"
#include "TextureManager.h"
#include "WeaponSelectScene.h"
#include "WinApp.h"
#include <algorithm>
#include <cstring>
#include <memory>

using namespace DirectX;

namespace {
constexpr uint16_t kPreviewPort = 5006;
constexpr float kPreviewStaleSeconds = 0.75f;
constexpr float kCalibrationHoldSeconds = 3.0f;
constexpr float kStillMotionSpeed = 0.16f;
constexpr float kPreviewDimAlpha = 0.18f;
constexpr float kCameraStartupTimeoutSeconds = 8.0f;

XMFLOAT4 Color(float r, float g, float b, float a = 1.0f) {
    return {r, g, b, a};
}

float SmoothStep01(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

bool UsesPreviewLoadingOnly(GameScene::Mode mode) {
    return mode == GameScene::Mode::Tutorial;
}

bool AreBothHandsReady(const SwordUdpController::DebugHandState &left,
                       const SwordUdpController::DebugHandState &right) {
    return left.fresh && right.fresh && left.active && right.active;
}

bool IsCalibrationStill(bool previewReady, bool handsReady, float maxSpeed) {
    return previewReady && handsReady && maxSpeed <= kStillMotionSpeed;
}

XMFLOAT4 CalibrationGuideColor(bool still, float pulse) {
    return still ? Color(0.25f, 1.0f, 0.58f, 0.92f)
                 : Color(1.0f, 0.78f, 0.30f, 0.86f + 0.10f * pulse);
}

const std::array<const char *, 7> &BlockGlyphRows(char glyph) {
    static const auto glyphs = [] {
        std::array<std::array<const char *, 7>, 128> rows{};
        rows['A'] = {"01110", "10001", "10001", "11111", "10001", "10001", "10001"};
        rows['B'] = {"11110", "10001", "10001", "11110", "10001", "10001", "11110"};
        rows['C'] = {"01111", "10000", "10000", "10000", "10000", "10000", "01111"};
        rows['D'] = {"11110", "10001", "10001", "10001", "10001", "10001", "11110"};
        rows['E'] = {"11111", "10000", "10000", "11110", "10000", "10000", "11111"};
        rows['F'] = {"11111", "10000", "10000", "11110", "10000", "10000", "10000"};
        rows['G'] = {"01110", "10001", "10000", "10111", "10001", "10001", "01110"};
        rows['H'] = {"10001", "10001", "10001", "11111", "10001", "10001", "10001"};
        rows['I'] = {"11111", "00100", "00100", "00100", "00100", "00100", "11111"};
        rows['L'] = {"10000", "10000", "10000", "10000", "10000", "10000", "11111"};
        rows['M'] = {"10001", "11011", "10101", "10101", "10001", "10001", "10001"};
        rows['N'] = {"10001", "11001", "10101", "10011", "10001", "10001", "10001"};
        rows['O'] = {"01110", "10001", "10001", "10001", "10001", "10001", "01110"};
        rows['R'] = {"11110", "10001", "10001", "11110", "10100", "10010", "10001"};
        rows['S'] = {"01111", "10000", "10000", "01110", "00001", "00001", "11110"};
        rows['T'] = {"11111", "00100", "00100", "00100", "00100", "00100", "00100"};
        rows['W'] = {"10001", "10001", "10001", "10101", "10101", "10101", "01010"};
        return rows;
    }();
    const size_t index = static_cast<unsigned char>(glyph);
    return index < glyphs.size() ? glyphs[index] : glyphs[0];
}
} // namespace

HandLoadingScene::HandLoadingScene(
    const SwordInputCalibration &inputCalibration, float difficulty)
    : inputCalibration_(inputCalibration), difficulty_(difficulty) {}

HandLoadingScene::HandLoadingScene(
    const SwordInputCalibration &inputCalibration,
    GameScene::Mode destinationMode)
    : inputCalibration_(inputCalibration), destinationMode_(destinationMode) {}

HandLoadingScene::HandLoadingScene(const SwordInputCalibration &inputCalibration)
    : inputCalibration_(inputCalibration), destinationDifficultySelect_(true) {}

HandLoadingScene::HandLoadingScene(
    CameraAccuracyDebugScene::ReturnTarget returnTarget)
    : sensitivityReturnTarget_(returnTarget),
      destinationSensitivityAdjust_(true) {}

void HandLoadingScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    gameLogoImage_ = LoadTextureImage(L"app/resources/ui/title/gamelogo.png");
    faceCameraMessageImage_ = LoadTextureImage(
        L"app/resources/ui/hand_camera_confirm/face_camera_message.png");
    handTrackingStartRequested_ = false;
    stillTimer_ = 0.0f;
    sceneTimer_ = 0.0f;
    neutralSum_ = {XMFLOAT2{0.0f, 0.0f}, XMFLOAT2{0.0f, 0.0f}};
    restSpeedSum_ = {0.0f, 0.0f};
    neutralSampleCount_ = 0;
    inputCalibration_.controlType = InputControlType::Hand;
    inputCalibration_.hasHandNeutral = false;
    inputCalibration_.hasHandRestSpeed = false;
    handController_.SetCalibration(inputCalibration_);
    RequestHandTrackingStartOnce();
    previewReceiver_.Initialize(ctx_->rendering.texture, kPreviewPort);

    if (ctx_->rendering.postEffectManager != nullptr) {
        ctx_->rendering.postEffectManager->SetBaseProfile(PostProcessProfile{});
    }
}

void HandLoadingScene::Update() {
    RequestHandTrackingStartOnce();
    const float deltaTime = ctx_->frame.deltaTime;
    sceneTimer_ += deltaTime;
    previewReceiver_.Update(deltaTime);
    handController_.Update(deltaTime);

    const auto left = handController_.GetDebugHandState(0);
    const auto right = handController_.GetDebugHandState(1);
    const bool previewReady = previewReceiver_.HasFreshFrame(kPreviewStaleSeconds);
    const bool cameraStartupTimedOut =
        sceneTimer_ >= kCameraStartupTimeoutSeconds && !previewReady;
    if (cameraStartupTimedOut) {
        inputCalibration_.controlType = InputControlType::KeyboardMouse;
        sceneManager_->ChangeScene(std::make_unique<WeaponSelectScene>());
        return;
    }

    if (TryAdvancePreviewDestination(previewReady)) {
        return;
    }

    const bool handsReady = AreBothHandsReady(left, right);
    const float maxSpeed = (std::max)(left.motionSpeed, right.motionSpeed);
    const bool still = IsCalibrationStill(previewReady, handsReady, maxSpeed);

    if (still) {
        stillTimer_ = (std::min)(stillTimer_ + deltaTime,
                                 kCalibrationHoldSeconds);
        neutralSum_[0].x += left.rawPalm.x;
        neutralSum_[0].y += left.rawPalm.y;
        neutralSum_[1].x += right.rawPalm.x;
        neutralSum_[1].y += right.rawPalm.y;
        restSpeedSum_[0] += left.motionSpeed;
        restSpeedSum_[1] += right.motionSpeed;
        ++neutralSampleCount_;
    } else if (stillTimer_ > 0.0f || neutralSampleCount_ > 0) {
        stillTimer_ = 0.0f;
        neutralSum_ = {XMFLOAT2{0.0f, 0.0f}, XMFLOAT2{0.0f, 0.0f}};
        restSpeedSum_ = {0.0f, 0.0f};
        neutralSampleCount_ = 0;
    }

    if (stillTimer_ >= kCalibrationHoldSeconds && neutralSampleCount_ > 0) {
        const float invSamples = 1.0f / static_cast<float>(neutralSampleCount_);
        for (size_t i = 0; i < inputCalibration_.handNeutral.size(); ++i) {
            inputCalibration_.handNeutral[i] = {
                neutralSum_[i].x * invSamples,
                neutralSum_[i].y * invSamples,
            };
            inputCalibration_.handRestSpeed[i] = restSpeedSum_[i] * invSamples;
        }
        inputCalibration_.hasHandNeutral = true;
        inputCalibration_.hasHandRestSpeed = true;

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

bool HandLoadingScene::TryAdvancePreviewDestination(bool previewReady) {
    if (UsesPreviewLoadingOnly(destinationMode_)) {
        if (previewReady) {
            inputCalibration_.controlType = InputControlType::Hand;
            sceneManager_->ChangeScene(
                std::make_unique<GameScene>(inputCalibration_, destinationMode_));
        }
        return true;
    }
    if (destinationSensitivityAdjust_) {
        if (previewReady) {
            sceneManager_->ChangeScene(std::make_unique<CameraAccuracyDebugScene>(
                sensitivityReturnTarget_));
        }
        return true;
    }
    if (destinationDifficultySelect_) {
        if (previewReady) {
            inputCalibration_.controlType = InputControlType::Hand;
            sceneManager_->ChangeScene(
                std::make_unique<DifficultyCauldronScene>(inputCalibration_));
        }
        return true;
    }
    return false;
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
    previewReceiver_.DrawArea(ctx_->rendering.sprite, ctx_->rendering.texture,
                              0.0f, 0.0f, screenWidth, screenHeight,
                              kPreviewStaleSeconds, 1.0f);
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
             Color(0.0f, 0.0f, 0.0f, kPreviewDimAlpha));
    if (destinationDifficultySelect_ || destinationSensitivityAdjust_ ||
        UsesPreviewLoadingOnly(destinationMode_)) {
        DrawLoadingMark(screenWidth, screenHeight);
    } else {
        DrawCalibrationOverlay(screenWidth, screenHeight);
    }
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

HandLoadingScene::Image
HandLoadingScene::LoadTextureImage(const std::wstring &path) {
    Image image{};
    if (ctx_ == nullptr || ctx_->rendering.texture == nullptr) {
        return image;
    }
    image.textureId = ctx_->rendering.texture->Load(path);
    image.width =
        static_cast<float>(ctx_->rendering.texture->GetWidth(image.textureId));
    image.height =
        static_cast<float>(ctx_->rendering.texture->GetHeight(image.textureId));
    return image;
}

void HandLoadingScene::DrawLoadingMark(float screenWidth, float screenHeight) {
    const float radius = std::clamp(screenHeight * 0.082f, 48.0f, 72.0f);
    const float padding = std::clamp(screenWidth * 0.036f, 36.0f, 64.0f);
    const float centerX = screenWidth - padding - radius;
    const float centerY = screenHeight - padding - radius;
    const float messageW = std::clamp(screenWidth * 0.46f, 420.0f, 760.0f);
    DrawImageCentered(faceCameraMessageImage_, screenWidth * 0.5f,
                      screenHeight * 0.5f, messageW, screenHeight * 0.12f,
                      Color(1.0f, 1.0f, 1.0f, 0.96f));
    DrawLoadingRing(centerX, centerY, radius);
    DrawImageCentered(gameLogoImage_, centerX, centerY, radius * 1.30f,
                      radius * 0.70f, Color(1.0f, 1.0f, 1.0f, 0.94f));
}

void HandLoadingScene::DrawLoadingRing(float centerX, float centerY,
                                       float radius) {
    constexpr int kDotCount = 28;
    const float phase = sceneTimer_ * 4.2f;
    for (int i = 0; i < kDotCount; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kDotCount);
        const float angle = t * 6.28318530718f + phase;
        const float alpha = 0.18f + 0.78f * std::pow(t, 1.45f);
        const float dotSize = radius * (0.055f + 0.040f * t);
        const float x = centerX + std::cos(angle) * radius;
        const float y = centerY + std::sin(angle) * radius;
        DrawRect(x - dotSize * 0.5f, y - dotSize * 0.5f, dotSize, dotSize,
                 Color(1.0f, 1.0f, 1.0f, alpha));
    }
}

void HandLoadingScene::DrawImageCentered(const Image &image, float centerX,
                                         float centerY, float maxWidth,
                                         float maxHeight,
                                         const XMFLOAT4 &color) {
    if (image.textureId == 0 || image.width <= 0.0f || image.height <= 0.0f) {
        return;
    }
    const float scale =
        (std::min)(maxWidth / image.width, maxHeight / image.height);
    Sprite sprite{};
    sprite.textureId = image.textureId;
    sprite.position = {centerX - image.width * scale * 0.5f,
                       centerY - image.height * scale * 0.5f};
    sprite.size = {image.width * scale, image.height * scale};
    sprite.color = color;
    ctx_->rendering.sprite->DrawSprite(sprite);
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

void HandLoadingScene::DrawCalibrationOverlay(float screenWidth,
                                              float screenHeight) {
    const auto left = handController_.GetDebugHandState(0);
    const auto right = handController_.GetDebugHandState(1);
    const bool previewReady = previewReceiver_.HasFreshFrame(kPreviewStaleSeconds);
    const bool handsReady = left.fresh && right.fresh && left.active && right.active;
    const float maxSpeed = (std::max)(left.motionSpeed, right.motionSpeed);
    const bool still = previewReady && handsReady && maxSpeed <= kStillMotionSpeed;
    const float progress =
        std::clamp(stillTimer_ / kCalibrationHoldSeconds, 0.0f, 1.0f);
    const float pulse = 0.5f + 0.5f * std::sinf(sceneTimer_ * 8.0f);
    const XMFLOAT4 guideColor = CalibrationGuideColor(still, pulse);

    DrawFrame(18.0f, 18.0f, screenWidth - 36.0f, screenHeight - 36.0f, 4.0f,
              Color(0.92f, 0.96f, 1.0f, previewReady ? 0.56f : 0.26f));
    DrawHandGuide(left, screenWidth, screenHeight,
                  handsReady ? guideColor : Color(0.95f, 0.95f, 0.95f, 0.34f));
    DrawHandGuide(right, screenWidth, screenHeight,
                  handsReady ? guideColor : Color(0.95f, 0.95f, 0.95f, 0.34f));

    const float titleScale = std::clamp(screenWidth / 1500.0f, 0.72f, 1.05f);
    const char *title = still ? "HOLD STILL" : "FACE CAMERA";
    const float titleWidth = MeasureBlockText(title, titleScale);
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight * 0.18f,
             Color(0.0f, 0.0f, 0.0f, 0.45f));
    DrawBlockText(title, (screenWidth - titleWidth) * 0.5f,
                  screenHeight * 0.055f, titleScale,
                  still ? Color(0.80f, 1.0f, 0.86f, 0.96f)
                        : Color(1.0f, 0.92f, 0.72f, 0.96f));

    const float meterW = std::clamp(screenWidth * 0.42f, 360.0f, 680.0f);
    const float meterH = 18.0f;
    const float meterX = (screenWidth - meterW) * 0.5f;
    const float meterY = screenHeight * 0.83f;
    DrawRect(meterX, meterY, meterW, meterH, Color(0.0f, 0.0f, 0.0f, 0.58f));
    DrawFrame(meterX, meterY, meterW, meterH, 2.0f,
              Color(1.0f, 1.0f, 1.0f, 0.38f));
    DrawRect(meterX + 3.0f, meterY + 3.0f,
             (meterW - 6.0f) * SmoothStep01(progress), meterH - 6.0f,
             still ? Color(0.25f, 1.0f, 0.58f, 0.84f)
                   : Color(1.0f, 0.62f, 0.18f, 0.42f));

    const int countValue =
        still ? (std::max)(1, static_cast<int>(
                                  std::ceil(kCalibrationHoldSeconds - stillTimer_)))
              : 3;
    DrawCountdownNumber(countValue, screenWidth * 0.5f, screenHeight * 0.52f,
                        std::clamp(screenHeight / 760.0f, 0.78f, 1.20f),
                        still ? Color(0.82f, 1.0f, 0.88f, 0.92f)
                              : Color(1.0f, 0.92f, 0.72f, 0.36f));

    if (!handsReady) {
        const char *hint = "HOLD BOTH HANDS";
        const float hintScale = std::clamp(screenWidth / 1900.0f, 0.52f, 0.76f);
        const float hintWidth = MeasureBlockText(hint, hintScale);
        DrawBlockText(hint, (screenWidth - hintWidth) * 0.5f,
                      screenHeight * 0.73f, hintScale,
                      Color(1.0f, 0.84f, 0.46f, 0.86f));
    }
}

void HandLoadingScene::DrawHandGuide(
    const SwordUdpController::DebugHandState &hand, float screenWidth,
    float screenHeight, const XMFLOAT4 &color) {
    if (!hand.fresh || !hand.active) {
        return;
    }

    const float x = hand.rawPalm.x * screenWidth;
    const float y = hand.rawPalm.y * screenHeight;
    const float boxSize =
        std::clamp(hand.visualScale * screenHeight * 2.25f, 74.0f, 190.0f);
    const float boxX = x - boxSize * 0.5f;
    const float boxY = y - boxSize * 0.5f;
    DrawFrame(boxX, boxY, boxSize, boxSize, 4.0f, color);
    DrawRect(x - boxSize * 0.62f, y - 2.0f, boxSize * 1.24f, 4.0f, color);
    DrawRect(x - 2.0f, y - boxSize * 0.62f, 4.0f, boxSize * 1.24f, color);

    const float corner = boxSize * 0.28f;
    DrawRect(boxX - 7.0f, boxY - 7.0f, corner, 5.0f, color);
    DrawRect(boxX - 7.0f, boxY - 7.0f, 5.0f, corner, color);
    DrawRect(boxX + boxSize - corner + 7.0f, boxY - 7.0f, corner, 5.0f,
             color);
    DrawRect(boxX + boxSize + 2.0f, boxY - 7.0f, 5.0f, corner, color);
    DrawRect(boxX - 7.0f, boxY + boxSize + 2.0f, corner, 5.0f, color);
    DrawRect(boxX - 7.0f, boxY + boxSize - corner + 7.0f, 5.0f, corner,
             color);
    DrawRect(boxX + boxSize - corner + 7.0f, boxY + boxSize + 2.0f, corner,
             5.0f, color);
    DrawRect(boxX + boxSize + 2.0f, boxY + boxSize - corner + 7.0f, 5.0f,
             corner, color);
}

void HandLoadingScene::DrawFrame(float x, float y, float w, float h,
                                 float thickness, const XMFLOAT4 &color) {
    DrawRect(x, y, w, thickness, color);
    DrawRect(x, y + h - thickness, w, thickness, color);
    DrawRect(x, y, thickness, h, color);
    DrawRect(x + w - thickness, y, thickness, h, color);
}

void HandLoadingScene::DrawCountdownNumber(int value, float centerX,
                                           float centerY, float scale,
                                           const XMFLOAT4 &color) {
    const float digitW = 74.0f * scale;
    const float digitH = 124.0f * scale;
    DrawSevenSegmentDigit(value, centerX - digitW * 0.5f,
                          centerY - digitH * 0.5f, scale, color);
}

void HandLoadingScene::DrawSevenSegmentDigit(int value, float x, float y,
                                             float scale,
                                             const XMFLOAT4 &color) {
    const bool segments[10][7] = {
        {true, true, true, true, true, true, false},
        {false, true, true, false, false, false, false},
        {true, true, false, true, true, false, true},
        {true, true, true, true, false, false, true},
        {false, true, true, false, false, true, true},
        {true, false, true, true, false, true, true},
        {true, false, true, true, true, true, true},
        {true, true, true, false, false, false, false},
        {true, true, true, true, true, true, true},
        {true, true, true, true, false, true, true},
    };
    value = std::clamp(value, 0, 9);
    const float t = 10.0f * scale;
    const float w = 74.0f * scale;
    const float h = 124.0f * scale;
    const float midY = y + h * 0.5f - t * 0.5f;
    if (segments[value][0]) {
        DrawRect(x + t, y, w - t * 2.0f, t, color);
    }
    if (segments[value][1]) {
        DrawRect(x + w - t, y + t, t, h * 0.5f - t, color);
    }
    if (segments[value][2]) {
        DrawRect(x + w - t, midY + t, t, h * 0.5f - t, color);
    }
    if (segments[value][3]) {
        DrawRect(x + t, y + h - t, w - t * 2.0f, t, color);
    }
    if (segments[value][4]) {
        DrawRect(x, midY + t, t, h * 0.5f - t, color);
    }
    if (segments[value][5]) {
        DrawRect(x, y + t, t, h * 0.5f - t, color);
    }
    if (segments[value][6]) {
        DrawRect(x + t, midY, w - t * 2.0f, t, color);
    }
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
    const auto &rows = BlockGlyphRows(glyph);
    if (rows[0] == nullptr) {
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
