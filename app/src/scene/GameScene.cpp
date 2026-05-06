#include "GameScene.h"
#include "CollisionUtil.h"
#include "DirectXCommon.h"
#include "Input.h"
#include "ModelManager.h"
#include "SceneManager.h"
#include "PlayerTuningPresetIO.h"
#include "SpriteManager.h"
#include "TextureManager.h"
#include "WinApp.h"
#include <string>
#ifndef IMGUI_DISABLED
#include "imgui.h"
#include "imgui_internal.h"
#endif
#include <algorithm>
#include <cmath>
#include <memory>
#include "EnemyTuningPresetIO.h"

using namespace DirectX;

static const std::string kBossAnimIdle = "Action";
static const std::string kBossAnimMove = "Action.001";
static const std::string kBossAnimSweep =
    "\xE6\xA8\xAA\xE8\x96\x99\xE3\x81\x8E\xE6\x89\x95\xE3\x81\x84";
static const std::string kBossAnimWave =
    "\xE6\xB3\xA2\xE7\x8A\xB6\xE6\x94\xBB\xE6\x92\x83";
static const std::string kBossAnimSmash =
    "\xE7\xB8\xA6\xE6\x8C\xAF\xE3\x82\x8A\xE4\xB8\x8B\xE3\x82\x8D\xE3\x81\x97";
static bool HasAnimation(const Model *model, const std::string &animationName);

static bool HasAnimation(const Model *model, const std::string &animationName) {
    if (!model) {
        return false;
    }

    return model->animations.find(animationName) != model->animations.end();
}

static std::string PickEnemyAnimation(const Model *model, const Enemy &enemy,
                                      bool &outLoop) {
    outLoop = true;

    if (!model || model->animations.empty()) {
        return {};
    }

    switch (enemy.GetActionKind()) {
    case ActionKind::Smash:
        outLoop = false;
        if (HasAnimation(model, kBossAnimSmash)) {
            return kBossAnimSmash;
        }
        break;

    case ActionKind::Sweep:
        outLoop = false;
        if (HasAnimation(model, kBossAnimSweep)) {
            return kBossAnimSweep;
        }
        break;

    case ActionKind::Shot:
    case ActionKind::Wave:
        outLoop = false;
        if (HasAnimation(model, kBossAnimWave)) {
            return kBossAnimWave;
        }
        break;

    case ActionKind::Warp:
    case ActionKind::Stalk:
    case ActionKind::None:
    default:
        if (HasAnimation(model, kBossAnimIdle)) {
            return kBossAnimIdle;
        }
        break;
    }

    if (HasAnimation(model, kBossAnimIdle)) {
        return kBossAnimIdle;
    }
    if (HasAnimation(model, kBossAnimMove)) {
        return kBossAnimMove;
    }

    return model->currentAnimation.empty() ? model->animations.begin()->first
                                           : model->currentAnimation;
}

static const char *GetCounterAxisName(SwordCounterAxis axis) {
    switch (axis) {
    case SwordCounterAxis::Vertical:
        return "Vertical";
    case SwordCounterAxis::Horizontal:
        return "Horizontal";
    default:
        return "None";
    }
}

static bool IsWithinCounterJustWindow(const Enemy &enemy) {
    const AttackTimingParam *timing = enemy.GetCurrentAttackTimingPublic();
    if (!timing) {
        return false;
    }

    float t = enemy.GetCurrentActionTimePublic();

    float activeStart = timing->activeStartTime;
    float activeEnd = timing->activeEndTime;

    if (activeEnd < activeStart) {
        return false;
    }

    float activeLen = activeEnd - activeStart;
    if (activeLen <= 0.0001f) {
        return false;
    }

    float justStart = activeStart + activeLen * 0.30f;
    float justEnd = activeStart + activeLen * 0.70f;

    return (t >= justStart && t <= justEnd);
}

static float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

static float EaseOutCubic(float t) {
    t = Clamp01(t);
    float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

#ifndef IMGUI_DISABLED
static ImU32 ToImColor(const XMFLOAT4 &color, float alphaScale = 1.0f) {
    int r = static_cast<int>(Clamp01(color.x) * 255.0f);
    int g = static_cast<int>(Clamp01(color.y) * 255.0f);
    int b = static_cast<int>(Clamp01(color.z) * 255.0f);
    int a = static_cast<int>(Clamp01(color.w * alphaScale) * 255.0f);
    return IM_COL32(r, g, b, a);
}
#endif // IMGUI_DISABLED

void GameScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);

    float aspect = static_cast<float>(ctx_->winApp->GetWidth()) /
                   static_cast<float>(ctx_->winApp->GetHeight());

    camera_.Initialize(aspect);
    camera_.SetMode(CameraMode::LookAt);
    camera_.UpdateMatrices();
    camera_.SetPerspectiveFovDeg(currentFovDeg_);
#ifdef _DEBUG
    debugCamera_.Initialize(aspect);
    debugCamera_.SetMode(CameraMode::Free);
    debugCamera_.UpdateMatrices();

    tripodCamera_.Initialize(aspect);
    camera_.SetMode(CameraMode::LookAt);
    tripodCamera_.SetPosition(tripodPos_);
    tripodCamera_.LookAt(tripodTarget_);
    tripodCamera_.UpdateMatrices();
#endif

    currentCamera_ = &camera_;

    DirectXCommon *dx = ctx_->dxCommon;
    ModelManager *model = ctx_->model;
    TextureManager *texture = ctx_->texture;

    dx->BeginUpload();

    uint32_t playerModel = model->Load(L"app/resources/models/player/player.glb");
    uint32_t swordModel = model->Load(L"app/resources/models/player/sword.glb");
    uint32_t enemyModel = 0;
    uint32_t bulletModel =
        ctx_->model->Load(L"app/resources/models/bullet/bullet.obj");
    try {
        enemyModel = model->Load(L"app/resources/models/boss/boss.gltf");
    } catch (const std::exception &) {
        enemyModel = model->Load(L"app/resources/models/enemy/enemy.glb");
    }

    dx->EndUpload();

    texture->ReleaseUploadBuffers();

    player_.Initialize(playerModel, swordModel);
    playerModelId_ = playerModel;
    enemy_.Initialize(enemyModel, bulletModel);
    enemyModelId_ = enemyModel;

    {
        PlayerTuningPreset playerPreset{};
        if (PlayerTuningPresetIO::Load("app/resources/player_tuning.txt",
                                       playerPreset)) {
            player_.ApplyTuningPreset(playerPreset);
        }
    }

    {
        EnemyTuningPreset enemyPreset{};
        if (EnemyTuningPresetIO::Load("app/resources/enemy_tuning.csv",
                                      enemyPreset) ||
            EnemyTuningPresetIO::Load("app/resources/enemy_tuning.txt",
                                      enemyPreset)) {
            enemy_.ApplyTuningPreset(enemyPreset);
        }
    }

    // 荳莠�E�遘ｰ繧�E�繝｡繝ｩ蛻晁E��蜷代″
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

    // 閧�E�雜翫�E�荳我ｺ�E�遘ｰ繧�E�繝｡繝ｩ蛻晁E��蜷代″
    const DirectX::XMFLOAT3 &playerPos = player_.GetTransform().position;
    const DirectX::XMFLOAT3 &enemyPos = enemy_.GetTransform().position;
    lockOnOrbitCameraPos_ = {playerPos.x, playerPos.y + lockOnOrbitHeight_,
                             playerPos.z - lockOnOrbitRadius_};
    lockOnLookAt_ = {
        playerPos.x * lockOnLookPlayerWeight_ +
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
    counterVignetteAlpha_ = 0.0f;

    activeElectricRing_ = {};
}

