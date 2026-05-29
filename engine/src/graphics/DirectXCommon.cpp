#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include "graphics/DxUtils.h"
#include "graphics/SrvManager.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <sstream>
#include <string>

using namespace DxUtils;

namespace {
std::filesystem::path GetDeviceRemovedLogPath() {
    wchar_t modulePath[MAX_PATH]{};
    const DWORD length =
        GetModuleFileNameW(nullptr, modulePath, static_cast<DWORD>(_countof(modulePath)));
    if (length == 0 || length >= _countof(modulePath)) {
        return std::filesystem::path(L"shingiittai_d3d12_device_removed.log");
    }

    std::filesystem::path path(modulePath);
    return path.parent_path() / L"shingiittai_d3d12_device_removed.log";
}

std::string HrToString(HRESULT hr) {
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << std::setw(8)
           << std::setfill('0') << static_cast<uint32_t>(hr);
    return stream.str();
}

std::string WideToUtf8(const wchar_t *text) {
    if (!text) {
        return {};
    }

    const int size =
        WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
        return {};
    }

    std::string result(static_cast<size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr,
                        nullptr);
    return result;
}

std::string PickDebugName(const char *nameA, const wchar_t *nameW) {
    if (nameA && nameA[0] != '\0') {
        return nameA;
    }
    return WideToUtf8(nameW);
}

void WriteDredAllocationList(std::ofstream &log, const char *label,
                             const D3D12_DRED_ALLOCATION_NODE *node) {
    log << label << ":\n";
    if (!node) {
        log << "  (none)\n";
        return;
    }

    uint32_t count = 0;
    while (node && count < 32) {
        const std::string name = PickDebugName(node->ObjectNameA, node->ObjectNameW);
        log << "  [" << count << "] type="
            << static_cast<uint32_t>(node->AllocationType) << " name="
            << (name.empty() ? "(unnamed)" : name) << "\n";
        node = node->pNext;
        ++count;
    }
    if (node) {
        log << "  ... truncated ...\n";
    }
}

void WriteDredData(std::ofstream &log, ID3D12Device *device) {
    if (!device) {
        log << "DRED: device is null\n";
        return;
    }

    Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedData> dred;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dred)))) {
        log << "DRED: ID3D12DeviceRemovedExtendedData unavailable\n";
        return;
    }

    D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT breadcrumbs{};
    if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput(&breadcrumbs))) {
        log << "DRED breadcrumbs:\n";
        const D3D12_AUTO_BREADCRUMB_NODE *node =
            breadcrumbs.pHeadAutoBreadcrumbNode;
        uint32_t nodeIndex = 0;
        while (node && nodeIndex < 32) {
            const std::string listName = PickDebugName(
                node->pCommandListDebugNameA, node->pCommandListDebugNameW);
            const std::string queueName = PickDebugName(
                node->pCommandQueueDebugNameA, node->pCommandQueueDebugNameW);
            const UINT32 last =
                node->pLastBreadcrumbValue ? *node->pLastBreadcrumbValue : 0;

            log << "  node[" << nodeIndex << "] list="
                << (listName.empty() ? "(unnamed)" : listName)
                << " queue=" << (queueName.empty() ? "(unnamed)" : queueName)
                << " count=" << node->BreadcrumbCount
                << " lastCompleted=" << last << "\n";

            if (node->pCommandHistory && node->BreadcrumbCount > 0) {
                UINT32 start = 0;
                if (last > 12) {
                    start = last - 12;
                    if (start > node->BreadcrumbCount) {
                        start = node->BreadcrumbCount;
                    }
                }
                UINT32 end = last + 4;
                if (end < start) {
                    end = start;
                }
                if (end > node->BreadcrumbCount) {
                    end = node->BreadcrumbCount;
                }
                for (UINT32 i = start; i < end; ++i) {
                    log << "    op[" << i << "]="
                        << static_cast<uint32_t>(node->pCommandHistory[i]);
                    if (i == last) {
                        log << " <- last completed";
                    }
                    log << "\n";
                }
            }

            node = node->pNext;
            ++nodeIndex;
        }
        if (node) {
            log << "  ... breadcrumb nodes truncated ...\n";
        }
    } else {
        log << "DRED breadcrumbs: unavailable\n";
    }

    D3D12_DRED_PAGE_FAULT_OUTPUT pageFault{};
    if (SUCCEEDED(dred->GetPageFaultAllocationOutput(&pageFault))) {
        log << "DRED page fault VA=0x" << std::uppercase << std::hex
            << pageFault.PageFaultVA << std::dec << "\n";
        WriteDredAllocationList(log, "DRED existing allocations",
                                pageFault.pHeadExistingAllocationNode);
        WriteDredAllocationList(log, "DRED recently freed allocations",
                                pageFault.pHeadRecentFreedAllocationNode);
    } else {
        log << "DRED page fault: unavailable\n";
    }
}

