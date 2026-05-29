#include "CameraAccuracyDebugScene.h"
#include "AppSceneServices.h"
#include "DirectXCommon.h"
#include "Input.h"
#include "ModelDrawEffect.h"
#include "ModelManager.h"
#include "PostEffectManager.h"
#include "SceneManager.h"
#include "Sprite.h"
#include "SpriteManager.h"
#include "TextureManager.h"
#include "TitleScene.h"
#include "WinApp.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <memory>

#ifdef DrawText
#undef DrawText
#endif

using namespace DirectX;

namespace {
constexpr uint16_t kPreviewPort = 5006;
constexpr float kPreviewStaleSeconds = 0.75f;
constexpr float kPi = 3.14159265f;

XMFLOAT4 Color(float r, float g, float b, float a = 1.0f) {
    return {r, g, b, a};
}

const std::array<const char *, 7> &GlyphRows(char c) {
    static const std::array<const char *, 7> blank = {
        "00000", "00000", "00000", "00000", "00000", "00000", "00000"};
    static const std::array<const char *, 7> unknown = {
        "11110", "00010", "00100", "01000", "01000", "00000", "01000"};
    static const auto glyphs = [] {
        std::array<std::array<const char *, 7>, 128> out{};
        for (auto &rows : out) {
            rows = blank;
        }
        auto set = [&](char ch, std::array<const char *, 7> rows) {
            out[static_cast<size_t>(ch)] = rows;
        };
        set('A', {"01110", "10001", "10001", "11111", "10001", "10001", "10001"});
        set('B', {"11110", "10001", "10001", "11110", "10001", "10001", "11110"});
        set('C', {"01111", "10000", "10000", "10000", "10000", "10000", "01111"});
        set('D', {"11110", "10001", "10001", "10001", "10001", "10001", "11110"});
        set('E', {"11111", "10000", "10000", "11110", "10000", "10000", "11111"});
        set('F', {"11111", "10000", "10000", "11110", "10000", "10000", "10000"});
        set('G', {"01111", "10000", "10000", "10011", "10001", "10001", "01110"});
        set('H', {"10001", "10001", "10001", "11111", "10001", "10001", "10001"});
        set('I', {"11111", "00100", "00100", "00100", "00100", "00100", "11111"});
        set('J', {"00111", "00010", "00010", "00010", "00010", "10010", "01100"});
        set('K', {"10001", "10010", "10100", "11000", "10100", "10010", "10001"});
        set('L', {"10000", "10000", "10000", "10000", "10000", "10000", "11111"});
        set('M', {"10001", "11011", "10101", "10101", "10001", "10001", "10001"});
        set('N', {"10001", "11001", "10101", "10011", "10001", "10001", "10001"});
        set('O', {"01110", "10001", "10001", "10001", "10001", "10001", "01110"});
        set('P', {"11110", "10001", "10001", "11110", "10000", "10000", "10000"});
        set('Q', {"01110", "10001", "10001", "10001", "10101", "10010", "01101"});
        set('R', {"11110", "10001", "10001", "11110", "10100", "10010", "10001"});
        set('S', {"01111", "10000", "10000", "01110", "00001", "00001", "11110"});
        set('T', {"11111", "00100", "00100", "00100", "00100", "00100", "00100"});
        set('U', {"10001", "10001", "10001", "10001", "10001", "10001", "01110"});
        set('V', {"10001", "10001", "10001", "10001", "10001", "01010", "00100"});
        set('W', {"10001", "10001", "10001", "10101", "10101", "11011", "10001"});
        set('X', {"10001", "10001", "01010", "00100", "01010", "10001", "10001"});
        set('Y', {"10001", "10001", "01010", "00100", "00100", "00100", "00100"});
        set('Z', {"11111", "00001", "00010", "00100", "01000", "10000", "11111"});
        set('0', {"01110", "10001", "10011", "10101", "11001", "10001", "01110"});
        set('1', {"00100", "01100", "00100", "00100", "00100", "00100", "01110"});
        set('2', {"01110", "10001", "00001", "00010", "00100", "01000", "11111"});
        set('3', {"11110", "00001", "00001", "01110", "00001", "00001", "11110"});
        set('4', {"00010", "00110", "01010", "10010", "11111", "00010", "00010"});
        set('5', {"11111", "10000", "10000", "11110", "00001", "00001", "11110"});
        set('6', {"00111", "01000", "10000", "11110", "10001", "10001", "01110"});
        set('7', {"11111", "00001", "00010", "00100", "01000", "01000", "01000"});
        set('8', {"01110", "10001", "10001", "01110", "10001", "10001", "01110"});
        set('9', {"01110", "10001", "10001", "01111", "00001", "00010", "11100"});
        set('.', {"00000", "00000", "00000", "00000", "00000", "01100", "01100"});
        set(',', {"00000", "00000", "00000", "00000", "01100", "00100", "01000"});
        set(':', {"00000", "01100", "01100", "00000", "01100", "01100", "00000"});
        set('-', {"00000", "00000", "00000", "11111", "00000", "00000", "00000"});
        set('+', {"00000", "00100", "00100", "11111", "00100", "00100", "00000"});
        set('/', {"00001", "00010", "00010", "00100", "01000", "01000", "10000"});
        set('(', {"00010", "00100", "01000", "01000", "01000", "00100", "00010"});
        set(')', {"01000", "00100", "00010", "00010", "00010", "00100", "01000"});
        set('[', {"01110", "01000", "01000", "01000", "01000", "01000", "01110"});
        set(']', {"01110", "00010", "00010", "00010", "00010", "00010", "01110"});
        set('=', {"00000", "11111", "00000", "11111", "00000", "00000", "00000"});
        set(' ', blank);
        return out;
    }();

    const unsigned char index = static_cast<unsigned char>(c);
    if (index >= glyphs.size()) {
        return unknown;
    }
    const auto &rows = glyphs[index];
    bool allBlank = true;
    for (const char *row : rows) {
        allBlank = allBlank && std::strcmp(row, "00000") == 0;
    }
    return allBlank && c != ' ' ? unknown : rows;
}

float Clamp01(float value) { return std::clamp(value, 0.0f, 1.0f); }
} // namespace

