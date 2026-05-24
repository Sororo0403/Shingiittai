#include <WinSock2.h>
#include <WS2tcpip.h>
#include "TipScene.h"
#include "AppSceneServices.h"
#include "DirectXCommon.h"
#include "GameScene.h"
#include "Input.h"
#include "PostProcessSystem.h"
#include "SceneManager.h"
#include "SpriteManager.h"
#include "TextureManager.h"
#include "WinApp.h"
#include <DirectXTex.h>
#include <array>
#include <algorithm>
#include <cstring>
#include <memory>
#include <sstream>

using namespace DirectX;

namespace {
constexpr float kHandSwingStartSpeed = 0.78f;
constexpr float kHandSwingResetSpeed = 0.32f;
constexpr int kRequiredHandSwings = 3;
constexpr uint16_t kPreviewPort = 5006;
constexpr float kPreviewStaleSeconds = 0.75f;

XMFLOAT4 Color(float r, float g, float b, float a = 1.0f) {
    return {r, g, b, a};
}

bool IsHandControl(InputControlType controlType) {
    return controlType == InputControlType::Hand;
}

SOCKET ToSocket(uintptr_t value) { return static_cast<SOCKET>(value); }
} // namespace

TipScene::TipScene(const SwordInputCalibration &inputCalibration)
    : inputCalibration_(inputCalibration) {}

TipScene::~TipScene() { ClosePreviewSocket(); }

void TipScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    sceneTime_ = 0.0f;
    handSwingCount_ = 0;
    handSwingArmed_ = true;
    handTrackingStartRequested_ = false;

    if (inputCalibration_.controlType == InputControlType::JoyCon) {
        leftJoyCon_.Initialize(true);
        rightJoyCon_.Initialize(false);
    }
    if (IsHandControl(inputCalibration_.controlType)) {
        handController_.SetCalibration(inputCalibration_);
        RequestHandTrackingStartOnce();
        previewFrame_ = {};
        previewFrame_.rgbaPixels.resize(
            static_cast<size_t>(previewFrame_.width) *
            static_cast<size_t>(previewFrame_.height) * 4u);
        previewFrame_.textureId = ctx_->rendering.texture->CreateFromRgbaPixels(
            previewFrame_.width, previewFrame_.height,
            previewFrame_.rgbaPixels.data());
        previewJpegBuffer_.clear();
        previewChunkReceived_.clear();
        previewFrameId_ = 0;
        previewReceivedChunks_ = 0;
    }

    backgroundImage_ =
        LoadTextureImage(L"app/resources/select/weapon_select_bg.png");
    titleImage_ = LoadTextureImage(L"app/resources/text/tip_title.png");
    bodyImage_ = LoadTextureImage(L"app/resources/text/tip_body.png");
    switch (inputCalibration_.controlType) {
    case InputControlType::JoyCon:
        promptImage_ = LoadTextureImage(L"app/resources/text/tip_joycon.png");
        break;
    case InputControlType::Hand:
        promptImage_ = LoadTextureImage(L"app/resources/text/tip_hand.png");
        break;
    case InputControlType::KeyboardMouse:
    default:
        promptImage_ = LoadTextureImage(L"app/resources/text/tip_kbm.png");
        break;
    }

    PostProcessProfile postProfile{};
    postProfile.vignette.enabled = true;
    postProfile.vignette.strength = 0.26f;
    ctx_->rendering.postProcessSystem->SetProfile(postProfile);
}

void TipScene::Update() {
    sceneTime_ += ctx_->frame.deltaTime;
    if (inputCalibration_.controlType == InputControlType::JoyCon) {
        leftJoyCon_.Update(ctx_->frame.deltaTime);
        rightJoyCon_.Update(ctx_->frame.deltaTime);
    }
    if (IsHandControl(inputCalibration_.controlType)) {
        RequestHandTrackingStartOnce();
        handController_.Update(ctx_->frame.deltaTime);
        UpdateCameraPreview(ctx_->frame.deltaTime);
    }

    if (ShouldStart()) {
        sceneManager_->ChangeScene(std::make_unique<GameScene>(
            GameScene::RunMode::Play, inputCalibration_));
    }
}

