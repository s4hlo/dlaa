#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wrl.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <filesystem>
#include <chrono>
#include <vector>
#include <stdexcept>
#include <string>
#include <cmath>
#include <algorithm>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

constexpr UINT FrameCount = 2;

struct Vertex
{
    XMFLOAT3 position;
    XMFLOAT4 color;
};

struct alignas(256) ConstantBufferData
{
    XMFLOAT4X4 mvp;
};

inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr))
    {
        throw std::runtime_error("HRESULT failed.");
    }
}

inline D3D12_RESOURCE_BARRIER TransitionBarrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    return barrier;
}

class D3D12App
{
public:
    D3D12App() = default;
    ~D3D12App() = default;

    bool Initialize(HWND hwnd, UINT width, UINT height)
    {
        m_width = width;
        m_height = height;
        m_hwnd = hwnd;
        LoadPipeline();
        LoadAssets();
        return true;
    }

    void EnableMouseCapture()
    {
        m_mouseCapture = true;
        ShowCursor(FALSE);
        RECT rect;
        GetClientRect(m_hwnd, &rect);
        POINT center = { (rect.right - rect.left) / 2, (rect.bottom - rect.top) / 2 };
        ClientToScreen(m_hwnd, &center);
        SetCursorPos(center.x, center.y);
    }

    void DisableMouseCapture()
    {
        m_mouseCapture = false;
        ShowCursor(TRUE);
    }

    void Update()
    {
        const auto now = std::chrono::steady_clock::now();
        const float deltaTime = std::chrono::duration<float>(now - m_lastTime).count();
        m_lastTime = now;

        if (m_mouseCapture)
        {
            RECT rect;
            GetClientRect(m_hwnd, &rect);
            POINT center = { (rect.right - rect.left) / 2, (rect.bottom - rect.top) / 2 };
            ClientToScreen(m_hwnd, &center);

            POINT pt;
            GetCursorPos(&pt);
            m_yaw   += static_cast<float>(pt.x - center.x) * m_mouseSensitivity;
            m_pitch -= static_cast<float>(pt.y - center.y) * m_mouseSensitivity;
            m_pitch  = std::clamp(m_pitch, -XM_PIDIV2 + 0.01f, XM_PIDIV2 - 0.01f);
            SetCursorPos(center.x, center.y);
        }

        const float cp = cosf(m_pitch), sp = sinf(m_pitch);
        const float cy = cosf(m_yaw),   sy = sinf(m_yaw);
        const XMVECTOR forward = XMVectorSet(cp * sy, sp, cp * cy, 0.0f);
        const XMVECTOR flatFwd = XMVector3Normalize(XMVectorSet(sy, 0.0f, cy, 0.0f));
        const XMVECTOR up      = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
        const XMVECTOR right   = XMVector3Cross(up, flatFwd);

        XMVECTOR move = XMVectorZero();
        if (m_moveForward)  move = XMVectorAdd(move, flatFwd);
        if (m_moveBackward) move = XMVectorSubtract(move, flatFwd);
        if (m_moveRight)    move = XMVectorAdd(move, right);
        if (m_moveLeft)     move = XMVectorSubtract(move, right);

        if (XMVector3LengthSq(move).m128_f32[0] > 0.0f)
        {
            move = XMVector3Normalize(move) * (m_cameraSpeed * deltaTime);
            m_cameraPosition = XMVectorAdd(m_cameraPosition, move);
        }

        const XMVECTOR target = XMVectorAdd(m_cameraPosition, forward);
        const XMMATRIX view   = XMMatrixLookAtLH(m_cameraPosition, target, up);
        const XMMATRIX proj   = XMMatrixPerspectiveFovLH(XM_PIDIV4, static_cast<float>(m_width) / static_cast<float>(m_height), 0.1f, 100.0f);
        const XMMATRIX world  = XMMatrixIdentity();
        const XMMATRIX mvp    = XMMatrixTranspose(world * view * proj);

        ConstantBufferData cbData;
        XMStoreFloat4x4(&cbData.mvp, mvp);
        memcpy(m_constantBufferDataBegin, &cbData, sizeof(cbData));
    }

