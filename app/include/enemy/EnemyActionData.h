#pragma once
#include <DirectXMath.h>
#include <vector>

enum class ActionKind {
    None,
    Smash,
    Sweep,
    Shot,
    Wave,
    Warp,
    Stalk,
};

enum class ActionId {
    None,

    Smash,
    DelaySmash,

    Sweep,
    DoubleSweep,
    AdvanceSweep,

    Shot,
    Wave,

    WarpApproach,
    WarpEscape,
    WarpBackstab,
};

struct AttackParam {
    float damage = 0.0f;
    float knockback = 0.0f;
    DirectX::XMFLOAT3 hitBoxSize = {1.0f, 1.0f, 1.0f};
};

struct ActionMotionWindow {
    float chargeTime = 0.0f;
    float trackingEndTime = 0.0f;
    float activeStartTime = 0.0f;
    float activeEndTime = 0.0f;
    float recoveryStartTime = 0.0f;
    float totalTime = 0.0f;
};

struct ActionMoveParam {
    float forwardSpeed = 0.0f;
    float moveDuration = 0.0f;
};

struct ActionLinkCondition {
    float minDistance = 0.0f;
    float maxDistance = 9999.0f;
    bool requirePlayerGuarding = false;
};

struct ActionLink {
    ActionId nextId = ActionId::None;
    float chance = 0.0f;
    ActionLinkCondition condition{};
};

struct ActionData {
    ActionId id = ActionId::None;
    ActionKind kind = ActionKind::None;

    AttackParam attack{};
    ActionMotionWindow motion{};
    ActionMoveParam move{};

    bool usesLockedYaw = false;
    bool canBackWarpCancel = false;

    std::vector<ActionLink> links{};
};
