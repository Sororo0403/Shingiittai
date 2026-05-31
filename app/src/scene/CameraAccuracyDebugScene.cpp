#include "CameraAccuracyDebugScene.h"
#include "AppSceneServices.h"
#include "DirectXCommon.h"
#include "Input.h"
#include "ModelDrawEffect.h"
#include "ModelManager.h"
#include "OptionScene.h"
#include "PostEffectManager.h"
#include "SceneManager.h"
#include "Sprite.h"
#include "SpriteManager.h"
#include "TextureManager.h"
#include "TitleScene.h"
#include "TutorialSelectScene.h"
#include "WeaponSelectScene.h"
#include "WinApp.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

#ifdef DrawText
#undef DrawText
#endif

using namespace DirectX;

namespace {
constexpr uint16_t kPreviewPort = 5006;
constexpr float kPreviewStaleSeconds = 0.75f;
constexpr float kPi = 3.14159265f;
constexpr float kSensitivityStep = 0.05f;
constexpr float kVisualSlashNetDistanceThreshold = 0.027f;
constexpr float kHardSensitivityThresholdScale = 1.18f;
constexpr float kEasySensitivityThresholdScale = 0.48f;
constexpr float kVerticalSlashThresholdScale = 0.82f;
constexpr int kSensitivityCount = 4;

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

float SensitivityThresholdScale(float sensitivity) {
    return std::lerp(kHardSensitivityThresholdScale,
                     kEasySensitivityThresholdScale,
                     std::clamp(sensitivity, 0.0f, 1.0f));
}
} // namespace

CameraAccuracyDebugScene::CameraAccuracyDebugScene(ReturnTarget returnTarget)
    : returnTarget_(returnTarget) {}

CameraAccuracyDebugScene::~CameraAccuracyDebugScene() {
    previewReceiver_.Close();
    AppSceneServices::RequestHandTrackingStop();
}

void CameraAccuracyDebugScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    sceneTime_ = 0.0f;
    handTrackingStartRequested_ = false;
    neutralCapturedThisScene_ = false;
    selectedSensitivityIndex_ = 0;

    calibration_ = {};
    calibration_.controlType = InputControlType::Hand;
    controller_.SetCalibration(calibration_);
    RequestHandTrackingStartOnce();
    previewReceiver_.Initialize(ctx_->rendering.texture, kPreviewPort);
    titleImage_ =
        LoadTextureImage(L"app/resources/ui/sensitivity_adjust/title.png");
    controlsImage_ =
        LoadTextureImage(L"app/resources/ui/sensitivity_adjust/controls.png");
    sensitivityLabelImages_[0] =
        LoadTextureImage(L"app/resources/ui/sensitivity/label_ctrl.png");
    sensitivityLabelImages_[1] =
        LoadTextureImage(L"app/resources/ui/sensitivity/label_slash.png");
    sensitivityLabelImages_[2] =
        LoadTextureImage(L"app/resources/ui/sensitivity/label_vert.png");
    sensitivityLabelImages_[3] =
        LoadTextureImage(L"app/resources/ui/sensitivity/label_horz.png");
    for (int i = 0; i < 10; ++i) {
        digitImages_[static_cast<size_t>(i)] =
            LoadTextureImage(L"app/resources/ui/result/mplus/glyphs/char_" +
                             std::to_wstring(i) + L".png");
    }
    dotImage_ =
        LoadTextureImage(L"app/resources/ui/result/mplus/glyphs/char_dot.png");

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
        return;
    }
    if (input->IsKeyTrigger(DIK_ESCAPE)) {
        switch (returnTarget_) {
        case ReturnTarget::WeaponSelect:
            sceneManager_->ChangeScene(std::make_unique<WeaponSelectScene>());
            break;
        case ReturnTarget::TutorialSelect:
            sceneManager_->ChangeScene(std::make_unique<TutorialSelectScene>());
            break;
        case ReturnTarget::WeaponOption:
            sceneManager_->ChangeScene(std::make_unique<OptionScene>(
                OptionScene::ReturnTarget::WeaponSelect));
            break;
        case ReturnTarget::TutorialOption:
            sceneManager_->ChangeScene(std::make_unique<OptionScene>(
                OptionScene::ReturnTarget::TutorialSelect));
            break;
        case ReturnTarget::Title:
        default:
            sceneManager_->ChangeScene(std::make_unique<TitleScene>());
            break;
        }
        return;
    }
    if (input->IsKeyTrigger(DIK_C)) {
        CaptureNeutral();
    }
    if (input->IsKeyTrigger(DIK_R)) {
        ResetNeutral();
    }
    if (input->IsKeyTrigger(DIK_W) || input->IsKeyTrigger(DIK_UP)) {
        selectedSensitivityIndex_ =
            (selectedSensitivityIndex_ + kSensitivityCount - 1) %
            kSensitivityCount;
        AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Select);
    }
    if (input->IsKeyTrigger(DIK_S) || input->IsKeyTrigger(DIK_DOWN)) {
        selectedSensitivityIndex_ =
            (selectedSensitivityIndex_ + 1) % kSensitivityCount;
        AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Select);
    }
    if (input->IsKeyTrigger(DIK_A) || input->IsKeyTrigger(DIK_LEFT)) {
        AdjustSelectedSensitivity(-1);
    }
    if (input->IsKeyTrigger(DIK_D) || input->IsKeyTrigger(DIK_RIGHT)) {
        AdjustSelectedSensitivity(1);
    }
}

