#include "MagnetismicRenderer.h"

#include <Windows.h>
#include <cmath>
#include <cstring>
#include <d3dcompiler.h>

#pragma comment(lib, "d3dcompiler.lib")

namespace {

inline MagnetVec3 Normalize(const MagnetVec3 &v) {
    const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len <= 0.00001f) {
        return {1.0f, 0.0f, 0.0f};
    }
    return {v.x / len, v.y / len, v.z / len};
}

inline MagnetVec3 Mul(const MagnetVec3 &v, float s) {
    return {v.x * s, v.y * s, v.z * s};
}

inline MagnetVec3 Add(const MagnetVec3 &a, const MagnetVec3 &b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

inline void SetVertex(MagnetVertex &v, const MagnetVec3 &p, float u, float vv) {
    v.position[0] = p.x;
    v.position[1] = p.y;
    v.position[2] = p.z;
    v.uv[0] = u;
    v.uv[1] = vv;
}

inline void OutputShaderError(ID3DBlob *errBlob) {
    if (errBlob == nullptr) {
        return;
    }

    const char *msg =
        reinterpret_cast<const char *>(errBlob->GetBufferPointer());
    if (msg != nullptr) {
        OutputDebugStringA(msg);
    }
}

} // namespace

bool MagnetismicRenderer::Initialize(ID3D12Device *device,
                                     DXGI_FORMAT rtvFormat,
                                     DXGI_FORMAT dsvFormat,
                                     const wchar_t *vsPath,
                                     const wchar_t *psPath) {
    if (!CreateRootSignature(device)) {
        return false;
    }
    if (!CreatePipelineState(device, rtvFormat, dsvFormat, vsPath, psPath)) {
        return false;
    }
    if (!CreateQuadResources(device)) {
        return false;
    }
    if (!CreateConstantBuffers(device)) {
        return false;
    }
    return true;
}

void MagnetismicRenderer::BeginFrame(float deltaTime) {
    elapsedTime_ += deltaTime;
}

