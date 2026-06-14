#include "renderer.h"

void Renderer::Initialize(HWND hwnd, UINT width, UINT height)
{
    m_hwnd = hwnd;
    m_ctx.Initialize(hwnd, width, height);

    const std::wstring shaderPath = GetShaderPath();
    m_scenePass.Initialize(m_ctx, shaderPath);

    m_downsamplePass.Initialize(m_ctx, shaderPath);
    m_frameBuffers.Initialize(m_ctx, DownsamplePass::SSWidth, DownsamplePass::SSHeight);
    m_scenePass.SetDepthStencilView(m_frameBuffers.GetDSV());
    m_frameCapture.Initialize(m_ctx);
}

void Renderer::Update()
{
    m_camera.Update(m_hwnd);
    m_scenePass.UpdateConstants(m_camera,
        DownsamplePass::SSWidth, DownsamplePass::SSHeight);
}

void Renderer::Render()
{
    ThrowIfFailed(m_ctx.commandAllocators[m_ctx.frameIndex]->Reset());
    ThrowIfFailed(m_ctx.commandList->Reset(
        m_ctx.commandAllocators[m_ctx.frameIndex].Get(), nullptr));

    ID3D12GraphicsCommandList* cmd = m_ctx.commandList.Get();

    m_scenePass.ExecuteToTarget(cmd,
        m_downsamplePass.GetOffscreenRTV(),
        DownsamplePass::SSWidth, DownsamplePass::SSHeight);
    m_downsamplePass.Execute(m_ctx, cmd);

    const bool doCapture = m_frameCapture.IsPending();
    if (doCapture)
        m_frameCapture.RecordCapture(m_ctx, cmd, m_scenePass);

    ThrowIfFailed(cmd->Close());

    ID3D12CommandList* lists[] = { cmd };
    m_ctx.commandQueue->ExecuteCommandLists(1, lists);
    ThrowIfFailed(m_ctx.swapChain->Present(1, 0));
    m_ctx.WaitForPreviousFrame();

    if (doCapture) {
        m_frameCapture.SetJitter(m_scenePass.GetJitterCurr(), m_scenePass.GetJitterPrev());
        m_frameCapture.WriteToDisk();
    }
}

std::wstring Renderer::GetShaderPath() const
{
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    std::filesystem::path p = buffer;
    p = p.remove_filename() / L"shaders.hlsl";
    return p.native();
}
