#include "CreditScene.h"
#include "AppSceneServices.h"
#include "Input.h"
#include "SceneManager.h"
#include "SoundManager.h"
#include "Sprite.h"
#include "SpriteManager.h"
#include "TextureManager.h"
#include "TitleScene.h"
#include "TutorialSelectScene.h"
#include "WeaponSelectScene.h"
#include "WinApp.h"
#include <algorithm>
#include <array>
#include <memory>
#include <vector>

using namespace DirectX;

namespace {
constexpr float kIntroDuration = 0.42f;
constexpr float kSceneFadeInDuration = 0.92f;
constexpr float kTransitionDuration = 0.20f;
constexpr float kCreditRollDelay = 0.32f;
constexpr float kCreditRollSpeed = 42.0f;
constexpr float kCreditRollFastSpeed = 360.0f;
constexpr float kCreditVirtualWidth = 760.0f;
constexpr float kTaroLineCenterY = 374.0f;
constexpr float kAotoMoriLineCenterY = 650.0f;
constexpr float kTsunaguLineCenterY = 1178.0f;
constexpr float kLogoStartAfterTextPadding = 80.0f;
constexpr float kLogoStartBelowScreenPadding = 150.0f;
constexpr float kSignatureColumnCenterRatio = 0.78f;
constexpr float kSignatureMaxScale = 0.84f;

struct CreditLineSpec {
    const wchar_t *path;
    float centerY;
    float scale;
};

constexpr std::array<CreditLineSpec, 9> kCreditLineSpecs{{
    {L"app/resources/ui/credits/lines/title.png", 78.0f, 1.0f},
    {L"app/resources/ui/credits/lines/taro_name.png", 374.0f, 1.0f},
    {L"app/resources/ui/credits/lines/taro_role.png", 426.0f, 1.0f},
    {L"app/resources/ui/credits/lines/aotomori_name.png", 650.0f, 1.0f},
    {L"app/resources/ui/credits/lines/aotomori_role.png", 702.0f, 1.0f},
    {L"app/resources/ui/credits/lines/yutaka_name.png", 914.0f, 1.0f},
    {L"app/resources/ui/credits/lines/yutaka_role.png", 966.0f, 1.0f},
    {L"app/resources/ui/credits/lines/tsunagu_miyashita_name.png", 1178.0f,
     1.0f},
    {L"app/resources/ui/credits/lines/tsunagu_miyashita_role.png", 1228.0f,
     1.0f},
}};

XMFLOAT4 MakeColor(float r, float g, float b, float a = 1.0f) {
    return {r, g, b, a};
}

float SmoothStep(float t) { return t * t * (3.0f - 2.0f * t); }

float Smooth01(float t) {
    return SmoothStep(std::clamp(t, 0.0f, 1.0f));
}

} // namespace

CreditScene::CreditScene(ReturnTarget returnTarget)
    : returnTarget_(returnTarget) {}

CreditScene::~CreditScene() { StopCreditBgm(); }

void CreditScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    AppSceneServices::RequestHandTrackingStop();
    sceneTime_ = 0.0f;
    creditRollDistance_ = 0.0f;
    introTimer_ = 0.0f;
    transitionTimer_ = 0.0f;
    returnRequested_ = false;

    backgroundScene_ =
        std::make_unique<GameScene>(GameScene::Mode::BackgroundOnly);
    backgroundScene_->Initialize(ctx);

    LoadCreditLineImages();
    logoImage_ = LoadTextureImage(L"app/resources/ui/title/gamelogo.png");
    thankYouImage_ =
        LoadTextureImage(L"app/resources/ui/credits/thank_you_for_playing.png");
    taroSignatureImage_ =
        LoadTextureImage(L"app/resources/ui/credits/taro_signature.png");
    aotoMoriSignatureImage_ =
        LoadTextureImage(L"app/resources/ui/credits/name_AotoMori.png");
    tsunaguSignatureImage_ =
        LoadTextureImage(L"app/resources/ui/credits/tsunagu_sign.png");

    StartCreditBgm();
}

void CreditScene::Update() {
    const float deltaTime = ctx_->frame.deltaTime;
    sceneTime_ += deltaTime;
    introTimer_ =
        (std::min)(introTimer_ + deltaTime, kIntroDuration + 0.2f);
    if (backgroundScene_) {
        backgroundScene_->Update();
    }

    Input *input = ctx_->systems.input;
    if (!returnRequested_ && sceneTime_ >= kCreditRollDelay) {
        const float rollSpeed =
            input != nullptr && input->IsKeyPress(DIK_SPACE)
                ? kCreditRollFastSpeed
                : kCreditRollSpeed;
        creditRollDistance_ += deltaTime * rollSpeed;
    }

    if (returnRequested_) {
        transitionTimer_ += deltaTime;
        if (transitionTimer_ >= kTransitionDuration) {
            sceneManager_->ChangeScene(CreateReturnScene());
        }
        return;
    }

    if (input != nullptr && input->IsKeyTrigger(DIK_ESCAPE)) {
        AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Cancel);
        BeginReturnToTitle();
    }
}

