#include "EnemyAnimationDebugScene.h"
#include "DirectXCommon.h"
#include "Enemy.h"
#include "GameScene.h"
#include "Input.h"
#include "Material.h"
#include "Model.h"
#include "ModelManager.h"
#include "PostEffectRenderer.h"
#include "SceneManager.h"
#include "TextureManager.h"
#include "WinApp.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <memory>

using namespace DirectX;

namespace {
constexpr float kPi = 3.14159265f;
constexpr float kTwoPi = 6.28318530f;
const std::string kBossAnimSmash =
    "\xE7\xB8\xA6\xE6\x8C\xAF\xE3\x82\x8A\xE4\xB8\x8B\xE3\x82\x8D\xE3\x81\x97";
const std::string kBossAnimSweep =
    "\xE6\xA8\xAA\xE8\x96\x99\xE3\x81\x8E\xE6\x89\x95\xE3\x81\x84";
const std::string kBossAnimWave =
    "\xE6\xB3\xA2\xE7\x8A\xB6\xE6\x94\xBB\xE6\x92\x83";
const std::string kBossAnimShot =
    "\xE5\xBC\xBE\xE7\x99\xBA\xE5\xB0\x84";
const std::string kBossAnimTeleport =
    "\xE3\x83\x86\xE3\x83\xAC\xE3\x83\x9D\xE3\x83\xBC\xE3\x83\x88";
const std::string kBossBoneBase =
    "\xE3\x83\x9C\xE3\x83\xBC\xE3\x83\xB3";

struct SkillPreview {
    const char *name = "";
    ActionKind kind = ActionKind::None;
};

const SkillPreview kSkillPreviews[] = {
    {"Smash", ActionKind::Smash}, {"Sweep", ActionKind::Sweep},
    {"Shot", ActionKind::Shot},   {"BladeClash", ActionKind::BladeClash},
    {"Wave", ActionKind::Wave},   {"Cage", ActionKind::Cage},
    {"Nova", ActionKind::Nova},
    {"Warp", ActionKind::Warp},
    {"Stalk", ActionKind::Stalk},
};

XMFLOAT4 MakeQuat(float pitch, float yaw, float roll) {
    XMFLOAT4 q{};
    XMStoreFloat4(&q, XMQuaternionRotationRollPitchYaw(pitch, yaw, roll));
    return q;
}

Material MakeDebugFloorMaterial() {
    Material material{};
    material.color = {0.16f, 0.15f, 0.14f, 1.0f};
    material.enableTexture = 0;
    material.reflectionStrength = 0.26f;
    material.reflectionFresnelStrength = 0.18f;
    material.reflectionRoughness = 0.52f;
    return material;
}

std::string BossBoneName(const char *suffix) {
    return suffix ? (kBossBoneBase + suffix) : kBossBoneBase;
}

float Clamp01(float value) { return std::clamp(value, 0.0f, 1.0f); }

float Smooth01(float value) {
    const float t = Clamp01(value);
    return t * t * (3.0f - 2.0f * t);
}

int FindBoneIndex(const Model &model, const std::string &name) {
    const auto it = model.boneMap.find(name);
    if (it == model.boneMap.end()) {
        return -1;
    }
    return static_cast<int>(it->second);
}

XMFLOAT3 GetMatrixTranslation(const XMFLOAT4X4 &matrix) {
    return {matrix._41, matrix._42, matrix._43};
}

XMMATRIX MakePivotRotation(const XMFLOAT3 &pivot, float pitch, float yaw,
                           float roll) {
    return XMMatrixTranslation(-pivot.x, -pivot.y, -pivot.z) *
           XMMatrixRotationRollPitchYaw(pitch, yaw, roll) *
           XMMatrixTranslation(pivot.x, pivot.y, pivot.z);
}

void ApplyDeltaToBoneTree(Model &model, int boneIndex, const XMMATRIX &delta) {
    if (boneIndex < 0 ||
        static_cast<size_t>(boneIndex) >= model.skeletonSpaceMatrices.size()) {
        return;
    }

    std::vector<int> stack;
    stack.push_back(boneIndex);

    while (!stack.empty()) {
        const int current = stack.back();
        stack.pop_back();

        XMMATRIX currentMatrix =
            XMLoadFloat4x4(&model.skeletonSpaceMatrices[current]);
        XMStoreFloat4x4(&model.skeletonSpaceMatrices[current],
                        currentMatrix * delta);

        for (size_t i = 0; i < model.bones.size(); ++i) {
            if (model.bones[i].parentIndex == current) {
                stack.push_back(static_cast<int>(i));
            }
        }
    }
}

void PoseBoneTree(Model &model, int boneIndex, float pitch, float yaw,
                  float roll) {
    if (boneIndex < 0 ||
        static_cast<size_t>(boneIndex) >= model.skeletonSpaceMatrices.size()) {
        return;
    }

    const XMFLOAT3 pivot = GetMatrixTranslation(model.skeletonSpaceMatrices[boneIndex]);
    ApplyDeltaToBoneTree(model, boneIndex,
                         MakePivotRotation(pivot, pitch, yaw, roll));
}

bool HasAnimation(const Model *model, const std::string &animationName) {
    return model && model->animations.find(animationName) != model->animations.end();
}

std::string PickSkillAnimation(const Model *model, ActionKind kind) {
    switch (kind) {
    case ActionKind::Smash:
        return HasAnimation(model, kBossAnimSmash) ? kBossAnimSmash : std::string{};
    case ActionKind::Sweep:
        return HasAnimation(model, kBossAnimSweep) ? kBossAnimSweep : std::string{};
    case ActionKind::Shot:
    case ActionKind::BladeClash:
        if (HasAnimation(model, kBossAnimShot)) {
            return kBossAnimShot;
        }
        return HasAnimation(model, kBossAnimWave) ? kBossAnimWave : std::string{};
    case ActionKind::Wave:
    case ActionKind::Cage:
    case ActionKind::Nova:
        return HasAnimation(model, kBossAnimWave) ? kBossAnimWave : std::string{};
    case ActionKind::Warp:
        return HasAnimation(model, kBossAnimTeleport) ? kBossAnimTeleport
                                                      : std::string{};
    case ActionKind::Stalk:
    case ActionKind::None:
    default:
        return {};
    }
}

const char *ActionStepName(ActionStep step) {
    switch (step) {
    case ActionStep::Charge:
        return "Charge";
    case ActionStep::Active:
        return "Active";
    case ActionStep::Recovery:
        return "Recovery";
    case ActionStep::Start:
        return "Start";
    case ActionStep::Move:
        return "Move";
    case ActionStep::Hold:
        return "Hold";
    case ActionStep::End:
        return "End";
    case ActionStep::None:
    default:
        return "None";
    }
}
} // namespace

void EnemyAnimationDebugScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);

    if (ctx_->postEffectRenderer) {
        ctx_->postEffectRenderer->SetColorMode(PostEffectRenderer::ColorMode::None);
        ctx_->postEffectRenderer->SetVignettingStrength(0.18f);
        ctx_->postEffectRenderer->SetVignettingEnabled(true);
        ctx_->postEffectRenderer->SetRadialBlurStrength(0.0f);
        ctx_->postEffectRenderer->SetRandomMode(PostEffectRenderer::RandomMode::None);
        ctx_->postEffectRenderer->SetRandomStrength(0.0f);
    }

    const float aspect = static_cast<float>(ctx_->winApp->GetWidth()) /
                         static_cast<float>(ctx_->winApp->GetHeight());
    camera_.Initialize(aspect);
    camera_.SetMode(CameraMode::LookAt);
    camera_.SetPerspectiveFovDeg(58.0f);

    ctx_->dxCommon->BeginUpload();
    enemyModelId_ = ctx_->model->Load(L"app/resources/models/boss/boss.gltf");
    floorTextureId_ = ctx_->texture->Load(L"app/resources/sprites/white64x64.png");
    floorModelId_ =
        ctx_->model->CreatePlane(floorTextureId_, MakeDebugFloorMaterial());
    ctx_->dxCommon->EndUpload();
    ctx_->texture->ReleaseUploadBuffers();

    enemyTf_.position = {0.0f, -0.65f, -1.35f};
    enemyTf_.rotation = MakeQuat(0.0f, 0.0f, 0.0f);
    enemyTf_.scale = {1.0f, 1.0f, 1.0f};

    floorTf_.position = {0.0f, -0.05f, 0.0f};
    floorTf_.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
    floorTf_.scale = {18.0f, 18.0f, 1.0f};

    if (Model *enemyModel = ctx_->model->GetModel(enemyModelId_)) {
        animationNames_.reserve(enemyModel->animations.size());
        for (const auto &[name, clip] : enemyModel->animations) {
            (void)clip;
            animationNames_.push_back(name);
        }
        std::sort(animationNames_.begin(), animationNames_.end());
    }

    PlaySelectedAnimation();
    UpdateCamera();
    UpdateLighting();
}