Microsoft::WRL::ComPtr<IDXGIAdapter1>
PickHighPerformanceAdapter(IDXGIFactory7 *factory) {
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    if (!factory) {
        return adapter;
    }

    for (UINT index = 0;; ++index) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> candidate;
        if (FAILED(factory->EnumAdapterByGpuPreference(
                index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                IID_PPV_ARGS(&candidate)))) {
            break;
        }

        DXGI_ADAPTER_DESC1 desc{};
        candidate->GetDesc1(&desc);
        if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
            continue;
        }
        if (SUCCEEDED(D3D12CreateDevice(candidate.Get(),
                                        D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device),
                                        nullptr))) {
            adapter = candidate;
            break;
        }
    }

    return adapter;
}
} // namespace

DirectXCommon::~DirectXCommon() {
    if (fenceEvent_) {
        CloseHandle(fenceEvent_);
        fenceEvent_ = nullptr;
    }
}

void DirectXCommon::Initialize(HWND hwnd, int width, int height) {
    CreateFactory();
    CreateDevice();
    CreateCommandQueue();
    CreateCommandAllocator();
    CreateCommandList();
    CreateSwapChain(hwnd, width, height);
    CreateRTV();
    CreateSceneRenderTarget(width, height);
    CreateViewport(width, height);
    CreateScissor(width, height);
    CreateDepthStencil(width, height);
    CreateFence();
}

void DirectXCommon::BeginFrame() {
    ++diagnosticFrameId_;
    TrackGpuPhase("BeginFrame");
    WaitForFrame(backBufferIndex_);
    ID3D12CommandAllocator* commandAllocator =
        commandAllocators_[backBufferIndex_].Get();
    ThrowIfFailed(commandAllocator->Reset(),
                  "commandAllocator_->Reset failed");
    ThrowIfFailed(commandList_->Reset(commandAllocator, nullptr),
                  "commandList_->Reset failed");
    isCommandListRecording_ = true;

    ApplySceneViewportAndScissor();
    TrackGpuPhase("BeginFrame.ResetComplete");
}

void DirectXCommon::BeginScenePass() {
    TrackGpuPhase("BeginScenePass");
    TransitionSceneColor(D3D12_RESOURCE_STATE_RENDER_TARGET);

    auto sceneRtv = GetSceneRtvHandle();
    auto dsvHandle = dsvHeap_->GetCPUDescriptorHandleForHeapStart();

    ApplySceneViewportAndScissor();
    commandList_->OMSetRenderTargets(1, &sceneRtv, FALSE, &dsvHandle);
    commandList_->ClearRenderTargetView(sceneRtv, clearColor_, 0, nullptr);
    commandList_->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f,
                                        0, 0, nullptr);
}

void DirectXCommon::RestoreSceneRenderState(bool clearDepth) {
    TrackGpuPhase("RestoreSceneRenderState");
    TransitionSceneColor(D3D12_RESOURCE_STATE_RENDER_TARGET);

    auto sceneRtv = GetSceneRtvHandle();
    auto dsvHandle = dsvHeap_->GetCPUDescriptorHandleForHeapStart();

    ApplySceneViewportAndScissor();
    commandList_->OMSetRenderTargets(1, &sceneRtv, FALSE, &dsvHandle);
    if (clearDepth) {
        commandList_->ClearDepthStencilView(
            dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    }
}

void DirectXCommon::ClearDepth() {
    TrackGpuPhase("ClearDepth");
    auto dsvHandle = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
    commandList_->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH,
                                        1.0f, 0, 0, nullptr);
}