    void Render()
    {
        PopulateCommandList();
        ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
        m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
        ThrowIfFailed(m_swapChain->Present(1, 0));
        WaitForPreviousFrame();
    }

    void OnKeyDown(WPARAM key)
    {
        switch (key)
        {
        case 'W': m_moveForward = true; break;
        case 'S': m_moveBackward = true; break;
        case 'A': m_moveLeft = true; break;
        case 'D': m_moveRight = true; break;
        default: break;
        }
    }

    void OnKeyUp(WPARAM key)
    {
        switch (key)
        {
        case 'W': m_moveForward = false; break;
        case 'S': m_moveBackward = false; break;
        case 'A': m_moveLeft = false; break;
        case 'D': m_moveRight = false; break;
        default: break;
        }
    }

private:
    void LoadPipeline()
    {
        UINT dxgiFactoryFlags = 0;
#if defined(_DEBUG)
        {
            ComPtr<ID3D12Debug> debugController;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
            {
                debugController->EnableDebugLayer();
                dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
            }
        }
#endif

        ComPtr<IDXGIFactory4> factory;
        ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

        ComPtr<IDXGIAdapter1> hardwareAdapter;
        for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != factory->EnumAdapters1(adapterIndex, &hardwareAdapter); ++adapterIndex)
        {
            DXGI_ADAPTER_DESC1 desc;
            hardwareAdapter->GetDesc1(&desc);
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
            if (SUCCEEDED(D3D12CreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr))) break;
        }

        ComPtr<ID3D12Device> device;
        ThrowIfFailed(D3D12CreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
        m_device = device;

        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));

        DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
        swapChainDesc.BufferCount = FrameCount;
        swapChainDesc.Width = m_width;
        swapChainDesc.Height = m_height;
        swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapChainDesc.SampleDesc.Count = 1;

        ComPtr<IDXGISwapChain1> swapChain;
        ThrowIfFailed(factory->CreateSwapChainForHwnd(
            m_commandQueue.Get(),
            m_hwnd,
            &swapChainDesc,
            nullptr,
            nullptr,
            &swapChain));

        ThrowIfFailed(factory->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER));
        ThrowIfFailed(swapChain.As(&m_swapChain));
        m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = FrameCount;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

        m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        for (UINT n = 0; n < FrameCount; ++n)
        {
            ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
            m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
            rtvHandle.ptr += m_rtvDescriptorSize;
        }

        for (UINT n = 0; n < FrameCount; ++n)
        {
            ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[n])));
        }

        ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
        m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (m_fenceEvent == nullptr)
        {
            ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
        }
    }

    void LoadAssets()
    {
        const std::wstring shaderPath = GetShaderPath();

        ComPtr<ID3DBlob> vertexShader;
        ComPtr<ID3DBlob> pixelShader;
        ComPtr<ID3DBlob> errorBlob;

        ThrowIfFailed(D3DCompileFromFile(shaderPath.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "VSMain", "vs_5_0", 0, 0, &vertexShader, &errorBlob));
        ThrowIfFailed(D3DCompileFromFile(shaderPath.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "PSMain", "ps_5_0", 0, 0, &pixelShader, &errorBlob));

        CreateRootSignature();

        D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
        {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        };

        D3D12_RASTERIZER_DESC rasterizerDesc = {};
        rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
        rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
        rasterizerDesc.FrontCounterClockwise = FALSE;
        rasterizerDesc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
        rasterizerDesc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
        rasterizerDesc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
        rasterizerDesc.DepthClipEnable = TRUE;
        rasterizerDesc.MultisampleEnable = FALSE;
        rasterizerDesc.AntialiasedLineEnable = FALSE;
        rasterizerDesc.ForcedSampleCount = 0;
        rasterizerDesc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

        D3D12_BLEND_DESC blendDesc = {};
        blendDesc.AlphaToCoverageEnable = FALSE;
        blendDesc.IndependentBlendEnable = FALSE;
        blendDesc.RenderTarget[0].BlendEnable = FALSE;
        blendDesc.RenderTarget[0].LogicOpEnable = FALSE;
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
        blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        D3D12_DEPTH_STENCIL_DESC depthStencilDesc = {};
        depthStencilDesc.DepthEnable = FALSE;
        depthStencilDesc.StencilEnable = FALSE;

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
        psoDesc.pRootSignature = m_rootSignature.Get();
        psoDesc.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
        psoDesc.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };
        psoDesc.RasterizerState = rasterizerDesc;
        psoDesc.BlendState = blendDesc;
        psoDesc.DepthStencilState = depthStencilDesc;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.SampleDesc.Count = 1;

        ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));

        ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[m_frameIndex].Get(), m_pipelineState.Get(), IID_PPV_ARGS(&m_commandList)));
        ThrowIfFailed(m_commandList->Close());

        CreateCubeBuffers();
        CreateConstantBuffer();
        CreateCBVHeap();

        m_lastTime = std::chrono::steady_clock::now();
        m_cameraPosition = XMVectorSet(0.0f, 0.0f, -6.0f, 0.0f);
    }

    void CreateRootSignature()
    {
        D3D12_ROOT_PARAMETER rootParameters[1] = {};
        rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        rootParameters[0].Descriptor.ShaderRegister = 0;
        rootParameters[0].Descriptor.RegisterSpace = 0;
        rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

        D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
        rootSignatureDesc.NumParameters = _countof(rootParameters);
        rootSignatureDesc.pParameters = rootParameters;
        rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;
        ThrowIfFailed(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
        ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
    }

    void CreateCubeBuffers()
    {
        Vertex vertices[] =
        {
            {{-1.0f, -1.0f, -1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
            {{-1.0f, +1.0f, -1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
            {{+1.0f, +1.0f, -1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
            {{+1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 0.0f, 1.0f}},
            {{-1.0f, -1.0f, +1.0f}, {1.0f, 0.0f, 1.0f, 1.0f}},
            {{-1.0f, +1.0f, +1.0f}, {0.0f, 1.0f, 1.0f, 1.0f}},
            {{+1.0f, +1.0f, +1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
            {{+1.0f, -1.0f, +1.0f}, {0.5f, 0.5f, 0.5f, 1.0f}},
        };

        const UINT vertexBufferSize = sizeof(vertices);

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC vertexBufferDesc = {};
        vertexBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        vertexBufferDesc.Alignment = 0;
        vertexBufferDesc.Width = vertexBufferSize;
        vertexBufferDesc.Height = 1;
        vertexBufferDesc.DepthOrArraySize = 1;
        vertexBufferDesc.MipLevels = 1;
        vertexBufferDesc.Format = DXGI_FORMAT_UNKNOWN;
        vertexBufferDesc.SampleDesc.Count = 1;
        vertexBufferDesc.SampleDesc.Quality = 0;
        vertexBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        vertexBufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        ThrowIfFailed(m_device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &vertexBufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_vertexBuffer)));

        UINT8* pVertexDataBegin = nullptr;
        D3D12_RANGE readRange = {0, 0};
        ThrowIfFailed(m_vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)));
        memcpy(pVertexDataBegin, vertices, sizeof(vertices));
        m_vertexBuffer->Unmap(0, nullptr);

        m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
        m_vertexBufferView.StrideInBytes = sizeof(Vertex);
        m_vertexBufferView.SizeInBytes = vertexBufferSize;

        uint16_t indices[] =
        {
            0, 1, 2, 0, 2, 3,
            4, 6, 5, 4, 7, 6,
            4, 5, 1, 4, 1, 0,
            3, 2, 6, 3, 6, 7,
            1, 5, 6, 1, 6, 2,
            4, 0, 3, 4, 3, 7,
        };

        const UINT indexBufferSize = sizeof(indices);
        D3D12_RESOURCE_DESC indexBufferDesc = {};
        indexBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        indexBufferDesc.Alignment = 0;
        indexBufferDesc.Width = indexBufferSize;
        indexBufferDesc.Height = 1;
        indexBufferDesc.DepthOrArraySize = 1;
        indexBufferDesc.MipLevels = 1;
        indexBufferDesc.Format = DXGI_FORMAT_UNKNOWN;
        indexBufferDesc.SampleDesc.Count = 1;
        indexBufferDesc.SampleDesc.Quality = 0;
        indexBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        indexBufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        ThrowIfFailed(m_device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &indexBufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_indexBuffer)));

        UINT8* pIndexDataBegin = nullptr;
        ThrowIfFailed(m_indexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pIndexDataBegin)));
        memcpy(pIndexDataBegin, indices, sizeof(indices));
        m_indexBuffer->Unmap(0, nullptr);

        m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
        m_indexBufferView.Format = DXGI_FORMAT_R16_UINT;
        m_indexBufferView.SizeInBytes = indexBufferSize;
    }

    void CreateConstantBuffer()
    {
        const UINT constantBufferSize = (sizeof(ConstantBufferData) + 255) & ~255;

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC constantBufferDesc = {};
        constantBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        constantBufferDesc.Alignment = 0;
        constantBufferDesc.Width = constantBufferSize;
        constantBufferDesc.Height = 1;
        constantBufferDesc.DepthOrArraySize = 1;
        constantBufferDesc.MipLevels = 1;
        constantBufferDesc.Format = DXGI_FORMAT_UNKNOWN;
        constantBufferDesc.SampleDesc.Count = 1;
        constantBufferDesc.SampleDesc.Quality = 0;
        constantBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        constantBufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        ThrowIfFailed(m_device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &constantBufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_constantBuffer)));

        D3D12_RANGE readRange = { 0, 0 };
        ThrowIfFailed(m_constantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_constantBufferDataBegin)));
        memset(m_constantBufferDataBegin, 0, constantBufferSize);
    }

    void CreateCBVHeap()
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.NumDescriptors = 1;
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_cbvHeap)));

        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = m_constantBuffer->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = (sizeof(ConstantBufferData) + 255) & ~255;
        m_device->CreateConstantBufferView(&cbvDesc, m_cbvHeap->GetCPUDescriptorHandleForHeapStart());
    }

    void PopulateCommandList()
    {
        ThrowIfFailed(m_commandAllocators[m_frameIndex]->Reset());
        ThrowIfFailed(m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), m_pipelineState.Get()));

        D3D12_RESOURCE_BARRIER barrierToRender = TransitionBarrier(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
        m_commandList->ResourceBarrier(1, &barrierToRender);

        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtvHandle.ptr += m_frameIndex * m_rtvDescriptorSize;
        m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

        D3D12_VIEWPORT viewport = {};
        viewport.TopLeftX = 0.0f;
        viewport.TopLeftY = 0.0f;
        viewport.Width = static_cast<float>(m_width);
        viewport.Height = static_cast<float>(m_height);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        m_commandList->RSSetViewports(1, &viewport);

        D3D12_RECT scissorRect = { 0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height) };
        m_commandList->RSSetScissorRects(1, &scissorRect);

        const float clearColor[] = { 0.1f, 0.15f, 0.25f, 1.0f };
        m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

        m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
        m_commandList->SetGraphicsRootConstantBufferView(0, m_constantBuffer->GetGPUVirtualAddress());

        m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
        m_commandList->IASetIndexBuffer(&m_indexBufferView);

        m_commandList->DrawIndexedInstanced(36, 1, 0, 0, 0);

        D3D12_RESOURCE_BARRIER barrierToPresent = TransitionBarrier(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        m_commandList->ResourceBarrier(1, &barrierToPresent);

        ThrowIfFailed(m_commandList->Close());
    }

    void WaitForPreviousFrame()
    {
        const UINT64 currentFenceValue = m_fenceValue;
        ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), currentFenceValue));
        m_fenceValue++;

        if (m_fence->GetCompletedValue() < currentFenceValue)
        {
            ThrowIfFailed(m_fence->SetEventOnCompletion(currentFenceValue, m_fenceEvent));
            WaitForSingleObject(m_fenceEvent, INFINITE);
        }

        m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
    }

    std::wstring GetShaderPath() const
    {
        std::filesystem::path exePath;
        wchar_t buffer[MAX_PATH];
        GetModuleFileNameW(nullptr, buffer, MAX_PATH);
        exePath = buffer;
        exePath = exePath.remove_filename();
        exePath /= L"shaders.hlsl";
        return exePath.native();
    }