void EnemyAnimationDebugScene::Update() {
    Input *input = ctx_->input;

    if (input->IsKeyTrigger(DIK_F7) || input->IsKeyTrigger(DIK_TAB)) {
        sceneManager_->ChangeScene(std::make_unique<GameScene>());
        return;
    }

    if (input->IsKeyTrigger(DIK_SPACE)) {
        animationPaused_ = !animationPaused_;
    }
    if (input->IsKeyTrigger(DIK_F1)) {
        SelectPreviewMode(PreviewMode::Clip);
    }
    if (input->IsKeyTrigger(DIK_F2)) {
        SelectPreviewMode(PreviewMode::Skill);
    }
    if (input->IsKeyTrigger(DIK_L)) {
        animationLoop_ = !animationLoop_;
        if (previewMode_ == PreviewMode::Clip) {
            PlaySelectedAnimation();
        }
    }
    if (input->IsKeyTrigger(DIK_P)) {
        skillPhase2Preview_ = !skillPhase2Preview_;
        ApplySelectedSkillPose();
    }
    if (input->IsKeyTrigger(DIK_R) && previewMode_ == PreviewMode::Skill) {
        RestartSelectedSkill();
    }

    const int keyToIndex[] = {DIK_1, DIK_2, DIK_3, DIK_4, DIK_5,
                              DIK_6, DIK_7, DIK_8, DIK_9};
    for (int i = 0; i < static_cast<int>(std::size(keyToIndex)); ++i) {
        if (!input->IsKeyTrigger(keyToIndex[i])) {
            continue;
        }

        if (previewMode_ == PreviewMode::Clip &&
            i < static_cast<int>(animationNames_.size())) {
            selectedAnimationIndex_ = i;
            PlaySelectedAnimation();
        } else if (previewMode_ == PreviewMode::Skill &&
                   i < static_cast<int>(std::size(kSkillPreviews))) {
            selectedSkillIndex_ = i;
            RestartSelectedSkill();
        }
    }

    if (input->IsKeyTrigger(DIK_TAB)) {
        SelectNextItem(1);
    }
    if (input->IsKeyTrigger(DIK_BACKSPACE)) {
        SelectNextItem(-1);
    }

    if (input->IsKeyPress(DIK_LEFT)) {
        cameraYaw_ -= ctx_->deltaTime * 1.6f;
    }
    if (input->IsKeyPress(DIK_RIGHT)) {
        cameraYaw_ += ctx_->deltaTime * 1.6f;
    }
    if (input->IsKeyPress(DIK_UP)) {
        cameraDistance_ -= ctx_->deltaTime * 4.0f;
    }
    if (input->IsKeyPress(DIK_DOWN)) {
        cameraDistance_ += ctx_->deltaTime * 4.0f;
    }
    cameraDistance_ = std::clamp(cameraDistance_, 7.0f, 24.0f);

    if (input->IsKeyTrigger(DIK_MINUS)) {
        playbackSpeed_ = (std::max)(0.1f, playbackSpeed_ - 0.1f);
    }
    if (input->IsKeyTrigger(DIK_EQUALS)) {
        playbackSpeed_ = (std::min)(2.5f, playbackSpeed_ + 0.1f);
    }

    const float animationDelta =
        animationPaused_ ? 0.0f : ctx_->deltaTime * playbackSpeed_;
    if (previewMode_ == PreviewMode::Clip) {
        UpdateClipPreview(animationDelta);
    } else {
        UpdateSkillPreview(animationDelta);
    }

    sceneTime_ += ctx_->deltaTime;
    UpdateCamera();
    UpdateLighting();
}

void EnemyAnimationDebugScene::Draw() {
    ctx_->model->PreDraw();
    ctx_->model->Draw(floorModelId_, floorTf_, camera_);

    ModelDrawEffect effect{};
    effect.enabled = true;
    effect.color = {1.0f, 0.42f, 0.16f, 0.38f};
    effect.intensity = 0.16f + 0.05f * std::sinf(sceneTime_ * 3.0f);
    effect.fresnelPower = 2.4f;
    effect.noiseAmount = 0.18f;
    effect.time = sceneTime_;
    ctx_->model->SetDrawEffect(effect);
    ctx_->model->Draw(enemyModelId_, enemyTf_, camera_);
    ctx_->model->ClearDrawEffect();

    ctx_->model->PostDraw();
}

void EnemyAnimationDebugScene::DrawOverlay() {
}