CameraAccuracyDebugScene::~CameraAccuracyDebugScene() {
    previewReceiver_.Close();
    AppSceneServices::RequestHandTrackingStop();
}

void CameraAccuracyDebugScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    sceneTime_ = 0.0f;
    handTrackingStartRequested_ = false;
    neutralCapturedThisScene_ = false;

    calibration_ = {};
    calibration_.controlType = InputControlType::Hand;
    controller_.SetCalibration(calibration_);
    RequestHandTrackingStartOnce();
    previewReceiver_.Initialize(ctx_->rendering.texture, kPreviewPort);

    if (ctx_->rendering.postEffectManager != nullptr) {
        ctx_->rendering.postEffectManager->SetBaseProfile(PostProcessProfile{});
    }
    if (ctx_->rendering.dxCommon != nullptr) {
        ctx_->rendering.dxCommon->SetClearColor(0.018f, 0.020f, 0.026f, 1.0f);
    }

    const float aspect = static_cast<float>(ctx_->systems.winApp->GetWidth()) /
                         static_cast<float>(ctx_->systems.winApp->GetHeight());
    camera_.Initialize(aspect);
    camera_.SetPerspectiveFovDeg(48.0f);
    UpdateCamera();

    swordModelId_ =
        ctx_->rendering.model->Load(L"app/resources/models/player/sword.glb");
    playerModelId_ =
        ctx_->rendering.model->Load(L"app/resources/models/player/player.gltf");
    gamePreviewPlayer_.Initialize(playerModelId_, swordModelId_);
    gamePreviewPlayer_.SetInputCalibration(calibration_);
    OpenHandDebugLog();
}

void CameraAccuracyDebugScene::Update() {
    sceneTime_ += ctx_->frame.deltaTime;
    RequestHandTrackingStartOnce();
    controller_.Update(ctx_->frame.deltaTime);
    UpdateGamePreview(ctx_->frame.deltaTime);
    previewReceiver_.Update(ctx_->frame.deltaTime);
    UpdateCamera();

    Input *input = ctx_->systems.input;
    if (input == nullptr) {
        WriteHandDebugLog(ctx_->frame.deltaTime);
        return;
    }
    if (input->IsKeyTrigger(DIK_ESCAPE)) {
        WriteHandDebugLog(ctx_->frame.deltaTime);
        sceneManager_->ChangeScene(std::make_unique<TitleScene>());
        return;
    }
    if (input->IsKeyTrigger(DIK_1)) {
        pendingLogMarker_ = "MISS";
    }
    if (input->IsKeyTrigger(DIK_2)) {
        pendingLogMarker_ = "FALSE_HIT";
    }
    if (input->IsKeyTrigger(DIK_3)) {
        pendingLogMarker_ = "GOOD";
    }
    if (input->IsKeyTrigger(DIK_C)) {
        CaptureNeutral();
        pendingLogMarker_ = "CALIBRATE";
    }
    if (input->IsKeyTrigger(DIK_R)) {
        ResetNeutral();
        pendingLogMarker_ = "RESET";
    }
    WriteHandDebugLog(ctx_->frame.deltaTime);
}

