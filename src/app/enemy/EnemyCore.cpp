#include "Enemy.h"
#include "ModelManager.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

// ============================================================
// 初期化処理
// ============================================================
void Enemy::Initialize(uint32_t modelId) {
    modelId_ = modelId;
    hp_ = maxHp_;
    phase_ = BossPhase::Phase1;
    phaseTransitionActive_ = false;
    phaseTransitionTimer_ = 0.0f;

    tf_.position = {0.0f, 0.0f, 10.0f};
    tf_.scale = {1.0f, 1.0f, 1.0f};
    tf_.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    ResetWarpTrails();

    UpdateParts();
    ValidateAllTimings();
}

// ============================================================
// 毎フレーム更新処理（旧互換）
// ============================================================
void Enemy::Update(const DirectX::XMFLOAT3 &playerPos, float deltaTime,
                   bool playerGuarding) {
    PlayerCombatObservation obs{};
    obs.position = playerPos;
    obs.isGuarding = playerGuarding;
    Update(obs, deltaTime);
}

// ============================================================
// 毎フレーム更新処理（新版）
// ============================================================
void Enemy::Update(const PlayerCombatObservation &playerObs, float deltaTime) {
    if (deathFinished_) {
        return;
    }

    playerObs_ = playerObs;
    playerPos_ = playerObs.position;
    playerGuarding_ = playerObs.isGuarding;
    UpdateBossPhase();
    UpdateWarpTrails(deltaTime);

    if (isDying_) {
        deathTimer_ += deltaTime;

        float t = deathTimer_ / deathDuration_;
        if (t > 1.0f) {
            t = 1.0f;
        }

        tf_.position.y = deathStartY_ - deathSinkDistance_ * t;
        tf_.scale.x = 1.0f - 0.25f * t;
        tf_.scale.y = 1.0f - 0.55f * t;
        tf_.scale.z = 1.0f - 0.25f * t;

        UpdateBullets(deltaTime);
        UpdateWaves(deltaTime);
        UpdateParts();

        if (deathTimer_ >= deathDuration_) {
            deathFinished_ = true;
        }
        return;
    }

    if (phaseTransitionActive_) {
        phaseTransitionTimer_ += deltaTime;
        isAttackActive_ = false;
        isGuardActive_ = false;

        UpdateFacingToPlayerWithSpeed(deltaTime, idleTurnSpeed_ * 0.35f);
        UpdateBullets(deltaTime);
        UpdateWaves(deltaTime);
        UpdateParts();

        if (phaseTransitionTimer_ >= phaseTransitionDuration_) {
            phaseTransitionActive_ = false;
            phaseTransitionTimer_ = 0.0f;
            stateTimer_ = 0.0f;
        }
        return;
    }

    float currentDistance = GetDistanceToPlayer();
    float distanceDelta = std::fabs(currentDistance - lastDistanceToPlayer_);

    if (distanceDelta < stagnantDistanceThreshold_) {
        stagnantTimer_ += deltaTime;
    } else {
        stagnantTimer_ = 0.0f;
    }
    isDistanceStagnant_ = (stagnantTimer_ >= stagnantTimeThreshold_);

    if (currentDistance <= closePressureDistance_) {
        closePressureTimer_ += deltaTime;
        if (closePressureTimer_ > closePressureTimeThreshold_) {
            closePressureTimer_ = closePressureTimeThreshold_;
        }
    } else {
        closePressureTimer_ -= deltaTime;
        if (closePressureTimer_ < 0.0f) {
            closePressureTimer_ = 0.0f;
        }
    }

    if (currentDistance > farAttackDistance_) {
        farDistanceTimer_ += deltaTime;
    } else {
        farDistanceTimer_ = 0.0f;
    }

    if (warpEscapeCooldownTimer_ > 0.0f) {
        warpEscapeCooldownTimer_ -= deltaTime;
        if (warpEscapeCooldownTimer_ < 0.0f) {
            warpEscapeCooldownTimer_ = 0.0f;
        }
    }

    lastDistanceToPlayer_ = currentDistance;

    if (action_.kind == ActionKind::None) {
        UpdateFacingToPlayerWithSpeed(deltaTime, idleTurnSpeed_);
    }

    stateTimer_ += deltaTime;

    isAttackActive_ = false;
    isGuardActive_ = false;

    UpdateCounterAdaptation(deltaTime);

    if (hitReactionTimer_ > 0.0f) {
        stateTimer_ -= deltaTime;
        if (stateTimer_ < 0.0f) {
            stateTimer_ = 0.0f;
        }

        hitReactionTimer_ -= deltaTime;
        if (hitReactionTimer_ < 0.0f) {
            hitReactionTimer_ = 0.0f;
        }

        float dx = tf_.position.x - playerPos_.x;
        float dz = tf_.position.z - playerPos_.z;
        float len = std::sqrtf(dx * dx + dz * dz);
        if (len > 0.0001f) {
            dx /= len;
            dz /= len;

            tf_.position.x += dx * hitReactionMoveSpeed_ * deltaTime;
            tf_.position.z += dz * hitReactionMoveSpeed_ * deltaTime;
        }

        UpdateBullets(deltaTime);
        UpdateWaves(deltaTime);
        UpdateParts();
        return;
    }

    // ------------------------------------------------------------
    // カウンター成功リアクション
    // ------------------------------------------------------------
    if (playerObs_.justCountered) {
        RegisterCounterSuccessReaction();

        const bool isCounterBreakableAction =
            (action_.kind == ActionKind::Smash) ||
            (action_.kind == ActionKind::Sweep) ||
            (action_.kind == ActionKind::Rush);

        if (isCounterBreakableAction) {
            float dx = tf_.position.x - playerPos_.x;
            float dz = tf_.position.z - playerPos_.z;
            float len = std::sqrtf(dx * dx + dz * dz);
            if (len < 0.0001f) {
                len = 1.0f;
            }

            dx /= len;
            dz /= len;

            const float counterPushBack = 0.9f;
            tf_.position.x += dx * counterPushBack;
            tf_.position.z += dz * counterPushBack;

            EndAttack();
            ResetChainContext();
            ResetPostActionState();

            stateTimer_ = -0.20f;

            tactic_ = TacticState::Reset;
            closePressureTimer_ = 0.0f;
            stagnantTimer_ = 0.0f;
            isDistanceStagnant_ = false;

            UpdateFacingToPlayer();
            UpdateParts();
            return;
        }
    }

    UpdateByAction(deltaTime);

    UpdateBullets(deltaTime);
    UpdateWaves(deltaTime);

    UpdateParts();
}