void EnemyAnimationDebugScene::SelectPreviewMode(PreviewMode mode) {
    if (previewMode_ == mode) {
        return;
    }

    previewMode_ = mode;
    if (previewMode_ == PreviewMode::Clip) {
        PlaySelectedAnimation();
    } else {
        RestartSelectedSkill();
    }
}

void EnemyAnimationDebugScene::SelectNextItem(int direction) {
    if (previewMode_ == PreviewMode::Clip) {
        if (animationNames_.empty()) {
            return;
        }
        const int count = static_cast<int>(animationNames_.size());
        selectedAnimationIndex_ = (selectedAnimationIndex_ + direction + count) % count;
        PlaySelectedAnimation();
        return;
    }

    const int count = static_cast<int>(std::size(kSkillPreviews));
    selectedSkillIndex_ = (selectedSkillIndex_ + direction + count) % count;
    RestartSelectedSkill();
}

void EnemyAnimationDebugScene::PlaySelectedAnimation() {
    if (animationNames_.empty()) {
        return;
    }
    selectedAnimationIndex_ =
        std::clamp(selectedAnimationIndex_, 0,
                   static_cast<int>(animationNames_.size()) - 1);
    ctx_->model->PlayAnimation(enemyModelId_,
                               animationNames_[selectedAnimationIndex_],
                               animationLoop_);
    ctx_->model->UpdateAnimation(enemyModelId_, 0.0f);
}

void EnemyAnimationDebugScene::PlaySelectedSkillAnimation() {
    selectedSkillIndex_ =
        std::clamp(selectedSkillIndex_, 0,
                   static_cast<int>(std::size(kSkillPreviews)) - 1);

    Model *enemyModel = ctx_->model->GetModel(enemyModelId_);
    const std::string clip =
        PickSkillAnimation(enemyModel, kSkillPreviews[selectedSkillIndex_].kind);
    if (clip.empty()) {
        if (enemyModel) {
            enemyModel->currentAnimation.clear();
            enemyModel->animationTime = 0.0f;
            enemyModel->isPlaying = false;
            enemyModel->animationFinished = false;
        }
        ctx_->model->UpdateAnimation(enemyModelId_, 0.0f);
        return;
    }

    ctx_->model->PlayAnimation(enemyModelId_, clip, false);
    ctx_->model->UpdateAnimation(enemyModelId_, 0.0f);
}

void EnemyAnimationDebugScene::RestartSelectedSkill() {
    skillTimer_ = 0.0f;
    PlaySelectedSkillAnimation();
    ApplySelectedSkillPose();
}

void EnemyAnimationDebugScene::UpdateClipPreview(float animationDelta) {
    ctx_->model->UpdateAnimation(enemyModelId_, animationDelta);
}

void EnemyAnimationDebugScene::UpdateSkillPreview(float animationDelta) {
    const float duration = GetSelectedSkillDuration();
    if (animationDelta > 0.0f) {
        skillTimer_ += animationDelta;
        if (skillTimer_ >= duration) {
            skillTimer_ = std::fmod(skillTimer_, duration);
            PlaySelectedSkillAnimation();
        }
    }

    ctx_->model->UpdateAnimation(enemyModelId_, animationDelta);
    ApplySelectedSkillPose();
}

float EnemyAnimationDebugScene::GetSelectedSkillDuration() const {
    const ActionKind kind = kSkillPreviews[selectedSkillIndex_].kind;
    switch (kind) {
    case ActionKind::Nova:
        return 2.1f;
    case ActionKind::Warp:
        return 1.35f;
    case ActionKind::Stalk:
        return 1.20f;
    case ActionKind::Shot:
    case ActionKind::BladeClash:
    case ActionKind::Wave:
    case ActionKind::Cage:
        return 1.45f;
    case ActionKind::Smash:
    case ActionKind::Sweep:
    default:
        return 1.65f;
    }
}

ActionStep EnemyAnimationDebugScene::GetSelectedSkillStep(float skillTime) const {
    const ActionKind kind = kSkillPreviews[selectedSkillIndex_].kind;
    const float t = skillTime / GetSelectedSkillDuration();

    if (kind == ActionKind::Warp) {
        if (t < 0.24f) {
            return ActionStep::Start;
        }
        if (t < 0.72f) {
            return ActionStep::Move;
        }
        return ActionStep::End;
    }
    if (kind == ActionKind::Stalk) {
        return ActionStep::Move;
    }

    if (t < 0.42f) {
        return ActionStep::Charge;
    }
    if (t < 0.66f) {
        return ActionStep::Active;
    }
    return ActionStep::Recovery;
}