void CameraAccuracyDebugScene::Draw() {
    DrawDebugSwords();
}

void CameraAccuracyDebugScene::DrawTransparent() {
    const float w = static_cast<float>(ctx_->systems.winApp->GetWidth());
    const float h = static_cast<float>(ctx_->systems.winApp->GetHeight());

    previewReceiver_.Draw(ctx_->rendering.sprite, ctx_->rendering.texture,
                          kPreviewStaleSeconds);

    ctx_->rendering.sprite->PreDraw();
    DrawOverlay(w, h);
    ctx_->rendering.sprite->PostDraw();
}

bool CameraAccuracyDebugScene::RequestHandTrackingStartOnce() {
    if (handTrackingStartRequested_ || ctx_ == nullptr) {
        return handTrackingStartRequested_;
    }
    if (!AppSceneServices::HasHandTrackingStart()) {
        return false;
    }
    handTrackingStartRequested_ = AppSceneServices::RequestHandTrackingStart();
    return handTrackingStartRequested_;
}

void CameraAccuracyDebugScene::UpdateCamera() {
    if (ctx_ == nullptr || ctx_->systems.winApp == nullptr) {
        return;
    }
    const int width = ctx_->systems.winApp->GetWidth();
    const int height = ctx_->systems.winApp->GetHeight();
    if (width > 0 && height > 0) {
        camera_.SetAspect(static_cast<float>(width) / static_cast<float>(height));
    }

    const float orbit = std::sinf(sceneTime_ * 0.24f) * 0.12f;
    const XMFLOAT3 cameraPos{std::sinf(orbit) * 0.9f, 1.55f,
                             -5.8f + std::cosf(orbit) * 0.25f};
    camera_.SetPosition(cameraPos);
    AppLookAt(camera_, {0.0f, 0.82f, 0.0f});
    camera_.UpdateMatrices();
}

void CameraAccuracyDebugScene::CaptureNeutral() {
    bool captured = false;
    for (size_t i = 0; i < calibration_.handNeutral.size(); ++i) {
        const auto sample = controller_.GetDebugHandState(i);
        if (!sample.fresh || !sample.active) {
            continue;
        }
        calibration_.handNeutral[i] = sample.rawPalm;
        captured = true;
    }
    calibration_.hasHandNeutral = captured;
    neutralCapturedThisScene_ = captured;
    controller_.SetCalibration(calibration_);
    gamePreviewPlayer_.SetInputCalibration(calibration_);
}

void CameraAccuracyDebugScene::ResetNeutral() {
    calibration_.hasHandNeutral = false;
    calibration_.handNeutral = {XMFLOAT2{0.5f, 0.5f}, XMFLOAT2{0.5f, 0.5f}};
    neutralCapturedThisScene_ = false;
    controller_.SetCalibration(calibration_);
    gamePreviewPlayer_.SetInputCalibration(calibration_);
}

void CameraAccuracyDebugScene::UpdateGamePreview(float deltaTime) {
    SwordPose leftPose{};
    SwordPose rightPose{};
    if (controller_.IsActive(0)) {
        leftPose = controller_.GetPose(0);
    }
    if (controller_.IsActive(1)) {
        rightPose = controller_.GetPose(1);
    }

    gamePreviewPlayer_.UpdateDebugSwordPoses(leftPose, rightPose, deltaTime,
                                             {0.0f, 0.0f, 0.0f}, 0.0f);
}

