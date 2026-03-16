#pragma once
#include <DirectXMath.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct BoneInfo {
    std::string name;
    int parentIndex = -1;
    DirectX::XMFLOAT4X4 offsetMatrix{};
    DirectX::XMFLOAT4X4 localBindMatrix{};
};

struct AnimationKeyVec3 {
    float time = 0.0f;
    DirectX::XMFLOAT3 value{};
};

struct AnimationKeyQuat {
    float time = 0.0f;
    DirectX::XMFLOAT4 value{};
};

struct BoneAnimation {
    std::vector<AnimationKeyVec3> positions;
    std::vector<AnimationKeyQuat> rotations;
    std::vector<AnimationKeyVec3> scales;
};

struct AnimationClip {
    float duration = 0.0f;
    float ticksPerSecond = 1.0f;
    std::unordered_map<std::string, BoneAnimation> channels;
};

struct Model {
    uint32_t meshId = 0;
    uint32_t textureId = 0;
    uint32_t materialId = 0;

    std::vector<BoneInfo> bones;
    std::unordered_map<std::string, uint32_t> boneMap;

    std::unordered_map<std::string, AnimationClip> animations;

    std::vector<DirectX::XMFLOAT4X4> finalBoneMatrices;

    std::string currentAnimation = "";
    float animationTime = 0.0f;
    bool isLoop = true;
    bool isPlaying = true;
    bool animationFinished = false;
};