void CameraAccuracyDebugScene::Draw() {
    const float w = static_cast<float>(ctx_->systems.winApp->GetWidth());
    const float h = static_cast<float>(ctx_->systems.winApp->GetHeight());
    DrawPreviewBackground(w, h);
    DrawDebugSwords();
}

void CameraAccuracyDebugScene::DrawTransparent() {
    const float w = static_cast<float>(ctx_->systems.winApp->GetWidth());
    const float h = static_cast<float>(ctx_->systems.winApp->GetHeight());

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

CameraAccuracyDebugScene::Image
CameraAccuracyDebugScene::LoadTextureImage(const std::wstring &path) {
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

void CameraAccuracyDebugScene::AdjustSelectedSensitivity(int direction) {
    const float delta = static_cast<float>(direction) * kSensitivityStep;
    switch (selectedSensitivityIndex_) {
    case 0:
        AppSceneServices::SetCameraSensitivity(
            AppSceneServices::GetCameraSensitivity() + delta);
        break;
    case 1:
        AppSceneServices::SetCameraSlashSensitivity(
            AppSceneServices::GetCameraSlashSensitivity() + delta);
        break;
    case 2:
        AppSceneServices::SetCameraVerticalSensitivity(
            AppSceneServices::GetCameraVerticalSensitivity() + delta);
        break;
    case 3:
        AppSceneServices::SetCameraHorizontalSensitivity(
            AppSceneServices::GetCameraHorizontalSensitivity() + delta);
        break;
    default:
        return;
    }
    AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Select);
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
    model->ClearDrawEffect();
    model->PostDraw();
}

void CameraAccuracyDebugScene::DrawOverlay(float screenWidth,
                                           float screenHeight) {
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
             Color(0.0f, 0.0f, 0.0f, 0.10f));
    DrawRect(0.0f, 0.0f, screenWidth, 74.0f, Color(0.0f, 0.0f, 0.0f, 0.66f));
    DrawImage(titleImage_, 24.0f, 12.0f, 0.58f, 0.96f);
    DrawImage(controlsImage_, 374.0f, 23.0f, 0.62f, 0.88f);
    DrawSensitivityPanel(screenWidth);
}