void DirectXCommon::EndScenePass() {
    TrackGpuPhase("EndScenePass");
    TransitionSceneColor(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void DirectXCommon::BeginBackBufferPass(bool bindDepth) {
    TrackGpuPhase("BeginBackBufferPass");
    TransitionBackBuffer(backBufferIndex_,
                         D3D12_RESOURCE_STATE_RENDER_TARGET);

    SetBackBufferRenderTarget(true, bindDepth);
}

void DirectXCommon::EndFrame() {
    TrackGpuPhase("EndFrame.Begin");
    TransitionBackBuffer(backBufferIndex_, D3D12_RESOURCE_STATE_PRESENT);

    TrackGpuPhase("EndFrame.CloseCommandList");
    ThrowIfFailed(commandList_->Close(), "commandList_->Close failed");
    isCommandListRecording_ = false;

    ID3D12CommandList *lists[] = {commandList_.Get()};
    TrackGpuPhase("EndFrame.ExecuteCommandLists");
    commandQueue_->ExecuteCommandLists(1, lists);

    TrackGpuPhase("EndFrame.Present");
    HRESULT presentResult = swapChain_->Present(1, 0);
    if (FAILED(presentResult)) {
        HRESULT removedReason = device_->GetDeviceRemovedReason();
        WriteDeviceRemovedLog(presentResult, removedReason);
        if (FAILED(removedReason)) {
            ThrowIfFailed(removedReason, "D3D12 device removed");
        }
        ThrowIfFailed(presentResult, "swapChain_->Present failed");
    }

    const UINT presentedBufferIndex = backBufferIndex_;
    fenceValue_++;
    TrackGpuPhase("EndFrame.SignalFence");
    ThrowIfFailed(commandQueue_->Signal(fence_.Get(), fenceValue_),
                  "commandQueue_->Signal failed");
    frameFenceValues_[presentedBufferIndex] = fenceValue_;

    backBufferIndex_ = swapChain_->GetCurrentBackBufferIndex();
}

void DirectXCommon::Resize(int width, int height) {
    TrackGpuPhase("Resize");
    if (!swapChain_ || width <= 0 || height <= 0) {
        return;
    }

    WaitForGpu();

    for (auto &backBuffer : backBuffers_) {
        backBuffer.Reset();
    }
    sceneColorBuffer_.Reset();
    depthBuffer_.Reset();

    ThrowIfFailed(swapChain_->ResizeBuffers(
                      kSwapChainBufferCount, static_cast<UINT>(width),
                      static_cast<UINT>(height), kBackBufferFormat, 0),
                  "swapChain_->ResizeBuffers failed");

    backBufferIndex_ = swapChain_->GetCurrentBackBufferIndex();

    CreateRTV();
    CreateSceneRenderTarget(width, height);
    CreateViewport(width, height);
    CreateScissor(width, height);
    CreateDepthStencil(width, height);
    UpdateDepthStencilSrv();
    UpdateSceneColorSrv();
}

void DirectXCommon::BeginUpload() {
    TrackGpuPhase("BeginUpload");
    if (isCommandListRecording_) {
        if (uploadPassActive_) {
            ++uploadPassDepth_;
        }
        return;
    }

    WaitForFrame(backBufferIndex_);
    ID3D12CommandAllocator* commandAllocator =
        commandAllocators_[backBufferIndex_].Get();
    ThrowIfFailed(commandAllocator->Reset(),
                  "commandAllocator_->Reset failed");

    ThrowIfFailed(commandList_->Reset(commandAllocator, nullptr),
                  "commandList_->Reset failed");
    isCommandListRecording_ = true;
    uploadPassActive_ = true;
    uploadPassDepth_ = 1;
}

void DirectXCommon::EndUpload() {
    TrackGpuPhase("EndUpload");
    if (!uploadPassActive_) {
        return;
    }
    if (uploadPassDepth_ > 1) {
        --uploadPassDepth_;
        return;
    }

    ThrowIfFailed(commandList_->Close(), "commandList_->Close failed");
    isCommandListRecording_ = false;
    uploadPassActive_ = false;
    uploadPassDepth_ = 0;

    ID3D12CommandList *lists[] = {commandList_.Get()};
    TrackGpuPhase("EndUpload.ExecuteCommandLists");
    commandQueue_->ExecuteCommandLists(1, lists);

    WaitForGpu();
}

void DirectXCommon::WaitForGpu() {
    TrackGpuPhase("WaitForGpu");
    fenceValue_++;
    ThrowIfFailed(commandQueue_->Signal(fence_.Get(), fenceValue_),
                  "commandQueue_->Signal failed");

    if (fence_->GetCompletedValue() < fenceValue_) {
        ThrowIfFailed(fence_->SetEventOnCompletion(fenceValue_, fenceEvent_),
                      "fence_->SetEventOnCompletion failed");
        WaitForSingleObject(fenceEvent_, INFINITE);
    }
    for (UINT i = 0; i < kSwapChainBufferCount; ++i) {
        frameFenceValues_[i] = fenceValue_;
    }
}

void DirectXCommon::SetBackBufferRenderTarget(bool clear, bool bindDepth) {
    auto rtvHandle = GetBackBufferRtvHandle();

    auto dsvHandle = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
    D3D12_CPU_DESCRIPTOR_HANDLE *dsvHandlePtr =
        bindDepth ? &dsvHandle : nullptr;

    ApplySceneViewportAndScissor();
    commandList_->OMSetRenderTargets(1, &rtvHandle, FALSE, dsvHandlePtr);

    if (clear) {
        commandList_->ClearRenderTargetView(rtvHandle, clearColor_, 0,
                                            nullptr);
        if (bindDepth) {
            commandList_->ClearDepthStencilView(
                dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        }
    }
}

void DirectXCommon::ApplySceneViewportAndScissor() {
    commandList_->RSSetViewports(1, &sceneViewport_);
    commandList_->RSSetScissorRects(1, &sceneScissorRect_);
}

void DirectXCommon::TransitionSceneColor(D3D12_RESOURCE_STATES afterState) {
    if (!sceneColorBuffer_ || sceneColorState_ == afterState) {
        return;
    }

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        sceneColorBuffer_.Get(), sceneColorState_, afterState);
    TrackGpuPhase("TransitionSceneColor");
    commandList_->ResourceBarrier(1, &barrier);
    sceneColorState_ = afterState;
}

void DirectXCommon::TransitionBackBuffer(
    UINT index, D3D12_RESOURCE_STATES afterState) {
    if (index >= kSwapChainBufferCount || !backBuffers_[index] ||
        backBufferStates_[index] == afterState) {
        return;
    }

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        backBuffers_[index].Get(), backBufferStates_[index], afterState);
    TrackGpuPhase("TransitionBackBuffer");
    commandList_->ResourceBarrier(1, &barrier);
    backBufferStates_[index] = afterState;
}

void DirectXCommon::CreateDepthStencilSrv(SrvManager *srvManager) {
    srvManager_ = srvManager;
    depthSrvIndex_ = srvManager_->Allocate();
    depthSrvGpuHandle_ = srvManager_->GetGpuHandle(depthSrvIndex_);
    UpdateDepthStencilSrv();
}

void DirectXCommon::RegisterSceneColorSRV(SrvManager *srvManager) {
    if (srvManager == nullptr) {
        throw std::runtime_error("RegisterSceneColorSRV: srvManager is null");
    }

    srvManager_ = srvManager;
    if (sceneSrvIndex_ == UINT_MAX) {
        sceneSrvIndex_ = srvManager_->Allocate();
    }
    UpdateSceneColorSrv();
}

void DirectXCommon::TransitionDepthToShaderResource() {
    if (!depthBuffer_ ||
        depthState_ == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        return;
    }

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        depthBuffer_.Get(), depthState_,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    TrackGpuPhase("TransitionDepthToShaderResource");
    commandList_->ResourceBarrier(1, &barrier);
    depthState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
}

void DirectXCommon::TransitionDepthToWrite() {
    if (!depthBuffer_ || depthState_ == D3D12_RESOURCE_STATE_DEPTH_WRITE) {
        return;
    }

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        depthBuffer_.Get(), depthState_, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    TrackGpuPhase("TransitionDepthToWrite");
    commandList_->ResourceBarrier(1, &barrier);
    depthState_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;
}

void DirectXCommon::SetClearColor(const DirectX::XMFLOAT4 &color) {
    SetClearColor(color.x, color.y, color.z, color.w);
}

void DirectXCommon::SetClearColor(float r, float g, float b, float a) {
    clearColor_[0] = r;
    clearColor_[1] = g;
    clearColor_[2] = b;
    clearColor_[3] = a;
}

void DirectXCommon::ResetClearColor() {
    clearColor_[0] = kClearColor[0];
    clearColor_[1] = kClearColor[1];
    clearColor_[2] = kClearColor[2];
    clearColor_[3] = kClearColor[3];
}

bool DirectXCommon::IsDeviceRemoved() const {
    return device_ && FAILED(device_->GetDeviceRemovedReason());
}

void DirectXCommon::CreateFactory() {
    ThrowIfFailed(CreateDXGIFactory(IID_PPV_ARGS(&factory_)),
                  "CreateDXGIFactory failed");
}

void DirectXCommon::CreateDevice() {
#ifdef _DEBUG
    Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedDataSettings>
        dredSettings;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dredSettings)))) {
        dredSettings->SetAutoBreadcrumbsEnablement(
            D3D12_DRED_ENABLEMENT_FORCED_ON);
        dredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        dredSettings->SetWatsonDumpEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
    }