private:
    HWND m_hwnd = nullptr;
    UINT m_width = 1280;
    UINT m_height = 720;

    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<IDXGISwapChain3> m_swapChain;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    ComPtr<ID3D12Resource> m_renderTargets[FrameCount];
    ComPtr<ID3D12CommandAllocator> m_commandAllocators[FrameCount];
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pipelineState;
    ComPtr<ID3D12GraphicsCommandList> m_commandList;

    ComPtr<ID3D12Resource> m_vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView = {};
    ComPtr<ID3D12Resource> m_indexBuffer;
    D3D12_INDEX_BUFFER_VIEW m_indexBufferView = {};

    ComPtr<ID3D12Resource> m_constantBuffer;
    UINT8* m_constantBufferDataBegin = nullptr;
    ComPtr<ID3D12DescriptorHeap> m_cbvHeap;

    UINT m_frameIndex = 0;
    UINT m_rtvDescriptorSize = 0;
    ComPtr<ID3D12Fence> m_fence;
    UINT64 m_fenceValue = 1;
    HANDLE m_fenceEvent = nullptr;
    std::chrono::steady_clock::time_point m_lastTime;

    XMVECTOR m_cameraPosition = XMVectorSet(0.0f, 0.0f, -6.0f, 0.0f);
    float m_cameraSpeed = 3.0f;
    float m_yaw = 0.0f;
    float m_pitch = 0.0f;
    float m_mouseSensitivity = 0.002f;
    bool m_mouseCapture = false;
    bool m_moveForward = false;
    bool m_moveBackward = false;
    bool m_moveLeft = false;
    bool m_moveRight = false;
};

