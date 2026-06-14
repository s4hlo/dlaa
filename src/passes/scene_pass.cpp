#include "scene_pass.h"
#include "../d3d_context.h"

using namespace DirectX;

namespace
{
    float Halton(uint32_t index, uint32_t base)
    {
        float f = 1.0f, r = 0.0f;
        while (index > 0) { f /= base; r += f * (index % base); index /= base; }
        return r;
    }
}

void ScenePass::Initialize(D3DContext& ctx, const std::wstring& shaderPath)
{
    D3D12_ROOT_PARAMETER rootParams[1] = {};
    rootParams[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[0].Descriptor.ShaderRegister = 0;
    rootParams[0].Descriptor.RegisterSpace  = 0;
    rootParams[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters = _countof(rootParams);
    rsDesc.pParameters   = rootParams;
    rsDesc.Flags         = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> sig, err;
    ThrowIfFailed(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err));
    ThrowIfFailed(ctx.device->CreateRootSignature(0,
        sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&m_rootSig)));

    auto vs   = CompileShader(shaderPath, "VSMain",   "vs_5_0");
    auto ps   = CompileShader(shaderPath, "PSMain",   "ps_5_0");
    auto mvvs = CompileShader(shaderPath, "MVVSMain", "vs_5_0");
    auto mvps = CompileShader(shaderPath, "MVPSMain", "ps_5_0");

    D3D12_INPUT_ELEMENT_DESC inputDescs[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_RASTERIZER_DESC rasterDesc = {};
    rasterDesc.FillMode              = D3D12_FILL_MODE_SOLID;
    rasterDesc.CullMode              = D3D12_CULL_MODE_BACK;
    rasterDesc.FrontCounterClockwise = FALSE;
    rasterDesc.DepthBias             = D3D12_DEFAULT_DEPTH_BIAS;
    rasterDesc.DepthBiasClamp        = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    rasterDesc.SlopeScaledDepthBias  = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    rasterDesc.DepthClipEnable       = TRUE;

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
    dsDesc.DepthEnable    = TRUE;
    dsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    dsDesc.DepthFunc      = D3D12_COMPARISON_FUNC_LESS;
    dsDesc.StencilEnable  = FALSE;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout           = { inputDescs, _countof(inputDescs) };
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
    psoDesc.DSVFormat             = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc.Count      = 1;
    ThrowIfFailed(ctx.device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pso)));

    // MRT PSO: same state, different VS/PS and a second RT for motion vectors.
    psoDesc.VS                = { mvvs->GetBufferPointer(), mvvs->GetBufferSize() };
    psoDesc.PS                = { mvps->GetBufferPointer(), mvps->GetBufferSize() };
    psoDesc.NumRenderTargets  = 2;
    psoDesc.RTVFormats[1]     = DXGI_FORMAT_R16G16_FLOAT;
    ThrowIfFailed(ctx.device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_mvPso)));

    Vertex vertices[] = {
        {{-1.0f, -1.0f, -1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
        {{-1.0f, +1.0f, -1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
        {{+1.0f, +1.0f, -1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
        {{+1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 0.0f, 1.0f}},
        {{-1.0f, -1.0f, +1.0f}, {1.0f, 0.0f, 1.0f, 1.0f}},
        {{-1.0f, +1.0f, +1.0f}, {0.0f, 1.0f, 1.0f, 1.0f}},
        {{+1.0f, +1.0f, +1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
        {{+1.0f, -1.0f, +1.0f}, {0.5f, 0.5f, 0.5f, 1.0f}},
    };
    const UINT vbSize = sizeof(vertices);

    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type                 = D3D12_HEAP_TYPE_UPLOAD;
    uploadHeap.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    uploadHeap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    uploadHeap.CreationNodeMask     = 1;
    uploadHeap.VisibleNodeMask      = 1;

    auto MakeBufferDesc = [](UINT size) {
        D3D12_RESOURCE_DESC d = {};
        d.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        d.Width            = size;
        d.Height           = 1;
        d.DepthOrArraySize = 1;
        d.MipLevels        = 1;
        d.Format           = DXGI_FORMAT_UNKNOWN;
        d.SampleDesc.Count = 1;
        d.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        return d;
    };

    D3D12_RANGE noRead = { 0, 0 };
    void* pData        = nullptr;

    auto vbDesc = MakeBufferDesc(vbSize);
    ThrowIfFailed(ctx.device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE,
        &vbDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_vertexBuffer)));
    ThrowIfFailed(m_vertexBuffer->Map(0, &noRead, &pData));
    memcpy(pData, vertices, vbSize);
    m_vertexBuffer->Unmap(0, nullptr);

    m_vbv.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vbv.StrideInBytes  = sizeof(Vertex);
    m_vbv.SizeInBytes    = vbSize;

    uint16_t indices[] = {
        0, 1, 2,  0, 2, 3,
        4, 6, 5,  4, 7, 6,
        4, 5, 1,  4, 1, 0,
        3, 2, 6,  3, 6, 7,
        1, 5, 6,  1, 6, 2,
        4, 0, 3,  4, 3, 7,
    };
    const UINT ibSize = sizeof(indices);
    auto ibDesc = MakeBufferDesc(ibSize);
    ThrowIfFailed(ctx.device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE,
        &ibDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_indexBuffer)));
    ThrowIfFailed(m_indexBuffer->Map(0, &noRead, &pData));
    memcpy(pData, indices, ibSize);
    m_indexBuffer->Unmap(0, nullptr);

    m_ibv.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
    m_ibv.Format         = DXGI_FORMAT_R16_UINT;
    m_ibv.SizeInBytes    = ibSize;

    const UINT cbSize = (sizeof(ConstantBufferData) + 255) & ~255;
    auto cbDesc = MakeBufferDesc(cbSize);
    ThrowIfFailed(ctx.device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE,
        &cbDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_constantBuffer)));
    ThrowIfFailed(m_constantBuffer->Map(0, &noRead,
        reinterpret_cast<void**>(&m_cbDataBegin)));
    memset(m_cbDataBegin, 0, cbSize);
}

void ScenePass::UpdateConstants(const Camera& camera, UINT width, UINT height)
{
    const XMMATRIX view = camera.GetViewMatrix();
    const XMMATRIX proj = XMMatrixPerspectiveFovLH(
        XM_PIDIV4,
        static_cast<float>(width) / static_cast<float>(height),
        0.1f, 100.0f);

    // Halton(2,3) subpixel jitter, scaled by m_jitterScale (1.0 = [-0.5, 0.5] pixels).
    const uint32_t seq   = (m_frameCount % 16) + 1; // skip index 0 (returns 0,0)
    const float    jx_px = (Halton(seq, 2) - 0.5f) * m_jitterScale;
    const float    jy_px = (Halton(seq, 3) - 0.5f) * m_jitterScale;

    // Convert pixel offset to NDC offset and bake into projection.
    XMFLOAT4X4 projF;
    XMStoreFloat4x4(&projF, proj);
    projF._31 += jx_px * 2.0f / static_cast<float>(width);
    projF._32 += jy_px * 2.0f / static_cast<float>(height);
    const XMMATRIX projJittered = XMLoadFloat4x4(&projF);

    const XMMATRIX mvpUnjittered = XMMatrixTranspose(view * proj);
    const XMMATRIX mvpJittered   = XMMatrixTranspose(view * projJittered);

    ConstantBufferData cb;
    XMStoreFloat4x4(&cb.mvp,           mvpJittered);
    XMStoreFloat4x4(&cb.mvpUnjittered, mvpUnjittered);
    cb.mvpPrev = m_hasPrevMVP ? m_prevMVP : cb.mvpUnjittered;
    memcpy(m_cbDataBegin, &cb, sizeof(cb));

    m_jitterPrev = m_jitterCurr;
    m_jitterCurr = { jx_px, jy_px };
    XMStoreFloat4x4(&m_prevMVP, mvpUnjittered);
    m_hasPrevMVP = true;
    ++m_frameCount;
}

void ScenePass::DrawScene(ID3D12GraphicsCommandList* cmd,
                          D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
                          D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle, UINT w, UINT h)
{
    D3D12_VIEWPORT vp = {};
    vp.Width    = static_cast<float>(w);
    vp.Height   = static_cast<float>(h);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    cmd->RSSetViewports(1, &vp);

    D3D12_RECT scissor = { 0, 0, static_cast<LONG>(w), static_cast<LONG>(h) };
    cmd->RSSetScissorRects(1, &scissor);

    cmd->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
    cmd->ClearRenderTargetView(rtvHandle, kClearColor, 0, nullptr);
    cmd->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    cmd->SetPipelineState(m_pso.Get());
    cmd->SetGraphicsRootSignature(m_rootSig.Get());
    cmd->SetGraphicsRootConstantBufferView(0, m_constantBuffer->GetGPUVirtualAddress());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->IASetVertexBuffers(0, 1, &m_vbv);
    cmd->IASetIndexBuffer(&m_ibv);
    cmd->DrawIndexedInstanced(36, 1, 0, 0, 0);
}

void ScenePass::Execute(D3DContext& ctx, ID3D12GraphicsCommandList* cmd)
{
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
        ctx.rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtvHandle.ptr += ctx.frameIndex * ctx.rtvDescriptorSize;

    auto toRender = TransitionBarrier(ctx.renderTargets[ctx.frameIndex].Get(),
        D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmd->ResourceBarrier(1, &toRender);

    DrawScene(cmd, rtvHandle, m_dsvHandle, ctx.width, ctx.height);

    auto toPresent = TransitionBarrier(ctx.renderTargets[ctx.frameIndex].Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    cmd->ResourceBarrier(1, &toPresent);
}

void ScenePass::ExecuteToTarget(ID3D12GraphicsCommandList* cmd,
                                D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle, UINT w, UINT h)
{
    DrawScene(cmd, rtvHandle, m_dsvHandle, w, h);
}

void ScenePass::DrawTo(ID3D12GraphicsCommandList* cmd,
                       D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
                       D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle, UINT w, UINT h)
{
    DrawScene(cmd, rtvHandle, dsvHandle, w, h);
}

void ScenePass::DrawToMRT(ID3D12GraphicsCommandList* cmd,
                          D3D12_CPU_DESCRIPTOR_HANDLE rtvColor,
                          D3D12_CPU_DESCRIPTOR_HANDLE rtvMotion,
                          D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle, UINT w, UINT h)
{
    D3D12_VIEWPORT vp = {};
    vp.Width    = static_cast<float>(w);
    vp.Height   = static_cast<float>(h);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    cmd->RSSetViewports(1, &vp);

    D3D12_RECT scissor = { 0, 0, static_cast<LONG>(w), static_cast<LONG>(h) };
    cmd->RSSetScissorRects(1, &scissor);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2] = { rtvColor, rtvMotion };
    cmd->OMSetRenderTargets(2, rtvs, FALSE, &dsvHandle);
    cmd->ClearRenderTargetView(rtvColor, kClearColor, 0, nullptr);
    const float motionClear[4] = { 0.f, 0.f, 0.f, 0.f };
    cmd->ClearRenderTargetView(rtvMotion, motionClear, 0, nullptr);
    cmd->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    cmd->SetPipelineState(m_mvPso.Get());
    cmd->SetGraphicsRootSignature(m_rootSig.Get());
    cmd->SetGraphicsRootConstantBufferView(0, m_constantBuffer->GetGPUVirtualAddress());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->IASetVertexBuffers(0, 1, &m_vbv);
    cmd->IASetIndexBuffer(&m_ibv);
    cmd->DrawIndexedInstanced(36, 1, 0, 0, 0);
}