#endif

    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter =
        PickHighPerformanceAdapter(factory_.Get());
    IUnknown *deviceAdapter = adapter ? adapter.Get() : nullptr;
    ThrowIfFailed(D3D12CreateDevice(deviceAdapter, D3D_FEATURE_LEVEL_11_0,
                                    IID_PPV_ARGS(&device_)),
                  "D3D12CreateDevice failed");
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    Microsoft::WRL::ComPtr<IDXGIAdapter> actualAdapter;
    if (SUCCEEDED(device_.As(&dxgiDevice)) &&
        SUCCEEDED(dxgiDevice->GetAdapter(&actualAdapter))) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> actualAdapter1;
        if (SUCCEEDED(actualAdapter.As(&actualAdapter1))) {
            actualAdapter1->GetDesc1(&adapterDesc_);
        }
    }
    device_->SetName(L"DirectXCommon.Device");
}

void DirectXCommon::CreateCommandQueue() {
    D3D12_COMMAND_QUEUE_DESC desc{};
    ThrowIfFailed(
        device_->CreateCommandQueue(&desc, IID_PPV_ARGS(&commandQueue_)),
        "CreateCommandQueue failed");
    commandQueue_->SetName(L"DirectXCommon.CommandQueue");
}

void DirectXCommon::CreateCommandAllocator() {
    for (UINT i = 0; i < kSwapChainBufferCount; ++i) {
        ThrowIfFailed(
            device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                            IID_PPV_ARGS(&commandAllocators_[i])),
            "CreateCommandAllocator failed");
        wchar_t name[64]{};
        swprintf_s(name, L"DirectXCommon.CommandAllocator[%u]", i);
        commandAllocators_[i]->SetName(name);
    }
}

