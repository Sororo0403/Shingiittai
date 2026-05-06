#include "GameScene.h"
#include "DirectXCommon.h"
#include "Input.h"
#include "ModelManager.h"
#include "SceneManager.h"
#include "SpriteManager.h"
#include "TextureManager.h"
#include "WinApp.h"
#include "Material.h"
#include <exception>
#include <cmath>

using namespace DirectX;

namespace {
constexpr float kPi = 3.14159265f;

Material MakeArenaMaterial(const XMFLOAT4 &color, bool useTexture = false,
                           float reflection = 0.08f) {
    Material material{};
    material.color = color;
    material.enableTexture = useTexture ? 1 : 0;
    material.reflectionStrength = reflection;
    material.reflectionFresnelStrength = reflection * 0.45f;
    material.reflectionRoughness = 0.58f;
    return material;
}

XMFLOAT4 MakeQuat(float pitch, float yaw, float roll) {
    XMFLOAT4 q{};
    XMStoreFloat4(&q, XMQuaternionRotationRollPitchYaw(pitch, yaw, roll));
    return q;
}
} // namespace

void GameScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);

    float aspect = static_cast<float>(ctx_->winApp->GetWidth()) /
                   static_cast<float>(ctx_->winApp->GetHeight());

    camera_.Initialize(aspect);
    camera_.SetMode(CameraMode::LookAt);
    camera_.UpdateMatrices();
    camera_.SetPerspectiveFovDeg(currentFovDeg_);

    DirectXCommon *dx = ctx_->dxCommon;
    ModelManager *model = ctx_->model;
    TextureManager *texture = ctx_->texture;

    dx->BeginUpload();

    uint32_t playerModel =
        model->Load(L"app/resources/models/player/player.glb");
    uint32_t swordModel = model->Load(L"app/resources/models/player/sword.glb");
    uint32_t enemyModel = enemyModel =
        model->Load(L"app/resources/models/boss/boss.gltf");
    uint32_t bulletModel =
        ctx_->model->Load(L"app/resources/models/bullet/bullet.obj");
    arenaNoiseTextureId_ = texture->CreateNoiseTexture(256, 256);
    arenaFloorModelId_ = model->CreatePlane(
        arenaNoiseTextureId_,
        MakeArenaMaterial({0.28f, 0.30f, 0.32f, 1.0f}, true, 0.10f));
    arenaInnerRingModelId_ = model->CreateRing(
        0, MakeArenaMaterial({0.82f, 0.70f, 0.46f, 1.0f}, false, 0.18f), 96,
        4.9f, 4.35f);
    arenaOuterRingModelId_ = model->CreateRing(
        0, MakeArenaMaterial({0.34f, 0.42f, 0.52f, 1.0f}, false, 0.14f), 128,
        12.3f, 11.6f);
    arenaColumnModelId_ = model->CreateCylinder(
        0, MakeArenaMaterial({0.38f, 0.40f, 0.43f, 1.0f}, false, 0.12f), 24,
        0.30f, 0.42f, 5.4f);
    arenaDomeModelId_ = model->CreateCylinder(
        0, MakeArenaMaterial({0.12f, 0.32f, 0.58f, 0.22f}, false, 0.03f), 96,
        3.8f, 13.2f, 8.4f);
    arenaBarrierRingModelId_ = model->CreateRing(
        0, MakeArenaMaterial({0.24f, 0.68f, 1.0f, 0.45f}, false, 0.05f), 128,
        13.1f, 12.9f);
    dx->EndUpload();

    texture->ReleaseUploadBuffers();

    player_.Initialize(playerModel, swordModel, selectedWeaponType_);
    playerModelId_ = playerModel;
    enemy_.Initialize(enemyModel, bulletModel);
    enemyModelId_ = enemyModel;

    cameraYaw_ = 0.0f;
    cameraPitch_ = 0.0f;
    isLockOn_ = true;
    rushChargeAssistStrength_ = 4.0f;
    rushChargeAssistMaxStep_ = 6.0f;
    rushActiveAssistStrength_ = 5.0f;
    rushActiveAssistMaxStep_ = 8.0f;
    rushLeadDistance_ = 2.5f;
    currentFovDeg_ = normalFovDeg_;
    targetFovDeg_ = normalFovDeg_;

    const DirectX::XMFLOAT3 &playerPos = player_.GetTransform().position;
    const DirectX::XMFLOAT3 &enemyPos = enemy_.GetTransform().position;
    lockOnOrbitCameraPos_ = {playerPos.x, playerPos.y + lockOnOrbitHeight_,
                             playerPos.z - lockOnOrbitRadius_};
    lockOnLookAt_ = {playerPos.x * lockOnLookPlayerWeight_ +
                         enemyPos.x * lockOnLookEnemyWeight_,
                     (playerPos.y + cameraLookHeight_) * 0.52f +
                         (enemyPos.y + 1.30f) * 0.48f,
                     playerPos.z * lockOnLookPlayerWeight_ +
                         enemyPos.z * lockOnLookEnemyWeight_};
    if (Model *playerModelData = model->GetModel(playerModelId_)) {
        if (!playerModelData->animations.empty()) {
            model->PlayAnimation(playerModelId_,
                                 playerModelData->currentAnimation, true);
        }
    }
    bullet_.Initialize(bulletModel);

    SyncEnemyAnimation();
    UpdateSceneLighting();
    counterCinematicActive_ = false;
    enemyAnimationFrozen_ = false;
}

