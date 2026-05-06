#include "Enemy.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <initializer_list>

namespace {
float Random01() {
    return static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
}

ActionStep GetDefaultStepForChain(ActionKind kind) {
    return kind == ActionKind::Movement ? ActionStep::Move : ActionStep::Charge;
}

void AddChainModule(ChainContext &chain, ActionKind module) {
    if (module == ActionKind::None ||
        chain.moduleCount >= ChainContext::kMaxModules ||
        chain.moduleCount >= chain.maxSteps) {
        return;
    }

    chain.modules[chain.moduleCount++] = module;
}

void AddChainRecipe(ChainContext &chain,
                    std::initializer_list<ActionKind> modules) {
    for (ActionKind module : modules) {
        AddChainModule(chain, module);
    }
}

void BuildChainRecipe(ChainContext &chain, ActionKind starter,
                      int maxSteps) {
    chain = ChainContext{};
    chain.active = true;
    chain.maxSteps = (std::min)(maxSteps, ChainContext::kMaxModules);

    const int roll = std::rand() % 5;
    switch (starter) {
    case ActionKind::Melee:
        if (roll == 0) {
            AddChainRecipe(chain, {ActionKind::Movement, ActionKind::Melee});
        } else if (roll == 1) {
            AddChainRecipe(chain, {ActionKind::Warp, ActionKind::Ranged});
        } else if (roll == 2) {
            AddChainRecipe(chain, {ActionKind::Ranged, ActionKind::Warp,
                                   ActionKind::Melee});
        } else if (roll == 3) {
            AddChainRecipe(chain, {ActionKind::Movement, ActionKind::Ranged});
        } else {
            AddChainRecipe(chain, {ActionKind::Warp, ActionKind::Melee});
        }
        break;
    case ActionKind::Ranged:
        if (roll == 0) {
            AddChainRecipe(chain, {ActionKind::Movement, ActionKind::Ranged});
        } else if (roll == 1) {
            AddChainRecipe(chain, {ActionKind::Warp, ActionKind::Melee});
        } else if (roll == 2) {
            AddChainRecipe(chain, {ActionKind::Movement, ActionKind::Melee});
        } else if (roll == 3) {
            AddChainRecipe(chain, {ActionKind::Warp, ActionKind::Ranged});
        } else {
            AddChainRecipe(chain, {ActionKind::Melee, ActionKind::Warp,
                                   ActionKind::Ranged});
        }
        break;
    case ActionKind::Movement:
        if (roll == 0) {
            AddChainRecipe(chain, {ActionKind::Melee});
        } else if (roll == 1) {
            AddChainRecipe(chain, {ActionKind::Ranged});
        } else if (roll == 2) {
            AddChainRecipe(chain, {ActionKind::Warp, ActionKind::Melee});
        } else if (roll == 3) {
            AddChainRecipe(chain, {ActionKind::Melee, ActionKind::Warp,
                                   ActionKind::Ranged});
        } else {
            AddChainRecipe(chain, {ActionKind::Ranged, ActionKind::Movement,
                                   ActionKind::Melee});
        }
        break;
    case ActionKind::Warp:
        if (roll == 0) {
            AddChainRecipe(chain, {ActionKind::Melee});
        } else if (roll == 1) {
            AddChainRecipe(chain, {ActionKind::Ranged});
        } else if (roll == 2) {
            AddChainRecipe(chain, {ActionKind::Movement, ActionKind::Melee});
        } else if (roll == 3) {
            AddChainRecipe(chain, {ActionKind::Ranged, ActionKind::Movement});
        } else {
            AddChainRecipe(chain, {ActionKind::Melee, ActionKind::Ranged});
        }
        break;
    default:
        break;
    }

    if (chain.moduleCount <= 0) {
        chain.active = false;
    }
}
} // namespace

void Enemy::UpdateWarpByStep(float deltaTime) {
    switch (action_.step) {
    case ActionStep::Start:
        UpdateWarpStart(deltaTime);
        break;
    case ActionStep::Move:
        UpdateWarpMove(deltaTime);
        break;
    case ActionStep::End:
        UpdateWarpEnd(deltaTime);
        break;
    default:
        EndAttack();
        break;
    }
}

bool Enemy::IsWarpSuspendedForPresentation() const {
    return suspendWarpForPresentation_;
}

