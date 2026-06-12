#pragma once
#include "d3d_helpers.h"

struct D3DContext;

// Owns render-resolution per-frame buffers used by the passes and, later,
// by dataset capture. Currently: depth only. Future: normals, motion vectors.
class FrameBuffers
{
public:
    void Initialize(D3DContext& ctx, UINT width, UINT height);

    D3D12_CPU_DESCRIPTOR_HANDLE GetDSV() const;
    ID3D12Resource*             GetDepthResource() const { return m_depthBuffer.Get(); }

private:
    ComPtr<ID3D12Resource>       m_depthBuffer;
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
};
