#pragma once
#include <Windows.h>
#include <stdexcept>

namespace DxUtils {

/// <summary>
/// HRESULTを検証し、失敗していれば例外を送出する
/// </summary>
/// <param name="hr">判定対象となるHRESULT</param>
/// <param name="msg">失敗時に例外として投げるエラーメッセージ</param>
inline void ThrowIfFailed(HRESULT hr, const char *msg) {
    if (FAILED(hr)) {
        char buf[256];
        sprintf_s(buf, "%s (HRESULT=0x%08X)\n", msg, hr);
        OutputDebugStringA(buf);
        throw std::runtime_error(buf);
    }
}

/// <summary>
/// 256byteアラインを行う
/// </summary>
/// <param name="size">256byteアラインを行うサイズ</param>
/// <returns>256byteアライン後のサイズ</returns>
inline UINT Align256(UINT size) { return (size + 0xFF) & ~0xFF; }

} // namespace DxUtils