// ============================================================
// 描画処理
// ============================================================
void Enemy::Draw(ModelManager *modelManager, const Camera &camera) {
    if (deathFinished_) {
        return;
    }

    const bool isWarpMoveHidden =
        (action_.kind == ActionKind::Warp && action_.step == ActionStep::Move);
    ModelDrawEffect warpEffect{};

    if (action_.kind == ActionKind::Warp) {
        warpEffect.enabled = true;
        warpEffect.additiveBlend = true;
        warpEffect.color = {0.70f, 0.04f, 0.22f, 0.82f};
        warpEffect.intensity =
            (action_.step == ActionStep::Move) ? 2.05f : 1.70f;
        warpEffect.fresnelPower = 4.6f;
        warpEffect.noiseAmount = 0.88f;
        warpEffect.time = stateTimer_;

        modelManager->SetDrawEffect(warpEffect);
    }

    auto drawEnemyParts = [&](const Transform &body, const Transform &left,
                              const Transform &right) {
        modelManager->Draw(modelId_, body, camera);
        modelManager->Draw(modelId_, left, camera);
        modelManager->Draw(modelId_, right, camera);
    };

    if (isVisible_ && !isWarpMoveHidden) {
        drawEnemyParts(bodyTf_, leftHandTf_, rightHandTf_);
    }

    if (action_.kind == ActionKind::Warp) {
        for (const auto &trail : warpTrailGhosts_) {
            if (!trail.isActive || trail.life <= 0.0f) {
                continue;
            }

            float alpha = trail.life / warpTrailLife_;
            Transform trailBody = bodyTf_;
            Transform trailLeft = leftHandTf_;
            Transform trailRight = rightHandTf_;

            trailBody.position = trail.position;
            trailLeft.position = trail.position;
            trailRight.position = trail.position;

            trailBody.scale.x *= 0.12f * trail.scale;
            trailBody.scale.y *= (0.92f + 0.18f * alpha) * trail.scale;
            trailBody.scale.z *= (1.45f + 1.10f * alpha) * trail.scale;
            trailLeft.scale.x *= 0.08f * trail.scale;
            trailLeft.scale.y *= 0.22f * trail.scale;
            trailLeft.scale.z *= (0.72f + 0.42f * alpha) * trail.scale;
            trailRight.scale.x *= 0.08f * trail.scale;
            trailRight.scale.y *= 0.22f * trail.scale;
            trailRight.scale.z *= (0.72f + 0.42f * alpha) * trail.scale;

            drawEnemyParts(trailBody, trailLeft, trailRight);
        }
    }

    if (action_.kind == ActionKind::Warp) {
        float warpPulse = 0.5f + 0.5f * std::sinf(stateTimer_ * 28.0f);
        float previewScale =
            warpArrivalPreviewScale_ + 0.10f * warpPulse;

        float usedYaw = facingYaw_;
        if (warp_.type == WarpType::Approach && warp_.approachSlot !=
                                               WarpApproachSlot::LongFront) {
            usedYaw = lockedAttackYaw_;
        }

        float forwardX = std::sinf(usedYaw);
        float forwardZ = std::cosf(usedYaw);
        float rightX = std::cosf(usedYaw);
        float rightZ = -std::sinf(usedYaw);
        const float pathDx = warp_.targetPos.x - warp_.departurePos.x;
        const float pathDz = warp_.targetPos.z - warp_.departurePos.z;
        float pathLen = std::sqrtf(pathDx * pathDx + pathDz * pathDz);
        float pathDirX = forwardX;
        float pathDirZ = forwardZ;
        if (pathLen > 0.0001f) {
            pathDirX = pathDx / pathLen;
            pathDirZ = pathDz / pathLen;
        }

        auto drawWarpParticleCluster = [&](const DirectX::XMFLOAT3 &center,
                                           float timeBias) {
            for (int i = 0; i < warpParticleCount_; ++i) {
                float angle = stateTimer_ * 8.0f + timeBias +
                              (6.2831853f * static_cast<float>(i) /
                               static_cast<float>(warpParticleCount_));
                float radius =
                    warpParticleRadius_ * (0.75f + 0.25f * warpPulse);
                float orbitX = std::cosf(angle) * radius;
                float orbitZ = std::sinf(angle) * radius;
                float height = warpParticleHeight_ *
                               (0.45f + 0.55f *
                                           std::sinf(angle * 1.7f + timeBias));

                Transform particleTf = bodyTf_;
                particleTf.position = center;
                particleTf.position.x += orbitX;
                particleTf.position.y += height;
                particleTf.position.z += orbitZ;

                float scalePulse = 0.85f + 0.25f *
                                               std::sinf(angle * 2.1f +
                                                         stateTimer_ * 14.0f);
                particleTf.scale = {
                    warpParticleScale_ * scalePulse,
                    warpParticleScale_ * (1.15f + 0.10f * warpPulse),
                    warpParticleScale_ * scalePulse,
                };
                modelManager->Draw(modelId_, particleTf, camera);
            }
        };

        auto drawWarpStreakBurst = [&](const DirectX::XMFLOAT3 &center,
                                       float spreadScale, float timeBias) {
            for (int i = 0; i < warpParticleCount_ + 2; ++i) {
                float ratio = static_cast<float>(i) /
                              static_cast<float>(warpParticleCount_ + 1);
                float side = (ratio - 0.5f) * 2.0f;
                float pulse = 0.65f + 0.35f *
                                          std::sinf(stateTimer_ * 16.0f +
                                                    timeBias + ratio * 9.0f);
                float forwardOffset = spreadScale * (0.20f + ratio * 0.95f);
                float sideOffset = warpParticleRadius_ * 0.55f * side;

                Transform streakTf = bodyTf_;
                streakTf.position = center;
                streakTf.position.x += pathDirX * forwardOffset +
                                       rightX * sideOffset;
                streakTf.position.z += pathDirZ * forwardOffset +
                                       rightZ * sideOffset;
                streakTf.position.y += warpParticleHeight_ * (0.16f + ratio * 0.4f);
                streakTf.scale = {
                    warpParticleScale_ * (0.16f + 0.05f * pulse),
                    warpParticleScale_ * (0.72f + 0.25f * pulse),
                    warpParticleScale_ * (2.9f + 2.4f * spreadScale),
                };
                modelManager->Draw(modelId_, streakTf, camera);
            }
        };

        if ((action_.step == ActionStep::Start || action_.step == ActionStep::Move) &&
            warp_.hasDeparturePos) {
            for (int i = 0; i < 2; ++i) {
                float t = static_cast<float>(i + 1) / 2.0f;
                Transform echoBody = bodyTf_;
                Transform echoLeft = leftHandTf_;
                Transform echoRight = rightHandTf_;

                echoBody.position = warp_.departurePos;
                echoLeft.position = warp_.departurePos;
                echoRight.position = warp_.departurePos;

                float sideSign = 0.0f;
                if (warp_.approachSlot == WarpApproachSlot::FrontLeft) {
                    sideSign = -1.0f;
                } else if (warp_.approachSlot == WarpApproachSlot::FrontRight) {
                    sideSign = 1.0f;
                }

                float sideOffset = warpDepartureEchoOffset_ * t * sideSign;
                float backOffset = warpDepartureEchoOffset_ * (1.0f - t * 0.35f);

                echoBody.position.x += (-forwardX) * backOffset + rightX * sideOffset;
                echoBody.position.z += (-forwardZ) * backOffset + rightZ * sideOffset;
                echoBody.position.y += 0.04f * t;
                echoBody.scale.x *= 0.96f - 0.08f * t;
                echoBody.scale.y *= 0.92f - 0.10f * t;
                echoBody.scale.z *= 0.94f - 0.08f * t;

                echoLeft.position.x += (-forwardX) * backOffset + rightX * sideOffset;
                echoLeft.position.z += (-forwardZ) * backOffset + rightZ * sideOffset;
                echoLeft.position.y += 0.04f * t;
                echoLeft.scale.x *= 0.94f - 0.08f * t;
                echoLeft.scale.y *= 0.94f - 0.08f * t;
                echoLeft.scale.z *= 0.94f - 0.08f * t;

                echoRight.position.x += (-forwardX) * backOffset + rightX * sideOffset;
                echoRight.position.z += (-forwardZ) * backOffset + rightZ * sideOffset;
                echoRight.position.y += 0.04f * t;
                echoRight.scale.x *= 0.94f - 0.08f * t;
                echoRight.scale.y *= 0.94f - 0.08f * t;
                echoRight.scale.z *= 0.94f - 0.08f * t;

                drawEnemyParts(echoBody, echoLeft, echoRight);
            }

            drawWarpParticleCluster(warp_.departurePos, 0.0f);
            drawWarpStreakBurst(warp_.departurePos, 0.34f, 0.3f);
        }

        if (action_.step == ActionStep::Move && warp_.hasDeparturePos &&
            warp_.hasValidTarget) {
            DirectX::XMFLOAT3 moveCenter{};
            moveCenter.x = (warp_.departurePos.x + warp_.targetPos.x) * 0.5f;
            moveCenter.y =
                (warp_.departurePos.y + warp_.targetPos.y) * 0.5f + 0.08f;
            moveCenter.z = (warp_.departurePos.z + warp_.targetPos.z) * 0.5f;
            for (int i = 0; i < 1; ++i) {
                float t = 0.5f;

                Transform ghostBody = bodyTf_;
                Transform ghostLeft = leftHandTf_;
                Transform ghostRight = rightHandTf_;

                DirectX::XMFLOAT3 ghostCenter{};
                ghostCenter.x = warp_.departurePos.x +
                                (warp_.targetPos.x - warp_.departurePos.x) * t;
                ghostCenter.y = warp_.departurePos.y +
                                (warp_.targetPos.y - warp_.departurePos.y) * t +
                                0.06f * (1.0f - std::fabs(0.5f - t));
                ghostCenter.z = warp_.departurePos.z +
                                (warp_.targetPos.z - warp_.departurePos.z) * t;

                ghostBody.position = ghostCenter;
                ghostLeft.position = ghostCenter;
                ghostRight.position = ghostCenter;

                float sideBlend = (t - 0.5f) * 2.0f;
                ghostBody.position.x += rightX * warpDepartureEchoOffset_ * 0.30f *
                                        sideBlend;
                ghostBody.position.z += rightZ * warpDepartureEchoOffset_ * 0.30f *
                                        sideBlend;
                ghostLeft.position.x += rightX * warpDepartureEchoOffset_ * 0.30f *
                                        sideBlend;
                ghostLeft.position.z += rightZ * warpDepartureEchoOffset_ * 0.30f *
                                        sideBlend;
                ghostRight.position.x += rightX * warpDepartureEchoOffset_ * 0.30f *
                                         sideBlend;
                ghostRight.position.z += rightZ * warpDepartureEchoOffset_ * 0.30f *
                                         sideBlend;

                ghostBody.scale.x *= warpMoveGhostScaleX_ * 0.78f;
                ghostBody.scale.y *= warpMoveGhostScaleY_ * 0.92f;
                ghostBody.scale.z *= warpMoveGhostScaleZ_ * 0.82f;
                ghostLeft.scale.x *= warpMoveGhostScaleX_ * 0.62f;
                ghostLeft.scale.y *= warpMoveGhostScaleY_ * 0.60f;
                ghostLeft.scale.z *= warpMoveGhostScaleZ_ * 0.62f;
                ghostRight.scale.x *= warpMoveGhostScaleX_ * 0.62f;
                ghostRight.scale.y *= warpMoveGhostScaleY_ * 0.60f;
                ghostRight.scale.z *= warpMoveGhostScaleZ_ * 0.62f;

                drawEnemyParts(ghostBody, ghostLeft, ghostRight);
            }
            drawWarpParticleCluster(moveCenter, 1.7f);
            drawWarpStreakBurst(moveCenter, 0.92f, 1.2f);
        }

        if ((action_.step == ActionStep::Start || action_.step == ActionStep::Move ||
             action_.step == ActionStep::End) &&
            warp_.hasValidTarget) {
            for (int i = 0; i < 2; ++i) {
                float t = static_cast<float>(i + 1) / 2.0f;
                Transform echoBody = bodyTf_;
                Transform echoLeft = leftHandTf_;
                Transform echoRight = rightHandTf_;

                echoBody.position = warp_.targetPos;
                echoLeft.position = warp_.targetPos;
                echoRight.position = warp_.targetPos;

                float sideOffset = warpArrivalEchoOffset_ * t;
                float heightOffset =
                    warpArrivalPreviewHeight_ * (1.0f + 0.5f * warpPulse) * t;

                if (warp_.approachSlot == WarpApproachSlot::FrontLeft) {
                    echoBody.position.x += (-rightX) * sideOffset;
                    echoBody.position.z += (-rightZ) * sideOffset;
                    echoLeft.position.x += (-rightX) * sideOffset;
                    echoLeft.position.z += (-rightZ) * sideOffset;
                    echoRight.position.x += (-rightX) * sideOffset;
                    echoRight.position.z += (-rightZ) * sideOffset;
                } else if (warp_.approachSlot == WarpApproachSlot::FrontRight) {
                    echoBody.position.x += rightX * sideOffset;
                    echoBody.position.z += rightZ * sideOffset;
                    echoLeft.position.x += rightX * sideOffset;
                    echoLeft.position.z += rightZ * sideOffset;
                    echoRight.position.x += rightX * sideOffset;
                    echoRight.position.z += rightZ * sideOffset;
                } else {
                    echoBody.position.x += forwardX * sideOffset;
                    echoBody.position.z += forwardZ * sideOffset;
                    echoLeft.position.x += forwardX * sideOffset;
                    echoLeft.position.z += forwardZ * sideOffset;
                    echoRight.position.x += forwardX * sideOffset;
                    echoRight.position.z += forwardZ * sideOffset;
                }

                echoBody.position.y += heightOffset;
                echoLeft.position.y += heightOffset;
                echoRight.position.y += heightOffset;

                echoBody.scale.x *= previewScale - 0.24f * t;
                echoBody.scale.y *= previewScale + 0.08f * (1.0f - t);
                echoBody.scale.z *= previewScale - 0.12f * t;
                echoLeft.scale.x *= 0.90f + 0.08f * warpPulse;
                echoLeft.scale.y *= 0.90f + 0.08f * warpPulse;
                echoLeft.scale.z *= 0.90f + 0.08f * warpPulse;
                echoRight.scale.x *= 0.90f + 0.08f * warpPulse;
                echoRight.scale.y *= 0.90f + 0.08f * warpPulse;
                echoRight.scale.z *= 0.90f + 0.08f * warpPulse;

                drawEnemyParts(echoBody, echoLeft, echoRight);
            }

            drawWarpParticleCluster(warp_.targetPos, 3.1f);
            drawWarpStreakBurst(warp_.targetPos, 0.58f, 2.4f);
        }
    }

    if (action_.kind == ActionKind::Warp) {
        modelManager->ClearDrawEffect();
    }

    for (const auto &bullet : bullets_) {
        if (!bullet.isAlive) {
            continue;
        }

        Transform bulletTf = tf_;
        bulletTf.position = bullet.position;
        bulletTf.scale = {0.2f, 0.2f, 0.2f};

        modelManager->Draw(modelId_, bulletTf, camera);
    }

    for (const auto &wave : waves_) {
        if (!wave.isAlive) {
            continue;
        }

        Transform waveTf = tf_;
        waveTf.position = wave.position;
        waveTf.scale = {0.6f, 0.2f, 1.2f};

        modelManager->Draw(modelId_, waveTf, camera);
    }
}

