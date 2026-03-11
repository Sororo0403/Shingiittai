#include "Animator.h"
#include <DirectXMath.h>
#include <cmath>

using namespace DirectX;

namespace {

XMFLOAT3 LerpVec3(const XMFLOAT3 &a, const XMFLOAT3 &b, float t) {
    return {
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
    };
}

float SafeInv(float x) {
    if (std::fabs(x) < 0.000001f) {
        return 0.0f;
    }
    return 1.0f / x;
}

} // namespace

XMFLOAT3 Animator::SampleVec3(const std::vector<AnimationKeyVec3> &keys,
                              float time) {
    if (keys.empty()) {
        return {0.0f, 0.0f, 0.0f};
    }

    if (keys.size() == 1 || time <= keys.front().time) {
        return keys.front().value;
    }

    if (time >= keys.back().time) {
        return keys.back().value;
    }

    for (size_t i = 0; i + 1 < keys.size(); i++) {
        const auto &k0 = keys[i];
        const auto &k1 = keys[i + 1];

        if (time >= k0.time && time <= k1.time) {
            float len = k1.time - k0.time;
            float t = (time - k0.time) * SafeInv(len);
            return LerpVec3(k0.value, k1.value, t);
        }
    }

    return keys.back().value;
}

XMFLOAT4 Animator::SampleQuat(const std::vector<AnimationKeyQuat> &keys,
                              float time) {
    if (keys.empty()) {
        return {0.0f, 0.0f, 0.0f, 1.0f};
    }

    if (keys.size() == 1 || time <= keys.front().time) {
        return keys.front().value;
    }

    if (time >= keys.back().time) {
        return keys.back().value;
    }

    for (size_t i = 0; i + 1 < keys.size(); i++) {
        const auto &k0 = keys[i];
        const auto &k1 = keys[i + 1];

        if (time >= k0.time && time <= k1.time) {
            float len = k1.time - k0.time;
            float t = (time - k0.time) * SafeInv(len);

            XMVECTOR q0 = XMLoadFloat4(&k0.value);
            XMVECTOR q1 = XMLoadFloat4(&k1.value);
            XMVECTOR q = XMQuaternionSlerp(q0, q1, t);

            XMFLOAT4 result;
            XMStoreFloat4(&result, q);
            return result;
        }
    }

    return keys.back().value;
}

XMMATRIX Animator::MakeAnimatedLocalMatrix(const BoneInfo &bone,
                                           const Model &model, float time) {
    auto it = model.animation.channels.find(bone.name);
    if (it == model.animation.channels.end()) {
        return XMLoadFloat4x4(&bone.localBindMatrix);
    }

    const BoneAnimation &anim = it->second;

    XMFLOAT3 pos = anim.positions.empty() ? XMFLOAT3{0.0f, 0.0f, 0.0f}
                                          : SampleVec3(anim.positions, time);

    XMFLOAT3 scl = anim.scales.empty() ? XMFLOAT3{1.0f, 1.0f, 1.0f}
                                       : SampleVec3(anim.scales, time);

    XMFLOAT4 rot = anim.rotations.empty() ? XMFLOAT4{0.0f, 0.0f, 0.0f, 1.0f}
                                          : SampleQuat(anim.rotations, time);

    XMVECTOR q = XMQuaternionNormalize(XMLoadFloat4(&rot));

    return XMMatrixScaling(scl.x, scl.y, scl.z) *
           XMMatrixRotationQuaternion(q) *
           XMMatrixTranslation(pos.x, pos.y, pos.z);
}

void Animator::Update(Model &model, float deltaTime) {
    if (model.bones.empty()) {
        return;
    }

    const size_t boneCount = model.bones.size();

    if (model.finalBoneMatrices.size() != boneCount) {
        model.finalBoneMatrices.resize(boneCount);
    }

    if (model.animation.duration <= 0.0f) {
        for (size_t i = 0; i < boneCount; i++) {
            model.finalBoneMatrices[i] = model.bones[i].offsetMatrix;
        }
        return;
    }

    currentTime_ += deltaTime * model.animation.ticksPerSecond;

    while (currentTime_ > model.animation.duration) {
        currentTime_ -= model.animation.duration;
    }

    std::vector<XMMATRIX> localMatrices(boneCount);
    std::vector<XMMATRIX> globalMatrices(boneCount);

    for (size_t i = 0; i < boneCount; i++) {
        localMatrices[i] =
            MakeAnimatedLocalMatrix(model.bones[i], model, currentTime_);
    }

    for (size_t i = 0; i < boneCount; i++) {
        int parent = model.bones[i].parentIndex;

        if (parent < 0) {
            globalMatrices[i] = localMatrices[i];
        } else {
            globalMatrices[i] = localMatrices[i] * globalMatrices[parent];
        }
    }

    for (size_t i = 0; i < boneCount; i++) {
        XMMATRIX offset = XMLoadFloat4x4(&model.bones[i].offsetMatrix);
        XMMATRIX final = offset * globalMatrices[i];
        XMStoreFloat4x4(&model.finalBoneMatrices[i], final);
    }
}