void CameraAccuracyDebugScene::OpenHandDebugLog() {
    try {
        const auto path =
            std::filesystem::temp_directory_path() /
            "shingiittai_hand_debug_game.csv";
        handDebugLogPath_ = path.string();
        handDebugLog_.open(path, std::ios::out | std::ios::trunc);
        if (!handDebugLog_) {
            handDebugLogPath_.clear();
            return;
        }
        handDebugLog_
            << "frame,sceneTime,dt,marker,hand,packetChanged,packetSeq,"
               "packetFrame,packetTimestampMs,packetHandCount,bodyTracked,"
               "bodyCorrected,fresh,active,lost,reacquired,slash,slashStart,"
               "rawX,rawY,neutralX,neutralY,calX,calY,"
               "dirX,dirY,motionSpeed,packetDtMs,packetDx,packetDy,"
               "packetMotionSpeed,nearEdge,sourceLabel,sourceScore,"
               "staleTimer,orientX,orientY,orientZ,orientW,"
               "neutralCaptured\n";
        handDebugLog_ << std::fixed << std::setprecision(6);
    } catch (...) {
        handDebugLogPath_.clear();
    }
}

void CameraAccuracyDebugScene::WriteHandDebugLog(float deltaTime) {
    if (!handDebugLog_) {
        return;
    }

    for (size_t i = 0; i < 2; ++i) {
        const auto sample = controller_.GetDebugHandState(i);
        const bool lost = previousLogActive_[i] && !sample.active;
        const bool reacquired = !previousLogActive_[i] && sample.active;
        const bool slashStart = !previousLogSlash_[i] && sample.isSlashMode;

        handDebugLog_
            << handDebugLogFrame_ << ',' << sceneTime_ << ',' << deltaTime
            << ',' << pendingLogMarker_ << ',' << i << ','
            << (sample.packetChanged ? 1 : 0) << ',' << sample.packetSequence
            << ',' << sample.packetFrame << ',' << sample.packetTimestampMs
            << ',' << sample.handCount << ',' << (sample.bodyTracked ? 1 : 0)
            << ',' << (sample.bodyCorrected ? 1 : 0) << ','
            << (sample.fresh ? 1 : 0) << ','
            << (sample.active ? 1 : 0) << ',' << (lost ? 1 : 0) << ','
            << (reacquired ? 1 : 0) << ',' << (sample.isSlashMode ? 1 : 0)
            << ',' << (slashStart ? 1 : 0) << ',' << sample.rawPalm.x << ','
            << sample.rawPalm.y << ',' << sample.neutral.x << ','
            << sample.neutral.y << ',' << sample.calibratedPalm.x << ','
            << sample.calibratedPalm.y << ',' << sample.slashDir.x << ','
            << sample.slashDir.y << ',' << sample.motionSpeed << ','
            << sample.packetDeltaMs << ',' << sample.packetDeltaPalm.x << ','
            << sample.packetDeltaPalm.y << ',' << sample.packetMotionSpeed
            << ',' << (sample.nearEdge ? 1 : 0) << ','
            << sample.sourceLabel << ',' << sample.sourceScore << ','
            << sample.staleTimer << ',' << sample.orientation.x << ','
            << sample.orientation.y << ',' << sample.orientation.z << ','
            << sample.orientation.w << ','
            << (neutralCapturedThisScene_ ? 1 : 0) << '\n';

        previousLogActive_[i] = sample.active;
        previousLogSlash_[i] = sample.isSlashMode;
    }
    ++handDebugLogFrame_;
    pendingLogMarker_.clear();

    if ((handDebugLogFrame_ % 30) == 0) {
        handDebugLog_.flush();
    }
}

SwordPose
CameraAccuracyDebugScene::MakePoseFromPalm(const XMFLOAT2 &palm) const {
    SwordPose pose{};
    const float dirX = std::clamp((palm.x - 0.5f) * 2.0f, -1.0f, 1.0f);
    const float dirY = std::clamp((0.5f - palm.y) * 2.0f, -1.0f, 1.0f);
    pose.slashDir = {dirX, dirY};
    pose.isSlashMode = true;

    const float yaw = dirX * 0.82f;
    const float pitch = -dirY * 0.72f;
    const XMVECTOR qYaw =
        XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 0), yaw);
    const XMVECTOR qPitch =
        XMQuaternionRotationAxis(XMVectorSet(1, 0, 0, 0), pitch);
    XMStoreFloat4(&pose.orientation,
                  XMQuaternionNormalize(XMQuaternionMultiply(qPitch, qYaw)));
    return pose;
}