// ============================================================
// 被ダメージ処理
// ============================================================
void Enemy::TakeDamage(float damage) {
    if (deathFinished_ || isDying_) {
        return;
    }

    hp_ -= damage;

    if (hp_ < 0.0f) {
        hp_ = 0.0f;
    }

    UpdateBossPhase();

    if (hp_ <= 0.0f) {
        isDying_ = true;
        deathTimer_ = 0.0f;
        deathStartY_ = tf_.position.y;
        hitReactionTimer_ = 0.0f;
        tf_.scale = {1.0f, 1.0f, 1.0f};
        EndAttack();
        UpdateParts();
        return;
    }

    hitReactionTimer_ = hitReactionDuration_;
}

void Enemy::UpdateWarpTrails(float deltaTime) {
    for (auto &trail : warpTrailGhosts_) {
        if (!trail.isActive) {
            continue;
        }

        trail.life -= deltaTime;
        if (trail.life <= 0.0f) {
            trail.life = 0.0f;
            trail.isActive = false;
        }
    }
}

void Enemy::EmitWarpTrailGhost(const DirectX::XMFLOAT3 &position, float scale) {
    int slot = -1;
    for (int i = 0; i < kWarpTrailGhostCount_; ++i) {
        if (!warpTrailGhosts_[i].isActive) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        slot = 0;
        for (int i = 1; i < kWarpTrailGhostCount_; ++i) {
            if (warpTrailGhosts_[i].life < warpTrailGhosts_[slot].life) {
                slot = i;
            }
        }
    }

    warpTrailGhosts_[slot].position = position;
    warpTrailGhosts_[slot].life = warpTrailLife_;
    warpTrailGhosts_[slot].scale = scale;
    warpTrailGhosts_[slot].isActive = true;
}

