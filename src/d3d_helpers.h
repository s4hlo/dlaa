#pragma once
#include <windows.h>
#include <wrl.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <stdexcept>
#include <string>

using Microsoft::WRL::ComPtr;

constexpr UINT FrameCount = 2;

constexpr float kClearColor[4] = { 0.1f, 0.15f, 0.25f, 1.0f };

struct Vertex
{
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT4 color;
};

#pragma warning(push)
#pragma warning(disable: 4324)
struct alignas(256) ConstantBufferData
{
    DirectX::XMFLOAT4X4 mvp;
};
#pragma warning(pop)

inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr))
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "HRESULT 0x%08X", static_cast<unsigned>(hr));
        throw std::runtime_error(buf);
    }
}

inline ComPtr<ID3DBlob> CompileShader(const std::wstring& path, const char* entry, const char* target)
{
    ComPtr<ID3DBlob> blob, errBlob;
    HRESULT hr = D3DCompileFromFile(path.c_str(), nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE, entry, target, 0, 0, &blob, &errBlob);
    if (FAILED(hr))
    {
        std::string msg = errBlob
            ? static_cast<const char*>(errBlob->GetBufferPointer())
            : "unknown error";
        throw std::runtime_error("Shader compile error [" + std::string(entry) + "]: " + msg);
    }
    return blob;
}

inline D3D12_RESOURCE_BARRIER TransitionBarrier(
    ID3D12Resource*       resource,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource   = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter  = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    return barrier;
}
