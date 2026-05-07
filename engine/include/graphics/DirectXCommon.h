#pragma once
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

class SrvManager;

/// <summary>
/// Direct3D 12 のデバイスと描画フレーム管理を担う
/// </summary>
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
    /// フレーム終了処理
    /// </summary>
    void EndFrame();

    /// <summary>
    /// シーンカラー用RTへの描画開始
    /// </summary>
    void BeginScenePass();

    /// <summary>
    /// シーンカラー用RTへの描画終了
    /// </summary>
    void EndScenePass();

    /// <summary>
    /// バックバッファへの描画開始
    /// </summary>
    void BeginBackBufferPass();

    /// <summary>
    /// 現在のバックバッファを描画先に設定する
    /// </summary>
    /// <param name="clear">trueの場合、色と深度をクリアする</param>
    void SetBackBufferRenderTarget(bool clear = false, bool bindDepth = true);

    /// <summary>
    /// 深度バッファをシェーダーから読めるSRVとして登録する
    /// </summary>
    void CreateDepthStencilSrv(SrvManager *srvManager);

    /// <summary>
    /// シーンカラーをシェーダーから読めるSRVとして登録する
    /// </summary>
    void RegisterSceneColorSRV(SrvManager *srvManager);

    /// <summary>
    /// 深度バッファをシェーダー読み取り状態へ遷移する
    /// </summary>
    void TransitionDepthToShaderResource();

    /// <summary>
    /// 深度バッファを深度書き込み状態へ遷移する
    /// </summary>
    void TransitionDepthToWrite();

    /// <summary>
    /// 描画ターゲットと深度バッファを新しいサイズに合わせて再生成する
    /// </summary>
    /// <param name="width">クライアント領域の幅</param>
    /// <param name="height">クライアント領域の高さ</param>
    void Resize(int width, int height);

    /// <summary>
    /// アップロード開始処理
    /// </summary>
    void BeginUpload();

    /// <summary>
    /// アップデート終了処理
    /// </summary>
    void EndUpload();

    /// <summary>
    /// GPU同期待ち
    /// </summary>
    void WaitForGpu();

    /// <summary>
    /// D3D12デバイスを取得する
    /// </summary>
    /// <returns>D3D12デバイス</returns>
    ID3D12Device *GetDevice() const { return device_.Get(); }
    /// <summary>
    /// コマンドキューを取得する
    /// </summary>
    /// <returns>コマンドキュー</returns>
    ID3D12CommandQueue *GetCommandQueue() const { return commandQueue_.Get(); }
    /// <summary>
    /// グラフィックスコマンドリストを取得する
    /// </summary>
    /// <returns>コマンドリスト</returns>
    ID3D12GraphicsCommandList *GetCommandList() const {
        return commandList_.Get();
    }
    /// <summary>
    /// スワップチェーンのバッファ数を取得する
    /// </summary>
    /// <returns>バックバッファ数</returns>
    UINT GetSwapChainBufferCount() const { return kSwapChainBufferCount; }
    /// <summary>
    /// 深度ステンシルビューのCPUハンドルを取得する
    /// </summary>
    D3D12_CPU_DESCRIPTOR_HANDLE GetDepthStencilView() const {
        return dsvHeap_->GetCPUDescriptorHandleForHeapStart();
    }
    /// <summary>
    /// 深度SRVのGPUハンドルを取得する
    /// </summary>
    D3D12_GPU_DESCRIPTOR_HANDLE GetDepthStencilGpuHandle() const {
        return depthSrvGpuHandle_;
    }
    ID3D12Resource *GetSceneColorBuffer() const {
        return sceneColorBuffer_.Get();
    }
    UINT GetSceneSrvIndex() const { return sceneSrvIndex_; }
    D3D12_GPU_DESCRIPTOR_HANDLE
    GetSceneSrvGpuHandle(const SrvManager *srvManager) const;

  private:
    /// <summary>
    /// DXGIファクトリを生成する
    /// </summary>
    void CreateFactory();
    /// <summary>
    /// D3D12デバイスを生成する
    /// </summary>
    void CreateDevice();
    /// <summary>
    /// コマンドキューを生成する
    /// </summary>
    void CreateCommandQueue();
    /// <summary>
    /// コマンドアロケータを生成する
    /// </summary>
    void CreateCommandAllocator();
    /// <summary>
    /// コマンドリストを生成する
    /// </summary>
    void CreateCommandList();
    /// <summary>
    /// スワップチェーンを生成する
    /// </summary>
    void CreateSwapChain(HWND hwnd, int width, int height);
    /// <summary>
    /// RTVヒープを生成する
    /// </summary>
    void CreateRTV();
    /// <summary>
    /// シーンカラー用RTを生成する
    /// </summary>
    void CreateSceneRenderTarget(int width, int height);
    /// <summary>
    /// ビューポートを設定する
    /// </summary>
    void CreateViewport(int width, int height);
    /// <summary>
    /// シザー矩形を設定する
    /// </summary>
    void CreateScissor(int width, int height);
    /// <summary>
    /// 深度ステンシルバッファを生成する
    /// </summary>
    void CreateDepthStencil(int width, int height);
    void UpdateDepthStencilSrv();
    D3D12_CPU_DESCRIPTOR_HANDLE GetBackBufferRtvHandle() const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetSceneRtvHandle() const;
    /// <summary>
    /// GPU同期用フェンスを生成する
    /// </summary>
    void CreateFence();

  private:
    static constexpr UINT kSwapChainBufferCount = 2;
    static constexpr UINT kSceneRtvIndex = kSwapChainBufferCount;
    static constexpr float kClearColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};

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
    SrvManager *srvManager_ = nullptr;
    UINT depthSrvIndex_ = UINT_MAX;
    D3D12_GPU_DESCRIPTOR_HANDLE depthSrvGpuHandle_{};
    D3D12_RESOURCE_STATES depthState_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;
};
