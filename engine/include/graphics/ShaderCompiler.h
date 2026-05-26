#pragma once
#include "core/AssetManager.h"
#include "debug/DebugLog.h"
#include "graphics/DxUtils.h"
#include <dxcapi.h>
#include <Windows.h>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
#include <wrl.h>

namespace ShaderCompiler {

/// <summary>
/// シェーダーファイルの実在パスを探索する
/// </summary>
/// <param name="path">探索対象の相対または絶対パス</param>
/// <returns>解決済みのシェーダーパス</returns>
inline std::wstring ResolveShaderPath(const std::wstring &path) {
    return AssetManager::ResolvePath(std::filesystem::path(path)).wstring();
}

inline std::wstring Widen(const std::string &value) {
    return std::wstring(value.begin(), value.end());
}

inline std::wstring NormalizeShaderTarget(const std::string &target) {
    if (target.rfind("vs_", 0) == 0) {
        return L"vs_6_6";
    }
    if (target.rfind("ps_", 0) == 0) {
        return L"ps_6_6";
    }
    if (target.rfind("cs_", 0) == 0) {
        return L"cs_6_6";
    }
    throw std::runtime_error("Unsupported shader target: " + target);
}

inline std::string BlobToString(IDxcBlobUtf8 *blob) {
    if (!blob || blob->GetStringLength() == 0) {
        return {};
    }
    return std::string(blob->GetStringPointer(), blob->GetStringLength());
}

inline std::string NarrowAscii(const std::wstring &value) {
    std::string result;
    result.reserve(value.size());
    for (wchar_t ch : value) {
        result.push_back(static_cast<char>(ch));
    }
    return result;
}

/// <summary>
/// HLSLシェーダーをDXCでShader Model 6.6へコンパイルする
/// </summary>
inline Microsoft::WRL::ComPtr<IDxcBlob>
Compile(const std::wstring &path, const std::string &entry,
        const std::string &target) {
    using Microsoft::WRL::ComPtr;

    const std::wstring resolvedPath = ResolveShaderPath(path);
    const std::wstring wideEntry = Widen(entry);
    const std::wstring normalizedTarget = NormalizeShaderTarget(target);

    ComPtr<IDxcUtils> utils;
    ComPtr<IDxcCompiler3> compiler;
    ComPtr<IDxcIncludeHandler> includeHandler;
    DxUtils::ThrowIfFailed(
        DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils)),
        "DxcCreateInstance(DxcUtils) failed");
    DxUtils::ThrowIfFailed(
        DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler)),
        "DxcCreateInstance(DxcCompiler) failed");
    DxUtils::ThrowIfFailed(utils->CreateDefaultIncludeHandler(&includeHandler),
                           "CreateDefaultIncludeHandler failed");

    ComPtr<IDxcBlobEncoding> source;
    DxUtils::ThrowIfFailed(
        utils->LoadFile(resolvedPath.c_str(), nullptr, &source),
        "DXC LoadFile failed");

    DxcBuffer sourceBuffer{};
    sourceBuffer.Ptr = source->GetBufferPointer();
    sourceBuffer.Size = source->GetBufferSize();
    sourceBuffer.Encoding = DXC_CP_UTF8;

    std::vector<LPCWSTR> arguments = {
        resolvedPath.c_str(), L"-E", wideEntry.c_str(), L"-T",
        normalizedTarget.c_str(), L"-HV", L"2021", L"-all_resources_bound"};
#ifdef _DEBUG
    arguments.push_back(L"-Zi");
    arguments.push_back(L"-Qembed_debug");
    arguments.push_back(L"-Od");
#else
    arguments.push_back(L"-O3");
#endif

    ComPtr<IDxcResult> result;
    HRESULT hr = compiler->Compile(&sourceBuffer, arguments.data(),
                                   static_cast<UINT32>(arguments.size()),
                                   includeHandler.Get(), IID_PPV_ARGS(&result));
    DxUtils::ThrowIfFailed(hr, "DXC Compile invocation failed");

    ComPtr<IDxcBlobUtf8> errors;
    result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
    const std::string errorMessage = BlobToString(errors.Get());
    if (!errorMessage.empty()) {
        OutputDebugStringA(errorMessage.c_str());
#ifdef _DEBUG
        DebugLog::Get().Write(
            "Graphics", "ShaderCompiler", "compile_message", errorMessage,
            {{"path", std::filesystem::path(resolvedPath).generic_string()},
             {"entry", entry},
             {"target", NarrowAscii(normalizedTarget)}});
#endif
    }

    HRESULT status = S_OK;
    DxUtils::ThrowIfFailed(result->GetStatus(&status), "DXC GetStatus failed");
    if (FAILED(status)) {
        throw std::runtime_error(errorMessage.empty() ? "DXC shader compile failed"
                                                     : errorMessage);
    }

    ComPtr<IDxcBlob> shader;
    DxUtils::ThrowIfFailed(
        result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shader), nullptr),
        "DXC GetOutput object failed");
    return shader;
}

} // namespace ShaderCompiler
