#include "SoundTestScene.h"
#include "AppSceneServices.h"
#include "Input.h"
#include "SceneManager.h"
#include "Sprite.h"
#include "SpriteManager.h"
#include "TextureManager.h"
#include "TutorialSelectScene.h"
#include "WeaponSelectScene.h"
#include "WinApp.h"
#include <algorithm>
#include <cmath>
#include <memory>

using namespace DirectX;

namespace {
constexpr float kIntroDuration = 1.10f;
constexpr float kBackgroundRevealDuration = 0.84f;
constexpr float kBarIntroDelay = 0.24f;
constexpr float kBarIntroDuration = 0.62f;
constexpr float kContentFadeDelay = 0.48f;
constexpr float kContentFadeDuration = 0.50f;
constexpr float kTransitionDuration = 0.16f;
constexpr float kControlsPadding = 32.0f;

XMFLOAT4 Color(float r, float g, float b, float a = 1.0f) {
    return {r, g, b, a};
}

float SmoothStep(float t) { return t * t * (3.0f - 2.0f * t); }

float Smooth01(float t) {
    return SmoothStep(std::clamp(t, 0.0f, 1.0f));
}
} // namespace

SoundTestScene::SoundTestScene(ReturnTarget returnTarget)
    : returnTarget_(returnTarget) {}

SoundTestScene::~SoundTestScene() { StopPlayingTrack(); }

void SoundTestScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    AppSceneServices::RequestHandTrackingStop();
    AppSceneServices::StopMenuBgm(&ctx);
    sceneTime_ = 0.0f;
    introTimer_ = 0.0f;
    transitionTimer_ = 0.0f;
    selectedIndex_ = 0;
    playingIndex_ = -1;
    playingVoiceHandle_ = SoundManager::kInvalidVoiceHandle;
    playbackPaused_ = false;
    visualizerBars_.fill(0.0f);
    visualizerEnergy_ = 0.0f;
    returnRequested_ = false;

    backgroundScene_ =
        std::make_unique<GameScene>(GameScene::Mode::BackgroundOnly);
    backgroundScene_->Initialize(ctx);

    titleImage_ =
        LoadTextureImage(L"app/resources/ui/sound_test/title.png");
    controlsImage_ =
        LoadTextureImage(L"app/resources/ui/sound_test/controls.png");
    playingStatusImage_ =
        LoadTextureImage(L"app/resources/ui/sound_test/playing.png");
    authorImage_ =
        LoadTextureImage(L"app/resources/ui/sound_test/author_aotomori.png");
    keyAImage_ = LoadTextureImage(L"app/resources/ui/sound_test/key_a.png");
    keyDImage_ = LoadTextureImage(L"app/resources/ui/sound_test/key_d.png");
    tracks_ = {{
        {L"app/resources/audio/bgm/bgm_TitleTheme.wav",
         L"app/resources/ui/sound_test/track_title.png", true, 0.28f},
        {L"app/resources/audio/bgm/bgm_MenuTheme.wav",
         L"app/resources/ui/sound_test/track_menu.png", true, 0.28f},
        {L"app/resources/audio/bgm/bgm_TutorialTheme.wav",
         L"app/resources/ui/sound_test/track_tutorial.png", true, 0.28f},
        {L"app/resources/audio/bgm/bgm_Battle.wav",
         L"app/resources/ui/sound_test/track_battle.png", true, 0.28f},
        {L"app/resources/audio/se/combat/se_MetalSound.wav",
         L"app/resources/ui/sound_test/track_se_metal_sound.png", false, 0.84f},
        {L"app/resources/audio/se/combat/se_Shot.wav",
         L"app/resources/ui/sound_test/track_se_shot.png", false, 0.84f},
        {L"app/resources/audio/se/combat/se_Slash.wav",
         L"app/resources/ui/sound_test/track_se_slash.png", false, 0.84f},
        {L"app/resources/audio/se/ui/se_Cancel.mp3",
         L"app/resources/ui/sound_test/track_se_cancel.png", false, 0.95f},
        {L"app/resources/audio/se/ui/se_Select.mp3",
         L"app/resources/ui/sound_test/track_se_select.png", false, 0.95f},
        {L"app/resources/audio/se/ui/se_Selected.mp3",
         L"app/resources/ui/sound_test/track_se_selected.png", false, 0.95f},
    }};

    for (Track &track : tracks_) {
        track.soundId = ctx_->systems.sound != nullptr
                            ? ctx_->systems.sound->LoadOrCreateSilent(track.path)
                            : SoundManager::kInvalidSoundId;
        track.label = LoadTextureImage(track.labelPath);
    }
}

