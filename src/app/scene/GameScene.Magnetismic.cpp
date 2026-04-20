#include "GameScene.h"
#include "Camera.h"
#include "DirectXCommon.h"
#include "Enemy.h"
#include "MagnetismicRenderer.h"
#include "SceneContext.h"

using namespace DirectX;

namespace {

MagnetMatrix4x4 ToMagnetMatrix(const XMMATRIX &m) {
    XMFLOAT4X4 f{};
    XMStoreFloat4x4(&f, XMMatrixTranspose(m));

    MagnetMatrix4x4 out{};
    out.m[0][0] = f._11;
    out.m[0][1] = f._12;
    out.m[0][2] = f._13;
    out.m[0][3] = f._14;

    out.m[1][0] = f._21;
    out.m[1][1] = f._22;
    out.m[1][2] = f._23;
    out.m[1][3] = f._24;

    out.m[2][0] = f._31;
    out.m[2][1] = f._32;
    out.m[2][2] = f._33;
    out.m[2][3] = f._34;

    out.m[3][0] = f._41;
    out.m[3][1] = f._42;
    out.m[3][2] = f._43;
    out.m[3][3] = f._44;
    return out;
}

void ExtractCameraAxes(const Camera &camera, MagnetVec3 &outRight,
                       MagnetVec3 &outUp) {
    XMMATRIX invView = XMMatrixInverse(nullptr, camera.GetView());

    XMFLOAT4X4 iv{};
    XMStoreFloat4x4(&iv, invView);

    outRight = {iv._11, iv._12, iv._13};
    outUp = {iv._21, iv._22, iv._23};
}

} // namespace

void GameScene::UpdateMagnetismic() {
    if (ctx_ == nullptr || ctx_->magnetismicRenderer == nullptr) {
        return;
    }

    magnetismicTime_ += ctx_->deltaTime;
    ctx_->magnetismicRenderer->BeginFrame(ctx_->deltaTime);
}

void GameScene::DrawMagnetismic() {
    if (!magnetismicEnabled_) {
        return;
    }

    if (ctx_ == nullptr || ctx_->magnetismicRenderer == nullptr) {
        return;
    }

    if (currentCamera_ == nullptr) {
        return;
    }

    // =========================================================
    // 常時表示テスト版
    // - Warp 条件は見ない
    // - 敵の少し上に固定
    // - 大きめサイズで全体像を確認
    // =========================================================
    XMFLOAT3 magnetPos = enemy_.GetTransform().position;
    magnetPos.y += 1.8f;

    MagnetismicInstance inst{};
    inst.position = {magnetPos.x, magnetPos.y, magnetPos.z};

    inst.time = magnetismicTime_;

    // まずは大きく見せる
    inst.size = 2.2f;
    inst.intensity = 1.35f;
    inst.alpha = 0.95f;

    // 形を見るためのベース値
    inst.swirlA = 3.2f;
    inst.swirlB = 2.8f;
    inst.noiseScale = 3.0f;
    inst.stepScale = 0.85f;

    // コア寄り配色
    inst.colorA = {1.6f, 0.45f, 1.1f, 1.0f};
    inst.colorB = {0.05f, 0.75f, 1.6f, 1.0f};

    inst.brightness = 1.9f;
    inst.distFade = 0.28f;
    inst.innerBoost = 1.55f;

    // ゆるい脈動だけ入れる
    {
        float t = magnetismicTime_;

        float pulse1 = sinf(t * 2.8f) * 0.5f + 0.5f;
        float pulse2 = sinf(t * 5.6f + 1.2f) * 0.5f + 0.5f;
        float pulse = powf(pulse1 * 0.7f + pulse2 * 0.3f, 1.8f);

        inst.size *= (1.0f + pulse * 0.12f);
        inst.intensity *= (1.0f + pulse * 0.18f);
    }

    MagnetVec3 camRight{};
    MagnetVec3 camUp{};
    ExtractCameraAxes(*currentCamera_, camRight, camUp);

    XMMATRIX viewProj = currentCamera_->GetView() * currentCamera_->GetProj();
    MagnetMatrix4x4 magnetViewProj = ToMagnetMatrix(viewProj);

    ctx_->magnetismicRenderer->Draw(ctx_->dxCommon->GetCommandList(), inst,
                                    magnetViewProj, camRight, camUp);
}