void Enemy::ResetWarpTrails() {
    warpTrailEmitTimer_ = 0.0f;
    for (auto &trail : warpTrailGhosts_) {
        trail = WarpTrailGhost{};
    }
}

void Enemy::ConsumeBullet(size_t index) {
    if (index >= bullets_.size()) {
        return;
    }

    bullets_[index].isAlive = false;
    bullets_[index].lifeTime = 0.0f;
}

void Enemy::ConsumeWave(size_t index) {
    if (index >= waves_.size()) {
        return;
    }

    waves_[index].isAlive = false;
    waves_[index].traveledDistance = waves_[index].maxDistance;
}

void Enemy::NotifyAttackConnected() { currentActionConnected_ = true; }

void Enemy::NotifyAttackGuarded() { currentActionGuarded_ = true; }

// ============================================================
// action ベース更新
// ============================================================
void Enemy::UpdateByAction(float deltaTime) {
    if (action_.kind == ActionKind::None) {
        UpdateIdle(deltaTime);
        return;
    }

    switch (action_.kind) {
    case ActionKind::Smash:
        UpdateSmashByStep(deltaTime);
        break;
    case ActionKind::Sweep:
        UpdateSweepByStep(deltaTime);
        break;
    case ActionKind::Shot:
        UpdateShotByStep(deltaTime);
        break;
    case ActionKind::Wave:
        UpdateWaveByStep(deltaTime);
        break;
    case ActionKind::Rush:
        UpdateRushByStep(deltaTime);
        break;
    case ActionKind::Warp:
        UpdateWarpByStep(deltaTime);
        break;
    case ActionKind::Guard:
        UpdateGuardByStep(deltaTime);
        break;
    case ActionKind::Stalk: // 追加
        UpdateStalkByStep(deltaTime);
        break;
    default:
        UpdateIdle(deltaTime);
        break;
    }
}