void MagnetismicRenderer::Draw(ID3D12GraphicsCommandList *commandList,
                               const MagnetismicInstance &inst,
                               const MagnetMatrix4x4 &viewProj,
                               const MagnetVec3 &cameraRight,
                               const MagnetVec3 &cameraUp) {
    if (commandList == nullptr || mappedVB_ == nullptr ||
        mappedVS_ == nullptr || mappedPS_ == nullptr) {
        return;
    }

    UpdateQuadVertices(inst, cameraRight, cameraUp);

    mappedVS_->world = MakeIdentity();
    mappedVS_->viewProj = viewProj;

    mappedPS_->time = inst.time;
    mappedPS_->intensity = inst.intensity;
    mappedPS_->alpha = inst.alpha;
    mappedPS_->scale = inst.size;

    mappedPS_->swirlA = inst.swirlA;
    mappedPS_->swirlB = inst.swirlB;
    mappedPS_->noiseScale = inst.noiseScale;
    mappedPS_->stepScale = inst.stepScale;

    mappedPS_->colorA[0] = inst.colorA.x;
    mappedPS_->colorA[1] = inst.colorA.y;
    mappedPS_->colorA[2] = inst.colorA.z;
    mappedPS_->colorA[3] = inst.colorA.w;

    mappedPS_->colorB[0] = inst.colorB.x;
    mappedPS_->colorB[1] = inst.colorB.y;
    mappedPS_->colorB[2] = inst.colorB.z;
    mappedPS_->colorB[3] = inst.colorB.w;

    mappedPS_->params0[0] = inst.brightness;
    mappedPS_->params0[1] = inst.distFade;
    mappedPS_->params0[2] = inst.innerBoost;
    mappedPS_->params0[3] = 0.0f;

    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetPipelineState(pipelineState_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    commandList->IASetVertexBuffers(0, 1, &vbView_);

    commandList->SetGraphicsRootConstantBufferView(
        0, vsConstantBuffer_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(
        1, psConstantBuffer_->GetGPUVirtualAddress());

    commandList->DrawInstanced(4, 1, 0, 0);
}

bool MagnetismicRenderer::CreateRootSignature(ID3D12Device *device) {
    D3D12_ROOT_PARAMETER params[2] = {};

    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].Descriptor.RegisterSpace = 0;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].Descriptor.ShaderRegister = 1;
    params[1].Descriptor.RegisterSpace = 0;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 2;
    desc.pParameters = params;
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> sigBlob;
    ComPtr<ID3DBlob> errBlob;
    HRESULT hr = D3D12SerializeRootSignature(
        &desc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
    if (FAILED(hr)) {
        OutputShaderError(errBlob.Get());
        return false;
    }

    hr = device->CreateRootSignature(0, sigBlob->GetBufferPointer(),
                                     sigBlob->GetBufferSize(),
                                     IID_PPV_ARGS(&rootSignature_));
    return SUCCEEDED(hr);
}

bool MagnetismicRenderer::CreatePipelineState(ID3D12Device *device,
                                              DXGI_FORMAT rtvFormat,
                                              DXGI_FORMAT dsvFormat,
                                              const wchar_t *vsPath,
                                              const wchar_t *psPath) {
    UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ComPtr<ID3DBlob> vsBlob;
    ComPtr<ID3DBlob> psBlob;
    ComPtr<ID3DBlob> errBlob;

    HRESULT hr = D3DCompileFromFile(
        vsPath, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "vs_5_0",
        compileFlags, 0, &vsBlob, &errBlob);
    if (FAILED(hr)) {
        OutputShaderError(errBlob.Get());
        return false;
    }

    hr = D3DCompileFromFile(psPath, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                            "main", "ps_5_0", compileFlags, 0, &psBlob,
                            &errBlob);
    if (FAILED(hr)) {
        OutputShaderError(errBlob.Get());
        return false;
    }

    D3D12_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_BLEND_DESC blend = {};
    blend.AlphaToCoverageEnable = FALSE;
    blend.IndependentBlendEnable = FALSE;
    auto &rt = blend.RenderTarget[0];
    rt.BlendEnable = TRUE;
    rt.LogicOpEnable = FALSE;
    rt.SrcBlend = D3D12_BLEND_ONE;
    rt.DestBlend = D3D12_BLEND_ONE;
    rt.BlendOp = D3D12_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D12_BLEND_ONE;
    rt.DestBlendAlpha = D3D12_BLEND_ONE;
    rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rt.LogicOp = D3D12_LOGIC_OP_NOOP;
    rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_RASTERIZER_DESC rasterizer = {};
    rasterizer.FillMode = D3D12_FILL_MODE_SOLID;
    rasterizer.CullMode = D3D12_CULL_MODE_NONE;
    rasterizer.FrontCounterClockwise = FALSE;
    rasterizer.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    rasterizer.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    rasterizer.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    rasterizer.DepthClipEnable = TRUE;
    rasterizer.MultisampleEnable = FALSE;
    rasterizer.AntialiasedLineEnable = FALSE;
    rasterizer.ForcedSampleCount = 0;
    rasterizer.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    D3D12_DEPTH_STENCIL_DESC depth = {};
    depth.DepthEnable = TRUE;
    depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    depth.StencilEnable = FALSE;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSignature_.Get();
    psoDesc.VS = {vsBlob->GetBufferPointer(), vsBlob->GetBufferSize()};
    psoDesc.PS = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};
    psoDesc.BlendState = blend;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.RasterizerState = rasterizer;
    psoDesc.DepthStencilState = depth;
    psoDesc.InputLayout = {layout, _countof(layout)};
    psoDesc.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = rtvFormat;
    psoDesc.DSVFormat = dsvFormat;
    psoDesc.SampleDesc.Count = 1;

    hr = device->CreateGraphicsPipelineState(&psoDesc,
                                             IID_PPV_ARGS(&pipelineState_));
    return SUCCEEDED(hr);
}