Transform CameraAccuracyDebugScene::BuildSwordTransform(
    const SwordPose &pose, const XMFLOAT3 &anchor, bool isLeft) const {
    Transform transform{};
    const float handOffsetX = isLeft ? -0.34f : 0.34f;
    const XMVECTOR swordRot = XMQuaternionNormalize(XMLoadFloat4(&pose.orientation));
    XMStoreFloat4(&transform.rotation, swordRot);

    const XMVECTOR shoulder =
        XMVectorSet(anchor.x + handOffsetX, anchor.y, anchor.z, 0.0f);
    const XMVECTOR arm =
        XMVector3Rotate(XMVectorSet(0.0f, 0.0f, 0.70f, 0.0f), swordRot);
    XMStoreFloat3(&transform.position, XMVectorAdd(shoulder, arm));
    transform.scale = {1.18f, 1.18f, 1.18f};
    return transform;
}

void CameraAccuracyDebugScene::DrawDebugSwords() {
    ModelManager *model = ctx_->rendering.model;
    if (model == nullptr || swordModelId_ == 0) {
        return;
    }

    model->PreDraw();
    gamePreviewPlayer_.Draw(model, camera_, true, false, 0.82f);

    for (size_t i = 0; i < 2; ++i) {
        const bool isLeft = i == 0;
        const auto sample = controller_.GetDebugHandState(i);

        const SwordPose rawPose = MakePoseFromPalm(sample.rawPalm);
        const SwordPose correctedPose =
            sample.active ? SwordPose{sample.slashDir, sample.orientation,
                                      sample.isSlashMode}
                          : MakePoseFromPalm(sample.calibratedPalm);

        ModelDrawEffect rawEffect{};
        rawEffect.enabled = true;
        rawEffect.blendOverride = ModelDrawEffectBlendOverride::Alpha;
        rawEffect.color = {0.18f, 0.58f, 1.0f, sample.active ? 0.48f : 0.20f};
        rawEffect.intensity = 0.12f;
        rawEffect.surfaceTint = 0.85f;
        rawEffect.baseDim = 0.18f;
        model->SetDrawEffect(rawEffect);
        model->Draw(swordModelId_,
                    BuildSwordTransform(rawPose, {-1.35f, 0.78f, 0.0f}, isLeft),
                    camera_);

        ModelDrawEffect correctedEffect{};
        correctedEffect.enabled = true;
        correctedEffect.blendOverride = ModelDrawEffectBlendOverride::Alpha;
        correctedEffect.color = {1.0f, 0.72f, 0.20f,
                                 sample.active ? 0.88f : 0.26f};
        correctedEffect.intensity = 0.16f;
        correctedEffect.surfaceTint = 0.82f;
        correctedEffect.baseDim = 0.05f;
        model->SetDrawEffect(correctedEffect);
        model->Draw(swordModelId_,
                    BuildSwordTransform(correctedPose, {1.35f, 0.78f, 0.0f},
                                        isLeft),
                    camera_);
    }
    model->ClearDrawEffect();
    model->PostDraw();
}