// ============================================================
// 行動開始・行動遷移
// ============================================================
void Enemy::BeginAction(ActionKind kind, ActionStep step) {
    lastActionKind_ = kind;

    if (kind != ActionKind::Rush) {
        rushFromShotCombo_ = false;
    }

    if (kind == ActionKind::Warp) {
        stagnantTimer_ = 0.0f;
        isDistanceStagnant_ = false;
        ResetWarpTrails();

        if (warp_.type == WarpType::Escape) {
            warpEscapeCooldownTimer_ = warpEscapeCooldown_;
        }
    } else {
        ResetWarpContext();
    }

    action_.kind = kind;
    action_.id = MakeDefaultActionId(kind);

    if (kind == ActionKind::Smash) {
        float r =
            static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);

        float useDelayChance = delaySmashChance_;
        if (tactic_ == TacticState::CounterBait || playerObs_.isCounterStance) {
            useDelayChance += 0.20f;
        }
        if (phase_ == BossPhase::Phase2) {
            useDelayChance += phase2DelaySmashBonus_;
        }

        if (r < useDelayChance) {
            action_.id = ActionId::DelaySmash;
        }
    }

    if (kind == ActionKind::Sweep) {
        float r =
            static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);

        float useDoubleChance = doubleSweepChance_;
        if (tactic_ == TacticState::CounterPunish || IsCounterFailObserved()) {
            useDoubleChance += 0.20f;
        }

        if (r < useDoubleChance) {
            action_.id = ActionId::DoubleSweep;
        }
    }

    action_.step = step;

    hasTrackingLocked_ = false;
    holdConfigured_ = false;
    currentHoldDuration_ = 0.0f;
    isAttackActive_ = false;
    stateTimer_ = 0.0f;
    isDoubleSweepSecondStage_ = false;
    currentActionConnected_ = false;
    currentActionGuarded_ = false;
    rushFollowupEvaluated_ = false;
    rushWillSweepFollowup_ = false;
    ResetPreAttackPresentationState();
    ResetRecoveryBranchState();

    // 追加：Stalk用初期化
    if (kind == ActionKind::Stalk) {
        stalkMoveDir_ = (std::rand() % 2 == 0) ? -1.0f : 1.0f;

        int biasRand = std::rand() % 3;
        if (biasRand == 0) {
            stalkForwardBias_ = -1.0f;
        } else if (biasRand == 1) {
            stalkForwardBias_ = 0.0f;
        } else {
            stalkForwardBias_ = 1.0f;
        }
    }
}