void CreditScene::Draw() {
    const float screenWidth =
        static_cast<float>(ctx_->systems.winApp->GetWidth());
    const float screenHeight =
        static_cast<float>(ctx_->systems.winApp->GetHeight());

    if (backgroundScene_) {
        backgroundScene_->Draw();
    }

    ctx_->rendering.sprite->PreDraw();
    DrawOverlay(screenWidth, screenHeight);
    DrawCredits(screenWidth, screenHeight);
    DrawTransition(screenWidth, screenHeight);
    ctx_->rendering.sprite->PostDraw();
}

CreditScene::Image CreditScene::LoadTextureImage(const std::wstring &path) {
    Image image{};
    image.textureId = ctx_->rendering.texture->Load(path);
    image.width =
        static_cast<float>(ctx_->rendering.texture->GetWidth(image.textureId));
    image.height =
        static_cast<float>(ctx_->rendering.texture->GetHeight(image.textureId));
    return image;
}

void CreditScene::LoadCreditLineImages() {
    creditLines_.clear();
    creditLines_.reserve(kCreditLineSpecs.size());
    for (const CreditLineSpec &spec : kCreditLineSpecs) {
        creditLines_.push_back(
            {LoadTextureImage(spec.path), spec.centerY, spec.scale});
    }
}

void CreditScene::BeginReturnToTitle() {
    returnRequested_ = true;
    transitionTimer_ = 0.0f;
}

void CreditScene::DrawOverlay(float screenWidth, float screenHeight) {
    const float intro = Smooth01(introTimer_ / kIntroDuration);
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
             MakeColor(0.0f, 0.0f, 0.0f, 0.76f + (1.0f - intro) * 0.24f));
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight * 0.20f,
             MakeColor(0.0f, 0.0f, 0.0f, 0.34f * intro));
    DrawRect(0.0f, screenHeight * 0.78f, screenWidth, screenHeight * 0.22f,
             MakeColor(0.0f, 0.0f, 0.0f, 0.38f * intro));
}

void CreditScene::DrawCredits(float screenWidth, float screenHeight) {
    const float intro = Smooth01((introTimer_ - 0.08f) / 0.24f);

    const float bodyScale =
        std::clamp(screenWidth * 0.42f / kCreditVirtualWidth, 0.54f, 0.86f);
    const float bodyW = kCreditVirtualWidth * bodyScale;
    const float textCenterX = screenWidth * 0.5f;
    const float rollStartY = screenHeight + 42.0f;
    const float rollY = rollStartY - creditRollDistance_;
    float lastTextBottom = 0.0f;
    for (const CreditLine &line : creditLines_) {
        const float lineScale = bodyScale * line.scale;
        lastTextBottom =
            (std::max)(lastTextBottom,
                       (line.centerY + line.image.height * line.scale * 0.5f) *
                           bodyScale);
        const float lineX =
            textCenterX - line.image.width * lineScale * 0.5f;
        const float lineY =
            rollY + line.centerY * bodyScale -
            line.image.height * lineScale * 0.5f;
        DrawImage(line.image, lineX, lineY, lineScale, intro);
    }

    const float bodyRight = (screenWidth + bodyW) * 0.5f;
    const float rightMarginW = screenWidth - bodyRight;
    const auto drawSignature = [&](const Image &image, float centerY,
                                   float minScale, float maxScale) {
        if (image.width <= 0.0f || rightMarginW <= 140.0f) {
            return;
        }
        const float signatureScale =
            std::clamp(rightMarginW * 0.56f / (std::max)(image.width, 1.0f),
                       minScale, maxScale);
        const float signatureW = image.width * signatureScale;
        const float signatureH = image.height * signatureScale;
        const float signatureCenterX = std::clamp(
            screenWidth * kSignatureColumnCenterRatio,
            bodyRight + rightMarginW * 0.22f,
            screenWidth - rightMarginW * 0.18f);
        const float signatureX = signatureCenterX - signatureW * 0.5f;
        const float signatureY =
            rollY + centerY * bodyScale - signatureH * 0.5f;
        DrawImage(image, signatureX, signatureY, signatureScale, intro);
    };
    drawSignature(taroSignatureImage_, kTaroLineCenterY, 0.38f,
                  kSignatureMaxScale);
    drawSignature(aotoMoriSignatureImage_, kAotoMoriLineCenterY, 0.16f, 0.42f);
    drawSignature(tsunaguSignatureImage_, kTsunaguLineCenterY, 0.24f, 0.46f);

    if (logoImage_.width > 0.0f && logoImage_.height > 0.0f) {
        const float logoScale =
            (std::min)({1.0f,
                        (screenWidth * 0.72f) /
                            (std::max)(logoImage_.width, 1.0f),
                        (screenHeight * 0.32f) /
                            (std::max)(logoImage_.height, 1.0f)});
        const float logoW = logoImage_.width * logoScale;
        const float logoH = logoImage_.height * logoScale;
        const float logoDelayDistance =
            rollStartY + lastTextBottom + kLogoStartAfterTextPadding;
        const float logoRollDistance =
            (std::max)(0.0f, creditRollDistance_ - logoDelayDistance);
        const float logoCenterY =
            (std::max)(screenHeight * 0.5f,
                       screenHeight + kLogoStartBelowScreenPadding -
                           logoRollDistance);
        DrawImage(logoImage_, (screenWidth - logoW) * 0.5f,
                  logoCenterY - logoH * 0.5f, logoScale, intro);

        if (thankYouImage_.width > 0.0f && thankYouImage_.height > 0.0f) {
            const float messageScale =
                (std::min)({0.82f,
                            (screenWidth * 0.58f) /
                                (std::max)(thankYouImage_.width, 1.0f),
                            (screenHeight * 0.12f) /
                                (std::max)(thankYouImage_.height, 1.0f)});
            const float messageW = thankYouImage_.width * messageScale;
            const float messageY =
                logoCenterY + logoH * 0.5f + 18.0f * messageScale;
            DrawImage(thankYouImage_, (screenWidth - messageW) * 0.5f,
                      messageY, messageScale, intro);
        }
    }
}