void SoundTestScene::Update() {
    sceneTime_ += ctx_->frame.deltaTime;
    introTimer_ = (std::min)(introTimer_ + ctx_->frame.deltaTime,
                             kIntroDuration + 0.2f);
    if (backgroundScene_) {
        backgroundScene_->Update();
    }
    RefreshPlayingState();
    UpdateVisualizer(ctx_->frame.deltaTime);

    if (returnRequested_) {
        transitionTimer_ += ctx_->frame.deltaTime;
        if (transitionTimer_ >= kTransitionDuration) {
            sceneManager_->ChangeScene(CreateReturnScene());
        }
        return;
    }

    Input *input = ctx_->systems.input;
    if (input == nullptr) {
        return;
    }

    if (input->IsKeyTrigger(DIK_ESCAPE)) {
        BeginReturn();
        return;
    }
    if (input->IsKeyTrigger(DIK_A) || input->IsKeyTrigger(DIK_LEFT)) {
        selectedIndex_ =
            (selectedIndex_ + static_cast<int>(kTrackCount) - 1) %
            static_cast<int>(kTrackCount);
        StopPlayingTrack();
    }
    if (input->IsKeyTrigger(DIK_D) || input->IsKeyTrigger(DIK_RIGHT)) {
        selectedIndex_ = (selectedIndex_ + 1) % static_cast<int>(kTrackCount);
        StopPlayingTrack();
    }
    if (input->IsKeyTrigger(DIK_SPACE) || input->IsKeyTrigger(DIK_RETURN)) {
        ToggleSelectedTrackPlayback();
    }
}

void SoundTestScene::Draw() {
    const float screenWidth =
        static_cast<float>(ctx_->systems.winApp->GetWidth());
    const float screenHeight =
        static_cast<float>(ctx_->systems.winApp->GetHeight());

    if (backgroundScene_) {
        backgroundScene_->Draw();
    }

    ctx_->rendering.sprite->PreDraw();
    DrawOverlay(screenWidth, screenHeight);
    DrawAudioVisualizer(screenWidth, screenHeight);
    DrawPanel(screenWidth, screenHeight);
    DrawControlsPrompt(screenWidth, screenHeight);
    DrawTransition(screenWidth, screenHeight);
    ctx_->rendering.sprite->PostDraw();
}

SoundTestScene::Image
SoundTestScene::LoadTextureImage(const std::wstring &path) {
    Image image{};
    image.textureId = ctx_->rendering.texture->Load(path);
    image.width =
        static_cast<float>(ctx_->rendering.texture->GetWidth(image.textureId));
    image.height =
        static_cast<float>(ctx_->rendering.texture->GetHeight(image.textureId));
    return image;
}

void SoundTestScene::BeginReturn() {
    returnRequested_ = true;
    transitionTimer_ = 0.0f;
}

void SoundTestScene::PlaySelectedTrack() {
    if (ctx_ == nullptr || ctx_->systems.sound == nullptr) {
        return;
    }
    StopPlayingTrack();

    const Track &track = tracks_[static_cast<size_t>(selectedIndex_)];
    if (track.soundId == SoundManager::kInvalidSoundId) {
        return;
    }

    const float volume = track.baseVolume *
                         (track.loop ? AppSceneServices::GetBgmVolume()
                                     : AppSceneServices::GetSeVolume());
    playingVoiceHandle_ =
        ctx_->systems.sound->Play(track.soundId, volume, track.loop);
    playingIndex_ = selectedIndex_;
    playbackPaused_ = false;
}