void Enemy::ChangeActionStep(ActionStep step) {
    action_.step = step;
    isAttackActive_ = false;
    stateTimer_ = 0.0f;

    if (step != ActionStep::Hold) {
        holdBranchType_ = HoldBranchType::None;
        holdBranchDecided_ = false;
        holdBranchDecisionTime_ = 0.0f;
    }

    if (step != ActionStep::Charge && step != ActionStep::Hold) {
        ResetPreAttackPresentationState();
    }
}

void Enemy::EndAttack() {
    action_.kind = ActionKind::None;
    action_.id = ActionId::None;
    action_.step = ActionStep::None;

    ResetWarpContext();
    ResetChainContext();
    ResetPostActionState();
    isVisible_ = true;

    hasTrackingLocked_ = false;
    holdConfigured_ = false;
    currentHoldDuration_ = 0.0f;
    isAttackActive_ = false;
    stateTimer_ = 0.0f;
    isDoubleSweepSecondStage_ = false;
    rushCurrentYaw_ = facingYaw_;
    currentActionConnected_ = false;
    currentActionGuarded_ = false;
    rushFollowupEvaluated_ = false;
    rushWillSweepFollowup_ = false;
    rushFromShotCombo_ = false;

    if (postCounterRhythmTimer_ <= 0.0f) {
        counterMemory_.consecutiveSuccess = 0;
    }

    ResetPreAttackPresentationState();
    ResetRecoveryBranchState();

    // 追加
    stalkMoveDir_ = 1.0f;
    stalkForwardBias_ = 0.0f;
}