void GameScene::Update() {
    Input *input = ctx_->input;
    const float baseDeltaTime = ctx_->deltaTime;
    const float gameplayDeltaTime = baseDeltaTime * ComputeGameplayTimeScale();
    const float playerDeltaTime =
        counterCinematicActive_ ? baseDeltaTime : gameplayDeltaTime;
    const float enemyDeltaTime =
        counterCinematicActive_ ? (baseDeltaTime * counterTimeScale_)
                               : gameplayDeltaTime;
#ifdef _DEBUG
    const bool freezeEnemyMotion = dbgFreezeEnemyMotion_;
#else
    const bool freezeEnemyMotion = false;
#endif

    if (input != nullptr && input->IsKeyTrigger(DIK_F1)) {
        dbgTriggerCounterRequested_ = true;
    }
    if (input != nullptr && input->IsKeyTrigger(DIK_F2)) {
        dbgTriggerWarpBackstabRequested_ = true;
    }
    UpdateCamera(input);

    ctx_->model->UpdateAnimation(playerModelId_, playerDeltaTime);
#ifdef _DEBUG
    if (currentCamera_ == &debugCamera_) {
        return;
    }
#endif

    // 蠖薙◁E��雁�E螳・
    // 蜈医↓繝励Ξ繧�E�繝､繝ｼ繧呈峩譁E��縺励※縲√◎縺�E�邨先棡繧脱nemy縺�E�貂｡縺・
    player_.Update(input, playerDeltaTime, enemy_.GetTransform().position,
                   cameraYaw_);
    sceneLightTime_ += baseDeltaTime;

    const auto playerSlashStates = player_.GetSwordSlashStates();

    PlayerCombatObservation playerObs{};
    playerObs.position = player_.GetTransform().position;
    playerObs.velocity = player_.GetVelocity();
    playerObs.isGuarding = player_.IsGuarding();
    playerObs.isCounterStance = player_.IsCounterStance();
    playerObs.justCountered = player_.JustCountered();
    playerObs.justCounterFailed = player_.JustCounterFailed();
    playerObs.justCounterEarly = player_.JustCounterEarly();
    playerObs.justCounterLate = player_.JustCounterLate();
    playerObs.isAttacking = playerSlashStates[0] || playerSlashStates[1];

    switch (player_.GetCounterAxis()) {
    case SwordCounterAxis::Vertical:
        playerObs.counterAxis = CounterAxis::Vertical;
        break;

    case SwordCounterAxis::Horizontal:
        playerObs.counterAxis = CounterAxis::Horizontal;
        break;

    default:
        playerObs.counterAxis = CounterAxis::None;
        break;
    }

    if (dbgTriggerWarpBackstabRequested_ && !freezeEnemyMotion) {
        dbgTriggerWarpBackstabRequested_ = false;
        enemy_.DebugTriggerWarpBackstab(playerObs);
        SyncEnemyAnimation();
    }

    if (!freezeEnemyMotion) {
        enemy_.Update(playerObs, enemyDeltaTime);
    }
    UpdateSceneLighting();

    SyncEnemyAnimation();
    SetEnemyAnimationFrozen(counterCinematicActive_);
    if (!enemyAnimationFrozen_) {
        ctx_->model->UpdateAnimation(enemyModelId_, enemyDeltaTime);
    }

   // UpdateEnemySlashEffects();

    //UpdateEnemySwordTrail();

    enemy_.UpdatePresentationEvents();

    {
        auto requests = enemy_.ConsumeElectricRingSpawnRequests();
        for (const auto &req : requests) {
            SpawnElectricRing(req.worldPos, req.isWarpEnd);
        }
    }

    UpdateElectricRing();

    UpdateBattleCamera();

    auto counterBox = player_.GetSword().GetCounterOBB();
    auto playerBox = player_.GetOBB();
    const bool isPlayerGuarding = player_.IsGuarding();
    const bool isPlayerCountering = player_.GetSword().IsSlashMode();
    const auto enemyBodyBox = enemy_.GetBodyOBB();
    const ActionKind enemyActionKind = enemy_.GetActionKind();
    const ActionStep enemyActionStep = enemy_.GetActionStep();
    bool startCounterCinematicThisFrame = false;
    bool stopCounterCinematicThisFrame = false;
    bool forceSyncEnemyAnimationThisFrame = false;
    auto triggerSuccessfulCounter = [&](float enemyDamage, float hitCooldown) {
        player_.NotifyCounterSuccess();
        if (enemy_.NotifyCountered()) {
            forceSyncEnemyAnimationThisFrame = true;
        }
        enemy_.TakeDamage(enemyDamage);
        playerHitCooldown_ = hitCooldown;
        startCounterCinematicThisFrame = true;
    };

    if (dbgTriggerCounterRequested_) {
        dbgTriggerCounterRequested_ = false;
        triggerSuccessfulCounter(
            (std::max)(enemy_.GetCurrentAttackDamage(), 1.0f), 0.2f);
    }

    if (enemyHitCooldown_ > 0.0f) {
        enemyHitCooldown_ -= gameplayDeltaTime;
        if (enemyHitCooldown_ < 0.0f) {
            enemyHitCooldown_ = 0.0f;
        }
    }

    if (playerHitCooldown_ > 0.0f) {
        playerHitCooldown_ -= gameplayDeltaTime;
        if (playerHitCooldown_ < 0.0f) {
            playerHitCooldown_ = 0.0f;
        }
    }

    dbgHitLeftHand_ = false;
    dbgHitRightHand_ = false;
    dbgHitBody_ = false;
    dbgWaveHitPlayer_ = false;
    dbgPlayerGuardedHit_ = false;

    const auto swords = player_.GetSwords();
    const auto swordSlashStates = player_.GetSwordSlashStates();

    for (size_t i = 0; i < swords.size(); ++i) {
        const Sword *sword = swords[i];
        if (sword == nullptr || !swordSlashStates[i]) {
            continue;
        }

        auto swordHitBox = sword->GetOBB();
        auto bodyBox = enemy_.GetBodyOBB();
        auto leftHandBox = enemy_.GetLeftHandOBB();
        auto rightHandBox = enemy_.GetRightHandOBB();

        bool hitLeftHand = CollisionUtil::CheckOBB(swordHitBox, leftHandBox);
        bool hitRightHand = CollisionUtil::CheckOBB(swordHitBox, rightHandBox);
        bool hitBody = CollisionUtil::CheckOBB(swordHitBox, bodyBox);

        dbgHitLeftHand_ = dbgHitLeftHand_ || hitLeftHand;
        dbgHitRightHand_ = dbgHitRightHand_ || hitRightHand;
        dbgHitBody_ = dbgHitBody_ || hitBody;

        if (enemyHitCooldown_ <= 0.0f) {
            if (hitBody) {
                enemy_.TakeDamage(10.0f);
                enemyHitCooldown_ = 0.2f;
                if (counterCinematicActive_) {
                    stopCounterCinematicThisFrame = true;
                }
            }
        }

        if (enemyHitCooldown_ > 0.0f) {
            break;
        }
    }

    const bool isEnemySmashCounterWindow =
        (enemyActionKind == ActionKind::Smash &&
         enemyActionStep == ActionStep::Active);
    const bool isEnemySweepCounterWindow =
        (enemyActionKind == ActionKind::Sweep &&
         enemyActionStep == ActionStep::Active);
    const bool isEnemySmashMeleeWindow =
        (enemyActionKind == ActionKind::Smash &&
         (enemyActionStep == ActionStep::Active ||
          enemyActionStep == ActionStep::Recovery));
    const bool isEnemySweepMeleeWindow =
        (enemyActionKind == ActionKind::Sweep &&
         (enemyActionStep == ActionStep::Active ||
          enemyActionStep == ActionStep::Recovery));
    const bool isEnemyMeleeActive =
        isEnemySmashMeleeWindow || isEnemySweepMeleeWindow;

    const float enemyAttackDamage = enemy_.GetCurrentAttackDamage();
    const float enemyAttackKnockback = enemy_.GetCurrentAttackKnockback();
    const bool isCounterAxisMatch =
        (isEnemySmashCounterWindow &&
         player_.GetCounterAxis() == SwordCounterAxis::Vertical) ||
        (isEnemySweepCounterWindow &&
         player_.GetCounterAxis() == SwordCounterAxis::Horizontal);
    const bool canCounterThisHit =
        player_.IsCounterStance() && isCounterAxisMatch;

    bool bossHitPlayer = false;

    if (!freezeEnemyMotion && isEnemyMeleeActive) {
        auto enemyAttackBox = enemy_.GetAttackOBB();
        bossHitPlayer = CollisionUtil::CheckOBB(enemyAttackBox, playerBox);

        if (bossHitPlayer && playerHitCooldown_ <= 0.0f) {
            float dx = player_.GetTransform().position.x -
                       enemy_.GetTransform().position.x;
            float dz = player_.GetTransform().position.z -
                       enemy_.GetTransform().position.z;
            float len = std::sqrt(dx * dx + dz * dz);
            if (len < 0.0001f) {
                len = 1.0f;
            }

            dx /= len;
            dz /= len;

            if (canCounterThisHit) {
                triggerSuccessfulCounter(enemyAttackDamage, 0.2f);
                bossHitPlayer = false;
            } else if (isPlayerGuarding) {
                dbgPlayerGuardedHit_ = true;
                enemy_.NotifyAttackGuarded();
                player_.TakeDamage(enemyAttackDamage * kGuardDamageMultiplier);
                player_.AddKnockback({dx * (enemyAttackKnockback * 0.5f), 0.0f,
                                      dz * (enemyAttackKnockback * 0.5f)});
                playerHitCooldown_ = 0.2f;
            } else {
                enemy_.NotifyAttackConnected();
                player_.TakeDamage(enemyAttackDamage);
                player_.AddKnockback({dx * enemyAttackKnockback, 0.0f,
                                      dz * enemyAttackKnockback});
                playerHitCooldown_ = 0.4f;
            }
        }
    }

    dbgBossHitPlayer_ = bossHitPlayer;
    dbgBulletHitPlayer_ = false;

    if (freezeEnemyMotion) {
        if (startCounterCinematicThisFrame) {
            counterCinematicActive_ = true;
            SetEnemyAnimationFrozen(true);
        }
        if (stopCounterCinematicThisFrame) {
            counterCinematicActive_ = false;
            SetEnemyAnimationFrozen(false);
        }
        UpdateCounterVignette(baseDeltaTime);
        return;
    }

    const auto &bullets = enemy_.GetBullets();
    for (size_t i = 0; i < bullets.size(); ++i) {
        const auto &bullet = bullets[i];
        if (!bullet.isAlive) {
            continue;
        }

        OBB bulletBox{};
        bulletBox.center = bullet.position;
        bulletBox.size = enemy_.GetBulletHitBoxSize();
        bulletBox.rotation = player_.GetTransform().rotation;

        if (!bullet.isReflected && isPlayerCountering &&
            CollisionUtil::CheckOBB(bulletBox, counterBox)) {
            enemy_.ReflectBullet(i, enemy_.GetTransform().position);
            dbgBulletHitPlayer_ = false;
            continue;
        }

        if (bullet.isReflected) {
            if (CollisionUtil::CheckOBB(bulletBox, enemyBodyBox) &&
                enemyHitCooldown_ <= 0.0f) {
                reflectDamage_ = enemy_.GetBulletDamage() * damageMultiplier_;
                enemy_.TakeDamage(reflectDamage_);
                enemy_.DestroyBullet(i);
                enemyHitCooldown_ = 0.2f;
            }
            continue;
        }

        if (CollisionUtil::CheckOBB(bulletBox, playerBox)) {
            dbgBulletHitPlayer_ = true;

            if (playerHitCooldown_ <= 0.0f) {
                float vx = bullet.velocity.x;
                float vz = bullet.velocity.z;
                float len = std::sqrt(vx * vx + vz * vz);
                if (len < 0.0001f) {
                    len = 1.0f;
                }

                vx /= len;
                vz /= len;

                if (player_.IsCounterStance()) {
                    triggerSuccessfulCounter(enemy_.GetBulletDamage() * 2.0f,
                                             0.12f);
                } else if (isPlayerGuarding) {
                    dbgPlayerGuardedHit_ = true;
                    player_.TakeDamage(enemy_.GetBulletDamage() *
                                       kGuardDamageMultiplier);
                    player_.AddKnockback(
                        {vx * (enemy_.GetBulletKnockback() * 0.5f), 0.0f,
                         vz * (enemy_.GetBulletKnockback() * 0.5f)});
                    enemy_.DestroyBullet(i);
                    playerHitCooldown_ = 0.15f;
                } else {
                    player_.TakeDamage(enemy_.GetBulletDamage());
                    player_.AddKnockback({vx * enemy_.GetBulletKnockback(),
                                          0.0f,
                                          vz * enemy_.GetBulletKnockback()});
                    enemy_.DestroyBullet(i);
                    playerHitCooldown_ = 0.3f;
                }
            }

            enemy_.ConsumeBullet(i);
            break;
        }
    }

    dbgWaveHitPlayer_ = false;

    const auto &waves = enemy_.GetWaves();
    for (size_t i = 0; i < waves.size(); ++i) {
        const auto &wave = waves[i];
        if (!wave.isAlive) {
            continue;
        }

        OBB waveBox{};
        waveBox.center = wave.position;
        waveBox.size = enemy_.GetWaveHitBoxSize();
        waveBox.rotation = player_.GetTransform().rotation;

        if (!wave.isReflected && isPlayerCountering &&
            CollisionUtil::CheckOBB(waveBox, counterBox)) {
            enemy_.ReflectWave(i, enemy_.GetTransform().position);
            dbgWaveHitPlayer_ = false;
            continue;
        }

        if (wave.isReflected) {
            if (CollisionUtil::CheckOBB(waveBox, enemyBodyBox) &&
                enemyHitCooldown_ <= 0.0f) {
                reflectDamage_ = enemy_.GetWaveDamage() * damageMultiplier_;
                enemy_.TakeDamage(reflectDamage_);
                enemy_.DestroyWave(i);
                enemyHitCooldown_ = 0.2f;
            }
            continue;
        }

        if (CollisionUtil::CheckOBB(waveBox, playerBox)) {
            dbgWaveHitPlayer_ = true;

            if (playerHitCooldown_ <= 0.0f) {
                float vx = wave.direction.x;
                float vz = wave.direction.z;
                float len = std::sqrt(vx * vx + vz * vz);
                if (len < 0.0001f) {
                    len = 1.0f;
                }

                vx /= len;
                vz /= len;

                if (player_.IsCounterStance()) {
                    triggerSuccessfulCounter(enemy_.GetWaveDamage() * 2.0f,
                                             0.12f);
                } else if (isPlayerGuarding) {
                    dbgPlayerGuardedHit_ = true;
                    player_.TakeDamage(enemy_.GetWaveDamage() *
                                       kGuardDamageMultiplier);
                    player_.AddKnockback(
                        {vx * (enemy_.GetWaveKnockback() * 0.5f), 0.0f,
                         vz * (enemy_.GetWaveKnockback() * 0.5f)});
                    enemy_.DestroyWave(i);
                    playerHitCooldown_ = 0.15f;
                } else {
                    player_.TakeDamage(enemy_.GetWaveDamage());
                    player_.AddKnockback({vx * enemy_.GetWaveKnockback(), 0.0f,
                                          vz * enemy_.GetWaveKnockback()});
                    enemy_.DestroyWave(i);
                    playerHitCooldown_ = 0.35f;
                }
            }

            enemy_.ConsumeWave(i);
            break;
        }
    }

    if (startCounterCinematicThisFrame) {
        counterCinematicActive_ = true;
        SetEnemyAnimationFrozen(true);
    }
    if (stopCounterCinematicThisFrame) {
        counterCinematicActive_ = false;
        SetEnemyAnimationFrozen(false);
    }
    if (forceSyncEnemyAnimationThisFrame) {
        SyncEnemyAnimation();
        SetEnemyAnimationFrozen(true);
    }

    UpdateCounterVignette(baseDeltaTime);
}

float GameScene::ComputeGameplayTimeScale() const {
    return 1.0f;
}

void GameScene::SetEnemyAnimationFrozen(bool frozen) {
    if (ctx_ == nullptr || ctx_->model == nullptr) {
        enemyAnimationFrozen_ = frozen;
        return;
    }

    Model *enemyModel = ctx_->model->GetModel(enemyModelId_);
    if (enemyModel == nullptr) {
        enemyAnimationFrozen_ = frozen;
        return;
    }

    if (frozen) {
        if (HasAnimation(enemyModel, kBossAnimIdle)) {
            ctx_->model->PlayAnimation(enemyModelId_, kBossAnimIdle, true);
            ctx_->model->UpdateAnimation(enemyModelId_, 0.0f);
            enemyAnimationName_ = kBossAnimIdle;
            enemyAnimationLoop_ = true;
        }
        enemyModel->isPlaying = false;
        enemyModel->animationTime = 0.0f;
        enemyModel->animationFinished = false;
        enemyAnimationFrozen_ = true;
        return;
    }

    if (enemyAnimationFrozen_ && !enemyModel->animationFinished &&
        !enemyModel->currentAnimation.empty()) {
        enemyModel->isPlaying = true;
    }
    enemyAnimationFrozen_ = false;
}

void GameScene::UpdateCounterVignette(float deltaTime) {
    const float targetAlpha = counterCinematicActive_ ? 1.0f : 0.0f;
    float step = counterVignetteFadeSpeed_ * deltaTime;
    if (step > 1.0f) {
        step = 1.0f;
    }
    counterVignetteAlpha_ += (targetAlpha - counterVignetteAlpha_) * step;
    counterVignetteAlpha_ =
        std::clamp(counterVignetteAlpha_, 0.0f, 1.0f);
}

void GameScene::DrawCounterVignette() {
}

void GameScene::SyncEnemyAnimation() {
    ModelManager *modelManager = ctx_->model;
    Model *enemyModel = modelManager->GetModel(enemyModelId_);
    if (!enemyModel || enemyModel->animations.empty()) {
        return;
    }

    bool shouldLoop = true;
    std::string nextAnimation = PickEnemyAnimation(enemyModel, enemy_, shouldLoop);
    if (nextAnimation.empty()) {
        return;
    }

    if (enemyAnimationName_ == nextAnimation && enemyAnimationLoop_ == shouldLoop) {
        return;
    }

    modelManager->PlayAnimation(enemyModelId_, nextAnimation, shouldLoop);
    modelManager->UpdateAnimation(enemyModelId_, 0.0f);
    enemyAnimationName_ = nextAnimation;
    enemyAnimationLoop_ = shouldLoop;
}