bool Enemy::DecideWarpTargetNearPlayer(DirectX::XMFLOAT3 &outTarget) {
    float playerForwardX = playerPos_.x - tf_.position.x;
    float playerForwardZ = playerPos_.z - tf_.position.z;
    float playerForwardLength =
        std::sqrt(playerForwardX * playerForwardX + playerForwardZ * playerForwardZ);

    if (playerForwardLength <= 0.0001f) {
        playerForwardX = std::sin(facingYaw_);
        playerForwardZ = std::cos(facingYaw_);
        playerForwardLength = 1.0f;
    }

    playerForwardX /= playerForwardLength;
    playerForwardZ /= playerForwardLength;

    const float backX = -playerForwardX;
    const float backZ = -playerForwardZ;
    const float rightX = playerForwardZ;
    const float rightZ = -playerForwardX;

    outTarget = playerPos_;
    const float sideOffset =
        (Random01() * 2.0f - 1.0f) * warpApproachSideDistance_;
    const float forwardDistance =
        warpApproachForwardDistance_ +
        (warpApproachLongFrontDistance_ - warpApproachForwardDistance_) *
            Random01();
    outTarget.x += backX * forwardDistance + rightX * sideOffset;
    outTarget.z += backZ * forwardDistance + rightZ * sideOffset;
    outTarget.y = tf_.position.y;
    return true;
}

bool Enemy::DecideWarpTargetFarFromPlayer(DirectX::XMFLOAT3 &outTarget) const {
    const float angle =
        static_cast<float>(std::rand() % 360) * 3.14159265f / 180.0f;
    const float t =
        static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    const float radius =
        warpNearRadiusMax_ + (warpNearRadiusMax_ - warpNearRadiusMin_) * t + 2.0f;

    outTarget = playerPos_;
    outTarget.x += std::cos(angle) * radius;
    outTarget.z += std::sin(angle) * radius;
    outTarget.y = tf_.position.y;
    return true;
}

bool Enemy::PrepareWarpContext() {
    if (IsWarpSuspendedForPresentation()) {
        ResetWarpContext();
        return false;
    }

    ResetWarpContext();
    warp_.type = WarpType::Approach;
    if (!DecideWarpTargetNearPlayer(warp_.targetPos)) {
        ResetWarpContext();
        return false;
    }

    warp_.hasValidTarget = true;
    warp_.followupKind = ActionKind::Melee;
    warp_.followupStep = ActionStep::Charge;
    return true;
}

void Enemy::ResetWarpContext() { warp_ = WarpContext{}; }

void Enemy::ResetChainContext() { chain_ = ChainContext{}; }

bool Enemy::DecideNextChainAction(ActionKind finishedKind, ActionKind &outKind,
                                  ActionStep &outStep) const {
    (void)finishedKind;

    outKind = ActionKind::None;
    outStep = ActionStep::None;

    if (!chain_.active || chain_.moduleIndex >= chain_.moduleCount) {
        return false;
    }

    const float distance = GetDistanceToPlayer();
    ActionKind module = chain_.modules[chain_.moduleIndex];
    if (module == ActionKind::Warp && IsWarpSuspendedForPresentation()) {
        module = ActionKind::Movement;
    } else if (module == ActionKind::Movement &&
               stalkRepeatCount_ >= stalkRepeatLimit_) {
        module = distance <= config_.core.nearAttackDistance
                     ? ActionKind::Melee
                     : ActionKind::Ranged;
    }

    switch (module) {
    case ActionKind::Melee:
        if (distance > config_.core.nearAttackDistance + 0.8f) {
            outKind = IsWarpSuspendedForPresentation() ? ActionKind::Movement
                                                       : ActionKind::Warp;
        } else {
            outKind = ActionKind::Melee;
        }
        break;
    case ActionKind::Ranged:
        outKind = ActionKind::Ranged;
        break;
    case ActionKind::Movement:
        outKind = ActionKind::Movement;
        break;
    case ActionKind::Warp:
        outKind = ActionKind::Warp;
        break;
    default:
        return false;
    }

    outStep = GetDefaultStepForChain(outKind);
    return outKind != ActionKind::None && outStep != ActionStep::None;
}

bool Enemy::BeginChainFollowup(ActionKind finishedKind, ActionKind nextKind,
                               ActionStep nextStep) {
    if (nextKind == ActionKind::None || nextStep == ActionStep::None) {
        return false;
    }

    if (nextKind == ActionKind::Warp) {
        if (IsWarpSuspendedForPresentation()) {
            return false;
        }

        const float distance = GetDistanceToPlayer();
        const bool shouldRetreat =
            finishedKind == ActionKind::Melee &&
            distance <= config_.chain.nearRetreatDistance;

        ResetWarpContext();
        warp_.type = shouldRetreat ? WarpType::Escape : WarpType::Approach;
        const bool hasTarget =
            shouldRetreat ? DecideWarpTargetFarFromPlayer(warp_.targetPos)
                          : DecideWarpTargetNearPlayer(warp_.targetPos);
        if (!hasTarget) {
            ResetWarpContext();
            return false;
        }

        warp_.hasValidTarget = true;
        BeginAction(ActionKind::Warp, ActionStep::Start);
        return true;
    }

    if (nextKind == ActionKind::Movement) {
        BeginStalkAction();
        return true;
    }

    BeginAction(nextKind, nextStep);
    return true;
}