void CreditScene::DrawTransition(float screenWidth, float screenHeight) {
    const float introFade =
        1.0f - Smooth01((sceneTime_ - 0.08f) / kSceneFadeInDuration);
    if (introFade > 0.0f) {
        DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
                 MakeColor(0.0f, 0.0f, 0.0f, introFade));
    }

    if (!returnRequested_) {
        return;
    }
    const float t =
        std::clamp(transitionTimer_ / kTransitionDuration, 0.0f, 1.0f);
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
             MakeColor(0.0f, 0.0f, 0.0f, SmoothStep(t)));
}

void CreditScene::DrawRect(float x, float y, float w, float h,
                           const XMFLOAT4 &color) {
    Sprite sprite{};
    sprite.position = {x, y};
    sprite.size = {w, h};
    sprite.color = color;
    sprite.textureId = 0;
    ctx_->rendering.sprite->DrawSprite(sprite);
}

void CreditScene::DrawFrame(float x, float y, float w, float h,
                            float thickness, const XMFLOAT4 &color) {
    DrawRect(x, y, w, thickness, color);
    DrawRect(x, y + h - thickness, w, thickness, color);
    DrawRect(x, y, thickness, h, color);
    DrawRect(x + w - thickness, y, thickness, h, color);
}

void CreditScene::DrawImage(const Image &image, float x, float y, float scale,
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

void CreditScene::StartCreditBgm() {
    if (ctx_ == nullptr || ctx_->systems.sound == nullptr ||
        creditBgmVoiceHandle_ != SoundManager::kInvalidVoiceHandle) {
        return;
    }

    creditBgmSoundId_ = ctx_->systems.sound->LoadOrCreateSilent(
        L"app/resources/audio/bgm/bgm_TitleTheme.wav");
    creditBgmVoiceHandle_ =
        ctx_->systems.sound->Play(creditBgmSoundId_,
                                  0.24f * AppSceneServices::GetBgmVolume(),
                                  true);
}

void CreditScene::StopCreditBgm() {
    if (ctx_ == nullptr || ctx_->systems.sound == nullptr ||
        creditBgmVoiceHandle_ == SoundManager::kInvalidVoiceHandle) {
        return;
    }

    ctx_->systems.sound->Stop(creditBgmVoiceHandle_);
    creditBgmVoiceHandle_ = SoundManager::kInvalidVoiceHandle;
}

std::unique_ptr<BaseScene> CreditScene::CreateReturnScene() const {
    switch (returnTarget_) {
    case ReturnTarget::WeaponSelect:
        return std::make_unique<WeaponSelectScene>();
    case ReturnTarget::TutorialSelect:
        return std::make_unique<TutorialSelectScene>();
    case ReturnTarget::Title:
    default:
        return std::make_unique<TitleScene>();
    }
}