static D3D12App* g_app = nullptr;

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_LBUTTONDOWN:
        if (g_app) g_app->EnableMouseCapture();
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE && g_app) g_app->DisableMouseCapture();
        if (g_app) g_app->OnKeyDown(wParam);
        return 0;

    case WM_KEYUP:
        if (g_app) g_app->OnKeyUp(wParam);
        return 0;

    case WM_KILLFOCUS:
        if (g_app) g_app->DisableMouseCapture();
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProc(hwnd, message, wParam, lParam);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    const wchar_t windowClassName[] = L"DirectX12CubeWindowClass";

    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = windowClassName;

    if (!RegisterClassEx(&wc))
    {
        return -1;
    }

    const UINT width = 1280;
    const UINT height = 720;
    RECT windowRect = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
    AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hwnd = CreateWindowEx(
        0,
        windowClassName,
        L"DX12 Cube - WASD Camera",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        nullptr,
        nullptr,
        hInstance,
        nullptr);

    if (!hwnd)
    {
        return -1;
    }

    ShowWindow(hwnd, nCmdShow);

    D3D12App app;
    g_app = &app;

    try
    {
        app.Initialize(hwnd, width, height);
    }
    catch (const std::exception& ex)
    {
        MessageBoxA(hwnd, ex.what(), "Initialization Error", MB_OK | MB_ICONERROR);
        return -1;
    }

    MSG msg = {};
    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            try
            {
                app.Update();
                app.Render();
            }
            catch (const std::exception& ex)
            {
                MessageBoxA(hwnd, ex.what(), "Runtime Error", MB_OK | MB_ICONERROR);
                return -1;
            }
        }
    }

    return static_cast<int>(msg.wParam);
}