bool Enemy::TryStartModularActionChain(ActionKind finishedKind) {
    if (finishedKind == ActionKind::None) {
        return false;
    }

    if (!chain_.active) {
        float chance = config_.chain.continueChance;
        if (phase_ == BossPhase::Phase2) {
            chance += config_.chain.phase2ContinueBonus;
        }
        if (finishedKind == ActionKind::Movement) {
            chance *= 0.75f;
        }

        chance = (std::clamp)(chance, 0.0f, 0.86f);
        if (Random01() >= chance) {
            return false;
        }

        const int maxSteps = (phase_ == BossPhase::Phase2)
                                 ? config_.chain.maxStepsPhase2
                                 : config_.chain.maxStepsPhase1;
        BuildChainRecipe(chain_, finishedKind, maxSteps);
        if (!chain_.active) {
            return false;
        }
    }

    if (chain_.moduleIndex >= chain_.moduleCount ||
        chain_.stepCount >= chain_.maxSteps) {
        ResetChainContext();
        return false;
    }

    ActionKind nextKind = ActionKind::None;
    ActionStep nextStep = ActionStep::None;
    if (!DecideNextChainAction(finishedKind, nextKind, nextStep)) {
        ResetChainContext();
        return false;
    }

    if (!BeginChainFollowup(finishedKind, nextKind, nextStep)) {
        ResetChainContext();
        return false;
    }

    chain_.stepCount++;
    chain_.moduleIndex++;
    return true;
}

bool Enemy::TryContinueChain() { return TryStartModularActionChain(action_.kind); }

void Enemy::UpdateWarpStart(float deltaTime) {
    if (!warp_.hasDeparturePos) {
        warp_.departurePos = tf_.position;
        warp_.hasDeparturePos = true;
    }

    if (warp_.type == WarpType::Approach) {
        UpdateFacingToPlayerWithSpeed(deltaTime, chargeTurnSpeed_ * 0.45f);
    }

    isVisible_ = false;
    warp_.collisionDisabled = true;

    if (stateTimer_ >= config_.warp.startTime) {
        warpTrailEmitTimer_ = 0.0f;
        EmitWarpTrailGhost(warp_.departurePos, warpTrailScaleMax_);
        ChangeActionStep(ActionStep::Move);
    }
}

void Enemy::UpdateWarpMove(float deltaTime) {
    if (!warp_.hasValidTarget) {
        EndAttack();
        return;
    }

    float t = 1.0f;
    if (config_.warp.moveTime > 0.0001f) {
        t = stateTimer_ / config_.warp.moveTime;
    }
    t = (t < 0.0f) ? 0.0f : ((t > 1.0f) ? 1.0f : t);

    const float eased = 1.0f - std::pow(1.0f - t, 2.6f);
    tf_.position.x =
        warp_.departurePos.x + (warp_.targetPos.x - warp_.departurePos.x) * eased;
    tf_.position.y =
        warp_.departurePos.y + (warp_.targetPos.y - warp_.departurePos.y) * eased;
    tf_.position.z =
        warp_.departurePos.z + (warp_.targetPos.z - warp_.departurePos.z) * eased;

    warpTrailEmitTimer_ += deltaTime;
    while (warpTrailEmitTimer_ >= warpTrailInterval_) {
        warpTrailEmitTimer_ -= warpTrailInterval_;
        const float scale =
            warpTrailScaleMax_ - (warpTrailScaleMax_ - warpTrailScaleMin_) * t;
        EmitWarpTrailGhost(tf_.position, scale);
    }

    UpdateFacingToPlayer();
    LockCurrentFacing();

    if (stateTimer_ >= config_.warp.moveTime) {
        tf_.position = warp_.targetPos;
        ChangeActionStep(ActionStep::End);
    }
}

void Enemy::UpdateWarpEnd(float deltaTime) {
    isVisible_ = true;
    warp_.collisionDisabled = false;
    UpdateFacingToPlayerWithSpeed(deltaTime, idleTurnSpeed_ * 0.55f);

    if (stateTimer_ < config_.warp.endTime) {
        return;
    }

    const ActionKind followupKind = warp_.followupKind;
    const ActionStep followupStep = warp_.followupStep;
    const ChainContext chainSnapshot = chain_;

    EndAttack();
    if (followupKind != ActionKind::None && followupStep != ActionStep::None) {
        chain_ = chainSnapshot;
        tactic_ = followupKind;
        BeginAction(followupKind, followupStep);
        return;
    }

    if (chainSnapshot.active) {
        chain_ = chainSnapshot;
        if (TryStartModularActionChain(ActionKind::Warp)) {
            return;
        }
    }

    tactic_ = DecideTactic();
    BeginActionFromTactic(tactic_);
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