void CameraAccuracyDebugScene::DrawOverlay(float screenWidth,
                                           float screenHeight) {
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
             Color(0.0f, 0.0f, 0.0f, 0.20f));
    DrawRect(0.0f, 0.0f, screenWidth, 34.0f, Color(0.0f, 0.0f, 0.0f, 0.72f));
    DrawText("CAMERA SWORD DEBUG", 18.0f, 10.0f, 2.0f,
             Color(0.78f, 0.90f, 1.0f, 0.92f));
    DrawText("ESC TITLE  C CALIB  R RESET  1 MISS  2 FALSE  3 GOOD", 312.0f,
             12.0f, 1.4f, Color(0.82f, 0.78f, 0.62f, 0.88f));
    DrawText("LOG GAME CSV: TEMP/SHINGIITTAI_HAND_DEBUG_GAME.CSV", 18.0f,
             38.0f, 1.15f, Color(0.66f, 0.76f, 0.86f, 0.76f));
    DrawText("LOG RAW JSONL: TEMP/SHINGIITTAI_HAND_RAW.JSONL", 18.0f,
             54.0f, 1.15f, Color(0.66f, 0.76f, 0.86f, 0.76f));

    DrawText("RAW SWORD", screenWidth * 0.5f - 232.0f,
             screenHeight * 0.50f - 158.0f, 2.0f,
             Color(0.36f, 0.72f, 1.0f, 0.82f));
    DrawText("GAME PREVIEW", screenWidth * 0.5f - 58.0f,
             screenHeight * 0.50f - 204.0f, 1.75f,
             Color(0.86f, 0.92f, 0.98f, 0.88f));
    DrawText("CORRECTED SWORD", screenWidth * 0.5f + 66.0f,
             screenHeight * 0.50f - 158.0f, 2.0f,
             Color(1.0f, 0.76f, 0.28f, 0.88f));

    const float panelW = (std::min)(300.0f, screenWidth * 0.22f);
    const float panelH = 208.0f;
    DrawHandPanel("LEFT HAND", "INDEX 0", 0, screenWidth - panelW - 18.0f,
                  56.0f, panelW, panelH);
    DrawHandPanel("RIGHT HAND", "INDEX 1", 1, screenWidth - panelW - 18.0f,
                  286.0f, panelW, panelH);

    const float statsX = 18.0f;
    const float statsY = (std::max)(234.0f, screenHeight - 174.0f);
    DrawRect(statsX - 10.0f, statsY - 14.0f, 496.0f, 154.0f,
             Color(0.0f, 0.0f, 0.0f, 0.54f));
    DrawFrame(statsX - 10.0f, statsY - 14.0f, 496.0f, 154.0f, 2.0f,
              Color(0.38f, 0.46f, 0.54f, 0.55f));
    DrawText(neutralCapturedThisScene_ ? "NEUTRAL ACTIVE" : "NEUTRAL OFF",
             statsX, statsY - 2.0f, 1.7f,
             neutralCapturedThisScene_ ? Color(0.86f, 1.0f, 0.58f, 0.90f)
                                       : Color(0.92f, 0.62f, 0.42f, 0.86f));
    DrawHandStats(0, statsX, statsY + 28.0f);
    DrawHandStats(1, statsX, statsY + 78.0f);
}

void CameraAccuracyDebugScene::DrawHandPanel(const char *title,
                                             const char *subtitle,
                                             size_t handIndex, float x, float y,
                                             float w, float h) {
    const auto sample = controller_.GetDebugHandState(handIndex);
    DrawRect(x, y, w, h, Color(0.018f, 0.026f, 0.036f, 0.78f));
    DrawFrame(x, y, w, h, 2.0f,
              sample.active ? Color(0.38f, 0.84f, 0.62f, 0.72f)
                            : Color(0.75f, 0.38f, 0.25f, 0.55f));
    DrawText(title, x + 12.0f, y + 12.0f, 1.6f,
             Color(0.86f, 0.92f, 0.98f, 0.88f));
    DrawText(subtitle, x + w - 82.0f, y + 14.0f, 1.2f,
             Color(0.55f, 0.62f, 0.70f, 0.84f));

    const float mapX = x + 16.0f;
    const float mapY = y + 42.0f;
    const float mapW = w - 32.0f;
    const float mapH = h - 98.0f;
    DrawRect(mapX, mapY, mapW, mapH, Color(0.0f, 0.0f, 0.0f, 0.42f));
    DrawFrame(mapX, mapY, mapW, mapH, 1.5f, Color(0.36f, 0.42f, 0.50f, 0.54f));
    DrawRect(mapX + mapW * 0.5f - 1.0f, mapY, 2.0f, mapH,
             Color(0.50f, 0.58f, 0.68f, 0.24f));
    DrawRect(mapX, mapY + mapH * 0.5f - 1.0f, mapW, 2.0f,
             Color(0.50f, 0.58f, 0.68f, 0.24f));

    auto toPanel = [&](const XMFLOAT2 &point) {
        return XMFLOAT2{mapX + Clamp01(point.x) * mapW,
                        mapY + Clamp01(point.y) * mapH};
    };
    const XMFLOAT2 raw = toPanel(sample.rawPalm);
    const XMFLOAT2 neutral = toPanel(sample.neutral);
    const XMFLOAT2 corrected = toPanel(sample.calibratedPalm);
    DrawPoint(neutral.x, neutral.y, 5.0f, Color(0.92f, 0.92f, 0.92f, 0.64f));
    DrawPoint(raw.x, raw.y, 6.0f, Color(0.22f, 0.62f, 1.0f, 0.88f));
    DrawPoint(corrected.x, corrected.y, 6.0f,
              Color(1.0f, 0.72f, 0.18f, 0.92f));

    const float centerX = mapX + mapW * 0.5f;
    const float centerY = mapY + mapH * 0.5f;
    DrawRect((std::min)(centerX, corrected.x), centerY - 2.0f,
             std::abs(corrected.x - centerX) + 2.0f, 4.0f,
             Color(1.0f, 0.72f, 0.18f, 0.48f));
    DrawRect(corrected.x - 2.0f, (std::min)(centerY, corrected.y), 4.0f,
             std::abs(corrected.y - centerY) + 2.0f,
             Color(1.0f, 0.72f, 0.18f, 0.48f));

    DrawText("BLUE RAW  WHITE NEUTRAL  GOLD CORRECTED", x + 12.0f,
             y + h - 42.0f, 1.05f, Color(0.72f, 0.78f, 0.84f, 0.82f));
    DrawText(sample.active ? "DETECTED" : "NO HAND", x + 12.0f, y + h - 22.0f,
             1.35f, sample.active ? Color(0.46f, 1.0f, 0.64f, 0.92f)
                                   : Color(1.0f, 0.46f, 0.30f, 0.84f));
}

