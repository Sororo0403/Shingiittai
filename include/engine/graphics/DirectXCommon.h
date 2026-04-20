#pragma once
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#include "SrvManager.h"
class DirectXCommon {
  public:
    /// <summary>
    /// 初期化処理
    /// </summary>
    /// <param name="hwnd">ウィンドウハンドル</param>
    /// <param name="width">クライアント領域の幅</param>
    /// <param name="height">クライアント領域の高さ</param>
    void Initialize(HWND hwnd, int width, int height);

    /// <summary>
    /// フレーム開始処理
    /// </summary>
    void BeginFrame();

    /// <summary>
    /// sceneRT への描画開始
    /// </summary>
    void BeginScenePass();

    /// <summary>
    /// sceneRT への描画終了（SRV読取状態へ遷移）
    /// </summary>
    void EndScenePass();

    /// <summary>
    /// バックバッファ描画開始
    /// </summary>
    void BeginBackBufferPass();

    /// <summary>
    /// フレーム終了処理
    /// </summary>
    void EndFrame();

    /// <summary>
    /// アップロード開始処理
    /// </summary>
    void BeginUpload();

    /// <summary>
    /// アップロード終了処理
    /// </summary>
    void EndUpload();

    /// <summary>
    /// GPU同期待ち
    /// </summary>
    void WaitForGpu();

    // Getter
    ID3D12Device *GetDevice() const { return device_.Get(); }
    ID3D12CommandQueue *GetCommandQueue() const { return commandQueue_.Get(); }
    ID3D12GraphicsCommandList *GetCommandList() const {
        return commandList_.Get();
    }
    UINT GetSwapChainBufferCount() const { return kSwapChainBufferCount; }

    ID3D12Resource *GetSceneColorBuffer() const {
        return sceneColorBuffer_.Get();
    }

    /// <summary>
    /// sceneColorBuffer_ を SRV 登録する
    /// </summary>
    void RegisterSceneColorSRV(SrvManager *srvManager);

    UINT GetSceneSrvIndex() const { return sceneSrvIndex_; }

    D3D12_GPU_DESCRIPTOR_HANDLE
    GetSceneSrvGpuHandle(const SrvManager *srvManager) const {
        return srvManager->GetGpuHandle(sceneSrvIndex_);
    }

  private:
    // Create
    void CreateFactory();
    void CreateDevice();
    void CreateCommandQueue();
    void CreateCommandAllocator();
    void CreateCommandList();
    void CreateSwapChain(HWND hwnd, int width, int height);
    void CreateRTV();
    void CreateSceneRenderTarget(int width, int height);
    void CreateViewport(int width, int height);
    void CreateScissor(int width, int height);
    void CreateDepthStencil(int width, int height);
    void CreateFence();

    D3D12_CPU_DESCRIPTOR_HANDLE GetBackBufferRtvHandle() const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetSceneRtvHandle() const;

  private:
    static constexpr UINT kSwapChainBufferCount = 2;
    static constexpr UINT kSceneRtvIndex = kSwapChainBufferCount;
    static constexpr float kClearColor[4] = {0.1f, 0.2f, 0.4f, 1.0f};

    Microsoft::WRL::ComPtr<IDXGIFactory7> factory_;
    Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain_;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue_;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> backBuffers_[kSwapChainBufferCount];
    Microsoft::WRL::ComPtr<ID3D12Resource> sceneColorBuffer_;
    UINT sceneSrvIndex_ = UINT_MAX;
    UINT rtvDescriptorSize_ = 0;
    UINT backBufferIndex_ = 0;

    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    UINT64 fenceValue_ = 0;
    HANDLE fenceEvent_ = nullptr;

    D3D12_VIEWPORT viewport_{};
    D3D12_RECT scissorRect_{};

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> depthBuffer_;
};