#pragma once
#include <d3d12.h>

struct D3DContext;

struct RenderPass
{
    virtual void Execute(D3DContext& ctx, ID3D12GraphicsCommandList* cmd) = 0;
    virtual ~RenderPass() = default;
};
