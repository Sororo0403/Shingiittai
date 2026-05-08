#pragma once
#include "BaseScene.h"
#include "Camera.h"
#include "EnemyActionData.h"
#include "Transform.h"
#include <cstdint>
#include <string>
#include <vector>

enum class ActionStep;

class EnemyAnimationDebugScene : public BaseScene {
  public:
    void Initialize(const SceneContext &ctx) override;
    void Update() override;
    void Draw() override;
    void DrawOverlay() override;

  private:
    enum class PreviewMode { Clip, Skill };

    void PlaySelectedAnimation();
    void PlaySelectedSkillAnimation();
    void RestartSelectedSkill();
    void SelectPreviewMode(PreviewMode mode);
    void SelectNextItem(int direction);
    void UpdateClipPreview(float animationDelta);
    void UpdateSkillPreview(float animationDelta);
    void ApplySelectedSkillPose();
    ActionStep GetSelectedSkillStep(float skillTime) const;
    float GetSelectedSkillDuration() const;
    void UpdateCamera();
    void UpdateLighting();

  private:
    Camera camera_{};
    uint32_t enemyModelId_ = 0;
    uint32_t floorModelId_ = 0;
    uint32_t floorTextureId_ = 0;
    Transform enemyTf_{};
    Transform floorTf_{};
    std::vector<std::string> animationNames_{};
    int selectedAnimationIndex_ = 0;
    int selectedSkillIndex_ = 0;
    PreviewMode previewMode_ = PreviewMode::Clip;
    bool animationLoop_ = true;
    bool animationPaused_ = false;
    bool skillPhase2Preview_ = true;
    float playbackSpeed_ = 1.0f;
    float skillTimer_ = 0.0f;
    float cameraYaw_ = 0.0f;
    float cameraDistance_ = 13.5f;
    float sceneTime_ = 0.0f;
};