void CameraAccuracyDebugScene::DrawPreviewBackground(float screenWidth,
                                                     float screenHeight) {
    ctx_->rendering.sprite->PreDraw();
    previewReceiver_.DrawArea(ctx_->rendering.sprite, ctx_->rendering.texture,
                              0.0f, 0.0f, screenWidth, screenHeight,
                              kPreviewStaleSeconds, 1.0f, true);
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
             Color(0.0f, 0.0f, 0.0f, 0.34f));
    ctx_->rendering.sprite->PostDraw();
}

void CameraAccuracyDebugScene::DrawSensitivityPanel(float screenWidth) {
    const float controlSensitivity = AppSceneServices::GetCameraSensitivity();
    const float slashSensitivity = AppSceneServices::GetCameraSlashSensitivity();
    const float verticalSensitivity =
        AppSceneServices::GetCameraVerticalSensitivity();
    const float horizontalSensitivity =
        AppSceneServices::GetCameraHorizontalSensitivity();
    const float panelW = 560.0f;
    const float panelH = 198.0f;
    const float panelX = (screenWidth - panelW) * 0.5f;
    const float panelY = 64.0f;

    DrawRect(panelX - 5.0f, panelY - 5.0f, panelW + 10.0f, panelH + 10.0f,
             Color(1.0f, 0.95f, 0.62f, 0.52f));
    DrawRect(panelX, panelY, panelW, panelH, Color(0.0f, 0.0f, 0.0f, 0.92f));
    DrawFrame(panelX, panelY, panelW, panelH, 4.0f,
              Color(1.0f, 0.86f, 0.20f, 1.0f));

    DrawGaugeRow(0, sensitivityLabelImages_[0], controlSensitivity,
                 Color(0.0f, 0.92f, 1.0f, 1.0f), panelX, panelY + 22.0f,
                 panelW);
    DrawGaugeRow(1, sensitivityLabelImages_[1], slashSensitivity,
                 Color(1.0f, 0.28f, 0.20f, 1.0f), panelX, panelY + 64.0f,
                 panelW);
    DrawGaugeRow(2, sensitivityLabelImages_[2], verticalSensitivity,
                 Color(0.58f, 1.0f, 0.30f, 1.0f), panelX, panelY + 106.0f,
                 panelW);
    DrawGaugeRow(3, sensitivityLabelImages_[3], horizontalSensitivity,
                 Color(1.0f, 0.58f, 0.18f, 1.0f), panelX, panelY + 148.0f,
                 panelW);
}

void CameraAccuracyDebugScene::DrawGaugeRow(
    size_t index, const Image &label, float value, const XMFLOAT4 &barColor,
    float panelX, float rowY, float panelW) {
    const bool selected = static_cast<int>(index) == selectedSensitivityIndex_;
    const float labelX = panelX + 26.0f;
    const float barX = panelX + 180.0f;
    const float valueX = panelX + panelW - 72.0f;
    const float barW = panelW - 276.0f;
    const float barH = 20.0f;
    if (selected) {
        DrawRect(panelX + 12.0f, rowY - 8.0f, panelW - 24.0f, 34.0f,
                 Color(1.0f, 0.74f, 0.20f, 0.20f));
        DrawFrame(panelX + 12.0f, rowY - 8.0f, panelW - 24.0f, 34.0f,
                  2.0f, Color(1.0f, 0.78f, 0.24f, 0.70f));
    }
    DrawImageCentered(label, labelX + 48.0f, rowY + 10.0f, 110.0f, 28.0f,
                      selected ? 1.0f : 0.82f);
    DrawRect(barX, rowY, barW, barH, Color(0.06f, 0.07f, 0.09f, 0.94f));
    DrawRect(barX, rowY, barW * std::clamp(value, 0.0f, 1.0f), barH,
             barColor);
    DrawFrame(barX, rowY, barW, barH, 2.0f,
              selected ? Color(1.0f, 0.92f, 0.38f, 0.95f)
                       : Color(0.86f, 0.88f, 0.90f, 0.54f));
    DrawSensitivityValue(value, valueX, rowY - 2.0f, 0.34f,
                         selected ? Color(1.0f, 0.88f, 0.34f, 1.0f)
                                  : Color(0.92f, 0.94f, 0.96f, 0.84f));
}