void TipScene::Draw() {
    const float w = static_cast<float>(ctx_->systems.winApp->GetWidth());
    const float h = static_cast<float>(ctx_->systems.winApp->GetHeight());

    ctx_->rendering.sprite->PreDraw();
    DrawRect(0.0f, 0.0f, w, h, Color(0.018f, 0.020f, 0.024f, 1.0f));
    DrawImage(backgroundImage_, 0.0f, 0.0f,
              (std::max)(w / (std::max)(backgroundImage_.width, 1.0f),
                         h / (std::max)(backgroundImage_.height, 1.0f)),
              0.26f);
    DrawRect(w * 0.10f, h * 0.18f, w * 0.80f, h * 0.56f,
             Color(0.025f, 0.028f, 0.032f, 0.84f));
    DrawRect(w * 0.10f, h * 0.18f, w * 0.80f, 6.0f,
             Color(1.0f, 0.82f, 0.18f, 0.92f));
    DrawImage(titleImage_, (w - titleImage_.width) * 0.5f, h * 0.23f, 1.0f);
    DrawImage(bodyImage_, (w - bodyImage_.width) * 0.5f, h * 0.42f, 1.0f,
              0.86f);
    const float pulse = 0.72f + 0.28f * std::sinf(sceneTime_ * 5.0f);
    DrawImage(promptImage_, (w - promptImage_.width) * 0.5f, h * 0.62f, 1.0f,
              pulse);
    if (IsHandControl(inputCalibration_.controlType)) {
        const float unit = 48.0f;
        const float startX = (w - unit * 3.0f - 18.0f * 2.0f) * 0.5f;
        for (int i = 0; i < 3; ++i) {
            DrawRect(startX + static_cast<float>(i) * (unit + 18.0f),
                     h * 0.78f, unit, 10.0f,
                     i < handSwingCount_ ? Color(0.10f, 0.74f, 0.36f, 1.0f)
                                         : Color(0.20f, 0.24f, 0.28f, 1.0f));
        }
    }
    ctx_->rendering.sprite->PostDraw();
}

void TipScene::DrawTransparent() {
    if (IsHandControl(inputCalibration_.controlType)) {
        DrawCameraPreview();
    }
}

TipScene::Image TipScene::LoadTextureImage(const std::wstring &path) {
    Image image{};
    image.textureId = ctx_->rendering.texture->Load(path);
    image.width = static_cast<float>(ctx_->rendering.texture->GetWidth(image.textureId));
    image.height =
        static_cast<float>(ctx_->rendering.texture->GetHeight(image.textureId));
    return image;
}

bool TipScene::ShouldStart() {
    switch (inputCalibration_.controlType) {
    case InputControlType::KeyboardMouse:
        return ctx_->systems.input->IsKeyTrigger(DIK_SPACE);
    case InputControlType::JoyCon: {
        constexpr int kFaceButtons = JSMASK_S | JSMASK_E | JSMASK_W | JSMASK_N;
        const bool left =
            leftJoyCon_.IsConnected() &&
            (leftJoyCon_.IsButtonTrigger(kFaceButtons));
        const bool right =
            rightJoyCon_.IsConnected() &&
            (rightJoyCon_.IsButtonTrigger(kFaceButtons));
        return left || right;
    }
    case InputControlType::Hand: {
        if (ctx_->systems.input != nullptr &&
            (ctx_->systems.input->IsKeyTrigger(DIK_SPACE) ||
             ctx_->systems.input->IsKeyTrigger(DIK_RETURN))) {
            return true;
        }
        const float speed =
            (std::max)(handController_.GetRawMotionSpeed(0),
                       handController_.GetRawMotionSpeed(1));
        if (handSwingArmed_ && speed >= kHandSwingStartSpeed) {
            ++handSwingCount_;
            handSwingArmed_ = false;
        }
        if (speed <= kHandSwingResetSpeed) {
            handSwingArmed_ = true;
        }
        return handSwingCount_ >= kRequiredHandSwings;
    }
    default:
        return false;
    }
}

void TipScene::RequestHandTrackingStartOnce() {
    if (handTrackingStartRequested_ || ctx_ == nullptr) {
        return;
    }

    if (AppSceneServices::HasHandTrackingStart()) {
        AppSceneServices::RequestHandTrackingStart();
    } else {
        return;
    }
    handTrackingStartRequested_ = true;
}

void TipScene::UpdateCameraPreview(float deltaTime) {
    previewFrame_.staleTimer += deltaTime;
    ReceivePreviewPackets();
}