void EnemyAnimationDebugScene::ApplySelectedSkillPose() {
    Model *enemyModel = ctx_->model->GetModel(enemyModelId_);
    if (enemyModel == nullptr || enemyModel->bones.empty() ||
        enemyModel->skeletonSpaceMatrices.empty()) {
        return;
    }

    const ActionKind action = kSkillPreviews[selectedSkillIndex_].kind;
    const ActionStep step = GetSelectedSkillStep(skillTimer_);
    const bool phase2 = skillPhase2Preview_;
    const float phaseScale = phase2 ? 1.18f : 1.0f;
    const float time = skillTimer_;
    const float pulse = std::sin(time * 7.5f);
    const float slowPulse = std::sin(time * 3.2f);

    const int root = FindBoneIndex(*enemyModel, BossBoneName(nullptr));
    const int spine = FindBoneIndex(*enemyModel, BossBoneName(".003"));
    const int chest = FindBoneIndex(*enemyModel, BossBoneName(".013"));
    const int head = FindBoneIndex(*enemyModel, BossBoneName(".004"));
    const int headTip = FindBoneIndex(*enemyModel, BossBoneName(".007"));
    const int upperA = FindBoneIndex(*enemyModel, BossBoneName(".014"));
    const int upperB = FindBoneIndex(*enemyModel, BossBoneName(".017"));
    const int upperC = FindBoneIndex(*enemyModel, BossBoneName(".018"));
    const int upperD = FindBoneIndex(*enemyModel, BossBoneName(".019"));

    auto poseArms = [&](float pitch, float yaw, float roll) {
        (void)pitch;
        (void)yaw;
        (void)roll;
        (void)upperA;
        (void)upperB;
        (void)upperC;
        (void)upperD;
    };

    PoseBoneTree(*enemyModel, spine, -0.025f * slowPulse, 0.0f,
                 0.025f * pulse);
    PoseBoneTree(*enemyModel, chest, 0.018f * pulse, 0.018f * slowPulse,
                 -0.020f * pulse);
    PoseBoneTree(*enemyModel, head, 0.025f * slowPulse, 0.020f * pulse, 0.0f);
    PoseBoneTree(*enemyModel, headTip, 0.020f * slowPulse, 0.0f, 0.0f);

    switch (action) {
    case ActionKind::Smash:
        if (step == ActionStep::Charge || step == ActionStep::Hold) {
            const float hold = 0.70f + 0.30f * pulse;
            PoseBoneTree(*enemyModel, root, -0.08f * phaseScale, 0.0f, 0.0f);
            PoseBoneTree(*enemyModel, chest, -0.20f * phaseScale, 0.0f,
                         0.05f * hold);
            poseArms(-0.58f * phaseScale, 0.08f, 0.18f * hold);
        } else if (step == ActionStep::Active) {
            const float strike = Smooth01(time / 0.18f);
            PoseBoneTree(*enemyModel, root, 0.10f * phaseScale, 0.0f, 0.0f);
            PoseBoneTree(*enemyModel, chest, 0.34f * phaseScale * strike,
                         0.0f, -0.06f);
            poseArms(0.72f * phaseScale * strike, 0.04f,
                     -0.16f * phaseScale);
        } else {
            PoseBoneTree(*enemyModel, chest, 0.12f, 0.0f, -0.04f);
            poseArms(0.20f, 0.0f, -0.08f);
        }
        break;

    case ActionKind::Sweep:
        if (step == ActionStep::Charge || step == ActionStep::Hold) {
            PoseBoneTree(*enemyModel, chest, -0.06f, -0.30f * phaseScale,
                         -0.10f);
            poseArms(-0.05f, -0.34f * phaseScale, 0.30f);
        } else if (step == ActionStep::Active) {
            const float strike = Smooth01(time / 0.24f);
            PoseBoneTree(*enemyModel, root, 0.0f, 0.18f * phaseScale * strike,
                         0.0f);
            PoseBoneTree(*enemyModel, chest, 0.04f, 0.56f * phaseScale * strike,
                         0.14f);
            poseArms(0.08f, 0.74f * phaseScale * strike,
                     -0.28f * phaseScale);
        } else {
            PoseBoneTree(*enemyModel, chest, 0.04f, 0.18f, 0.05f);
            poseArms(0.02f, 0.24f, -0.10f);
        }
        break;

    case ActionKind::Shot:
    case ActionKind::BladeClash:
    case ActionKind::Wave:
    case ActionKind::Cage:
        if (step == ActionStep::Charge) {
            PoseBoneTree(*enemyModel, chest, -0.05f, 0.03f * pulse,
                         0.04f * pulse);
            poseArms(-0.22f * phaseScale, 0.10f * pulse, 0.06f);
        } else {
            PoseBoneTree(*enemyModel, chest, 0.08f, 0.0f, 0.0f);
            poseArms(0.28f * phaseScale, 0.03f, -0.05f);
        }
        break;

    case ActionKind::Nova:
        if (step == ActionStep::Charge) {
            const float chargePulse = 0.65f + 0.35f * std::sin(time * 18.0f);
            PoseBoneTree(*enemyModel, root, -0.12f * phaseScale, 0.0f,
                         0.04f * chargePulse);
            PoseBoneTree(*enemyModel, chest, -0.34f * phaseScale, 0.0f,
                         0.18f * chargePulse);
            PoseBoneTree(*enemyModel, head, -0.10f, 0.0f, 0.0f);
            poseArms(-0.70f * phaseScale, 0.16f * chargePulse,
                     0.34f * chargePulse);
        } else if (step == ActionStep::Active) {
            const float burst = 0.7f + 0.3f * std::sin(time * 34.0f);
            PoseBoneTree(*enemyModel, root, 0.16f * phaseScale, 0.0f,
                         0.08f * burst);
            PoseBoneTree(*enemyModel, chest, 0.44f * phaseScale, 0.0f,
                         -0.20f * burst);
            PoseBoneTree(*enemyModel, head, 0.16f, 0.0f, 0.0f);
            poseArms(0.64f * phaseScale, 0.44f * burst,
                     -0.52f * phaseScale);
        } else {
            PoseBoneTree(*enemyModel, chest, 0.08f, 0.0f, -0.06f);
            poseArms(0.16f, 0.10f, -0.08f);
        }
        break;

    case ActionKind::Warp:
        PoseBoneTree(*enemyModel, root, -0.06f, 0.0f, 0.0f);
        PoseBoneTree(*enemyModel, chest, -0.12f, 0.08f * pulse,
                     0.14f * slowPulse);
        poseArms(-0.16f, 0.16f * pulse, 0.24f * slowPulse);
        break;

    case ActionKind::Stalk:
        PoseBoneTree(*enemyModel, root, 0.04f * slowPulse, 0.0f,
                     0.06f * pulse);
        PoseBoneTree(*enemyModel, chest, -0.04f, 0.08f * pulse,
                     -0.05f * slowPulse);
        poseArms(0.06f * pulse, 0.12f * slowPulse, 0.10f * pulse);
        break;

    case ActionKind::None:
    default:
        poseArms(0.035f * pulse, 0.025f * slowPulse, 0.045f * pulse);
        break;
    }

    ctx_->model->GetRenderer()->UpdateSkinClusters(*enemyModel);
}