void GameScene::Draw() {
    ctx_->model->PreDraw();

    player_.Draw(ctx_->model, *currentCamera_);
    enemy_.Draw(ctx_->model, *currentCamera_);
    int aliveBulletCount = 0;
    for (const auto &bullet : enemy_.GetBullets()) {
        if (bullet.isAlive) {
            aliveBulletCount++;
        }
    }

    int aliveWaveCount = 0;
    for (const auto &wave : enemy_.GetWaves()) {
        if (wave.isAlive) {
            aliveWaveCount++;
        }
    }
    ctx_->model->PostDraw();

    DrawCounterVignette();

#ifdef _DEBUG
    ImGui::Begin("HitInfo");
    ImGui::Checkbox("Freeze Enemy Motion", &dbgFreezeEnemyMotion_);
    const ActionKind dbgEnemyActionKind = enemy_.GetActionKind();
    const ActionStep dbgEnemyActionStep = enemy_.GetActionStep();
    if (ImGui::Button("Trigger Counter")) {
        dbgTriggerCounterRequested_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop Counter Cinematic")) {
        counterCinematicActive_ = false;
    }
    ImGui::Text("Hit LeftHand : %s", dbgHitLeftHand_ ? "true" : "false");
    ImGui::Text("Hit RightHand: %s", dbgHitRightHand_ ? "true" : "false");
    ImGui::Text("Hit Body     : %s", dbgHitBody_ ? "true" : "false");
    ImGui::Text("Cooldown     : %.2f", enemyHitCooldown_);

    const ActionKind enemyActionKind = dbgEnemyActionKind;
    const ActionStep enemyActionStep = dbgEnemyActionStep;

    const char *actionKindName = "None";
    switch (enemyActionKind) {
    case ActionKind::Smash:
        actionKindName = "Smash";
        break;
    case ActionKind::Sweep:
        actionKindName = "Sweep";
        break;
    case ActionKind::Shot:
        actionKindName = "Shot";
        break;
    case ActionKind::Wave:
        actionKindName = "Wave";
        break;
    case ActionKind::Warp:
        actionKindName = "Warp";
        break;
    case ActionKind::Stalk:
        actionKindName = "Stalk";
        break;
    default:
        break;
    }

    const char *actionStepName = "None";
    switch (enemyActionStep) {
    case ActionStep::Charge:
        actionStepName = "Charge";
        break;
    case ActionStep::Active:
        actionStepName = "Active";
        break;
    case ActionStep::Recovery:
        actionStepName = "Recovery";
        break;
    case ActionStep::Start:
        actionStepName = "Start";
        break;
    case ActionStep::Move:
        actionStepName = "Move";
        break;
    case ActionStep::Hold:
        actionStepName = "Hold";
        break;
    case ActionStep::End:
        actionStepName = "End";
        break;
    default:
        break;
    }

    const char *tacticName = "DistanceAdjust";
    switch (enemy_.GetTacticState()) {
    case TacticState::Warp:
        tacticName = "Warp";
        break;
    case TacticState::Melee:
        tacticName = "Melee";
        break;
    case TacticState::Ranged:
        tacticName = "Ranged";
        break;
    case TacticState::DistanceAdjust:
        tacticName = "DistanceAdjust";
        break;
    }

    ImGui::Text("TacticState  : %s", tacticName);

    const bool isEnemySmashActive = (enemyActionKind == ActionKind::Smash &&
                                     enemyActionStep == ActionStep::Active);

    const bool isEnemySweepActive = (enemyActionKind == ActionKind::Sweep &&
                                     enemyActionStep == ActionStep::Active);

    const bool isEnemyAttackActive = isEnemySmashActive || isEnemySweepActive;

    ImGui::Text("ActionKind   : %s", actionKindName);
    ImGui::Text("ActionStep   : %s", actionStepName);
    ImGui::Text("AttackActive : %s", isEnemyAttackActive ? "true" : "false");

    ImGui::Text("BossHitPlayer: %s", dbgBossHitPlayer_ ? "true" : "false");
    ImGui::Text("DistanceToPlayer : %.2f", enemy_.GetDistanceToPlayer());
    ImGui::Text("FacingYaw       : %.2f", enemy_.GetFacingYaw());
    ImGui::Text("LockedAttackYaw : %.2f", enemy_.GetLockedAttackYaw());
    ImGui::Text("CameraYaw       : %.2f", cameraYaw_);
    ImGui::Text("CameraPitch     : %.2f", cameraPitch_);
    ImGui::Text("LockOn          : %s", isLockOn_ ? "true" : "false");
    ImGui::Text("Stagnant        : %s",
                enemy_.IsDistanceStagnant() ? "true" : "false");
    ImGui::Text("StagnantTimer   : %.2f", enemy_.GetStagnantTimer());
    ImGui::Text("LastDistance    : %.2f", enemy_.GetLastDistanceToPlayer());
    ImGui::Text("StagDistThresh  : %.2f",
                enemy_.GetStagnantDistanceThreshold());
    ImGui::Text("StagTimeThresh  : %.2f", enemy_.GetStagnantTimeThreshold());
    ImGui::Text("WarpBonus       : %d", enemy_.GetStagnantWarpBonus());
    ImGui::Text("BulletHitPlayer : %s", dbgBulletHitPlayer_ ? "true" : "false");
    ImGui::Text("AliveBullets    : %d", aliveBulletCount);
    auto warpPos = enemy_.GetWarpTargetPos();
    ImGui::Text("Visible         : %s", enemy_.IsVisible() ? "true" : "false");
    ImGui::Text("WarpTarget      : (%.2f, %.2f, %.2f)", warpPos.x, warpPos.y,
                warpPos.z);
    ImGui::Text("WaveHitPlayer   : %s", dbgWaveHitPlayer_ ? "true" : "false");
    ImGui::Text("AliveWaves      : %d", aliveWaveCount);
    ImGui::Text("EnemyHP         : %.1f", enemy_.GetHP());
    ImGui::Text("PlayerHP        : %.1f", player_.GetHP());
    ImGui::Text("PlayerHitCD     : %.2f", playerHitCooldown_);
    ImGui::Text("PlayerGuarded   : %s",
                dbgPlayerGuardedHit_ ? "true" : "false");
    ImGui::Text("PlayerGuard     : %s",
                player_.IsGuarding() ? "true" : "false");

    const bool dbgCounterJustWindow =
        (enemy_.GetActionKind() == ActionKind::Smash ||
         enemy_.GetActionKind() == ActionKind::Sweep) &&
        (enemy_.GetActionStep() == ActionStep::Active) &&
        IsWithinCounterJustWindow(enemy_);

    ImGui::Text("CounterStance   : %s",
                player_.IsCounterStance() ? "true" : "false");

    const char *counterAxisName = GetCounterAxisName(player_.GetCounterAxis());
    ImGui::Text("CounterAxis     : %s", counterAxisName);
    ImGui::Text("CounterJustWin  : %s",
                dbgCounterJustWindow ? "true" : "false");
    ImGui::Text("EnemyActionTime : %.3f", enemy_.GetCurrentActionTimePublic());
    ImGui::Text("SmashDamage     : %.2f", enemy_.GetSmashDamage());
    ImGui::Text("SweepDamage     : %.2f", enemy_.GetSweepDamage());
    ImGui::Text("BulletDamage    : %.2f", enemy_.GetBulletDamage());
    ImGui::Text("WaveDamage      : %.2f", enemy_.GetWaveDamage());
    ImGui::Text("BulletKB        : %.2f", enemy_.GetBulletKnockback());
    ImGui::Text("WaveKB          : %.2f", enemy_.GetWaveKnockback());
    ImGui::Text("reflectDamage   : %.2f", reflectDamage_);

    ImGui::Separator();
    ImGui::Text("=== Enemy Tuning ===");

    if (ImGui::TreeNode("Distance")) {
        ImGui::DragFloat("Enemy MaxHP", &enemy_.EditEnemyMaxHp(), 1.0f, 1.0f,
                         5000.0f);
        ImGui::DragFloat("Phase2 HP Ratio", &enemy_.EditPhase2HealthRatioThreshold(),
                         0.005f, 0.05f, 0.95f);
        ImGui::DragFloat("NearAttackDistance", &enemy_.EditNearAttackDistance(),
                         0.05f, 0.5f, 20.0f);
        ImGui::DragFloat("FarAttackDistance", &enemy_.EditFarAttackDistance(),
                         0.05f, 1.0f, 30.0f);
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Smash")) {
        auto &p = enemy_.EditSmashParam();
        ImGui::DragFloat("Smash Damage", &p.damage, 0.1f, 0.0f, 100.0f);
        ImGui::DragFloat("Smash Knockback", &p.knockback, 0.1f, 0.0f, 30.0f);
        ImGui::DragFloat3("Smash HitBox", &p.hitBoxSize.x, 0.05f, 0.1f, 10.0f);

        float &smashCharge = enemy_.EditSmashChargeTime();
        ImGui::DragFloat("Smash Charge", &smashCharge, 0.01f, 0.0f, 5.0f);

        /* ImGui::DragFloat("Smash Attack", &enemy_.EditSmashAttackTime(),
         0.01f, 0.0f, 5.0f); ImGui::DragFloat("Smash Recovery",
         &enemy_.EditSmashRecoveryTime(), 0.01f, 0.0f, 5.0f);
         ImGui::DragFloat("Smash Active Start",
                          &enemy_.EditSmashActiveStartTime(), 0.01f,
         0.0f, 1.0f); ImGui::DragFloat("Smash Active End",
         &enemy_.EditSmashActiveEndTime(), 0.01f, 0.0f, 1.0f);*/
        if (ImGui::TreeNode("Smash Timing")) {
            auto &t = enemy_.EditSmashTiming();
            ImGui::DragFloat("Smash Total", &t.totalTime, 0.01f, 0.0f, 3.0f);
            ImGui::DragFloat("Smash Active Start", &t.activeStartTime, 0.01f,
                             0.0f, 3.0f);
            ImGui::DragFloat("Smash Active End", &t.activeEndTime, 0.01f, 0.0f,
                             3.0f);
            ImGui::DragFloat("Smash Recovery Start", &t.recoveryStartTime,
                             0.01f, 0.0f, 3.0f);

            ImGui::DragFloat("Smash Tracking End", &t.trackingEndTime, 0.01f,
                             0.0f, 2.0f);

            ImGui::TreePop();
        }

    if (ImGui::TreeNode("Warp")) {
        ImGui::DragFloat("Warp Start Time", &enemy_.EditWarpStartTime(), 0.005f,
                         0.0f, 2.0f);
        ImGui::DragFloat("Warp Move Time", &enemy_.EditWarpMoveTime(), 0.005f,
                         0.0f, 2.0f);
        ImGui::DragFloat("Warp End Time", &enemy_.EditWarpEndTime(), 0.005f,
                         0.0f, 2.0f);
        ImGui::TreePop();
    }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Sweep")) {
        auto &p = enemy_.EditSweepParam();
        ImGui::DragFloat("Sweep Damage", &p.damage, 0.1f, 0.0f, 100.0f);
        ImGui::DragFloat("Sweep Knockback", &p.knockback, 0.1f, 0.0f, 30.0f);
        ImGui::DragFloat3("Sweep HitBox", &p.hitBoxSize.x, 0.05f, 0.1f, 10.0f);

        float &sweepCharge = enemy_.EditSweepChargeTime();
        ImGui::DragFloat("Sweep Charge", &sweepCharge, 0.01f, 0.0f, 5.0f);

        /* ImGui::DragFloat("Sweep Attack", &enemy_.EditSweepAttackTime(),
         0.01f, 0.0f, 5.0f); ImGui::DragFloat("Sweep Recovery",
         &enemy_.EditSweepRecoveryTime(), 0.01f, 0.0f, 5.0f);
         ImGui::DragFloat("Sweep Active Start",
                          &enemy_.EditSweepActiveStartTime(), 0.01f,
         0.0f, 1.0f); ImGui::DragFloat("Sweep Active End",
         &enemy_.EditSweepActiveEndTime(), 0.01f, 0.0f, 1.0f);*/
        if (ImGui::TreeNode("Sweep Timing")) {
            auto &t = enemy_.EditSweepTiming();
            ImGui::DragFloat("Sweep Total", &t.totalTime, 0.01f, 0.0f, 3.0f);
            ImGui::DragFloat("Sweep Active Start", &t.activeStartTime, 0.01f,
                             0.0f, 3.0f);
            ImGui::DragFloat("Sweep Active End", &t.activeEndTime, 0.01f, 0.0f,
                             3.0f);
            ImGui::DragFloat("Sweep Recovery Start", &t.recoveryStartTime,
                             0.01f, 0.0f, 3.0f);

            ImGui::DragFloat("Sweep Tracking End", &t.trackingEndTime, 0.01f,
                             0.0f, 2.0f);
            ImGui::TreePop();
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Bullet")) {
        auto &p = enemy_.EditBulletParam();
        ImGui::DragFloat("Bullet Damage", &p.damage, 0.1f, 0.0f, 100.0f);
        ImGui::DragFloat("Bullet Knockback", &p.knockback, 0.1f, 0.0f, 30.0f);
        ImGui::DragFloat3("Bullet HitBox", &p.hitBoxSize.x, 0.01f, 0.05f, 5.0f);

        ImGui::DragFloat("Bullet Speed", &enemy_.EditBulletSpeed(), 0.1f, 0.1f,
                         30.0f);
        ImGui::DragFloat("Bullet LifeTime", &enemy_.EditBulletLifeTime(), 0.01f,
                         0.1f, 10.0f);
        ImGui::DragFloat("Shot Charge", &enemy_.EditShotChargeTime(), 0.01f,
                         0.0f, 5.0f);
        ImGui::DragFloat("Shot Recovery", &enemy_.EditShotRecoveryTime(), 0.01f,
                         0.0f, 5.0f);
        ImGui::DragFloat("Shot Interval", &enemy_.EditShotInterval(), 0.01f,
                         0.01f, 2.0f);
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Wave")) {
        auto &p = enemy_.EditWaveParam();
        ImGui::DragFloat("Wave Damage", &p.damage, 0.1f, 0.0f, 100.0f);
        ImGui::DragFloat("Wave Knockback", &p.knockback, 0.1f, 0.0f, 30.0f);
        ImGui::DragFloat3("Wave HitBox", &p.hitBoxSize.x, 0.05f, 0.1f, 10.0f);

        ImGui::DragFloat("Wave Speed", &enemy_.EditWaveSpeed(), 0.1f, 0.1f,
                         30.0f);
        ImGui::DragFloat("Wave MaxDistance", &enemy_.EditWaveMaxDistance(),
                         0.1f, 0.1f, 50.0f);
        ImGui::DragFloat("Wave Charge", &enemy_.EditWaveChargeTime(), 0.01f,
                         0.0f, 5.0f);
        ImGui::DragFloat("Wave Recovery", &enemy_.EditWaveRecoveryTime(), 0.01f,
                         0.0f, 5.0f);
        ImGui::TreePop();
    }

    ImGui::Separator();
    ImGui::Text("=== Preset ===");

    static char presetPath[256] = "app/resources/enemy_tuning.csv";
    ImGui::InputText("Preset Path", presetPath, sizeof(presetPath));

    if (ImGui::Button("Save Preset")) {
        EnemyTuningPreset preset = enemy_.CreateTuningPreset();
        EnemyTuningPresetIO::Save(presetPath, preset);
    }

    ImGui::SameLine();

    if (ImGui::Button("Load Preset")) {
        EnemyTuningPreset preset{};
        if (EnemyTuningPresetIO::Load(presetPath, preset)) {
            enemy_.ApplyTuningPreset(preset);
        }
    }

    ImGui::SameLine();

    if (ImGui::Button("Reset Preset")) {
        enemy_.ResetTuningPreset();
    }

    ImGui::Separator();
    ImGui::Text("=== Player Tuning ===");
    if (ImGui::TreeNode("Player Parameters")) {
        PlayerTuningPreset playerPreset = player_.CreateTuningPreset();
        bool changed = false;
        changed |= ImGui::DragFloat("Player MaxHP", &playerPreset.maxHp, 1.0f,
                                    1.0f, 1000.0f);
        changed |= ImGui::DragFloat("Player CurrentHP", &playerPreset.initialHp,
                                    1.0f, 0.0f, 1000.0f);
        changed |= ImGui::DragFloat("Player MoveSpeed", &playerPreset.moveSpeed,
                                    0.05f, 0.0f, 30.0f);
        changed |= ImGui::DragFloat("Player DamageScale",
                                    &playerPreset.damageTakenScale, 0.01f, 0.0f,
                                    5.0f);

        if (changed) {
            player_.ApplyTuningPreset(playerPreset);
        }

        static char playerPresetPath[256] = "app/resources/player_tuning.txt";
        ImGui::InputText("Player Preset Path", playerPresetPath,
                         sizeof(playerPresetPath));

        if (ImGui::Button("Save Player Preset")) {
            PlayerTuningPresetIO::Save(playerPresetPath,
                                       player_.CreateTuningPreset());
        }

        ImGui::SameLine();

        if (ImGui::Button("Load Player Preset")) {
            PlayerTuningPreset loaded{};
            if (PlayerTuningPresetIO::Load(playerPresetPath, loaded)) {
                player_.ApplyTuningPreset(loaded);
            }
        }

        ImGui::SameLine();

        if (ImGui::Button("Reset Player Preset")) {
            player_.ResetTuningPreset();
        }

        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Action Weight")) {
        ImGui::DragInt("Near Smash Weight", &enemy_.EditNearSmashWeight(), 1.0f,
                       0, 100);
        ImGui::DragInt("Near Sweep Weight", &enemy_.EditNearSweepWeight(), 1.0f,
                       0, 100);
        ImGui::DragInt("Far Shot Weight", &enemy_.EditFarShotWeight(), 1.0f, 0,
                       100);
        ImGui::DragInt("Far Warp Weight", &enemy_.EditFarWarpWeight(), 1.0f, 0,
                       100);
        ImGui::DragInt("Far Wave Weight", &enemy_.EditFarWaveWeight(), 1.0f, 0,
                       100);
        ImGui::TreePop();
    }

    ImGui::Text("Chain: Sweep -> Warp -> Smash");
    ImGui::DragFloat("Sweep Warp Smash MaxDist",
                     &enemy_.EditSweepWarpSmashMaxDistance(), 0.1f, 0.0f,
                     20.0f);
    ImGui::DragFloat("Sweep Warp Smash Chance",
                     &enemy_.EditSweepWarpSmashChance(), 0.01f, 0.0f, 1.0f);

    ImGui::Text("Chain: Wave -> Warp -> Smash");
    ImGui::DragFloat("Wave Warp Smash MinDist",
                     &enemy_.EditWaveWarpSmashMinDistance(), 0.1f, 0.0f, 20.0f);
    ImGui::DragFloat("Wave Warp Smash Chance",
                     &enemy_.EditWaveWarpSmashChance(), 0.01f, 0.0f, 1.0f);

    ImGui::End();

    ImGui::Begin("Camera");

    if (ImGui::Button("Normal")) {
        currentCamera_ = &camera_;
    }
    ImGui::SameLine();
    if (ImGui::Button("Debug")) {
        currentCamera_ = &debugCamera_;
    }
    ImGui::SameLine();
    if (ImGui::Button("Tripod")) {
        currentCamera_ = &tripodCamera_;
    }

    if (ImGui::Button("Snap From Current")) {
        tripodPos_ = currentCamera_->GetPosition();

        // DebugCamera蟁E��遲厁E��喃orward縺九ｉtarget菴懊ａE
        DirectX::XMFLOAT3 rot = currentCamera_->GetRotation();

        float cosPitch = std::cosf(rot.x);

        DirectX::XMFLOAT3 forward = {std::sinf(rot.y) * cosPitch,
                                     std::sinf(rot.x),
                                     std::cosf(rot.y) * cosPitch};

        tripodTarget_ = {tripodPos_.x + forward.x, tripodPos_.y + forward.y,
                         tripodPos_.z + forward.z};
    }

    ImGui::End();
#endif

}

void GameScene::UpdateSceneLighting() {
    if (ctx_ == nullptr || ctx_->model == nullptr) {
        return;
    }

    const XMFLOAT3 &playerPos = player_.GetTransform().position;
    const XMFLOAT3 &enemyPos = enemy_.GetTransform().position;
    XMFLOAT3 accentAnchor = enemy_.GetTransform().position;
    if (enemy_.GetActionKind() == ActionKind::Warp) {
        accentAnchor = enemy_.GetWarpTargetPos();
    }

    const float pulse = 0.82f + 0.18f * std::sinf(sceneLightTime_ * 2.4f);
    const float actionBoost =
        enemy_.GetActionKind() == ActionKind::Warp ? 1.35f :
        enemy_.GetActionKind() == ActionKind::Wave ? 1.20f :
        enemy_.GetActionKind() == ActionKind::Shot ? 1.10f : 1.0f;

    XMFLOAT3 duelCenter = {
        (playerPos.x + enemyPos.x) * 0.5f,
        (playerPos.y + enemyPos.y) * 0.5f,
        (playerPos.z + enemyPos.z) * 0.5f,
    };

    SceneLighting lighting{};
    lighting.keyLightDirection = {-0.45f, -1.0f, 0.30f};
    lighting.keyLightColor = {1.25f, 1.14f, 1.02f, 1.0f};
    lighting.fillLightDirection = {0.70f, -0.28f, -0.65f};
    lighting.fillLightColor = {0.20f, 0.34f, 0.58f, 0.42f};
    lighting.ambientColor = {0.22f, 0.24f, 0.28f, 1.0f};
    lighting.lightingParams = {56.0f, 0.40f, 3.2f, 0.18f};

    lighting.pointLights[0].positionRange = {
        duelCenter.x,
        duelCenter.y + 2.4f,
        duelCenter.z - 0.7f,
        8.5f,
    };
    lighting.pointLights[0].colorIntensity = {
        1.00f,
        0.48f,
        0.26f,
        1.15f * pulse,
    };

    lighting.pointLights[1].positionRange = {
        accentAnchor.x,
        enemyPos.y + 1.6f,
        accentAnchor.z + 0.35f,
        6.8f,
    };
    lighting.pointLights[1].colorIntensity = {
        0.24f,
        0.52f,
        1.00f,
        0.70f * actionBoost,
    };

    ctx_->model->SetSceneLighting(lighting);
}

bool GameScene::ProjectWorldToScreen(const XMFLOAT3 &worldPos,
                                     XMFLOAT2 &outScreen) const {
    if (currentCamera_ == nullptr || ctx_ == nullptr || ctx_->winApp == nullptr) {
        return false;
    }

    XMMATRIX viewProj = currentCamera_->GetView() * currentCamera_->GetProj();
    XMVECTOR pos = XMVectorSet(worldPos.x, worldPos.y, worldPos.z, 1.0f);
    XMVECTOR clip = XMVector4Transform(pos, viewProj);
    float w = XMVectorGetW(clip);
    if (w <= 0.0001f) {
        return false;
    }

    float invW = 1.0f / w;
    float ndcX = XMVectorGetX(clip) * invW;
    float ndcY = XMVectorGetY(clip) * invW;
    float ndcZ = XMVectorGetZ(clip) * invW;
    if (ndcZ < 0.0f || ndcZ > 1.0f) {
        return false;
    }

    float width = static_cast<float>(ctx_->winApp->GetWidth());
    float height = static_cast<float>(ctx_->winApp->GetHeight());
    outScreen.x = (ndcX * 0.5f + 0.5f) * width;
    outScreen.y = (-ndcY * 0.5f + 0.5f) * height;
    return true;
}

bool GameScene::ComputeEnemySlashScreenEffect(XMFLOAT2 &outStart,
                                              XMFLOAT2 &outEnd,
                                              float &outPhaseAlpha,
                                              float &outActionTime,
                                              ActionKind &outActionKind) const {
    outStart = {};
    outEnd = {};
    outPhaseAlpha = 0.0f;
    outActionTime = 0.0f;
    outActionKind = ActionKind::None;

    const ActionKind actionKind = enemy_.GetActionKind();
    const ActionStep actionStep = enemy_.GetActionStep();
    const bool isEnemySlashAction =
        (actionKind == ActionKind::Smash || actionKind == ActionKind::Sweep) &&
        (actionStep == ActionStep::Charge || actionStep == ActionStep::Hold ||
         actionStep == ActionStep::Active || actionStep == ActionStep::Recovery);
    if (!isEnemySlashAction) {
        return false;
    }

    XMFLOAT3 slashStartWorld = enemy_.GetRightHandTransform().position;
    XMFLOAT3 slashEndWorld = enemy_.GetAttackOBB().center;

    const float yaw =
        (actionStep == ActionStep::Charge || actionStep == ActionStep::Hold)
            ? enemy_.GetFacingYaw()
            : enemy_.GetLockedAttackYaw();
    const float forwardX = std::sinf(yaw);
    const float forwardZ = std::cosf(yaw);
    const float rightX = std::cosf(yaw);
    const float rightZ = -std::sinf(yaw);

    if (actionKind == ActionKind::Smash) {
        slashStartWorld.x += -forwardX * 0.35f;
        slashStartWorld.y += 0.95f;
        slashStartWorld.z += -forwardZ * 0.35f;

        slashEndWorld.x += forwardX * 0.55f;
        slashEndWorld.y -= 0.55f;
        slashEndWorld.z += forwardZ * 0.55f;
    } else {
        const XMFLOAT3 attackCenter = enemy_.GetAttackOBB().center;
        const float sweepHalfWidth = enemy_.GetSweepAttackBoxSize().x * 0.55f;
        slashStartWorld = {attackCenter.x - rightX * sweepHalfWidth,
                           attackCenter.y + 0.28f,
                           attackCenter.z - rightZ * sweepHalfWidth};
        slashEndWorld = {attackCenter.x + rightX * sweepHalfWidth,
                         attackCenter.y - 0.18f,
                         attackCenter.z + rightZ * sweepHalfWidth};
    }

    if (!ProjectWorldToScreen(slashStartWorld, outStart) ||
        !ProjectWorldToScreen(slashEndWorld, outEnd)) {
        return false;
    }

    const AttackTimingParam *timing = enemy_.GetCurrentAttackTimingPublic();
    const float actionTime = enemy_.GetCurrentActionTimePublic();
    float phaseAlpha = 0.0f;
    if (actionStep == ActionStep::Charge || actionStep == ActionStep::Hold) {
        const float previewT =
            (timing != nullptr && timing->activeStartTime > 0.0001f)
                ? actionTime / timing->activeStartTime
                : 1.0f;
        phaseAlpha =
            enemySlashChargePreviewAlpha_ * (0.35f + 0.65f * EaseOutCubic(previewT));
    } else if (actionStep == ActionStep::Active) {
        float activeT = 0.0f;
        if (timing != nullptr && timing->activeEndTime > timing->activeStartTime) {
            activeT = (actionTime - timing->activeStartTime) /
                      (timing->activeEndTime - timing->activeStartTime);
        }
        phaseAlpha = enemySlashActiveAlpha_ * (1.0f - 0.22f * Clamp01(activeT));
    } else {
        float recoveryT = 0.0f;
        if (timing != nullptr && timing->totalTime > timing->recoveryStartTime) {
            recoveryT = (actionTime - timing->recoveryStartTime) /
                        (timing->totalTime - timing->recoveryStartTime);
        }
        phaseAlpha =
            enemySlashRecoveryAlpha_ * (1.0f - EaseOutCubic(recoveryT));
    }

    if (phaseAlpha <= 0.01f) {
        return false;
    }

    outPhaseAlpha = phaseAlpha;
    outActionTime = actionTime;
    outActionKind = actionKind;
    return true;
}

bool GameScene::ComputeEnemySlashWorldEffect(XMFLOAT3 &outStart,
                                             XMFLOAT3 &outEnd,
                                             float &outPhaseAlpha,
                                             float &outActionTime,
                                             ActionKind &outActionKind) const {
    outStart = {};
    outEnd = {};
    outPhaseAlpha = 0.0f;
    outActionTime = 0.0f;
    outActionKind = ActionKind::None;

    const ActionKind actionKind = enemy_.GetActionKind();
    const ActionStep actionStep = enemy_.GetActionStep();
    const bool isEnemySlashAction =
        (actionKind == ActionKind::Smash || actionKind == ActionKind::Sweep) &&
        (actionStep == ActionStep::Charge || actionStep == ActionStep::Hold ||
         actionStep == ActionStep::Active ||
         actionStep == ActionStep::Recovery);

    if (!isEnemySlashAction) {
        return false;
    }

    XMFLOAT3 slashStartWorld = enemy_.GetRightHandTransform().position;
    XMFLOAT3 slashEndWorld = enemy_.GetAttackOBB().center;

    const float yaw =
        (actionStep == ActionStep::Charge || actionStep == ActionStep::Hold)
            ? enemy_.GetFacingYaw()
            : enemy_.GetLockedAttackYaw();

    const float forwardX = std::sinf(yaw);
    const float forwardZ = std::cosf(yaw);
    const float rightX = std::cosf(yaw);
    const float rightZ = -std::sinf(yaw);

    if (actionKind == ActionKind::Smash) {
        slashStartWorld.x += -forwardX * 0.35f;
        slashStartWorld.y += 0.95f;
        slashStartWorld.z += -forwardZ * 0.35f;

        slashEndWorld.x += forwardX * 0.55f;
        slashEndWorld.y -= 0.55f;
        slashEndWorld.z += forwardZ * 0.55f;
    } else {
        const XMFLOAT3 attackCenter = enemy_.GetAttackOBB().center;
        const float sweepHalfWidth = enemy_.GetSweepAttackBoxSize().x * 0.55f;

        slashStartWorld = {attackCenter.x - rightX * sweepHalfWidth,
                           attackCenter.y + 0.28f,
                           attackCenter.z - rightZ * sweepHalfWidth};

        slashEndWorld = {attackCenter.x + rightX * sweepHalfWidth,
                         attackCenter.y - 0.18f,
                         attackCenter.z + rightZ * sweepHalfWidth};
    }

    const AttackTimingParam *timing = enemy_.GetCurrentAttackTimingPublic();
    const float actionTime = enemy_.GetCurrentActionTimePublic();

    float phaseAlpha = 0.0f;
    if (actionStep == ActionStep::Charge || actionStep == ActionStep::Hold) {
        const float previewT =
            (timing != nullptr && timing->activeStartTime > 0.0001f)
                ? actionTime / timing->activeStartTime
                : 1.0f;
        phaseAlpha = enemySlashChargePreviewAlpha_ *
                     (0.35f + 0.65f * EaseOutCubic(previewT));
    } else if (actionStep == ActionStep::Active) {
        float activeT = 0.0f;
        if (timing != nullptr &&
            timing->activeEndTime > timing->activeStartTime) {
            activeT = (actionTime - timing->activeStartTime) /
                      (timing->activeEndTime - timing->activeStartTime);
        }
        phaseAlpha = enemySlashActiveAlpha_ * (1.0f - 0.22f * Clamp01(activeT));
    } else {
        float recoveryT = 0.0f;
        if (timing != nullptr &&
            timing->totalTime > timing->recoveryStartTime) {
            recoveryT = (actionTime - timing->recoveryStartTime) /
                        (timing->totalTime - timing->recoveryStartTime);
        }
        phaseAlpha =
            enemySlashRecoveryAlpha_ * (1.0f - EaseOutCubic(recoveryT));
    }

    if (phaseAlpha <= 0.01f) {
        return false;
    }

    outStart = slashStartWorld;
    outEnd = slashEndWorld;
    outPhaseAlpha = phaseAlpha;
    outActionTime = actionTime;
    outActionKind = actionKind;
    return true;
}

void GameScene::DrawEnemySlashPass() {
#ifdef IMGUI_DISABLED
    return;
#else
    ImGuiContext *imguiCtx = ImGui::GetCurrentContext();
    if (imguiCtx == nullptr || imguiCtx->Viewports.Size <= 0) {
        return;
    }

    ImGuiViewport *mainViewport = ImGui::GetMainViewport();
    if (mainViewport == nullptr) {
        return;
    }

    ImDrawList *drawList = ImGui::GetBackgroundDrawList(mainViewport);
    if (drawList == nullptr) {
        return;
    }

    XMFLOAT2 slashStartScreenF{};
    XMFLOAT2 slashEndScreenF{};
    float phaseAlpha = 0.0f;
    float actionTime = 0.0f;
    ActionKind actionKind = ActionKind::None;
    if (!ComputeEnemySlashScreenEffect(slashStartScreenF, slashEndScreenF,
                                       phaseAlpha, actionTime, actionKind)) {
        return;
    }

    ImVec2 start(slashStartScreenF.x, slashStartScreenF.y);
    ImVec2 end(slashEndScreenF.x, slashEndScreenF.y);
    float dx = end.x - start.x;
    float dy = end.y - start.y;
    float length = std::sqrtf(dx * dx + dy * dy);
    if (length <= 0.0001f) {
        return;
    }

    float invLen = 1.0f / length;
    float dirX = dx * invLen;
    float dirY = dy * invLen;
    float perpX = -dirY;
    float perpY = dirX;

    float sweepCurve = (actionKind == ActionKind::Sweep) ? 52.0f : 30.0f;
    float pulse = 0.84f + 0.16f * std::sinf(actionTime * 36.0f);
    float curveOffset = sweepCurve * pulse;
    ImVec2 control((start.x + end.x) * 0.5f + perpX * curveOffset,
                   (start.y + end.y) * 0.5f + perpY * curveOffset);

    constexpr int kSlashSegments = 18;
    ImVec2 path[kSlashSegments + 1];
    const float outerThickness =
        enemySlashOuterThicknessPx_ *
        (actionKind == ActionKind::Sweep ? 1.15f : 1.0f) *
        (0.86f + phaseAlpha * 0.28f);
    const float coreThickness =
        enemySlashCoreThicknessPx_ * (0.82f + phaseAlpha * 0.24f);
    float widths[kSlashSegments + 1];
    for (int i = 0; i <= kSlashSegments; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(kSlashSegments);
        float inv = 1.0f - t;
        path[i].x = inv * inv * start.x + 2.0f * inv * t * control.x + t * t * end.x;
        path[i].y = inv * inv * start.y + 2.0f * inv * t * control.y + t * t * end.y;
        float tipTaper = 1.0f - std::fabs(t - 0.52f) * 1.35f;
        tipTaper = Clamp01(tipTaper);
        widths[i] = outerThickness * (0.25f + 0.75f * tipTaper);
    }

    XMFLOAT4 shadowColor = {0.03f, 0.01f, 0.02f, phaseAlpha * 0.96f};
    XMFLOAT4 darkRimColor = {0.17f, 0.01f, 0.04f, phaseAlpha * 0.82f};
    XMFLOAT4 hotColor = {0.96f, 0.12f, 0.30f, phaseAlpha * 0.68f};
    XMFLOAT4 coreColor = {1.00f, 0.95f, 0.97f, phaseAlpha * 0.94f};
    XMFLOAT4 bloomColor = {0.74f, 0.58f, 1.00f, phaseAlpha * 0.44f};

    ImVec2 ribbon[(kSlashSegments + 1) * 2];
    for (int i = 0; i <= kSlashSegments; ++i) {
        int prevIndex = (i == 0) ? i : i - 1;
        int nextIndex = (i == kSlashSegments) ? i : i + 1;
        float tangentX = path[nextIndex].x - path[prevIndex].x;
        float tangentY = path[nextIndex].y - path[prevIndex].y;
        float tangentLen =
            std::sqrtf(tangentX * tangentX + tangentY * tangentY);
        if (tangentLen <= 0.0001f) {
            tangentX = dirX;
            tangentY = dirY;
            tangentLen = 1.0f;
        }
        tangentX /= tangentLen;
        tangentY /= tangentLen;
        float normalX = -tangentY;
        float normalY = tangentX;
        float width = widths[i];
        ribbon[i] = ImVec2(path[i].x + normalX * width,
                           path[i].y + normalY * width);
        ribbon[(kSlashSegments + 1) * 2 - 1 - i] =
            ImVec2(path[i].x - normalX * width * 0.65f,
                   path[i].y - normalY * width * 0.65f);
    }

    ImVec2 echoPath[kSlashSegments + 1];
    for (int i = 0; i <= kSlashSegments; ++i) {
        echoPath[i] = ImVec2(path[i].x + perpX * enemySlashEchoOffsetPx_,
                             path[i].y + perpY * enemySlashEchoOffsetPx_);
    }

    drawList->AddConvexPolyFilled(ribbon, (kSlashSegments + 1) * 2,
                                  ToImColor(shadowColor, 0.46f));
    drawList->AddPolyline(path, kSlashSegments + 1, ToImColor(bloomColor, 0.80f),
                          false, outerThickness * 1.55f);
    drawList->AddPolyline(echoPath, kSlashSegments + 1,
                          ToImColor(shadowColor, 0.55f), false,
                          outerThickness * 1.05f);
    drawList->AddPolyline(path, kSlashSegments + 1, ToImColor(shadowColor), false,
                          outerThickness);
    drawList->AddPolyline(path, kSlashSegments + 1, ToImColor(darkRimColor), false,
                          outerThickness * 0.72f);
    drawList->AddPolyline(path, kSlashSegments + 1, ToImColor(hotColor), false,
                          outerThickness * 0.34f);
    drawList->AddPolyline(path, kSlashSegments + 1, ToImColor(coreColor), false,
                          coreThickness);

    const ImVec2 impact = path[kSlashSegments];
    const float branchScale = (actionKind == ActionKind::Sweep) ? 0.82f : 0.68f;
    const float branchLen = length * branchScale * 0.22f;
    const float branchForward = length * 0.08f;

    ImVec2 branchA0(impact.x - perpX * branchLen, impact.y - perpY * branchLen);
    ImVec2 branchA1(impact.x + perpX * branchLen + dirX * branchForward,
                    impact.y + perpY * branchLen + dirY * branchForward);
    ImVec2 branchB0(impact.x + perpX * branchLen * 0.52f - dirX * branchForward * 0.7f,
                    impact.y + perpY * branchLen * 0.52f - dirY * branchForward * 0.7f);
    ImVec2 branchB1(impact.x - perpX * branchLen * 0.78f + dirX * branchForward * 0.5f,
                    impact.y - perpY * branchLen * 0.78f + dirY * branchForward * 0.5f);

    drawList->AddLine(branchA0, branchA1, ToImColor(darkRimColor), outerThickness * 0.22f);
    drawList->AddLine(branchA0, branchA1, ToImColor(coreColor), coreThickness * 0.46f);
    drawList->AddLine(branchB0, branchB1, ToImColor(hotColor), coreThickness * 0.38f);

    constexpr int kInkSpikeCount = 5;
    for (int i = 0; i < kInkSpikeCount; ++i) {
        float ratio = static_cast<float>(i) / static_cast<float>(kInkSpikeCount - 1);
        int pathIndex = static_cast<int>(ratio * static_cast<float>(kSlashSegments));
        float spikeScale = (0.45f + 0.55f * (1.0f - ratio)) *
                           (actionKind == ActionKind::Sweep ? 1.22f : 1.0f);
        float spikeLen = length * 0.12f * spikeScale;
        float sweepBias = std::sinf(actionTime * 18.0f + ratio * 7.0f) * 18.0f;
        ImVec2 spikeStart(path[pathIndex].x + dirX * (ratio * 22.0f),
                          path[pathIndex].y + dirY * (ratio * 22.0f));
        ImVec2 spikeEnd(spikeStart.x - perpX * (spikeLen + sweepBias),
                        spikeStart.y - perpY * (spikeLen + sweepBias));
        drawList->AddLine(spikeStart, spikeEnd, ToImColor(shadowColor, 0.92f),
                          outerThickness * (0.18f + 0.12f * spikeScale));
    }

    constexpr int kSparkCount = 12;
    for (int i = 0; i < kSparkCount; ++i) {
        float ratio = static_cast<float>(i) / static_cast<float>(kSparkCount);
        float angle = actionTime * 16.0f + ratio * DirectX::XM_2PI * 1.17f;
        float sparkLen =
            enemySlashSparkSpreadPx_ * (0.28f + 0.72f * (1.0f - ratio)) *
            (0.84f + 0.18f * pulse);
        float offset = enemySlashSparkSpreadPx_ * 0.20f * ratio;
        ImVec2 sparkStart(
            impact.x + dirX * offset + std::cosf(angle) * 8.0f,
            impact.y + dirY * offset + std::sinf(angle * 1.2f) * 8.0f);
        ImVec2 sparkEnd(
            sparkStart.x + std::cosf(angle) * sparkLen + perpX * sparkLen * 0.12f,
            sparkStart.y + std::sinf(angle) * sparkLen + perpY * sparkLen * 0.12f);

        drawList->AddLine(sparkStart, sparkEnd, ToImColor(hotColor, 0.82f - ratio * 0.3f),
                          1.2f + (1.0f - ratio) * 1.8f);
    }

    ImVec2 flashQuad[4] = {
        ImVec2(impact.x - perpX * (28.0f + phaseAlpha * 20.0f) - dirX * 4.0f,
               impact.y - perpY * (28.0f + phaseAlpha * 20.0f) - dirY * 4.0f),
        ImVec2(impact.x + dirX * (44.0f + phaseAlpha * 22.0f),
               impact.y + dirY * (44.0f + phaseAlpha * 22.0f)),
        ImVec2(impact.x + perpX * (16.0f + phaseAlpha * 10.0f) - dirX * 6.0f,
               impact.y + perpY * (16.0f + phaseAlpha * 10.0f) - dirY * 6.0f),
        ImVec2(impact.x - dirX * (18.0f + phaseAlpha * 8.0f),
               impact.y - dirY * (18.0f + phaseAlpha * 8.0f))};
    drawList->AddConvexPolyFilled(flashQuad, 4, ToImColor(coreColor, 0.78f));
    drawList->AddLine(ImVec2(impact.x - perpX * 42.0f, impact.y - perpY * 42.0f),
                      ImVec2(impact.x + perpX * 42.0f, impact.y + perpY * 42.0f),
                      ToImColor(shadowColor, 0.88f), 3.2f);

    drawList->AddCircleFilled(impact, 11.0f + phaseAlpha * 8.0f,
                              ToImColor(coreColor, 0.68f));
    drawList->AddCircleFilled(impact, 21.0f + phaseAlpha * 12.0f,
                              ToImColor(hotColor, 0.26f));
    drawList->AddCircleFilled(impact, 34.0f + phaseAlpha * 16.0f,
                              ToImColor(bloomColor, 0.14f));
#endif // IMGUI_DISABLED
}

void GameScene::UpdateEnemySlashEffects() {
    prevEnemyActionKind_ = enemy_.GetActionKind();
    prevEnemyActionStep_ = enemy_.GetActionStep();
    enemySlashActiveLatched_ = false;
}

void GameScene::UpdateEnemySwordTrail() {
}

void GameScene::DrawWarpSmokePass() {
    if (enemy_.GetActionKind() != ActionKind::Warp || ctx_ == nullptr ||
        ctx_->sprite == nullptr) {
        return;
    }

    ActionStep warpStep = enemy_.GetActionStep();
    float stepAlphaScale = 0.0f;
    switch (warpStep) {
    case ActionStep::Start:
        stepAlphaScale = 0.95f;
        break;
    case ActionStep::Move:
        stepAlphaScale = 1.15f;
        break;
    case ActionStep::End:
        stepAlphaScale = 1.0f;
        break;
    default:
        return;
    }

    XMFLOAT2 targetScreenF{};
    bool hasTarget = ProjectWorldToScreen(enemy_.GetWarpTargetPos(), targetScreenF);

    XMFLOAT2 sourceScreenF{};
    bool hasSource = enemy_.HasWarpDeparturePos() &&
                     ProjectWorldToScreen(enemy_.GetWarpDeparturePos(),
                                          sourceScreenF);
    if (!hasSource && !hasTarget) {
        return;
    }

    SpriteManager *spriteMgr = ctx_->sprite;
    Sprite &smoke = spriteMgr->GetSprite(warpSmokeSpriteId_);
    Sprite &darkSmoke = spriteMgr->GetSprite(warpSmokeDarkSpriteId_);
    float time = enemy_.GetCurrentActionTimePublic();

    auto drawSmokeCluster = [&](const XMFLOAT2 &center, float sizeScale,
                                float alphaScale, float travelBiasX,
                                float travelBiasY, bool stretch,
                                const XMFLOAT4 &baseColor, int layers,
                                float radialSpreadScale,
                                SpriteBlendMode blendMode,
                                bool useDarkTexture = false) {
        for (int i = 0; i < layers; ++i) {
            float ratio = (layers > 1)
                              ? static_cast<float>(i) /
                                    static_cast<float>(layers - 1)
                              : 0.0f;
            float angle =
                time * (2.4f + radialSpreadScale * 0.45f) +
                ratio * DirectX::XM_PIDIV2 * (1.6f + radialSpreadScale);
            float driftX =
                std::cosf(angle) *
                    (14.0f + 10.0f * ratio) * radialSpreadScale +
                travelBiasX;
            float driftY =
                std::sinf(angle * 1.3f) *
                    (10.0f + 7.0f * ratio) * radialSpreadScale +
                travelBiasY;
            float sizeX = warpSmokeBaseSizePx_ * sizeScale *
                          (1.0f + 0.20f * ratio);
            float sizeY = warpSmokeBaseSizePx_ * sizeScale *
                          (1.0f + 0.28f * ratio);
            if (stretch) {
                sizeY += warpSmokeMoveStretchPx_ * warpMoveSmokeStretchScale_ *
                         (0.55f + ratio * 0.45f);
                sizeX *= 0.82f;
            }

            Sprite &targetSprite =
                (useDarkTexture || blendMode == SpriteBlendMode::Modulate)
                    ? darkSmoke
                    : smoke;
            const uint32_t targetSpriteId =
                (useDarkTexture || blendMode == SpriteBlendMode::Modulate)
                    ? warpSmokeDarkSpriteId_
                    : warpSmokeSpriteId_;

            targetSprite.size = {sizeX, sizeY};
            targetSprite.position = {center.x + driftX - sizeX * 0.5f,
                                     center.y + driftY - sizeY * 0.5f};
            targetSprite.blendMode = blendMode;
            float alpha = 0.0f;
            if (blendMode == SpriteBlendMode::Modulate) {
                alpha = alphaScale * (1.0f - ratio * 0.08f) * baseColor.w;
                alpha *= 1.55f;
                if (alpha > 1.0f) {
                    alpha = 1.0f;
                }
            } else {
                alpha = warpSmokeAlpha_ * alphaScale * (1.0f - ratio * 0.16f) *
                        baseColor.w;
            }
            targetSprite.color = {baseColor.x, baseColor.y, baseColor.z, alpha};
            spriteMgr->Draw(targetSpriteId);
        }
    };

    auto drawBurstBillboards = [&](const XMFLOAT2 &center, float intensity,
                                   float verticalBias) {
        constexpr int kBurstSprites = 16;
        for (int i = 0; i < kBurstSprites; ++i) {
            float ratio = static_cast<float>(i) /
                          static_cast<float>(kBurstSprites - 1);
            float angle = time * (3.2f + ratio * 1.1f) +
                          ratio * DirectX::XM_2PI * 1.18f;
            float spread =
                warpArrivalBurstBillboardSpreadPx_ *
                (0.18f + 0.64f * ratio) * (0.82f + intensity * 0.22f);
            float driftX =
                std::cosf(angle) * spread +
                std::cosf(angle * 1.8f + 0.6f) * spread * 0.08f;
            float driftY =
                std::sinf(angle * 1.1f) * spread * 0.16f - verticalBias -
                (0.32f + ratio * 0.30f) * spread;
            float size =
                warpArrivalBurstBillboardSizePx_ *
                (0.46f + (1.0f - ratio) * 0.44f) *
                (0.90f + 0.18f * std::sinf(time * 18.0f + ratio * 9.0f)) *
                (0.84f + intensity * 0.18f);
            float alpha =
                warpArrivalBurstBillboardAlpha_ * intensity *
                (0.92f - ratio * 0.46f) *
                (0.84f + 0.10f * std::sinf(time * 14.0f + ratio * 11.0f));

            smoke.size = {size, size};
            smoke.position = {center.x + driftX - size * 0.5f,
                              center.y + driftY - size * 0.5f};
            smoke.blendMode = SpriteBlendMode::Alpha;

            if ((i % 5) == 0) {
                smoke.color = {1.0f, 0.76f, 0.82f, alpha * 0.52f};
            } else if ((i % 3) == 0) {
                smoke.color = {0.96f, 0.16f, 0.22f, alpha * 0.74f};
            } else {
                smoke.color = {0.82f, 0.03f, 0.08f, alpha * 0.88f};
            }
            spriteMgr->Draw(warpSmokeSpriteId_);
        }
    };

    auto drawArrivalDebugMarker = [&](const XMFLOAT2 &center) {
        smoke.blendMode = SpriteBlendMode::Alpha;

        smoke.size = {320.0f, 320.0f};
        smoke.position = {center.x - smoke.size.x * 0.5f,
                          center.y - smoke.size.y * 0.5f};
        smoke.color = {0.10f, 1.00f, 0.25f, 0.38f};
        spriteMgr->Draw(warpSmokeSpriteId_);

        smoke.size = {168.0f, 168.0f};
        smoke.position = {center.x - smoke.size.x * 0.5f,
                          center.y - smoke.size.y * 0.5f};
        smoke.color = {1.00f, 1.00f, 1.00f, 0.46f};
        spriteMgr->Draw(warpSmokeSpriteId_);

        smoke.size = {48.0f, 48.0f};
        smoke.position = {center.x - smoke.size.x * 0.5f,
                          center.y - smoke.size.y * 0.5f};
        smoke.color = {1.00f, 0.10f, 0.10f, 0.92f};
        spriteMgr->Draw(warpSmokeSpriteId_);
    };

    auto drawStepDebugMarker = [&](const XMFLOAT2 &center, ActionStep step) {
        smoke.blendMode = SpriteBlendMode::Alpha;

        XMFLOAT4 outerColor{};
        XMFLOAT4 innerColor{};
        XMFLOAT4 coreColor{};
        switch (step) {
        case ActionStep::Start:
            outerColor = {0.10f, 0.90f, 1.00f, 0.28f};
            innerColor = {0.82f, 1.00f, 1.00f, 0.42f};
            coreColor = {0.10f, 0.90f, 1.00f, 0.95f};
            break;
        case ActionStep::Move:
            outerColor = {1.00f, 0.85f, 0.10f, 0.28f};
            innerColor = {1.00f, 1.00f, 0.82f, 0.42f};
            coreColor = {1.00f, 0.85f, 0.10f, 0.95f};
            break;
        case ActionStep::End:
            outerColor = {0.10f, 1.00f, 0.25f, 0.28f};
            innerColor = {0.90f, 1.00f, 0.90f, 0.42f};
            coreColor = {1.00f, 0.10f, 0.10f, 0.95f};
            break;
        default:
            return;
        }

        smoke.size = {280.0f, 280.0f};
        smoke.position = {center.x - smoke.size.x * 0.5f,
                          center.y - smoke.size.y * 0.5f};
        smoke.color = outerColor;
        spriteMgr->Draw(warpSmokeSpriteId_);

        smoke.size = {140.0f, 140.0f};
        smoke.position = {center.x - smoke.size.x * 0.5f,
                          center.y - smoke.size.y * 0.5f};
        smoke.color = innerColor;
        spriteMgr->Draw(warpSmokeSpriteId_);

        smoke.size = {40.0f, 40.0f};
        smoke.position = {center.x - smoke.size.x * 0.5f,
                          center.y - smoke.size.y * 0.5f};
        smoke.color = coreColor;
        spriteMgr->Draw(warpSmokeSpriteId_);
    };

    auto drawArrivalSmokeMass = [&](const XMFLOAT2 &center, float intensity) {
        struct SmokeBlob {
            XMFLOAT2 offset;
            XMFLOAT2 size;
            float alpha;
        };

        constexpr SmokeBlob kBlobs[] = {
            {{-58.0f, -22.0f}, {278.0f, 226.0f}, 0.64f},
            {{64.0f, -26.0f}, {270.0f, 220.0f}, 0.62f},
            {{-12.0f, -94.0f}, {232.0f, 188.0f}, 0.46f},
            {{-116.0f, -72.0f}, {196.0f, 160.0f}, 0.34f},
            {{118.0f, -80.0f}, {190.0f, 154.0f}, 0.32f},
        };

        const float sizeBoost = 1.02f + intensity * 0.32f;

        for (const auto &blob : kBlobs) {
            darkSmoke.blendMode = SpriteBlendMode::DarkSmoke;
            darkSmoke.size = {blob.size.x * sizeBoost * 1.26f,
                              blob.size.y * sizeBoost * 1.26f};
            darkSmoke.position = {center.x + blob.offset.x - darkSmoke.size.x * 0.5f,
                                  center.y + blob.offset.y - darkSmoke.size.y * 0.5f};
            darkSmoke.color = {0.56f, 0.48f, 0.54f,
                               blob.alpha * warpArrivalSmokeDarkAlpha_ *
                                   (1.22f + intensity * 0.16f)};
            spriteMgr->Draw(warpSmokeDarkSpriteId_);

            darkSmoke.size = {darkSmoke.size.x * 0.84f, darkSmoke.size.y * 0.84f};
            darkSmoke.position = {center.x + blob.offset.x - darkSmoke.size.x * 0.5f,
                                  center.y + blob.offset.y - darkSmoke.size.y * 0.5f};
            darkSmoke.color = {0.34f, 0.24f, 0.30f,
                               blob.alpha * 0.72f * (1.02f + intensity * 0.10f)};
            spriteMgr->Draw(warpSmokeDarkSpriteId_);
        }
    };

    spriteMgr->PreDraw();

    if (warpStep == ActionStep::Start && hasSource) {
        // 黒�Eを主役にする
        drawSmokeCluster(sourceScreenF, warpSourceSmokeBloomScale_ * 1.08f,
                         stepAlphaScale * 1.05f, 0.0f, -8.0f, false,
                         {0.01f, 0.00f, 0.01f, 0.95f}, 7, 1.45f,
                         SpriteBlendMode::Modulate);

        // 赤粒っぽぁE��クセンチE
        drawSmokeCluster(sourceScreenF, warpSourceSmokeBloomScale_ * 0.42f,
                         stepAlphaScale * 0.90f, 0.0f, -12.0f, false,
                         {0.85f, 0.08f, 0.12f, 0.42f}, 3, 0.65f,
                         SpriteBlendMode::Alpha);
    }

    if (warpStep == ActionStep::Move && hasSource) {
        XMFLOAT2 mid = {(sourceScreenF.x + targetScreenF.x) * 0.5f,
                        (sourceScreenF.y + targetScreenF.y) * 0.5f};
        float dirX = targetScreenF.x - sourceScreenF.x;
        float dirY = targetScreenF.y - sourceScreenF.y;
        float dirLen = std::sqrtf(dirX * dirX + dirY * dirY);
        if (dirLen > 0.0001f) {
            dirX /= dirLen;
            dirY /= dirLen;
        } else {
            dirX = 0.0f;
            dirY = -1.0f;
        }

               // Move 中は中央の長ぁE�Eを弱める
        drawSmokeCluster(mid, 0.52f, stepAlphaScale * 0.40f, dirX * 6.0f,
                         dirY * 4.0f, true, {0.03f, 0.00f, 0.02f, 0.28f}, 2,
                         0.60f, SpriteBlendMode::Modulate);

        // departure 側に黒い残り馁E
        drawSmokeCluster(sourceScreenF, warpSourceSmokeBloomScale_ * 1.15f,
                         stepAlphaScale * 0.95f, -dirX * 4.0f,
                         -10.0f + dirY * 4.0f, false,
                         {0.01f, 0.00f, 0.01f, 0.92f}, 7, 1.55f,
                         SpriteBlendMode::Modulate);

        // 赤粒�E少量だぁE
        drawSmokeCluster(sourceScreenF, warpSourceSmokeBloomScale_ * 0.36f,
                         stepAlphaScale * 0.78f, dirX * 4.0f, -6.0f, false,
                         {0.90f, 0.10f, 0.14f, 0.34f}, 2, 0.55f,
                         SpriteBlendMode::Alpha);
    }

    if (hasTarget) {
        // arrival 側は黒�EをもぁE��段濁E��・庁E��する
        

        // 中央寁E��にさらに暗い芯を足ぁE
        

        // 赤粒�E少しだけ小さく�E寁E��低めにして黒�Eの中に浮かせめE
        

        float burstIntensity = stepAlphaScale *
                               (warpStep == ActionStep::Move ? 1.08f : 0.94f);
        XMFLOAT2 arrivalCenter = targetScreenF;
        drawStepDebugMarker(arrivalCenter, warpStep);
        if (warpStep == ActionStep::End) {
            arrivalCenter.x += 10.0f;
            arrivalCenter.y -= 18.0f;
            drawArrivalDebugMarker(arrivalCenter);
        } else {
            arrivalCenter.x += 34.0f;
            arrivalCenter.y -= 52.0f;
            drawArrivalSmokeMass(arrivalCenter, stepAlphaScale * 1.28f);
            drawBurstBillboards(arrivalCenter, burstIntensity * 0.14f, 38.0f);
            drawSmokeCluster(arrivalCenter, warpArrivalSmokeScale_ * 0.18f,
                             stepAlphaScale * 0.03f, 0.0f, -24.0f, false,
                             {0.88f, 0.05f, 0.10f, 0.02f}, 2, 0.20f,
                             SpriteBlendMode::Alpha);
        }
    }

    spriteMgr->PostDraw();
}

void GameScene::DrawWarpDistortionPass() {
#ifdef IMGUI_DISABLED
    return;
#else
    if (enemy_.GetActionKind() != ActionKind::Warp) {
        return;
    }

    ImGuiContext *imguiCtx = ImGui::GetCurrentContext();
    if (imguiCtx == nullptr || imguiCtx->Viewports.Size <= 0) {
        return;
    }

    ActionStep warpStep = enemy_.GetActionStep();
    float stepIntensity = 0.0f;
    switch (warpStep) {
    case ActionStep::Start:
        stepIntensity = 0.72f;
        break;
    case ActionStep::Move:
        stepIntensity = 1.0f;
        break;
    case ActionStep::End:
        stepIntensity = 0.84f;
        break;
    default:
        return;
    }

    XMFLOAT2 targetScreenF{};
    bool hasTarget = ProjectWorldToScreen(enemy_.GetWarpTargetPos(), targetScreenF);
    ImVec2 targetScreen(targetScreenF.x, targetScreenF.y);

    XMFLOAT2 sourceScreenF{};
    bool hasSource = enemy_.HasWarpDeparturePos() &&
                     ProjectWorldToScreen(enemy_.GetWarpDeparturePos(),
                                          sourceScreenF);
    if (!hasSource && !hasTarget) {
        return;
    }
    ImVec2 sourceScreen(sourceScreenF.x, sourceScreenF.y);

    float time = enemy_.GetCurrentActionTimePublic();
    float baseRadius = warpDistortionRadiusPx_ * (0.85f + 0.30f * stepIntensity);
    float jitter = warpDistortionJitterPx_ * (0.80f + 0.40f * std::sinf(time * 20.0f));
    float alpha = warpDistortionAlpha_ + warpDistortionMoveAlphaBonus_ *
                                              (warpStep == ActionStep::Move ? 1.0f : 0.0f);

    ImGuiViewport *mainViewport = ImGui::GetMainViewport();
    if (mainViewport == nullptr) {
        return;
    }

    ImDrawList *drawList = ImGui::GetBackgroundDrawList(mainViewport);
    if (drawList == nullptr) {
        return;
    }
    ImU32 bright = IM_COL32(255, 48, 108,
                            static_cast<int>(255.0f * alpha));
    ImU32 soft = IM_COL32(120, 22, 70,
                          static_cast<int>(255.0f * (alpha * 0.82f)));
    ImU32 slash = IM_COL32(255, 215, 235,
                           static_cast<int>(255.0f * (alpha * 0.78f)));

    auto drawDistortionAt = [&](const ImVec2 &center, float radiusScale,
                                float rotationBias) {
        constexpr int kSegments = 28;
        ImVec2 points[kSegments + 1];
        for (int i = 0; i <= kSegments; ++i) {
            float ratio = static_cast<float>(i) / static_cast<float>(kSegments);
            float angle = ratio * DirectX::XM_2PI + time * 6.0f + rotationBias;
            float wave = std::sinf(angle * 3.0f + time * 17.0f) * jitter;
            float radius = baseRadius * radiusScale + wave;
            points[i] = ImVec2(center.x + std::cosf(angle) * radius,
                               center.y + std::sinf(angle) * radius);
        }

        drawList->AddPolyline(points, kSegments + 1, soft, true,
                              warpDistortionThicknessPx_);
        drawList->AddCircle(center, baseRadius * radiusScale * 0.62f, bright, 24,
                            warpDistortionThicknessPx_ * 0.7f);
        drawList->AddCircle(center, baseRadius * radiusScale * 0.82f, bright, 28,
                            warpDistortionThicknessPx_ * 0.42f);

        for (int i = 0; i < 8; ++i) {
            float ratio = static_cast<float>(i) / 8.0f;
            float angle = ratio * DirectX::XM_2PI + time * 8.5f + rotationBias;
            float inner = baseRadius * radiusScale * 0.42f;
            float outer = inner + warpDistortionLineLengthPx_ *
                                      (0.75f + 0.25f * std::sinf(time * 18.0f + i));
            ImVec2 a(center.x + std::cosf(angle) * inner,
                     center.y + std::sinf(angle) * inner);
            ImVec2 b(center.x + std::cosf(angle) * outer,
                     center.y + std::sinf(angle) * outer);
            drawList->AddLine(a, b, bright, 1.6f);
        }
    };

    if (warpStep == ActionStep::Start && hasSource) {
        ImVec2 sourceFoot = sourceScreen;
        sourceFoot.y += warpDistortionFootOffsetPx_;
        drawDistortionAt(sourceFoot, 0.88f, 0.0f);
    }

    if (warpStep == ActionStep::Move && hasSource && hasTarget) {
        ImVec2 mid((sourceScreen.x + targetScreen.x) * 0.5f,
                   (sourceScreen.y + targetScreen.y) * 0.5f +
                       warpDistortionFootOffsetPx_ * 0.78f);
        drawDistortionAt(mid, 0.72f, 0.6f);
        drawList->AddLine(sourceScreen, targetScreen, soft, 0.8f);
    }

    if (hasTarget) {
        ImVec2 arrivalCenter = targetScreen;
        arrivalCenter.y += warpDistortionFootOffsetPx_ -
                           warpDistortionPreviewOffsetPx_ *
                               (warpStep == ActionStep::Start ? 0.55f : 0.18f);
        drawDistortionAt(arrivalCenter,
                         warpStep == ActionStep::Move ? 1.12f : 1.0f, 1.2f);
    }

    float dirX = 0.0f;
    float dirY = -1.0f;
    if (hasSource && hasTarget) {
        dirX = targetScreen.x - sourceScreen.x;
        dirY = targetScreen.y - sourceScreen.y;
        float dirLen = std::sqrtf(dirX * dirX + dirY * dirY);
        if (dirLen > 0.0001f) {
            dirX /= dirLen;
            dirY /= dirLen;
        } else {
            dirX = 0.0f;
            dirY = -1.0f;
        }
    }

    float perpX = -dirY;
    float perpY = dirX;
    float slashLen =
        baseRadius * (warpStep == ActionStep::Move ? 0.92f : 1.10f);
    float branchLen = slashLen * 0.76f;
    float branchOffset = baseRadius * 0.28f;

    if (hasTarget) {
        ImVec2 arrivalCenter = targetScreen;
        arrivalCenter.y -= warpDistortionPreviewOffsetPx_ *
                           (warpStep == ActionStep::Start ? 0.55f : 0.18f);
        ImVec2 slashA(arrivalCenter.x - perpX * slashLen,
                      arrivalCenter.y - perpY * slashLen);
        ImVec2 slashB(arrivalCenter.x + perpX * slashLen,
                      arrivalCenter.y + perpY * slashLen);
        ImVec2 slashC(arrivalCenter.x - perpX * branchLen + dirX * branchOffset,
                      arrivalCenter.y - perpY * branchLen + dirY * branchOffset);
        ImVec2 slashD(arrivalCenter.x + perpX * branchLen + dirX * branchOffset,
                      arrivalCenter.y + perpY * branchLen + dirY * branchOffset);
        ImVec2 slashE(arrivalCenter.x - perpX * (branchLen * 0.58f) -
                          dirX * branchOffset * 0.72f,
                      arrivalCenter.y - perpY * (branchLen * 0.58f) -
                          dirY * branchOffset * 0.72f);
        ImVec2 slashF(arrivalCenter.x + perpX * (branchLen * 0.58f) -
                          dirX * branchOffset * 0.72f,
                      arrivalCenter.y + perpY * (branchLen * 0.58f) -
                          dirY * branchOffset * 0.72f);

        drawList->AddLine(slashA, slashB, slash,
                          warpDistortionThicknessPx_ * 0.82f);
        drawList->AddLine(slashC, slashD, bright,
                          warpDistortionThicknessPx_ * 0.55f);
        drawList->AddLine(slashE, slashF, soft,
                          warpDistortionThicknessPx_ * 0.42f);
    }
#endif
}

// void GameScene::UpdateCamera(Input *input) {
// #ifdef _DEBUG
//     if (input->IsKeyTrigger(DIK_F11)) {
//         if (currentCamera_ == &camera_) {
//             currentCamera_ = &debugCamera_;
//         } else {
//             currentCamera_ = &camera_;
//         }
//     }
//
//     if (currentCamera_ == &debugCamera_) {
//         debugCamera_.Update(*input, ctx_->deltaTime);
//         currentCamera_->UpdateMatrices();
//         return;
//     }
// #else
//     (void)input;
// #endif
// }

// void GameScene::UpdateBattleCamera() {
//     auto &playerTf = player_.GetTransform();
//     auto &enemyTf = enemy_.GetTransform();
//
//     XMFLOAT3 playerPos = playerTf.position;
//     XMFLOAT3 enemyPos = enemyTf.position;
//
//     XMVECTOR playerPosV = XMLoadFloat3(&playerPos);
//     XMVECTOR enemyPosV = XMLoadFloat3(&enemyPos);
//
//     XMVECTOR forward = XMVector3Normalize(enemyPosV - playerPosV);
//
//     XMVECTOR camPos = playerPosV - forward * kCameraDistance +
//                       XMVectorSet(0, kCameraHeight, 0, 0);
//
//     XMFLOAT3 cameraPos;
//     XMStoreFloat3(&cameraPos, camPos);
//
//     camera_.SetPosition(cameraPos);
//     camera_.LookAt({enemyPos.x, kCameraHeight, enemyPos.z});
// }

void GameScene::UpdateCamera(Input *input) {
#ifdef _DEBUG
    // 蛻・�E�譖ｿ縺・
    if (input->IsKeyTrigger(DIK_F11)) {
        currentCamera_ = &debugCamera_;
    }
    if (input->IsKeyTrigger(DIK_F10)) {
        currentCamera_ = &camera_;
    }
    if (input->IsKeyTrigger(DIK_F9)) {
        currentCamera_ = &tripodCamera_;
    }

    // DebugCamera
    if (currentCamera_ == &debugCamera_) {
        debugCamera_.Update(*input, ctx_->deltaTime);
        debugCamera_.UpdateMatrices();
        return;
    }

    // TripodCamera・亥�E�悟�E蝗ｺ螳夲�E�・
    if (currentCamera_ == &tripodCamera_) {
        tripodCamera_.SetPosition(tripodPos_);
        tripodCamera_.LookAt(tripodTarget_);
        tripodCamera_.UpdateMatrices();
        return;
    }
#endif

    // ===== 騾壼�E��E�繧�E�繝｡繝ｩ =====

    // 繝ｭ繝�Eけ繧�E�繝ｳ蛻・�E�譖ｿ縺・
    if (input->IsKeyTrigger(DIK_Q)) {
        isLockOn_ = !isLockOn_;
    }

    float yawInput = 0.0f;
    float pitchInput = 0.0f;

#ifdef _DEBUG
    if (input->IsKeyPress(DIK_LEFT)) {
        yawInput -= 1.0f;
    }
    if (input->IsKeyPress(DIK_RIGHT)) {
        yawInput += 1.0f;
    }
    if (input->IsKeyPress(DIK_UP)) {
        pitchInput += 1.0f;
    }
    if (input->IsKeyPress(DIK_DOWN)) {
        pitchInput -= 1.0f;
    }
#endif

    cameraYaw_ += yawInput * cameraLookSensitivity_;
    cameraPitch_ += pitchInput * cameraLookSensitivity_;

    if (cameraPitch_ < cameraPitchMin_) {
        cameraPitch_ = cameraPitchMin_;
    }
    if (cameraPitch_ > cameraPitchMax_) {
        cameraPitch_ = cameraPitchMax_;
    }

    // 箝�E譛驥崎ｦ・�E�壹�E�E��薙〒繧�E�繝｡繝ｩ遒ｺ螳・
    UpdateBattleCamera();

    // 箝�E譛驥崎ｦ・�E�夊｡悟�E譖ｴ譁E��
    camera_.UpdateMatrices();
}

void GameScene::UpdateBattleCamera() {
    const auto &playerTf = player_.GetTransform();
    const auto &enemyTf = enemy_.GetTransform();

    const DirectX::XMFLOAT3 &playerPos = playerTf.position;
    const DirectX::XMFLOAT3 &enemyPos = enemyTf.position;

    // 謨�E�陦悟虚迥�E�諷九ｒ蜿門�E�・
    const ActionKind enemyActionKind = enemy_.GetActionKind();
    const ActionStep enemyActionStep = enemy_.GetActionStep();

    const bool isEnemyWarpStart = (enemyActionKind == ActionKind::Warp &&
                                   enemyActionStep == ActionStep::Start);

    const bool isEnemyWarpMove = (enemyActionKind == ActionKind::Warp &&
                                  enemyActionStep == ActionStep::Move);

    const bool isEnemyWarpEnd = (enemyActionKind == ActionKind::Warp &&
                                 enemyActionStep == ActionStep::End);
    const bool isEnemyPhaseTransition = enemy_.IsPhaseTransitionActive();
    const float enemyPhaseTransitionRatio = enemy_.GetPhaseTransitionRatio();

    // =========================
    // FOV繧�E�繝ｼ繧�E�繝�Eヨ豎ｺ螳・
    // =========================
    targetFovDeg_ = normalFovDeg_;

    if (isLockOn_) {
        targetFovDeg_ = lockOnFovDeg_;
    }

    if (isEnemyWarpStart || isEnemyWarpMove || isEnemyWarpEnd) {
        targetFovDeg_ = warpFovDeg_;
    }

    if (isEnemyPhaseTransition) {
        targetFovDeg_ = phaseTransitionFovDeg_;
    }

    float usedFovLerpSpeed = fovLerpSpeed_;
    if (isEnemyPhaseTransition) {
        usedFovLerpSpeed = phaseTransitionFovLerpSpeed_;
    }
    float fovAlpha = usedFovLerpSpeed * ctx_->deltaTime;
    if (fovAlpha > 1.0f) {
        fovAlpha = 1.0f;
    }

    currentFovDeg_ += (targetFovDeg_ - currentFovDeg_) * fovAlpha;
    camera_.SetPerspectiveFovDeg(currentFovDeg_);

    // =========================
    // 繝ｭ繝�Eけ繧�E�繝ｳ荳�E�縺�E�縺・yaw 陬懷勧
    // =========================
    if (isLockOn_) {
        DirectX::XMFLOAT3 assistTarget = enemyPos;

        float assistStrength = lockOnAssistStrength_;
        float assistMaxStep = lockOnAssistMaxStep_;

        if (isEnemyWarpStart) {
            assistStrength = warpStartAssistStrength_;
            assistMaxStep = warpStartAssistMaxStep_;
        } else if (isEnemyWarpMove) {
            // 繝ｯ繝ｼ繝礼�E��E�蜍穂ｸ�E�縺�E�辟｡送E�E↓謖ｯ繧雁屓縺輔�E縺・
            assistStrength = 0.0f;
            assistMaxStep = 0.0f;
        } else if (isEnemyWarpEnd) {
            assistStrength = warpEndAssistStrength_;
            assistMaxStep = warpEndAssistMaxStep_;
        } else if (isEnemyPhaseTransition) {
            assistStrength = lockOnAssistStrength_ * 1.35f;
            assistMaxStep = lockOnAssistMaxStep_ * 1.35f;
        }

        float dx = assistTarget.x - playerPos.x;
        float dz = assistTarget.z - playerPos.z;

        float targetYaw = std::atan2f(dx, dz);
        float diff = targetYaw - cameraYaw_;

        while (diff > 3.14159265f) {
            diff -= 6.28318530f;
        }
        while (diff < -3.14159265f) {
            diff += 6.28318530f;
        }

        float inputMagnitude = 0.0f;
#ifdef _DEBUG
        Input *input = ctx_->input;
        if (input->IsKeyPress(DIK_LEFT) || input->IsKeyPress(DIK_RIGHT)) {
            inputMagnitude = 1.0f;
        }
#endif

        float assistScale = 1.0f;
        if (inputMagnitude > 0.0f) {
            assistScale = lockOnInputReduce_;
        }

        float maxStep = assistMaxStep * assistScale * ctx_->deltaTime;
        float applied = diff * assistStrength * assistScale * ctx_->deltaTime;

        if (applied > maxStep) {
            applied = maxStep;
        } else if (applied < -maxStep) {
            applied = -maxStep;
        }

        cameraYaw_ += applied;
    }

    // =========================
    // yaw / pitch 縺九ｉ蝓�E�貁E�E�E��E�繧剁E��懊ａE
    // =========================
    float cosPitch = std::cosf(cameraPitch_);
    DirectX::XMFLOAT3 forward = {std::sinf(cameraYaw_) * cosPitch,
                                 std::sinf(cameraPitch_),
                                 std::cosf(cameraYaw_) * cosPitch};

    DirectX::XMFLOAT3 right = {std::cosf(cameraYaw_), 0.0f,
                               -std::sinf(cameraYaw_)};

    // =========================
    // 繧�E�繝｡繝ｩ蝓ｺ貁E��せ
    // =========================
    DirectX::XMFLOAT3 cameraTargetBase = {
        playerPos.x, playerPos.y + cameraLookHeight_, playerPos.z};

    DirectX::XMFLOAT3 cameraPos{};

    if (isLockOn_) {
        // ---------------------------------
        // 繝ｭ繝�Eけ繧�E�繝ｳ譎�E 謨�E�縺�E�縺�E�繝ｩ繧�E�繝ｳ蝓ｺ貁E��〒蜀・�E��E�霑ｽ蠕�E
        // ---------------------------------
        float toEnemyX = enemyPos.x - playerPos.x;
        float toEnemyZ = enemyPos.z - playerPos.z;
        float distXZ = std::sqrt(toEnemyX * toEnemyX + toEnemyZ * toEnemyZ);

        if (distXZ < 0.0001f) {
            distXZ = 1.0f;
        }

        float invLen = 1.0f / distXZ;
        float lineX = toEnemyX * invLen;
        float lineZ = toEnemyZ * invLen;

        // 謨�E�譁E��蜷代Λ繧�E�繝ｳ縺�E�蟁E��縺吶�E�蜿�E�繝吶け繝医΁E
        float orbitRightX = lineZ;
        float orbitRightZ = -lineX;

        // 謨�E�縺�E�縺�E�霍晞屬縺�E�蟁E���E�縺�E�縺大�E�後ｍ縺�E�蠑輔￥
        float pullT = 0.0f;
        {
            float minD = lockOnDistanceMin_;
            float maxD = lockOnDistanceMax_;
            float range = maxD - minD;
            if (range > 0.0001f) {
                pullT = (distXZ - minD) / range;
            }
            if (pullT < 0.0f) {
                pullT = 0.0f;
            }
            if (pullT > 1.0f) {
                pullT = 1.0f;
            }
        }

        float usedRadius = lockOnOrbitRadius_ + lockOnOrbitPullBackMax_ * pullT;
        if (isEnemyPhaseTransition) {
            usedRadius -= phaseTransitionPushIn_ * enemyPhaseTransitionRatio;
        }

        // cameraYaw_ 縺�E�謨�E�譁E��蜷代Λ繧�E�繝ｳ縺�E�縺�E�蟾�E�縺�E�縲∝�E蠑ｧ荳翫・蟾�E�蜿�E�菴咲�E��E�繧呈ｱ�E�繧√ａE
        float lineYaw = std::atan2f(lineX, lineZ);
        float yawDiff = cameraYaw_ - lineYaw;

        while (yawDiff > 3.14159265f) {
            yawDiff -= 6.28318530f;
        }
        while (yawDiff < -3.14159265f) {
            yawDiff += 6.28318530f;
        }

        // 逵滓ｨ�E�縺�E�縺�E�蝗槭�E�縺吶℁E��九�E隕九▼繧峨�E�縺�E�縺�E�蛻�E�髯・
        const float maxOrbitAngle = 0.65f;
        if (yawDiff > maxOrbitAngle) {
            yawDiff = maxOrbitAngle;
        } else if (yawDiff < -maxOrbitAngle) {
            yawDiff = -maxOrbitAngle;
        }

        float sinA = std::sinf(yawDiff);
        float cosA = std::cosf(yawDiff);

        DirectX::XMFLOAT3 pairCenter = {
            playerPos.x * lockOnLookPlayerWeight_ +
                enemyPos.x * lockOnLookEnemyWeight_,
            (playerPos.y + cameraLookHeight_) * 0.55f +
                (enemyPos.y + 1.35f) * 0.45f,
            playerPos.z * lockOnLookPlayerWeight_ +
                enemyPos.z * lockOnLookEnemyWeight_};

        DirectX::XMFLOAT3 desiredCameraPos = {
            pairCenter.x - lineX * usedRadius * cosA +
                orbitRightX * usedRadius * sinA +
                orbitRightX * lockOnOrbitSideBias_,
            pairCenter.y + lockOnOrbitHeight_ + 0.20f * pullT,
            pairCenter.z - lineZ * usedRadius * cosA +
                orbitRightZ * usedRadius * sinA +
                orbitRightZ * lockOnOrbitSideBias_};

        float posAlpha = lockOnOrbitLerpSpeed_ * ctx_->deltaTime;
        if (posAlpha > 1.0f) {
            posAlpha = 1.0f;
        }

        lockOnOrbitCameraPos_.x +=
            (desiredCameraPos.x - lockOnOrbitCameraPos_.x) * posAlpha;
        lockOnOrbitCameraPos_.y +=
            (desiredCameraPos.y - lockOnOrbitCameraPos_.y) * posAlpha;
        lockOnOrbitCameraPos_.z +=
            (desiredCameraPos.z - lockOnOrbitCameraPos_.z) * posAlpha;

        cameraPos = lockOnOrbitCameraPos_;
    } else {
        // ---------------------------------
        // 騾壼�E��E�譎�E 閧�E�雜翫�E�荳我ｺ�E�遘ｰ
        // ---------------------------------
        float dx = enemyPos.x - playerPos.x;
        float dz = enemyPos.z - playerPos.z;
        float enemyDistanceXZ = std::sqrt(dx * dx + dz * dz);
        float dynamicDistance = cameraDistance_;
        if (enemyDistanceXZ > 5.0f) {
            dynamicDistance +=
                (std::min)(1.1f, (enemyDistanceXZ - 5.0f) * 0.18f);
        }

        cameraPos = {cameraTargetBase.x - forward.x * dynamicDistance +
                         right.x * cameraSideOffset_,
                     cameraTargetBase.y + cameraHeight_ -
                         forward.y * dynamicDistance,
                     cameraTargetBase.z - forward.z * dynamicDistance +
                         right.z * cameraSideOffset_};

        if (isEnemyPhaseTransition) {
            cameraPos.x += forward.x * phaseTransitionPushIn_ *
                           enemyPhaseTransitionRatio;
            cameraPos.y += 0.12f * enemyPhaseTransitionRatio;
            cameraPos.z += forward.z * phaseTransitionPushIn_ *
                           enemyPhaseTransitionRatio;
        }

        // 髱槭Ο繝�Eけ譎ゅ・蜀・�E��E�逕ｨ迴�E�蝨�E�蛟､繧貞�E譛�E
        lockOnOrbitCameraPos_ = cameraPos;
    }

    // =========================
    // 豕ｨ隕也せ
    // =========================
    DirectX::XMFLOAT3 lookAt{};

    if (isLockOn_) {
        DirectX::XMFLOAT3 desiredLookAt = {
            playerPos.x * lockOnLookPlayerWeight_ +
                enemyPos.x * lockOnLookEnemyWeight_,
            (playerPos.y + cameraLookHeight_) * 0.52f +
                (enemyPos.y + 1.30f) * 0.48f,
            playerPos.z * lockOnLookPlayerWeight_ +
                enemyPos.z * lockOnLookEnemyWeight_};

        float lookAlpha = lockOnLookAtLerpSpeed_ * ctx_->deltaTime;
        if (lookAlpha > 1.0f) {
            lookAlpha = 1.0f;
        }

        lockOnLookAt_.x += (desiredLookAt.x - lockOnLookAt_.x) * lookAlpha;
        lockOnLookAt_.y += (desiredLookAt.y - lockOnLookAt_.y) * lookAlpha;
        lockOnLookAt_.z += (desiredLookAt.z - lockOnLookAt_.z) * lookAlpha;

        lookAt = lockOnLookAt_;
    } else {
        lookAt = {cameraTargetBase.x + forward.x * cameraLookAhead_,
                  cameraTargetBase.y + forward.y * cameraLookAhead_,
                  cameraTargetBase.z + forward.z * cameraLookAhead_};

        lockOnLookAt_ = lookAt;
    }

    if (isEnemyPhaseTransition) {
        DirectX::XMFLOAT3 transitionLookAt = {
            playerPos.x * (1.0f - phaseTransitionLookAtEnemyWeight_) +
                enemyPos.x * phaseTransitionLookAtEnemyWeight_,
            (playerPos.y + cameraLookHeight_) *
                    (1.0f - phaseTransitionLookAtEnemyWeight_) +
                (enemyPos.y + phaseTransitionLookAtHeight_) *
                    phaseTransitionLookAtEnemyWeight_,
            playerPos.z * (1.0f - phaseTransitionLookAtEnemyWeight_) +
                enemyPos.z * phaseTransitionLookAtEnemyWeight_};

        float blend = enemyPhaseTransitionRatio;
        lookAt.x += (transitionLookAt.x - lookAt.x) * blend;
        lookAt.y += (transitionLookAt.y - lookAt.y) * blend;
        lookAt.z += (transitionLookAt.z - lookAt.z) * blend;
    }

    camera_.SetPosition(cameraPos);
    camera_.LookAt(lookAt);
}

////////////////////////////
//lightring
///////////////////////////
void GameScene::SpawnElectricRing(const XMFLOAT3 &worldPos, bool isWarpEnd) {
    activeElectricRing_ = {};
    activeElectricRing_.active = true;
    activeElectricRing_.worldPos = worldPos;
    activeElectricRing_.worldPos.y += 1.1f;
    activeElectricRing_.time = 0.0f;

    if (!isWarpEnd) {
        // ワープ開姁E
        activeElectricRing_.lifeTime = 0.45f;
        activeElectricRing_.startRadius = 0.01f;
        activeElectricRing_.endRadius = 0.18f;
        activeElectricRing_.ringWidth = 0.012f;
        activeElectricRing_.distortionWidth = 0.040f;
        activeElectricRing_.distortionStrength = 0.022f;
        activeElectricRing_.swirlStrength = 0.008f;
        activeElectricRing_.cloudScale = 3.8f;
        activeElectricRing_.cloudIntensity = 1.10f;
        activeElectricRing_.brightness = 1.80f;
        activeElectricRing_.haloIntensity = 0.80f;
    } else {
        // ワープ終亁E
        activeElectricRing_.lifeTime = 0.65f;
        activeElectricRing_.startRadius = 0.02f;
        activeElectricRing_.endRadius = 0.28f;
        activeElectricRing_.ringWidth = 0.015f;
        activeElectricRing_.distortionWidth = 0.045f;
        activeElectricRing_.distortionStrength = 0.018f;
        activeElectricRing_.swirlStrength = 0.006f;
        activeElectricRing_.cloudScale = 3.5f;
        activeElectricRing_.cloudIntensity = 1.40f;
        activeElectricRing_.brightness = 2.40f;
        activeElectricRing_.haloIntensity = 1.00f;
    }
}

void GameScene::UpdateElectricRing() {
    if (!activeElectricRing_.active || ctx_ == nullptr) {
        return;
    }

    activeElectricRing_.time += ctx_->deltaTime;
    if (activeElectricRing_.time >= activeElectricRing_.lifeTime) {
        activeElectricRing_.active = false;
        return;
    }
}
