#include "HandCameraPreviewReceiver.h"
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <DirectXTex.h>
#include <algorithm>
#include <cstring>
#include <sstream>
#include <string>

using namespace DirectX;

namespace {
SOCKET ToSocket(uintptr_t value) { return static_cast<SOCKET>(value); }
}

HandCameraPreviewReceiver::~HandCameraPreviewReceiver() { CloseSocket(); }

void HandCameraPreviewReceiver::Update() {
    if (!EnsureSocket()) {
        return;
    }

    ReceivePackets();
}

bool HandCameraPreviewReceiver::ConsumeFrame(std::vector<uint8_t> &rgbaPixels,
                                             uint32_t &width,
                                             uint32_t &height) {
    if (!hasLatestJpeg_ || latestJpeg_.empty()) {
        return false;
    }

    ScratchImage scratch;
    TexMetadata metadata{};
    HRESULT hr = LoadFromWICMemory(latestJpeg_.data(), latestJpeg_.size(),
                                   WIC_FLAGS_FORCE_RGB, &metadata, scratch);
    hasLatestJpeg_ = false;
    if (FAILED(hr)) {
        return false;
    }

    ScratchImage converted;
    const Image *source = scratch.GetImage(0, 0, 0);
    if (source == nullptr) {
        return false;
    }

    if (source->format == DXGI_FORMAT_R8G8B8A8_UNORM) {
        width = static_cast<uint32_t>(source->width);
        height = static_cast<uint32_t>(source->height);
        rgbaPixels.resize(static_cast<size_t>(source->slicePitch));
        std::memcpy(rgbaPixels.data(), source->pixels, rgbaPixels.size());
        hasDecodedFrame_ = true;
        return true;
    }

    hr = Convert(scratch.GetImages(), scratch.GetImageCount(),
                 scratch.GetMetadata(), DXGI_FORMAT_R8G8B8A8_UNORM,
                 TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT, converted);
    if (FAILED(hr)) {
        return false;
    }

    const Image *image = converted.GetImage(0, 0, 0);
    if (image == nullptr) {
        return false;
    }

    width = static_cast<uint32_t>(image->width);
    height = static_cast<uint32_t>(image->height);
    rgbaPixels.resize(static_cast<size_t>(image->slicePitch));
    std::memcpy(rgbaPixels.data(), image->pixels, rgbaPixels.size());
    hasDecodedFrame_ = true;
    return true;
}

bool HandCameraPreviewReceiver::EnsureSocket() {
    if (socketReady_) {
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
    address.sin_port = htons(kPort);

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

    socket_ = static_cast<uintptr_t>(udpSocket);
    socketReady_ = true;
    return true;
}

void HandCameraPreviewReceiver::ReceivePackets() {
    uint8_t buffer[kMaxPacketSize]{};
    for (;;) {
        sockaddr_in from{};
        int fromLength = sizeof(from);
        const int bytes = recvfrom(ToSocket(socket_),
                                   reinterpret_cast<char *>(buffer),
                                   static_cast<int>(sizeof(buffer)), 0,
                                   reinterpret_cast<sockaddr *>(&from),
                                   &fromLength);
        if (bytes == SOCKET_ERROR) {
            const int error = WSAGetLastError();
            if (error == WSAEWOULDBLOCK) {
                return;
            }
            return;
        }

        const uint8_t *lineEnd = std::find(buffer, buffer + bytes, '\n');
        if (lineEnd == buffer + bytes) {
            continue;
        }

        const std::string header(reinterpret_cast<const char *>(buffer),
                                 reinterpret_cast<const char *>(lineEnd));
        std::istringstream stream(header);
        std::string magic;
        uint32_t frameId = 0;
        uint16_t chunkIndex = 0;
        uint16_t chunkCount = 0;
        uint32_t totalBytes = 0;
        if (!(stream >> magic >> frameId >> chunkIndex >> chunkCount >>
              totalBytes) ||
            magic != "SGCAM" || chunkCount == 0 || chunkIndex >= chunkCount ||
            totalBytes == 0 || totalBytes > kMaxFrameBytes ||
            chunkCount > 256) {
            continue;
        }

        const uint8_t *payload = lineEnd + 1;
        const size_t payloadSize = static_cast<size_t>(buffer + bytes - payload);
        const size_t offset =
            static_cast<size_t>(chunkIndex) * kPreviewChunkBytes;
        if (offset >= totalBytes || offset + payloadSize > totalBytes) {
            continue;
        }

        if (assemblyBuffer_.empty() || frameId != assemblingFrameId_ ||
            chunkCount != assemblingChunkCount_ ||
            totalBytes != assemblingTotalBytes_) {
            ResetAssembly(frameId, chunkCount, totalBytes);
        }

        if (receivedChunks_[chunkIndex] == 0) {
            std::memcpy(assemblyBuffer_.data() + offset, payload, payloadSize);
            receivedChunks_[chunkIndex] = 1;
            ++receivedChunkCount_;
        }

        if (receivedChunkCount_ == assemblingChunkCount_) {
            latestJpeg_ = assemblyBuffer_;
            hasLatestJpeg_ = true;
            assemblyBuffer_.clear();
            receivedChunks_.clear();
            receivedChunkCount_ = 0;
        }
    }
}

void HandCameraPreviewReceiver::ResetAssembly(uint32_t frameId,
                                              uint16_t chunkCount,
                                              uint32_t totalBytes) {
    assemblingFrameId_ = frameId;
    assemblingChunkCount_ = chunkCount;
    assemblingTotalBytes_ = totalBytes;
    receivedChunkCount_ = 0;
    assemblyBuffer_.assign(totalBytes, 0);
    receivedChunks_.assign(chunkCount, 0);
}

void HandCameraPreviewReceiver::CloseSocket() {
    if (socketReady_) {
        closesocket(ToSocket(socket_));
        WSACleanup();
    }

    socket_ = UINTPTR_MAX;
    socketReady_ = false;
}