void EnemyAnimationDebugScene::UpdateCamera() {
    const float x = std::sinf(cameraYaw_) * cameraDistance_;
    const float z = std::cosf(cameraYaw_) * cameraDistance_;
    camera_.SetPosition({x, 3.60f, z});
    camera_.LookAt({0.0f, 4.00f, 0.0f});
    camera_.UpdateMatrices();
}

void EnemyAnimationDebugScene::UpdateLighting() {
    SceneLighting lighting{};
    lighting.keyLightDirection = {-0.45f, -1.0f, 0.25f};
    lighting.keyLightColor = {1.25f, 1.04f, 0.82f, 1.0f};
    lighting.fillLightDirection = {0.68f, -0.25f, -0.72f};
    lighting.fillLightColor = {0.28f, 0.38f, 0.52f, 0.48f};
    lighting.ambientColor = {0.20f, 0.18f, 0.16f, 1.0f};
    lighting.pointLights[0].positionRange = {0.0f, 3.2f, -1.6f, 8.0f};
    lighting.pointLights[0].colorIntensity = {1.0f, 0.52f, 0.18f, 1.0f};
    lighting.pointLights[1].positionRange = {2.4f, 2.2f, 2.2f, 6.0f};
    lighting.pointLights[1].colorIntensity = {0.32f, 0.56f, 1.0f, 0.58f};
    lighting.lightingParams = {88.0f, 0.34f, 1.25f, 0.10f};
    ctx_->model->SetSceneLighting(lighting);
}
