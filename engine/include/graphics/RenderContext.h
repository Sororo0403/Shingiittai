#pragma once
#include "core/FrameTimer.h"
#include "graphics/RenderPass.h"
#include <cstdint>
#include <d3d12.h>

class Camera;
class DirectXCommon;
class SrvManager;

/// <summary>
/// 現在の描画パスで共有するデバイス、カメラ、フレーム情報を保持する
/// </summary>
struct RenderContext {
    DirectXCommon *dxCommon = nullptr;
    SrvManager *srv = nullptr;
    const Camera *camera = nullptr;
    RenderPass pass = RenderPass::None;
    FrameTime frameTime{};
    float deltaTime = 0.0f;
    uint32_t width = 0;
    uint32_t height = 0;
    D3D12_GPU_DESCRIPTOR_HANDLE sceneColor{};
    D3D12_GPU_DESCRIPTOR_HANDLE sceneDepth{};
};