bool TipScene::EnsurePreviewSocket() {
    if (previewSocketReady_) {
        return true;
    }

    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return false;
    }

    SOCKET udpSocket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udpSocket == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(kPreviewPort);
    if (bind(udpSocket, reinterpret_cast<sockaddr *>(&address),
             sizeof(address)) == SOCKET_ERROR) {
        closesocket(udpSocket);
        WSACleanup();
        return false;
    }

    u_long nonBlocking = 1;
    if (ioctlsocket(udpSocket, FIONBIO, &nonBlocking) == SOCKET_ERROR) {
        closesocket(udpSocket);
        WSACleanup();
        return false;
    }

    previewSocket_ = static_cast<uintptr_t>(udpSocket);
    previewSocketReady_ = true;
    return true;
}

void TipScene::ClosePreviewSocket() {
    if (previewSocketReady_) {
        closesocket(ToSocket(previewSocket_));
        WSACleanup();
    }
    previewSocket_ = UINTPTR_MAX;
    previewSocketReady_ = false;
}

void TipScene::ReceivePreviewPackets() {
    if (!EnsurePreviewSocket()) {
        return;
    }

    std::array<uint8_t, 1600> buffer{};
    for (;;) {
        sockaddr_in from{};
        int fromLength = sizeof(from);
        const int bytes = recvfrom(ToSocket(previewSocket_),
                                   reinterpret_cast<char *>(buffer.data()),
                                   static_cast<int>(buffer.size()), 0,
                                   reinterpret_cast<sockaddr *>(&from),
                                   &fromLength);
        if (bytes == SOCKET_ERROR) {
            return;
        }
        HandlePreviewPacket(buffer.data(), bytes);
    }
}

void TipScene::HandlePreviewPacket(const uint8_t *data, int bytes) {
    const uint8_t *newline = static_cast<const uint8_t *>(
        std::memchr(data, '\n', static_cast<size_t>(bytes)));
    if (newline == nullptr) {
        return;
    }

    const std::string header(reinterpret_cast<const char *>(data),
                             reinterpret_cast<const char *>(newline));
    std::istringstream stream(header);
    std::string magic;
    uint32_t frameId = 0;
    size_t chunkIndex = 0;
    size_t chunkCount = 0;
    size_t totalSize = 0;
    if (!(stream >> magic >> frameId >> chunkIndex >> chunkCount >> totalSize) ||
        magic != "SGCAM" || chunkCount == 0 || chunkIndex >= chunkCount ||
        totalSize == 0 || totalSize > 1024u * 1024u) {
        return;
    }

    const uint8_t *payload = newline + 1;
    const size_t payloadSize = static_cast<size_t>(data + bytes - payload);
    const size_t offset = chunkIndex * 1150u;
    if (offset >= totalSize || payloadSize > totalSize - offset) {
        return;
    }

    if (frameId != previewFrameId_ ||
        previewChunkReceived_.size() != chunkCount ||
        previewJpegBuffer_.size() != totalSize) {
        previewFrameId_ = frameId;
        previewJpegBuffer_.assign(totalSize, 0u);
        previewChunkReceived_.assign(chunkCount, false);
        previewReceivedChunks_ = 0;
    }

    if (!previewChunkReceived_[chunkIndex]) {
        std::memcpy(previewJpegBuffer_.data() + offset, payload, payloadSize);
        previewChunkReceived_[chunkIndex] = true;
        ++previewReceivedChunks_;
    }

    if (previewReceivedChunks_ == previewChunkReceived_.size()) {
        DecodePreviewJpeg(previewJpegBuffer_);
    }
}