void GameScene::Update() {
    Input *input = ctx_->input;
    const float baseDeltaTime = ctx_->deltaTime;
    const float gameplayDeltaTime = baseDeltaTime * ComputeGameplayTimeScale();
    const float playerDeltaTime =
        counterCinematicActive_ ? baseDeltaTime : gameplayDeltaTime;
    const float enemyDeltaTime = counterCinematicActive_
                                     ? (baseDeltaTime * counterTimeScale_)
                                     : gameplayDeltaTime;
    UpdateCamera(input);

    ctx_->model->UpdateAnimation(playerModelId_, playerDeltaTime);

    player_.Update(input, playerDeltaTime, enemy_.GetTransform().position,
                   cameraYaw_);
    sceneLightTime_ += baseDeltaTime;

    enemy_.Update(BuildPlayerCombatObservation(), enemyDeltaTime);
    UpdateSceneLighting();

    SyncEnemyAnimation();
    SetEnemyAnimationFrozen(counterCinematicActive_);
    if (!enemyAnimationFrozen_) {
        ctx_->model->UpdateAnimation(enemyModelId_, enemyDeltaTime);
    }

    UpdateBattleCamera();

    UpdateCombat(gameplayDeltaTime);
}

void GameScene::Draw() {
    ctx_->model->PreDraw();

    DrawArena();
    player_.Draw(ctx_->model, camera_);
    enemy_.Draw(ctx_->model, camera_);
    ctx_->model->PostDraw();
}

void GameScene::DrawArena() {
    ModelManager *model = ctx_->model;

    Transform floor{};
    floor.position = {0.0f, -0.04f, 0.0f};
    floor.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
    floor.scale = {28.0f, 28.0f, 1.0f};
    model->Draw(arenaFloorModelId_, floor, camera_);

    Transform innerRing{};
    innerRing.position = {0.0f, 0.012f, 0.0f};
    innerRing.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
    model->Draw(arenaInnerRingModelId_, innerRing, camera_);

    Transform outerRing{};
    outerRing.position = {0.0f, 0.018f, 0.0f};
    outerRing.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
    model->Draw(arenaOuterRingModelId_, outerRing, camera_);

    for (int i = 0; i < 8; ++i) {
        const float angle = static_cast<float>(i) * kPi * 0.25f;
        const float x = std::sinf(angle) * 11.9f;
        const float z = std::cosf(angle) * 11.9f;

        Transform column{};
        column.position = {x, 0.0f, z};
        column.rotation = MakeQuat(0.0f, -angle, 0.0f);
        model->Draw(arenaColumnModelId_, column, camera_);
    }

    ModelDrawEffect barrierEffect{};
    barrierEffect.enabled = true;
    barrierEffect.additiveBlend = true;
    barrierEffect.disableCulling = true;
    barrierEffect.color = {0.22f, 0.70f, 1.0f, 0.55f};
    barrierEffect.intensity = 0.42f + 0.08f * std::sinf(sceneLightTime_ * 1.8f);
    barrierEffect.fresnelPower = 1.2f;
    barrierEffect.noiseAmount = 0.35f;
    barrierEffect.time = sceneLightTime_;
    model->SetDrawEffect(barrierEffect);

    Transform dome{};
    dome.position = {0.0f, 0.0f, 0.0f};
    dome.scale = {1.0f, 1.0f, 1.0f};
    model->Draw(arenaDomeModelId_, dome, camera_);

    for (int i = 0; i < 3; ++i) {
        Transform ring{};
        ring.position = {0.0f, 1.9f + static_cast<float>(i) * 2.15f, 0.0f};
        ring.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
        ring.scale = {1.0f - static_cast<float>(i) * 0.12f,
                      1.0f - static_cast<float>(i) * 0.12f, 1.0f};
        model->Draw(arenaBarrierRingModelId_, ring, camera_);
    }

    model->ClearDrawEffect();
}