void DirectXCommon::CreateCommandList() {
    ThrowIfFailed(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                             commandAllocators_[backBufferIndex_].Get(),
                                             nullptr,
                                             IID_PPV_ARGS(&commandList_)),
                  "CreateCommandList failed");
    commandList_->SetName(L"DirectXCommon.CommandList");

    ThrowIfFailed(commandList_->Close(), "commandList_->Close failed");
}

void DirectXCommon::CreateSwapChain(HWND hwnd, int width, int height) {
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = width;
    desc.Height = height;
    desc.Format = kBackBufferFormat;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = kSwapChainBufferCount;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    Microsoft::WRL::ComPtr<IDXGISwapChain1> sc1;
    ThrowIfFailed(factory_->CreateSwapChainForHwnd(
                      commandQueue_.Get(), hwnd, &desc, nullptr, nullptr, &sc1),
                  "CreateSwapChainForHwnd failed");

    ThrowIfFailed(sc1.As(&swapChain_), "SwapChain As() failed");

    backBufferIndex_ = swapChain_->GetCurrentBackBufferIndex();
}

void DirectXCommon::CreateRTV() {
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heapDesc.NumDescriptors = kSwapChainBufferCount + 1;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    ThrowIfFailed(
        device_->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&rtvHeap_)),
        "CreateDescriptorHeap(RTV) failed");
    rtvHeap_->SetName(L"DirectXCommon.RtvHeap");

    rtvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    CD3DX12_CPU_DESCRIPTOR_HANDLE handle(
        rtvHeap_->GetCPUDescriptorHandleForHeapStart());

    for (UINT i = 0; i < kSwapChainBufferCount; i++) {
        ThrowIfFailed(swapChain_->GetBuffer(i, IID_PPV_ARGS(&backBuffers_[i])),
                      "swapChain_->GetBuffer failed");
        wchar_t name[64]{};
        swprintf_s(name, L"DirectXCommon.BackBuffer[%u]", i);
        backBuffers_[i]->SetName(name);

        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        rtvDesc.Format = kBackBufferFormat;

        device_->CreateRenderTargetView(backBuffers_[i].Get(), &rtvDesc,
                                        handle);
        backBufferStates_[i] = D3D12_RESOURCE_STATE_PRESENT;

        handle.Offset(1, rtvDescriptorSize_);
    }
}