void SoundTestScene::ToggleSelectedTrackPlayback() {
    if (ctx_ == nullptr || ctx_->systems.sound == nullptr) {
        return;
    }

    const bool selectedTrackActive =
        playingVoiceHandle_ != SoundManager::kInvalidVoiceHandle &&
        playingIndex_ == selectedIndex_;
    if (!selectedTrackActive) {
        PlaySelectedTrack();
        return;
    }

    if (playbackPaused_) {
        ctx_->systems.sound->Resume(playingVoiceHandle_);
        playbackPaused_ = false;
    } else {
        ctx_->systems.sound->Pause(playingVoiceHandle_);
        playbackPaused_ = true;
    }
}

void SoundTestScene::StopPlayingTrack() {
    if (ctx_ == nullptr || ctx_->systems.sound == nullptr ||
        playingVoiceHandle_ == SoundManager::kInvalidVoiceHandle) {
        return;
    }

    ctx_->systems.sound->Stop(playingVoiceHandle_);
    playingVoiceHandle_ = SoundManager::kInvalidVoiceHandle;
    playingIndex_ = -1;
    playbackPaused_ = false;
}

void SoundTestScene::RefreshPlayingState() {
    if (ctx_ == nullptr || ctx_->systems.sound == nullptr ||
        playingVoiceHandle_ == SoundManager::kInvalidVoiceHandle ||
        playbackPaused_) {
        return;
    }

    if (ctx_->systems.sound->IsPlaying(playingVoiceHandle_)) {
        return;
    }

    playingVoiceHandle_ = SoundManager::kInvalidVoiceHandle;
    playingIndex_ = -1;
}

void SoundTestScene::UpdateVisualizer(float deltaTime) {
    const bool hasPlayingTrack =
        ctx_ != nullptr && ctx_->systems.sound != nullptr &&
        playingVoiceHandle_ != SoundManager::kInvalidVoiceHandle &&
        playingIndex_ >= 0 &&
        !playbackPaused_ &&
        ctx_->systems.sound->IsPlaying(playingVoiceHandle_);

    float targetEnergy = 0.0f;
    if (hasPlayingTrack) {
        const Track &track = tracks_[static_cast<size_t>(playingIndex_)];
        const float position =
            ctx_->systems.sound->GetPlaybackPosition(playingVoiceHandle_);
        const float volume =
            ctx_->systems.sound->GetVoiceVolume(playingVoiceHandle_);
        std::array<float, kVisualizerBarCount> spectrum{};
        ctx_->systems.sound->FillSpectrumBands(track.soundId, position,
                                               spectrum.data(),
                                               spectrum.size());
        for (size_t i = 0; i < visualizerBars_.size(); ++i) {
            const float band = static_cast<float>(i) /
                               static_cast<float>(visualizerBars_.size() - 1);
            const float wave =
                0.5f + 0.5f * std::sinf(sceneTime_ * (2.0f + band * 5.4f) +
                                         band * 9.0f);
            const float shaped = std::clamp(
                std::pow(spectrum[i] * volume, 0.68f) *
                    (1.65f + wave * 0.42f),
                0.0f, 1.0f);
            const float rise = std::clamp(deltaTime * 42.0f, 0.0f, 1.0f);
            const float fall = std::clamp(deltaTime * 10.0f, 0.0f, 1.0f);
            const float blend = shaped > visualizerBars_[i] ? rise : fall;
            visualizerBars_[i] += (shaped - visualizerBars_[i]) * blend;
            targetEnergy += visualizerBars_[i];
        }
        targetEnergy /= static_cast<float>(visualizerBars_.size());
    } else {
        for (float &bar : visualizerBars_) {
            bar += (0.0f - bar) * std::clamp(deltaTime * 3.0f, 0.0f, 1.0f);
        }
    }

    visualizerEnergy_ +=
        (targetEnergy - visualizerEnergy_) *
        std::clamp(deltaTime * 7.5f, 0.0f, 1.0f);
}