void CameraAccuracyDebugScene::DrawHandStats(size_t handIndex, float x,
                                             float y) {
    const auto sample = controller_.GetDebugHandState(handIndex);
    char line[256]{};
    std::snprintf(line, sizeof(line),
                  "H%zu ACT=%d RAW %.2f %.2f  CAL %.2f %.2f",
                  handIndex, sample.active ? 1 : 0, sample.rawPalm.x,
                  sample.rawPalm.y, sample.calibratedPalm.x,
                  sample.calibratedPalm.y);
    DrawText(line, x, y, 1.25f, Color(0.82f, 0.88f, 0.94f, 0.86f));
    std::snprintf(line, sizeof(line),
                  "   DIR %.2f %.2f  SPEED %.2f  SLASH=%d", sample.slashDir.x,
                  sample.slashDir.y, sample.motionSpeed,
                  sample.isSlashMode ? 1 : 0);
    DrawText(line, x, y + 22.0f, 1.25f, Color(0.72f, 0.78f, 0.86f, 0.82f));
}

void CameraAccuracyDebugScene::DrawRect(float x, float y, float w, float h,
                                        const XMFLOAT4 &color) {
    if (w <= 0.0f || h <= 0.0f) {
        return;
    }
    Sprite sprite{};
    sprite.textureId = 0;
    sprite.position = {x, y};
    sprite.size = {w, h};
    sprite.color = color;
    ctx_->rendering.sprite->DrawSprite(sprite);
}

void CameraAccuracyDebugScene::DrawFrame(float x, float y, float w, float h,
                                         float thickness,
                                         const XMFLOAT4 &color) {
    DrawRect(x, y, w, thickness, color);
    DrawRect(x, y + h - thickness, w, thickness, color);
    DrawRect(x, y, thickness, h, color);
    DrawRect(x + w - thickness, y, thickness, h, color);
}

void CameraAccuracyDebugScene::DrawPoint(float x, float y, float radius,
                                         const XMFLOAT4 &color) {
    DrawRect(x - radius, y - radius, radius * 2.0f, radius * 2.0f, color);
    DrawRect(x - radius * 1.65f, y - 1.0f, radius * 3.3f, 2.0f,
             Color(color.x, color.y, color.z, color.w * 0.55f));
    DrawRect(x - 1.0f, y - radius * 1.65f, 2.0f, radius * 3.3f,
             Color(color.x, color.y, color.z, color.w * 0.55f));
}

void CameraAccuracyDebugScene::DrawText(const std::string &text, float x,
                                        float y, float scale,
                                        const XMFLOAT4 &color) {
    float cursorX = x;
    for (char raw : text) {
        char c = raw;
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
        if (c == '\n') {
            cursorX = x;
            y += 9.0f * scale;
            continue;
        }
        const auto &rows = GlyphRows(c);
        for (size_t row = 0; row < rows.size(); ++row) {
            for (size_t col = 0; col < 5; ++col) {
                if (rows[row][col] != '1') {
                    continue;
                }
                DrawRect(cursorX + static_cast<float>(col) * scale,
                         y + static_cast<float>(row) * scale, scale, scale,
                         color);
            }
        }
        cursorX += 6.0f * scale;
    }
}
