#include "frame_buffers.h"
#include "d3d_context.h"

void FrameBuffers::Initialize(D3DContext& ctx, UINT width, UINT height)
{
    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type                 = D3D12_HEAP_TYPE_DEFAULT;
    defaultHeap.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    defaultHeap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    defaultHeap.CreationNodeMask     = 1;
    defaultHeap.VisibleNodeMask      = 1;

    // R32_TYPELESS so dataset capture can later view this as an R32_FLOAT SRV
    // without recreating the resource; the DSV below must therefore be explicit.
    D3D12_RESOURCE_DESC depthDesc = {};
    depthDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Width            = width;
    depthDesc.Height           = height;
    depthDesc.DepthOrArraySize = 1;
    depthDesc.MipLevels        = 1;
    depthDesc.Format           = DXGI_FORMAT_R32_TYPELESS;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    depthDesc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearVal = {};
    clearVal.Format               = DXGI_FORMAT_D32_FLOAT;
    clearVal.DepthStencil.Depth   = 1.0f;
    clearVal.DepthStencil.Stencil = 0;

    ThrowIfFailed(ctx.device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE,
        &depthDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearVal,
        IID_PPV_ARGS(&m_depthBuffer)));

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(ctx.device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap)));

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format        = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Flags         = D3D12_DSV_FLAG_NONE;
    ctx.device->CreateDepthStencilView(m_depthBuffer.Get(), &dsvDesc,
        m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
}

D3D12_CPU_DESCRIPTOR_HANDLE FrameBuffers::GetDSV() const
{
    return m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
}
