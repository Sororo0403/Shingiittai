#include "ShaderCompiler.h"

Microsoft::WRL::ComPtr<ID3DBlob>
ShaderCompiler::Compile(const std::wstring &path, const std::string &entry,
                        const std::string &target) {
    Microsoft::WRL::ComPtr<ID3DBlob> shader;
    Microsoft::WRL::ComPtr<ID3DBlob> error;

    HRESULT hr = D3DCompileFromFile(
        path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, entry.c_str(),
        target.c_str(), D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION, 0,
        &shader, &error);

    if (FAILED(hr)) {
        if (error) {
            OutputDebugStringA(
                static_cast<const char *>(error->GetBufferPointer()));
        }
        DxUtils::ThrowIfFailed(hr, "Shader compile failed");
    }

    return shader;
}