void TipScene::DecodePreviewJpeg(const std::vector<uint8_t> &jpegData) {
    if (jpegData.empty()) {
        return;
    }

    DirectX::ScratchImage scratch;
    DirectX::TexMetadata metadata{};
    HRESULT hr = DirectX::LoadFromWICMemory(
        jpegData.data(), jpegData.size(), DirectX::WIC_FLAGS_FORCE_RGB,
        &metadata, scratch);
    if (FAILED(hr)) {
        return;
    }

    DirectX::ScratchImage converted;
    const DirectX::Image *image = scratch.GetImage(0, 0, 0);
    if (image != nullptr && image->format != DXGI_FORMAT_R8G8B8A8_UNORM) {
        hr = DirectX::Convert(*image, DXGI_FORMAT_R8G8B8A8_UNORM,
                              DirectX::TEX_FILTER_DEFAULT, 0.0f, converted);
        if (FAILED(hr)) {
            return;
        }
        image = converted.GetImage(0, 0, 0);
    }

    if (image == nullptr || image->pixels == nullptr || image->width == 0 ||
        image->height == 0 || image->width != previewFrame_.width ||
        image->height != previewFrame_.height) {
        return;
    }

    const size_t rowBytes = static_cast<size_t>(previewFrame_.width) * 4u;
    const size_t imageBytes =
        rowBytes * static_cast<size_t>(previewFrame_.height);
    if (previewFrame_.rgbaPixels.size() != imageBytes) {
        previewFrame_.rgbaPixels.resize(imageBytes);
    }

    for (uint32_t y = 0; y < previewFrame_.height; ++y) {
        std::memcpy(previewFrame_.rgbaPixels.data() + rowBytes * y,
                    image->pixels + image->rowPitch * y, rowBytes);
    }
    previewFrame_.valid = true;
    previewFrame_.dirty = true;
    previewFrame_.staleTimer = 0.0f;
}

void TipScene::UploadPreviewTextureIfNeeded() {
    if (!previewFrame_.dirty || !previewFrame_.valid ||
        previewFrame_.rgbaPixels.empty()) {
        return;
    }

    ctx_->rendering.texture->UpdateTexture2D(
        previewFrame_.textureId, previewFrame_.rgbaPixels.data(),
        static_cast<size_t>(previewFrame_.width) * 4u);
    previewFrame_.dirty = false;
}

void TipScene::DrawCameraPreview() {
    if (ctx_ == nullptr || ctx_->rendering.sprite == nullptr || ctx_->rendering.texture == nullptr ||
        ctx_->systems.winApp == nullptr) {
        return;
    }

    UploadPreviewTextureIfNeeded();
    constexpr float kMargin = 16.0f;
    constexpr float kPreviewWidth = 192.0f;
    const float previewAspect =
        static_cast<float>(previewFrame_.width) /
        static_cast<float>((std::max)(previewFrame_.height, 1u));
    const float previewHeight = kPreviewWidth / previewAspect;
    const bool fresh =
        previewFrame_.valid && previewFrame_.staleTimer <= kPreviewStaleSeconds;
    const float alpha = fresh ? 0.88f : 0.34f;

    ctx_->rendering.sprite->PreDraw();
    DrawRect(kMargin - 4.0f, kMargin - 4.0f, kPreviewWidth + 8.0f,
             previewHeight + 8.0f, Color(0.0f, 0.0f, 0.0f, 0.52f));
    if (previewFrame_.valid) {
        Sprite preview{};
        preview.textureId = previewFrame_.textureId;
        preview.position = {kMargin, kMargin};
        preview.size = {kPreviewWidth, previewHeight};
        preview.color = {1.0f, 1.0f, 1.0f, alpha};
        ctx_->rendering.sprite->DrawSprite(preview);
    }
    const XMFLOAT4 border =
        fresh ? XMFLOAT4{0.32f, 0.72f, 1.0f, 0.62f}
              : XMFLOAT4{0.90f, 0.72f, 0.22f, 0.50f};
    DrawRect(kMargin, kMargin, kPreviewWidth, 2.0f, border);
    DrawRect(kMargin, kMargin + previewHeight - 2.0f, kPreviewWidth, 2.0f,
             border);
    DrawRect(kMargin, kMargin, 2.0f, previewHeight, border);
    DrawRect(kMargin + kPreviewWidth - 2.0f, kMargin, 2.0f, previewHeight,
             border);
    ctx_->rendering.sprite->PostDraw();
}

void TipScene::DrawRect(float x, float y, float w, float h,
                        const XMFLOAT4 &color) {
    Sprite sprite{};
    sprite.position = {x, y};
    sprite.size = {w, h};
    sprite.color = color;
    sprite.textureId = 0;
    ctx_->rendering.sprite->DrawSprite(sprite);
}

void TipScene::DrawImage(const Image &image, float x, float y, float scale,
                         float alpha) {
    if (image.width <= 0.0f || image.height <= 0.0f) {
        return;
    }

    Sprite sprite{};
    sprite.position = {x, y};
    sprite.size = {image.width * scale, image.height * scale};
    sprite.color = {1.0f, 1.0f, 1.0f, alpha};
    sprite.textureId = image.textureId;
    ctx_->rendering.sprite->DrawSprite(sprite);
}