void Enemy::FinishCurrentAction() {
    ActionKind finishedKind = action_.kind;

    if (TryContinueChain()) {
        return;
    }

    if (TryBranchFromRecovery(finishedKind)) {
        if (recoveryBranchType_ == RecoveryBranchType::Recommit ||
            recoveryBranchType_ == RecoveryBranchType::DelayedSecond) {
            ActionKind nextKind = recoveryFollowupKind_;
            ActionStep nextStep = recoveryFollowupStep_;

            EndAttack();

            if (nextKind == ActionKind::Smash ||
                nextKind == ActionKind::Sweep || nextKind == ActionKind::Rush) {
                tactic_ = TacticState::Pressure;
            }

            recoveryFollowupKind_ = nextKind;
            recoveryFollowupStep_ = nextStep;
            return;
        }

        return;
    }

    if (TryStartBackWarpPostAction(finishedKind)) {
        return;
    }

    EndAttack();
}

void Enemy::UpdateBossPhase() {
    if (phase_ == BossPhase::Phase2 || maxHp_ <= 0.0f) {
        return;
    }

    float hpRatio = hp_ / maxHp_;
    if (hpRatio <= phase2HealthRatioThreshold_) {
        EndAttack();
        phase_ = BossPhase::Phase2;
        phaseTransitionActive_ = true;
        phaseTransitionTimer_ = 0.0f;
        stateTimer_ = 0.0f;
    }
}