void SoundTestScene::DrawOverlay(float screenWidth, float screenHeight) {
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

void SoundTestScene::DrawAudioVisualizer(float screenWidth,
                                         float screenHeight) {
    const float intro = Smooth01((introTimer_ - kBarIntroDelay) /
                                 kBarIntroDuration);
    if (intro <= 0.0f) {
        return;
    }

    const bool playingCurrent = playingIndex_ >= 0 && !playbackPaused_;
    const float barAreaW = screenWidth * 0.88f;
    const float barGap = 4.0f;
    const float barW =
        (barAreaW - barGap * static_cast<float>(visualizerBars_.size() - 1)) /
        static_cast<float>(visualizerBars_.size());
    const float startX = (screenWidth - barAreaW) * 0.5f;
    const float baseY = screenHeight * 0.82f;
    const float maxH = screenHeight * 0.42f;
    for (size_t i = 0; i < visualizerBars_.size(); ++i) {
        const float mirror =
            1.0f - std::abs(static_cast<float>(i) -
                            static_cast<float>(visualizerBars_.size() - 1) *
                                0.5f) /
                       (static_cast<float>(visualizerBars_.size() - 1) * 0.5f);
        const float beat =
            playingCurrent
                ? visualizerBars_[i]
                : 0.018f + 0.020f *
                             (0.5f + 0.5f * std::sinf(sceneTime_ * 1.8f +
                                                      static_cast<float>(i)));
        const float height =
            std::clamp(maxH * (0.018f + std::pow(beat, 1.18f) * 1.46f) *
                           (0.58f + 0.62f * mirror),
                       1.0f, maxH) *
            intro;
        if (height <= 1.0f) {
            continue;
        }
        const float x = startX + static_cast<float>(i) * (barW + barGap);
        const XMFLOAT4 color =
            i % 3 == 0
                ? Color(1.0f, 0.62f, 0.18f,
                        intro * (0.22f + beat * 0.62f))
                : Color(0.00f, 0.86f, 0.78f,
                        intro * (0.20f + beat * 0.70f));
        DrawRect(x, baseY - height, barW, height, color);
        DrawRect(x, baseY + 10.0f, barW, height * 0.52f,
                 Color(color.x, color.y, color.z, color.w * 0.34f));
    }
}

void SoundTestScene::DrawPanel(float screenWidth, float screenHeight) {
    const float intro =
        Smooth01((introTimer_ - kContentFadeDelay) / kContentFadeDuration);
    if (intro <= 0.0f) {
        return;
    }
    const float panelW = std::clamp(screenWidth * 0.58f, 660.0f, 900.0f);
    const float panelH = std::clamp(screenHeight * 0.58f, 400.0f, 540.0f);
    const float x = (screenWidth - panelW) * 0.5f;
    const float y =
        (screenHeight - panelH) * 0.5f + (1.0f - intro) * 30.0f;

    DrawRect(x + 12.0f, y + 14.0f, panelW, panelH,
             Color(0.0f, 0.0f, 0.0f, 0.34f * intro));
    DrawRect(x, y, panelW, panelH, Color(0.016f, 0.019f, 0.023f, 0.88f * intro));
    DrawFrame(x, y, panelW, panelH, 2.0f,
              Color(0.95f, 0.72f, 0.28f, 0.70f * intro));
    DrawRect(x + 12.0f, y + 12.0f, panelW - 24.0f, 2.0f,
             Color(1.0f, 0.84f, 0.38f, 0.22f * intro));
    DrawRect(x + 12.0f, y + panelH - 14.0f, panelW - 24.0f, 2.0f,
             Color(0.00f, 0.86f, 0.78f, 0.18f * intro));

    const float titleAreaW = panelW * 0.58f;
    const float titleAreaH = panelH * 0.17f;
    const float titleAreaY = y + panelH * 0.075f;
    const float titleScale =
        std::clamp((titleAreaW * 0.72f) / (std::max)(titleImage_.width, 1.0f),
                   0.82f, 1.26f);
    const float titleW = titleImage_.width * titleScale;
    const float titleH = titleImage_.height * titleScale;
    DrawImage(titleImage_, x + (panelW - titleW) * 0.5f,
              titleAreaY + (titleAreaH - titleH) * 0.5f,
              titleScale, 0.94f * intro);

    const bool playingCurrent =
        playingIndex_ == selectedIndex_ && !playbackPaused_;
    const XMFLOAT4 cardAccent =
        playingCurrent ? Color(0.00f, 0.86f, 0.78f, 0.70f * intro)
                       : Color(1.0f, 0.78f, 0.34f, 0.70f * intro);
    const XMFLOAT4 cardAccentDim =
        playingCurrent ? Color(0.00f, 0.86f, 0.78f, 0.34f * intro)
                       : Color(0.95f, 0.72f, 0.28f, 0.34f * intro);
    const float cardW = std::clamp(panelW * 0.72f, 500.0f, 680.0f);
    const float cardH = std::clamp(panelH * 0.36f, 150.0f, 205.0f);
    const float cardX = x + (panelW - cardW) * 0.5f;
    const float cardY = y + panelH * 0.34f;
    DrawRect(cardX + 9.0f, cardY + 10.0f, cardW, cardH,
             Color(0.0f, 0.0f, 0.0f, 0.28f * intro));
    DrawRect(cardX, cardY, cardW, cardH,
             Color(0.036f, 0.042f, 0.052f, 0.84f * intro));
    DrawFrame(cardX, cardY, cardW, cardH, 2.0f, cardAccent);
    DrawRect(cardX, cardY, cardW, cardH * 0.18f,
             playingCurrent ? Color(0.00f, 0.16f, 0.15f, 0.28f * intro)
                            : Color(0.12f, 0.08f, 0.03f, 0.22f * intro));
    DrawRect(cardX, cardY + cardH - 3.0f, cardW, 3.0f,
             cardAccent);

    const float iconSize = std::clamp(cardH * 0.58f, 82.0f, 118.0f);
    const float iconX = cardX + cardW * 0.24f;
    const float iconY = cardY + cardH * 0.50f;
    const float iconBoxSize = iconSize * 1.18f;
    DrawRect(iconX - iconBoxSize * 0.5f, iconY - iconBoxSize * 0.5f,
             iconBoxSize, iconBoxSize,
             playingCurrent ? Color(0.015f, 0.11f, 0.10f, 0.64f * intro)
                            : Color(0.018f, 0.020f, 0.026f, 0.66f * intro));
    DrawFrame(iconX - iconBoxSize * 0.5f, iconY - iconBoxSize * 0.5f,
              iconBoxSize, iconBoxSize, 2.0f,
              playingCurrent ? Color(0.00f, 0.86f, 0.78f, 0.62f * intro)
                             : Color(0.95f, 0.72f, 0.28f, 0.36f * intro));
    const Track &selectedTrack = tracks_[static_cast<size_t>(selectedIndex_)];
    if (selectedTrack.loop) {
        DrawMusicIcon(iconX, iconY, iconSize, intro, playingCurrent);
    } else {
        DrawSpeakerIcon(iconX, iconY, iconSize, intro, playingCurrent);
    }

    const Image &label = selectedTrack.label;
    const float labelScale =
        (std::min)({1.0f,
                    (cardH * 0.34f) / (std::max)(label.height, 1.0f),
                    (cardW * 0.46f) / (std::max)(label.width, 1.0f)});
    const float labelW = label.width * labelScale;
    const float labelH = label.height * labelScale;
    const float labelCenterX = cardX + cardW * 0.64f;
    DrawImage(label, labelCenterX - labelW * 0.5f,
              cardY + cardH * 0.34f - labelH * 0.5f, labelScale,
              0.96f * intro);

    const float authorScale =
        (std::min)({0.56f,
                    (cardH * 0.16f) / (std::max)(authorImage_.height, 1.0f),
                    (cardW * 0.34f) / (std::max)(authorImage_.width, 1.0f)});
    const float authorW = authorImage_.width * authorScale;
    const float authorH = authorImage_.height * authorScale;
    DrawImage(authorImage_, labelCenterX - authorW * 0.5f,
              cardY + cardH * 0.55f - authorH * 0.5f, authorScale,
              0.74f * intro);

    DrawRect(cardX + cardW * 0.45f, cardY + cardH * 0.67f, cardW * 0.36f,
             2.0f,
             playingCurrent ? cardAccentDim
                            : Color(0.62f, 0.66f, 0.72f, 0.20f * intro));
    const float keyScale = std::clamp(cardH * 0.32f /
                                          (std::max)(keyAImage_.height, 1.0f),
                                      0.38f, 0.58f);
    const float keyW = keyAImage_.width * keyScale;
    const float keyH = keyAImage_.height * keyScale;
    const float keyY = cardY + cardH * 0.50f - keyH * 0.5f;
    const float keyGap = 18.0f;
    const float keyPanelPadding = 14.0f;
    const float keyAX =
        (std::max)(x + keyPanelPadding, cardX - keyW - keyGap);
    const float keyDX =
        (std::min)(x + panelW - keyPanelPadding - keyW, cardX + cardW + keyGap);
    DrawImage(keyAImage_, keyAX, keyY, keyScale, 0.74f * intro);
    DrawImage(keyDImage_, keyDX, keyY, keyScale, 0.74f * intro);

    if (playingCurrent) {
        const float statusScale =
            (std::min)({0.82f,
                        (cardH * 0.22f) /
                            (std::max)(playingStatusImage_.height, 1.0f),
                        (cardW * 0.36f) /
                            (std::max)(playingStatusImage_.width, 1.0f)});
        const float statusW = playingStatusImage_.width * statusScale;
        const float statusH = playingStatusImage_.height * statusScale;
        const float statusX = labelCenterX - statusW * 0.5f;
        const float statusY = cardY + cardH * 0.80f - statusH * 0.5f;
        DrawRect(statusX - 18.0f, statusY + statusH * 0.5f - 5.0f,
                 10.0f, 10.0f, Color(0.00f, 0.86f, 0.78f, 0.86f * intro));
        DrawImage(playingStatusImage_, statusX, statusY, statusScale,
                  0.82f * intro);
    }
}

void SoundTestScene::DrawMusicIcon(float centerX, float centerY, float size,
                                   float alpha, bool playing) {
    const XMFLOAT4 line =
        playing ? Color(0.00f, 0.86f, 0.78f, 0.90f * alpha)
                : Color(1.0f, 0.78f, 0.34f, 0.82f * alpha);
    const XMFLOAT4 fill =
        playing ? Color(0.00f, 0.20f, 0.18f, 0.40f * alpha)
                : Color(0.16f, 0.12f, 0.070f, 0.38f * alpha);
    const float s = size / 100.0f;
    auto x = [&](float v) { return centerX + v * s; };
    auto y = [&](float v) { return centerY + v * s; };
    auto r = [&](float px, float py, float w, float h,
                 const XMFLOAT4 &color) {
        DrawRect(x(px), y(py), w * s, h * s, color);
    };
    auto f = [&](float px, float py, float w, float h) {
        DrawFrame(x(px), y(py), w * s, h * s, 4.0f * s, line);
    };

    r(-12.0f, -42.0f, 12.0f, 70.0f, line);
    r(0.0f, -42.0f, 44.0f, 10.0f, line);
    r(34.0f, -32.0f, 10.0f, 48.0f, line);
    r(-42.0f, 20.0f, 34.0f, 26.0f, fill);
    f(-42.0f, 20.0f, 34.0f, 26.0f);
    r(12.0f, 10.0f, 34.0f, 26.0f, fill);
    f(12.0f, 10.0f, 34.0f, 26.0f);

    if (!playing) {
        return;
    }

    for (int i = 0; i < 5; ++i) {
        const float t = sceneTime_ * 4.5f + static_cast<float>(i) * 0.75f;
        const float barH =
            (14.0f + (std::sinf(t) * 0.5f + 0.5f) * 28.0f) * s;
        const float barX =
            centerX + (-34.0f + static_cast<float>(i) * 17.0f) * s;
        DrawRect(barX, centerY + 56.0f * s - barH, 7.0f * s, barH,
                 Color(0.00f, 0.86f, 0.78f, 0.62f * alpha));
    }
}

void SoundTestScene::DrawSpeakerIcon(float centerX, float centerY, float size,
                                     float alpha, bool playing) {
    const XMFLOAT4 line =
        playing ? Color(0.00f, 0.86f, 0.78f, 0.90f * alpha)
                : Color(1.0f, 0.78f, 0.34f, 0.82f * alpha);
    const XMFLOAT4 fill =
        playing ? Color(0.00f, 0.20f, 0.18f, 0.40f * alpha)
                : Color(0.16f, 0.12f, 0.070f, 0.38f * alpha);
    const float s = size / 100.0f;
    auto x = [&](float v) { return centerX + v * s; };
    auto y = [&](float v) { return centerY + v * s; };
    auto r = [&](float px, float py, float w, float h,
                 const XMFLOAT4 &color) {
        DrawRect(x(px), y(py), w * s, h * s, color);
    };
    auto f = [&](float px, float py, float w, float h) {
        DrawFrame(x(px), y(py), w * s, h * s, 4.0f * s, line);
    };

    r(-44.0f, -26.0f, 20.0f, 52.0f, fill);
    f(-44.0f, -26.0f, 20.0f, 52.0f);
    r(-24.0f, -18.0f, 12.0f, 36.0f, line);
    r(-12.0f, -32.0f, 10.0f, 64.0f, line);
    r(-2.0f, -42.0f, 36.0f, 10.0f, line);
    r(-2.0f, 32.0f, 36.0f, 10.0f, line);
    r(24.0f, -32.0f, 10.0f, 74.0f, line);
    r(34.0f, -24.0f, 10.0f, 10.0f, line);
    r(34.0f, 14.0f, 10.0f, 10.0f, line);

    if (!playing) {
        return;
    }

    for (int i = 0; i < 3; ++i) {
        const float t = sceneTime_ * 5.2f + static_cast<float>(i) * 0.65f;
        const float pulse = 0.5f + 0.5f * std::sinf(t);
        const float waveX = (50.0f + static_cast<float>(i) * 12.0f) * s;
        const float waveH = (22.0f + static_cast<float>(i) * 18.0f +
                             pulse * 9.0f) *
                            s;
        DrawRect(centerX + waveX, centerY - waveH * 0.5f, 6.0f * s, waveH,
                 Color(0.00f, 0.86f, 0.78f, (0.54f - i * 0.10f) * alpha));
    }
}

void SoundTestScene::DrawControlsPrompt(float screenWidth,
                                        float screenHeight) {
    const float intro = Smooth01((introTimer_ - kContentFadeDelay - 0.10f) /
                                 (kContentFadeDuration * 0.72f));
    const float promptScale =
        (std::min)(0.80f, (screenWidth * 0.45f) /
                              (std::max)(controlsImage_.width, 1.0f));
    DrawImage(controlsImage_, kControlsPadding,
              screenHeight - (controlsImage_.height - 18.0f) * promptScale -
                  kControlsPadding,
              promptScale, 0.58f * intro);
}

void SoundTestScene::DrawTransition(float screenWidth, float screenHeight) {
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

void SoundTestScene::DrawRect(float x, float y, float w, float h,
                              const XMFLOAT4 &color) {
    Sprite sprite{};
    sprite.position = {x, y};
    sprite.size = {w, h};
    sprite.color = color;
    sprite.textureId = 0;
    ctx_->rendering.sprite->DrawSprite(sprite);
}

void SoundTestScene::DrawFrame(float x, float y, float w, float h,
                               float thickness, const XMFLOAT4 &color) {
    DrawRect(x, y, w, thickness, color);
    DrawRect(x, y + h - thickness, w, thickness, color);
    DrawRect(x, y, thickness, h, color);
    DrawRect(x + w - thickness, y, thickness, h, color);
}

void SoundTestScene::DrawImage(const Image &image, float x, float y,
                               float scale, float alpha) {
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

std::unique_ptr<BaseScene> SoundTestScene::CreateReturnScene() const {
    switch (returnTarget_) {
    case ReturnTarget::TutorialSelect:
        return std::make_unique<TutorialSelectScene>();
    case ReturnTarget::WeaponSelect:
    default:
        return std::make_unique<WeaponSelectScene>();
    }
}
