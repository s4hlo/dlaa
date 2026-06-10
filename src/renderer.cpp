#include "renderer.h"

void Renderer::Initialize(HWND hwnd, UINT width, UINT height)
{
    m_hwnd = hwnd;
    m_ctx.Initialize(hwnd, width, height);

    const std::wstring shaderPath = GetShaderPath();
    m_scenePass.Initialize(m_ctx, shaderPath);

#ifdef ENABLE_SUPERSAMPLING
    m_downsamplePass = std::make_unique<DownsamplePass>();
    m_downsamplePass->Initialize(m_ctx, shaderPath);
#endif
}

void Renderer::Update()
{
    m_camera.Update(m_hwnd);
    const UINT renderW = m_downsamplePass ? DownsamplePass::SSWidth  : m_ctx.width;
    const UINT renderH = m_downsamplePass ? DownsamplePass::SSHeight : m_ctx.height;
    m_scenePass.UpdateConstants(m_camera, renderW, renderH);
}

void Renderer::Render()
{
    ThrowIfFailed(m_ctx.commandAllocators[m_ctx.frameIndex]->Reset());
    ThrowIfFailed(m_ctx.commandList->Reset(
        m_ctx.commandAllocators[m_ctx.frameIndex].Get(), nullptr));

    ID3D12GraphicsCommandList* cmd = m_ctx.commandList.Get();

    if (m_downsamplePass)
    {
        m_scenePass.ExecuteToTarget(cmd,
            m_downsamplePass->GetOffscreenRTV(),
            DownsamplePass::SSWidth, DownsamplePass::SSHeight);
        m_downsamplePass->Execute(m_ctx, cmd);
    }
    else
    {
        m_scenePass.Execute(m_ctx, cmd);
    }

    ThrowIfFailed(cmd->Close());

    ID3D12CommandList* lists[] = { cmd };
    m_ctx.commandQueue->ExecuteCommandLists(1, lists);
    ThrowIfFailed(m_ctx.swapChain->Present(1, 0));
    m_ctx.WaitForPreviousFrame();
}

std::wstring Renderer::GetShaderPath() const
{
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    std::filesystem::path p = buffer;
    p = p.remove_filename() / L"shaders.hlsl";
    return p.native();
}
