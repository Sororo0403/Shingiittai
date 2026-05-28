#pragma once
#include "BaseScene.h"
#include "GameScene.h"
#include "SoundManager.h"
#include <DirectXMath.h>
#include <array>
#include <cstdint>
#include <memory>
#include <string>

class SoundTestScene : public BaseScene {
  public:
    enum class ReturnTarget {
        WeaponSelect,
        TutorialSelect,
    };

    explicit SoundTestScene(ReturnTarget returnTarget);
    ~SoundTestScene() override;

    void Initialize(const SceneContext &ctx) override;
    void Update() override;
    void Draw() override;
    void DrawTransparent() override {}

  private:
    struct Image {
        uint32_t textureId = 0;
        float width = 0.0f;
        float height = 0.0f;
    };

    struct Track {
        const wchar_t *path;
        const wchar_t *labelPath;
        uint32_t soundId = SoundManager::kInvalidSoundId;
        Image label{};
    };

    Image LoadTextureImage(const std::wstring &path);
    void BeginReturn();
    void PlaySelectedTrack();
    void ToggleSelectedTrackPlayback();
    void StopPlayingTrack();
    void UpdateVisualizer(float deltaTime);
    void DrawOverlay(float screenWidth, float screenHeight);
    void DrawAudioVisualizer(float screenWidth, float screenHeight);
    void DrawPanel(float screenWidth, float screenHeight);
    void DrawMusicIcon(float centerX, float centerY, float size, float alpha,
                       bool playing);
    void DrawControlsPrompt(float screenWidth, float screenHeight);
    void DrawTransition(float screenWidth, float screenHeight);
    void DrawRect(float x, float y, float w, float h,
                  const DirectX::XMFLOAT4 &color);
    void DrawFrame(float x, float y, float w, float h, float thickness,
                   const DirectX::XMFLOAT4 &color);
    void DrawImage(const Image &image, float x, float y, float scale = 1.0f,
                   float alpha = 1.0f);
    std::unique_ptr<BaseScene> CreateReturnScene() const;

    static constexpr size_t kTrackCount = 4;

    ReturnTarget returnTarget_ = ReturnTarget::WeaponSelect;
    std::unique_ptr<GameScene> backgroundScene_;
    Image titleImage_{};
    Image controlsImage_{};
    Image playingStatusImage_{};
    Image keyAImage_{};
    Image keyDImage_{};
    std::array<Track, kTrackCount> tracks_{};
    static constexpr size_t kVisualizerBarCount = 56;
    std::array<float, kVisualizerBarCount> visualizerBars_{};
    float visualizerEnergy_ = 0.0f;
    int selectedIndex_ = 0;
    int playingIndex_ = -1;
    uint32_t playingVoiceHandle_ = SoundManager::kInvalidVoiceHandle;
    bool playbackPaused_ = false;
    float sceneTime_ = 0.0f;
    float introTimer_ = 0.0f;
    float transitionTimer_ = 0.0f;
    bool returnRequested_ = false;
};