bool MagnetismicRenderer::CreateQuadResources(ID3D12Device *device) {
    const UINT bufferSize = sizeof(MagnetVertex) * 4;

    D3D12_HEAP_PROPERTIES heapProp = {};
    heapProp.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC resDesc = {};
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resDesc.Width = bufferSize;
    resDesc.Height = 1;
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels = 1;
    resDesc.SampleDesc.Count = 1;
    resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    HRESULT hr = device->CreateCommittedResource(
        &heapProp, D3D12_HEAP_FLAG_NONE, &resDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&vertexBuffer_));
    if (FAILED(hr)) {
        return false;
    }

    hr = vertexBuffer_->Map(0, nullptr, reinterpret_cast<void **>(&mappedVB_));
    if (FAILED(hr)) {
        return false;
    }

    vbView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
    vbView_.SizeInBytes = bufferSize;
    vbView_.StrideInBytes = sizeof(MagnetVertex);

    return true;
}

bool MagnetismicRenderer::CreateConstantBuffers(ID3D12Device *device) {
    auto createCB = [&](UINT size, ID3D12Resource **outRes,
                        void **outMap) -> bool {
        size = (size + 255) & ~255u;

        D3D12_HEAP_PROPERTIES heapProp = {};
        heapProp.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC resDesc = {};
        resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resDesc.Width = size;
        resDesc.Height = 1;
        resDesc.DepthOrArraySize = 1;
        resDesc.MipLevels = 1;
        resDesc.SampleDesc.Count = 1;
        resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        HRESULT hr = device->CreateCommittedResource(
            &heapProp, D3D12_HEAP_FLAG_NONE, &resDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(outRes));
        if (FAILED(hr)) {
            return false;
        }

        hr = (*outRes)->Map(0, nullptr, outMap);
        return SUCCEEDED(hr);
    };

    if (!createCB(sizeof(MagnetVSConstants),
                  vsConstantBuffer_.ReleaseAndGetAddressOf(),
                  reinterpret_cast<void **>(&mappedVS_))) {
        return false;
    }

    if (!createCB(sizeof(MagnetPSConstants),
                  psConstantBuffer_.ReleaseAndGetAddressOf(),
                  reinterpret_cast<void **>(&mappedPS_))) {
        return false;
    }

    std::memset(mappedVS_, 0, sizeof(MagnetVSConstants));
    std::memset(mappedPS_, 0, sizeof(MagnetPSConstants));

    return true;
}

void MagnetismicRenderer::UpdateQuadVertices(const MagnetismicInstance &inst,
                                             const MagnetVec3 &cameraRight,
                                             const MagnetVec3 &cameraUp) {
    MagnetVec3 right = Mul(Normalize(cameraRight), inst.size);
    MagnetVec3 up = Mul(Normalize(cameraUp), inst.size);

    MagnetVec3 p0 = Add(Add(inst.position, Mul(right, -1.0f)), Mul(up, 1.0f));
    MagnetVec3 p1 = Add(Add(inst.position, Mul(right, 1.0f)), Mul(up, 1.0f));
    MagnetVec3 p2 = Add(Add(inst.position, Mul(right, -1.0f)), Mul(up, -1.0f));
    MagnetVec3 p3 = Add(Add(inst.position, Mul(right, 1.0f)), Mul(up, -1.0f));

    SetVertex(mappedVB_[0], p0, 0.0f, 0.0f);
    SetVertex(mappedVB_[1], p1, 1.0f, 0.0f);
    SetVertex(mappedVB_[2], p2, 0.0f, 1.0f);
    SetVertex(mappedVB_[3], p3, 1.0f, 1.0f);
}

MagnetMatrix4x4 MagnetismicRenderer::MakeIdentity() {
    MagnetMatrix4x4 m = {};
    m.m[0][0] = 1.0f;
    m.m[1][1] = 1.0f;
    m.m[2][2] = 1.0f;
    m.m[3][3] = 1.0f;
    return m;
}