void DirectXCommon::CreateSceneRenderTarget(int width, int height) {
    D3D12_RESOURCE_DESC resDesc{};
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resDesc.Width = static_cast<UINT64>(width);
    resDesc.Height = static_cast<UINT>(height);
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels = 1;
    resDesc.Format = kSceneColorFormat;
    resDesc.SampleDesc.Count = 1;
    resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = kSceneColorFormat;
    clearValue.Color[0] = clearColor_[0];
    clearValue.Color[1] = clearColor_[1];
    clearValue.Color[2] = clearColor_[2];
    clearValue.Color[3] = clearColor_[3];

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);

    ThrowIfFailed(device_->CreateCommittedResource(
                      &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
                      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
                      IID_PPV_ARGS(&sceneColorBuffer_)),
                  "CreateCommittedResource(SceneRenderTarget) failed");
    sceneColorBuffer_->SetName(L"DirectXCommon.SceneColorBuffer");
    sceneColorState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Format = kSceneColorFormat;

    device_->CreateRenderTargetView(sceneColorBuffer_.Get(), &rtvDesc,
                                    GetSceneRtvHandle());
}

void DirectXCommon::CreateViewport(int width, int height) {
    sceneViewport_.TopLeftX = 0.0f;
    sceneViewport_.TopLeftY = 0.0f;
    sceneViewport_.Width = static_cast<float>(width);
    sceneViewport_.Height = static_cast<float>(height);
    sceneViewport_.MinDepth = 0.0f;
    sceneViewport_.MaxDepth = 1.0f;
}

void DirectXCommon::CreateScissor(int width, int height) {
    sceneScissorRect_.left = 0;
    sceneScissorRect_.top = 0;
    sceneScissorRect_.right = width;
    sceneScissorRect_.bottom = height;
}

void DirectXCommon::CreateDepthStencil(int width, int height) {
    D3D12_DESCRIPTOR_HEAP_DESC dsvDesc{};
    dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvDesc.NumDescriptors = 1;

    ThrowIfFailed(
        device_->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&dsvHeap_)),
        "CreateDescriptorHeap(DSV) failed");
    dsvHeap_->SetName(L"DirectXCommon.DsvHeap");

    D3D12_RESOURCE_DESC resDesc{};
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resDesc.Width = width;
    resDesc.Height = height;
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels = 1;
    resDesc.Format = kDepthResourceFormat;
    resDesc.SampleDesc.Count = 1;
    resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = kDepthStencilFormat;
    clearValue.DepthStencil.Depth = 1.0f;
    clearValue.DepthStencil.Stencil = 0;

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);

    ThrowIfFailed(device_->CreateCommittedResource(
                      &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
                      D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue,
                      IID_PPV_ARGS(&depthBuffer_)),
                  "CreateCommittedResource(DepthStencil) failed");
    depthBuffer_->SetName(L"DirectXCommon.DepthBuffer");

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDescView{};
    dsvDescView.Format = kDepthStencilFormat;
    dsvDescView.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

    device_->CreateDepthStencilView(
        depthBuffer_.Get(), &dsvDescView,
        dsvHeap_->GetCPUDescriptorHandleForHeapStart());
    depthState_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;
}

void DirectXCommon::UpdateDepthStencilSrv() {
    if (!srvManager_ || depthSrvIndex_ == UINT_MAX || !depthBuffer_) {
        return;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = kDepthSrvFormat;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    device_->CreateShaderResourceView(
        depthBuffer_.Get(), &srvDesc,
        srvManager_->GetCpuHandle(depthSrvIndex_));
}

void DirectXCommon::UpdateSceneColorSrv() {
    if (!srvManager_ || sceneSrvIndex_ == UINT_MAX || !sceneColorBuffer_) {
        return;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = kSceneColorFormat;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    device_->CreateShaderResourceView(
        sceneColorBuffer_.Get(), &srvDesc,
        srvManager_->GetCpuHandle(sceneSrvIndex_));
}

D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::GetBackBufferRtvHandle() const {
    return CD3DX12_CPU_DESCRIPTOR_HANDLE(
        rtvHeap_->GetCPUDescriptorHandleForHeapStart(),
        static_cast<INT>(backBufferIndex_),
        static_cast<INT>(rtvDescriptorSize_));
}

D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::GetSceneRtvHandle() const {
    return CD3DX12_CPU_DESCRIPTOR_HANDLE(
        rtvHeap_->GetCPUDescriptorHandleForHeapStart(),
        static_cast<INT>(kSceneRtvIndex), static_cast<INT>(rtvDescriptorSize_));
}

D3D12_GPU_DESCRIPTOR_HANDLE
DirectXCommon::GetSceneSrvGpuHandle(const SrvManager *srvManager) const {
    return srvManager->GetGpuHandle(sceneSrvIndex_);
}

void DirectXCommon::CreateFence() {
    ThrowIfFailed(
        device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)),
        "CreateFence failed");
    fence_->SetName(L"DirectXCommon.FrameFence");

    fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent_) {
        throw std::runtime_error("CreateEvent failed");
    }
}

