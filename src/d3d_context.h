#pragma once
#include "d3d_helpers.h"
#include <dxgi1_4.h>

struct D3DContext
{
    ComPtr<ID3D12Device>              device;
    ComPtr<ID3D12CommandQueue>        commandQueue;
    ComPtr<IDXGISwapChain3>           swapChain;
    ComPtr<ID3D12GraphicsCommandList> commandList;
    ComPtr<ID3D12CommandAllocator>    commandAllocators[FrameCount];
    ComPtr<ID3D12DescriptorHeap>      rtvHeap;
    ComPtr<ID3D12Resource>            renderTargets[FrameCount];
    ComPtr<ID3D12Fence>               fence;
    HANDLE                            fenceEvent        = nullptr;
    UINT64                            fenceValue        = 1;
    UINT                              frameIndex        = 0;
    UINT                              rtvDescriptorSize = 0;
    UINT                              width             = 0;
    UINT                              height            = 0;

    D3DContext() = default;
    ~D3DContext() { if (fenceEvent) CloseHandle(fenceEvent); }
    D3DContext(const D3DContext&)            = delete;
    D3DContext& operator=(const D3DContext&) = delete;

    void Initialize(HWND hwnd, UINT w, UINT h);
    void WaitForPreviousFrame();
};
