#pragma once
#include "render_pass.h"
#include "../d3d_helpers.h"
#include "../camera.h"
#include <d3dcompiler.h>
#include <string>

class ScenePass : public RenderPass
{
public:
    void Initialize(D3DContext& ctx, const std::wstring& shaderPath);
    void UpdateConstants(const Camera& camera, UINT width, UINT height);

    void Execute(D3DContext& ctx, ID3D12GraphicsCommandList* cmd) override;

    void ExecuteToTarget(ID3D12GraphicsCommandList* cmd,
                         D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle, UINT w, UINT h);

    // Render the cube to an explicit render target + depth target (used by capture).
    void DrawTo(ID3D12GraphicsCommandList* cmd,
                D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
                D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle, UINT w, UINT h);

    // MRT variant: RT0 = color, RT1 = motion vectors (R16G16F, NDC curr-prev).
    void DrawToMRT(ID3D12GraphicsCommandList* cmd,
                   D3D12_CPU_DESCRIPTOR_HANDLE rtvColor,
                   D3D12_CPU_DESCRIPTOR_HANDLE rtvMotion,
                   D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle, UINT w, UINT h);

    void SetDepthStencilView(D3D12_CPU_DESCRIPTOR_HANDLE dsv) { m_dsvHandle = dsv; }

private:
    void DrawScene(ID3D12GraphicsCommandList* cmd,
                   D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
                   D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle, UINT w, UINT h);

    ComPtr<ID3D12RootSignature> m_rootSig;
    ComPtr<ID3D12PipelineState> m_pso;
    ComPtr<ID3D12PipelineState> m_mvPso;
    ComPtr<ID3D12Resource>      m_vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW    m_vbv = {};
    ComPtr<ID3D12Resource>      m_indexBuffer;
    D3D12_INDEX_BUFFER_VIEW     m_ibv = {};
    ComPtr<ID3D12Resource>      m_constantBuffer;
    UINT8*                      m_cbDataBegin = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE m_dsvHandle   = {};
    DirectX::XMFLOAT4X4         m_prevMVP     = {};
    bool                        m_hasPrevMVP  = false;
};
