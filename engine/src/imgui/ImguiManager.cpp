#ifdef _DEBUG
#include "imgui/ImguiManager.h"
#include "core/WinApp.h"
#include "graphics/DirectXCommon.h"
#include "graphics/SrvManager.h"
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"

ImguiManager::~ImguiManager() noexcept {
    try {
        Finalize();
    } catch (...) {
    }
}

void ImguiManager::Initialize(WinApp *winApp, DirectXCommon *dxCommon,
                              SrvManager *srvManager) {
    Finalize();

    srvManager_ = srvManager;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(winApp->GetHwnd());

    ImGui_ImplDX12_InitInfo init_info{};
    init_info.Device = dxCommon->GetDevice();
    init_info.CommandQueue = dxCommon->GetCommandQueue();
    init_info.NumFramesInFlight = dxCommon->GetSwapChainBufferCount();
    init_info.RTVFormat = DirectXCommon::kBackBufferFormat;
    init_info.DSVFormat = DXGI_FORMAT_UNKNOWN;
    init_info.UserData = this;
    init_info.SrvDescriptorHeap = srvManager_->GetHeap();

    init_info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo *info,
                                        D3D12_CPU_DESCRIPTOR_HANDLE *out_cpu,
                                        D3D12_GPU_DESCRIPTOR_HANDLE *out_gpu) {
        auto *manager = static_cast<ImguiManager *>(info->UserData);
        uint32_t index = manager->srvManager_->Allocate();
        *out_cpu = manager->srvManager_->GetCpuHandle(index);
        *out_gpu = manager->srvManager_->GetGpuHandle(index);
        manager->allocatedSrvIndices_[out_cpu->ptr] = index;
    };

    init_info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo *info,
                                       D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                                       D3D12_GPU_DESCRIPTOR_HANDLE) {
        auto *manager = static_cast<ImguiManager *>(info->UserData);
        auto it = manager->allocatedSrvIndices_.find(cpuHandle.ptr);
        if (it == manager->allocatedSrvIndices_.end()) {
            return;
        }
        manager->srvManager_->Free(it->second);
        manager->allocatedSrvIndices_.erase(it);
    };

    ImGui_ImplDX12_Init(&init_info);
    initialized_ = true;
}

void ImguiManager::Finalize() {
    if (initialized_) {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        initialized_ = false;
    }

    if (srvManager_ != nullptr) {
        for (const auto &[handlePtr, index] : allocatedSrvIndices_) {
            (void)handlePtr;
            srvManager_->Free(index);
        }
    }
    allocatedSrvIndices_.clear();
    srvManager_ = nullptr;
}

void ImguiManager::Begin(ID3D12GraphicsCommandList *commandList) {
    ID3D12DescriptorHeap *heaps[] = {srvManager_->GetHeap()};
    commandList->SetDescriptorHeaps(1, heaps);

    ImGui_ImplWin32_NewFrame();
    ImGui_ImplDX12_NewFrame();
    ImGui::NewFrame();
}

void ImguiManager::End(ID3D12GraphicsCommandList *commandList) {
    ImGui::Render();

    ID3D12DescriptorHeap *heaps[] = {srvManager_->GetHeap()};
    commandList->SetDescriptorHeaps(1, heaps);

    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
}
#endif