void DirectXCommon::WaitForFrame(UINT frameIndex) {
    if (frameIndex >= kSwapChainBufferCount) {
        return;
    }

    const UINT64 fenceValue = frameFenceValues_[frameIndex];
    if (fenceValue == 0 || fence_->GetCompletedValue() >= fenceValue) {
        return;
    }

    ThrowIfFailed(fence_->SetEventOnCompletion(fenceValue, fenceEvent_),
                  "fence_->SetEventOnCompletion failed");
    WaitForSingleObject(fenceEvent_, INFINITE);
}

void DirectXCommon::TrackGpuPhase(const char *phase) {
    recentGpuPhases_[recentGpuPhaseCursor_] = phase;
    recentGpuPhaseCursor_ =
        (recentGpuPhaseCursor_ + 1) % kRecentGpuPhaseCount;
    if (recentGpuPhaseSize_ < kRecentGpuPhaseCount) {
        ++recentGpuPhaseSize_;
    }
}

void DirectXCommon::WriteDeviceRemovedLog(HRESULT presentResult,
                                          HRESULT removedReason) const {
    std::ofstream log(GetDeviceRemovedLogPath(), std::ios::app);
    if (!log) {
        return;
    }

    SYSTEMTIME now{};
    GetLocalTime(&now);
    log << "============================================================\n";
    log << "D3D12 device removed at " << now.wYear << "-"
        << std::setw(2) << std::setfill('0') << now.wMonth << "-"
        << std::setw(2) << std::setfill('0') << now.wDay << " "
        << std::setw(2) << std::setfill('0') << now.wHour << ":"
        << std::setw(2) << std::setfill('0') << now.wMinute << ":"
        << std::setw(2) << std::setfill('0') << now.wSecond << "."
        << std::setw(3) << std::setfill('0') << now.wMilliseconds
        << std::setfill(' ') << "\n";
    log << "presentResult=" << HrToString(presentResult)
        << " removedReason=" << HrToString(removedReason) << "\n";
    log << "diagnosticFrameId=" << diagnosticFrameId_
        << " backBufferIndex=" << backBufferIndex_
        << " fenceValue=" << fenceValue_
        << " completedFence="
        << (fence_ ? fence_->GetCompletedValue() : 0)
        << " recording=" << (isCommandListRecording_ ? "true" : "false")
        << " uploadActive=" << (uploadPassActive_ ? "true" : "false")
        << " uploadDepth=" << uploadPassDepth_ << "\n";

    for (UINT i = 0; i < kSwapChainBufferCount; ++i) {
        log << "frameFenceValues[" << i << "]=" << frameFenceValues_[i]
            << " backBufferState[" << i << "]="
            << static_cast<uint32_t>(backBufferStates_[i]) << "\n";
    }
    log << "sceneColorState=" << static_cast<uint32_t>(sceneColorState_)
        << " depthState=" << static_cast<uint32_t>(depthState_) << "\n";

    log << "recent gpu phases (oldest -> newest):\n";
    const uint32_t start =
        (recentGpuPhaseCursor_ + kRecentGpuPhaseCount - recentGpuPhaseSize_) %
        kRecentGpuPhaseCount;
    for (uint32_t i = 0; i < recentGpuPhaseSize_; ++i) {
        const uint32_t index = (start + i) % kRecentGpuPhaseCount;
        log << "  [" << i << "] "
            << (recentGpuPhases_[index] ? recentGpuPhases_[index] : "(null)")
            << "\n";
    }

    WriteDredData(log, device_.Get());
    log << "\n";
}
