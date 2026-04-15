#pragma once
#include "DxUtils.h"
#include <d3dcompiler.h>
#include <string>
#include <wrl.h>

namespace ShaderCompiler {

Microsoft::WRL::ComPtr<ID3DBlob> Compile(const std::wstring &path,
                                         const std::string &entry,
                                         const std::string &target);

} // namespace ShaderCompiler
