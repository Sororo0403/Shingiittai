#pragma once
#ifdef _DEBUG
#include <d3d12.h>
#include <unordered_map>

class DirectXCommon;
class SrvManager;
class WinApp;
struct ImGui_ImplDX12_InitInfo;

/// <summary>
/// ImGui の初期化とフレーム処理を管理する
/// </summary>
class ImguiManager {
  public:
    ~ImguiManager();

    /// <summary>
    /// ImGuiのWin32/DX12バックエンドを初期化する
    /// </summary>
    /// <param name="winApp">WinAppインスタンス</param>
    /// <param name="dxCommon">DirectXCommonインスタンス</param>
    /// <param name="srvManager">SrvManagerインスタンス</param>
    void Initialize(WinApp *winApp, DirectXCommon *dxCommon,
                    SrvManager *srvManager);

    /// <summary>
    /// ImGuiバックエンドとSRV割り当てを解放する
    /// </summary>
    void Finalize();

    /// <summary>
    /// ImGuiの新しいフレームを開始する
    /// </summary>
    /// <param name="commandList">コマンドリスト</param>
    void Begin(ID3D12GraphicsCommandList *commandList);

    /// <summary>
    /// ImGuiの描画データをコマンドリストへ積む
    /// </summary>
    /// <param name="commandList">コマンドリスト</param>
    void End(ID3D12GraphicsCommandList *commandList);

  private:
    bool IsReady() const;
    static void AllocateSrvDescriptor(ImGui_ImplDX12_InitInfo *info,
                                      D3D12_CPU_DESCRIPTOR_HANDLE *outCpu,
                                      D3D12_GPU_DESCRIPTOR_HANDLE *outGpu);
    static void FreeSrvDescriptor(ImGui_ImplDX12_InitInfo *info,
                                  D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                                  D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle);

    SrvManager *srvManager_ = nullptr;
    std::unordered_map<SIZE_T, UINT> allocatedSrvIndices_;
    bool contextCreated_ = false;
    bool win32Initialized_ = false;
    bool dx12Initialized_ = false;
};

#endif