float CameraAccuracyDebugScene::RequiredTravelForSensitivity(
    float axisSensitivity) const {
    return kVisualSlashNetDistanceThreshold *
           SensitivityThresholdScale(
               AppSceneServices::GetCameraSlashSensitivity()) *
           SensitivityThresholdScale(axisSensitivity);
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

    const float requiredH = RequiredTravelForSensitivity(
        AppSceneServices::GetCameraHorizontalSensitivity());
    const float requiredV = RequiredTravelForSensitivity(
                                AppSceneServices::GetCameraVerticalSensitivity()) *
                            kVerticalSlashThresholdScale;
    const float centerX = mapX + mapW * 0.5f;
    const float centerY = mapY + mapH * 0.5f;
    const float requiredPixelsH = requiredH * mapW;
    const float requiredPixelsV = requiredV * mapH;
    DrawFrame(centerX - requiredPixelsH, centerY - requiredPixelsV,
              requiredPixelsH * 2.0f, requiredPixelsV * 2.0f, 2.0f,
              Color(0.98f, 0.92f, 0.24f, 0.72f));
    DrawRect(centerX - requiredPixelsH, mapY, 3.0f, mapH,
             Color(0.0f, 0.92f, 1.0f, 0.58f));
    DrawRect(centerX + requiredPixelsH - 3.0f, mapY, 3.0f, mapH,
             Color(0.0f, 0.92f, 1.0f, 0.58f));
    DrawRect(mapX, centerY - requiredPixelsV, mapW, 3.0f,
             Color(0.58f, 1.0f, 0.30f, 0.58f));
    DrawRect(mapX, centerY + requiredPixelsV - 3.0f, mapW, 3.0f,
             Color(0.58f, 1.0f, 0.30f, 0.58f));

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

void CameraAccuracyDebugScene::DrawImage(const Image &image, float x, float y,
                                         float scale, float alpha) {
    if (ctx_ == nullptr || ctx_->rendering.sprite == nullptr ||
        image.textureId == 0) {
        return;
    }
    Sprite sprite{};
    sprite.textureId = image.textureId;
    sprite.position = {x, y};
    sprite.size = {image.width * scale, image.height * scale};
    sprite.color = {1.0f, 1.0f, 1.0f, alpha};
    ctx_->rendering.sprite->DrawSprite(sprite);
}

void CameraAccuracyDebugScene::DrawImageCentered(const Image &image,
                                                 float centerX, float centerY,
                                                 float maxWidth,
                                                 float maxHeight,
                                                 float alpha) {
    if (image.textureId == 0 || image.width <= 0.0f || image.height <= 0.0f) {
        return;
    }
    const float scale = std::min(maxWidth / image.width, maxHeight / image.height);
    DrawImage(image, centerX - image.width * scale * 0.5f,
              centerY - image.height * scale * 0.5f, scale, alpha);
}

void CameraAccuracyDebugScene::DrawSensitivityValue(
    float value, float x, float y, float scale, const XMFLOAT4 &color) {
    const int clamped = std::clamp(static_cast<int>(std::round(value * 100.0f)),
                                  0, 100);
    const int whole = clamped / 100;
    const int tens = (clamped / 10) % 10;
    const int ones = clamped % 10;
    const Image *parts[] = {&digitImages_[static_cast<size_t>(whole)],
                            &dotImage_,
                            &digitImages_[static_cast<size_t>(tens)],
                            &digitImages_[static_cast<size_t>(ones)]};
    float cursorX = x;
    for (const Image *part : parts) {
        if (part == nullptr || part->textureId == 0) {
            continue;
        }
        Sprite sprite{};
        sprite.textureId = part->textureId;
        sprite.position = {cursorX, y};
        sprite.size = {part->width * scale, part->height * scale};
        sprite.color = color;
        ctx_->rendering.sprite->DrawSprite(sprite);
        cursorX += part->width * scale + 2.0f;
    }
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
