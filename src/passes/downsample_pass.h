#pragma once
#include "render_pass.h"
#include "../d3d_helpers.h"
#include <string>

class DownsamplePass : public RenderPass
{
public:
    static constexpr UINT SSWidth  = 1600;
    static constexpr UINT SSHeight = 900;

    void Initialize(D3DContext& ctx, const std::wstring& shaderPath);

    D3D12_CPU_DESCRIPTOR_HANDLE GetOffscreenRTV() const;
    ID3D12Resource*             GetOffscreenRT()  const { return m_offscreenRT.Get(); }

    void Execute(D3DContext& ctx, ID3D12GraphicsCommandList* cmd) override;

private:
    ComPtr<ID3D12Resource>       m_offscreenRT;
    ComPtr<ID3D12DescriptorHeap> m_offscreenRTVHeap;
    ComPtr<ID3D12DescriptorHeap> m_srvHeap;
    ComPtr<ID3D12RootSignature>  m_rootSig;
    ComPtr<ID3D12PipelineState>  m_pso;
};
