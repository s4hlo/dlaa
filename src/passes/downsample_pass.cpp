#include "downsample_pass.h"
#include "../d3d_context.h"

void DownsamplePass::Initialize(D3DContext& ctx, const std::wstring& shaderPath)
{
    // Offscreen RT
    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type                  = D3D12_HEAP_TYPE_DEFAULT;
    defaultHeap.CPUPageProperty       = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    defaultHeap.MemoryPoolPreference  = D3D12_MEMORY_POOL_UNKNOWN;
    defaultHeap.CreationNodeMask      = 1;
    defaultHeap.VisibleNodeMask       = 1;

    D3D12_RESOURCE_DESC rtDesc = {};
    rtDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rtDesc.Width            = SSWidth;
    rtDesc.Height           = SSHeight;
    rtDesc.DepthOrArraySize = 1;
    rtDesc.MipLevels        = 1;
    rtDesc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
    rtDesc.SampleDesc.Count = 1;
    rtDesc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rtDesc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clearVal = {};
    clearVal.Format   = DXGI_FORMAT_R8G8B8A8_UNORM;
    clearVal.Color[0] = kClearColor[0]; clearVal.Color[1] = kClearColor[1];
    clearVal.Color[2] = kClearColor[2]; clearVal.Color[3] = kClearColor[3];

    ThrowIfFailed(ctx.device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE,
        &rtDesc, D3D12_RESOURCE_STATE_RENDER_TARGET, &clearVal,
        IID_PPV_ARGS(&m_offscreenRT)));

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = 1;
    rtvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(ctx.device->CreateDescriptorHeap(&rtvHeapDesc,
        IID_PPV_ARGS(&m_offscreenRTVHeap)));
    ctx.device->CreateRenderTargetView(m_offscreenRT.Get(), nullptr,
        m_offscreenRTVHeap->GetCPUDescriptorHandleForHeapStart());

    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = 1;
    srvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(ctx.device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_srvHeap)));

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels     = 1;
    ctx.device->CreateShaderResourceView(m_offscreenRT.Get(), &srvDesc,
        m_srvHeap->GetCPUDescriptorHandleForHeapStart());

    // Root signature: one SRV descriptor table + linear clamp sampler
    D3D12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors     = 1;
    srvRange.BaseShaderRegister = 0;
    srvRange.RegisterSpace      = 0;
    srvRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER rootParam = {};
    rootParam.ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParam.DescriptorTable.NumDescriptorRanges = 1;
    rootParam.DescriptorTable.pDescriptorRanges   = &srvRange;
    rootParam.ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ShaderRegister   = 0;
    sampler.RegisterSpace    = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters     = 1;
    rsDesc.pParameters       = &rootParam;
    rsDesc.NumStaticSamplers = 1;
    rsDesc.pStaticSamplers   = &sampler;
    rsDesc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> sig, err;
    ThrowIfFailed(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err));
    ThrowIfFailed(ctx.device->CreateRootSignature(0,
        sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&m_rootSig)));

    // Shaders
    auto vs = CompileShader(shaderPath, "QuadVS", "vs_5_0");
    auto ps = CompileShader(shaderPath, "QuadPS", "ps_5_0");

    // PSO
    D3D12_RASTERIZER_DESC rasterDesc = {};
    rasterDesc.FillMode        = D3D12_FILL_MODE_SOLID;
    rasterDesc.CullMode        = D3D12_CULL_MODE_NONE;
    rasterDesc.DepthClipEnable = TRUE;

    D3D12_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable           = FALSE;
    blendDesc.RenderTarget[0].LogicOpEnable         = FALSE;
    blendDesc.RenderTarget[0].SrcBlend              = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlend             = D3D12_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOp               = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha         = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha        = D3D12_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha          = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].LogicOp               = D3D12_LOGIC_OP_NOOP;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable   = FALSE;
    dsDesc.StencilEnable = FALSE;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout           = { nullptr, 0 };
    psoDesc.pRootSignature        = m_rootSig.Get();
    psoDesc.VS                    = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS                    = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psoDesc.RasterizerState       = rasterDesc;
    psoDesc.BlendState            = blendDesc;
    psoDesc.DepthStencilState     = dsDesc;
    psoDesc.SampleMask            = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets      = 1;
    psoDesc.RTVFormats[0]         = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count      = 1;
    ThrowIfFailed(ctx.device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pso)));
}

D3D12_CPU_DESCRIPTOR_HANDLE DownsamplePass::GetOffscreenRTV() const
{
    return m_offscreenRTVHeap->GetCPUDescriptorHandleForHeapStart();
}

void DownsamplePass::Execute(D3DContext& ctx, ID3D12GraphicsCommandList* cmd)
{
    // offscreen RT: RENDER_TARGET → SRV; swapchain: PRESENT → RENDER_TARGET
    D3D12_RESOURCE_BARRIER midBarriers[2] = {
        TransitionBarrier(m_offscreenRT.Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
        TransitionBarrier(ctx.renderTargets[ctx.frameIndex].Get(),
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_RENDER_TARGET),
    };
    cmd->ResourceBarrier(2, midBarriers);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
        ctx.rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtvHandle.ptr += ctx.frameIndex * ctx.rtvDescriptorSize;
    cmd->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    D3D12_VIEWPORT vp = {};
    vp.Width    = static_cast<float>(ctx.width);
    vp.Height   = static_cast<float>(ctx.height);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    cmd->RSSetViewports(1, &vp);

    D3D12_RECT scissor = { 0, 0,
        static_cast<LONG>(ctx.width), static_cast<LONG>(ctx.height) };
    cmd->RSSetScissorRects(1, &scissor);

    ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetPipelineState(m_pso.Get());
    cmd->SetGraphicsRootSignature(m_rootSig.Get());
    cmd->SetGraphicsRootDescriptorTable(0,
        m_srvHeap->GetGPUDescriptorHandleForHeapStart());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->DrawInstanced(3, 1, 0, 0);

    // swapchain: RENDER_TARGET → PRESENT; offscreen RT: SRV → RENDER_TARGET (next frame)
    D3D12_RESOURCE_BARRIER endBarriers[2] = {
        TransitionBarrier(ctx.renderTargets[ctx.frameIndex].Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PRESENT),
        TransitionBarrier(m_offscreenRT.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET),
    };
    cmd->ResourceBarrier(2, endBarriers);
}
