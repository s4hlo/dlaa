# Modularize DirectX12Cube Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Split `src/main.cpp` (~935 lines) into focused modules so new rendering techniques (depth, lighting, textures, post-processing) can be added without bloating a single file.

**Architecture:** `D3DContext` owns D3D12 infrastructure (device, swapchain, command list, fence). `RenderPass` is a pure-virtual interface. `ScenePass` draws the cube; `DownsamplePass` blits the SSAA offscreen target. `Renderer` owns context + camera + passes and drives Update/Render. `main.cpp` is reduced to WinMain + WindowProc only. The `#ifdef ENABLE_SUPERSAMPLING` blocks inside rendering logic are replaced by the presence/absence of `m_downsamplePass` in Renderer.

**Tech Stack:** C++17, DirectX 12, DXGI, D3DCompiler, WRL, CMake, Win32

---

## File Map

| File | Action | Responsibility |
|------|--------|----------------|
| `src/d3d_helpers.h` | Create | `ThrowIfFailed`, `TransitionBarrier`, `Vertex`, `ConstantBufferData`, `FrameCount` |
| `src/camera.h` | Create | Camera interface |
| `src/camera.cpp` | Create | Camera state, mouse/WASD logic |
| `src/d3d_context.h` | Create | D3DContext struct declaration |
| `src/d3d_context.cpp` | Create | Device, queue, swapchain, fence, command list creation |
| `src/passes/render_pass.h` | Create | Pure-virtual base |
| `src/passes/scene_pass.h` | Create | ScenePass interface |
| `src/passes/scene_pass.cpp` | Create | Cube root sig, PSO, buffers, draw |
| `src/passes/downsample_pass.h` | Create | DownsamplePass interface |
| `src/passes/downsample_pass.cpp` | Create | Offscreen RT, blit to swapchain |
| `src/renderer.h` | Create | Renderer interface |
| `src/renderer.cpp` | Create | Orchestrates context + camera + passes |
| `CMakeLists.txt` | Modify | Add new .cpp files to target |
| `src/main.cpp` | Modify | Strip to WinMain + WindowProc |

---

### Task 1: d3d_helpers.h

**Files:**
- Create: `src/d3d_helpers.h`

- [ ] Create `src/d3d_helpers.h`:

```cpp
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wrl.h>
#include <d3d12.h>
#include <DirectXMath.h>
#include <stdexcept>

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
        throw std::runtime_error("HRESULT failed.");
}

inline D3D12_RESOURCE_BARRIER TransitionBarrier(
    ID3D12Resource*       resource,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource   = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter  = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    return barrier;
}
```

- [ ] Commit:

```bash
git add src/d3d_helpers.h
git commit -m "refactor: add d3d_helpers.h with shared types and utilities"
```

---

### Task 2: camera.h / camera.cpp

**Files:**
- Create: `src/camera.h`
- Create: `src/camera.cpp`

- [ ] Create `src/camera.h`:

```cpp
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <DirectXMath.h>
#include <chrono>

using namespace DirectX;

class Camera
{
public:
    void EnableCapture(HWND hwnd);
    void DisableCapture();
    void Update(HWND hwnd);
    void OnKeyDown(WPARAM key);
    void OnKeyUp(WPARAM key);
    XMMATRIX GetViewMatrix() const;
    bool IsCapturing() const { return m_mouseCapture; }

private:
    XMVECTOR m_position     = XMVectorSet(0.0f, 0.0f, -6.0f, 0.0f);
    float    m_yaw          = 0.0f;
    float    m_pitch        = 0.0f;
    float    m_speed        = 3.0f;
    float    m_sensitivity  = 0.002f;
    bool     m_mouseCapture = false;
    bool     m_moveForward  = false;
    bool     m_moveBackward = false;
    bool     m_moveLeft     = false;
    bool     m_moveRight    = false;
    std::chrono::steady_clock::time_point m_lastTime =
        std::chrono::steady_clock::now();
};
```

- [ ] Create `src/camera.cpp`:

```cpp
#include "camera.h"
#include <algorithm>
#include <cmath>

void Camera::EnableCapture(HWND hwnd)
{
    m_mouseCapture = true;
    ShowCursor(FALSE);
    RECT rect;
    GetClientRect(hwnd, &rect);
    POINT center = { (rect.right - rect.left) / 2, (rect.bottom - rect.top) / 2 };
    ClientToScreen(hwnd, &center);
    SetCursorPos(center.x, center.y);
}

void Camera::DisableCapture()
{
    m_mouseCapture = false;
    ShowCursor(TRUE);
}

void Camera::Update(HWND hwnd)
{
    const auto  now = std::chrono::steady_clock::now();
    const float dt  = std::chrono::duration<float>(now - m_lastTime).count();
    m_lastTime = now;

    if (m_mouseCapture)
    {
        RECT rect;
        GetClientRect(hwnd, &rect);
        POINT center = { (rect.right - rect.left) / 2, (rect.bottom - rect.top) / 2 };
        ClientToScreen(hwnd, &center);
        POINT pt;
        GetCursorPos(&pt);
        m_yaw   += static_cast<float>(pt.x - center.x) * m_sensitivity;
        m_pitch -= static_cast<float>(pt.y - center.y) * m_sensitivity;
        m_pitch  = std::clamp(m_pitch, -XM_PIDIV2 + 0.01f, XM_PIDIV2 - 0.01f);
        SetCursorPos(center.x, center.y);
    }

    const float cp = cosf(m_pitch), sp = sinf(m_pitch);
    const float cy = cosf(m_yaw),   sy = sinf(m_yaw);
    const XMVECTOR flatFwd = XMVector3Normalize(XMVectorSet(sy, 0.0f, cy, 0.0f));
    const XMVECTOR up      = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    const XMVECTOR right   = XMVector3Cross(up, flatFwd);

    XMVECTOR move = XMVectorZero();
    if (m_moveForward)  move = XMVectorAdd(move, flatFwd);
    if (m_moveBackward) move = XMVectorSubtract(move, flatFwd);
    if (m_moveRight)    move = XMVectorAdd(move, right);
    if (m_moveLeft)     move = XMVectorSubtract(move, right);

    if (XMVector3LengthSq(move).m128_f32[0] > 0.0f)
        m_position = XMVectorAdd(m_position,
            XMVector3Normalize(move) * (m_speed * dt));
}

void Camera::OnKeyDown(WPARAM key)
{
    switch (key)
    {
    case 'W': m_moveForward  = true;  break;
    case 'S': m_moveBackward = true;  break;
    case 'A': m_moveLeft     = true;  break;
    case 'D': m_moveRight    = true;  break;
    default:  break;
    }
}

void Camera::OnKeyUp(WPARAM key)
{
    switch (key)
    {
    case 'W': m_moveForward  = false; break;
    case 'S': m_moveBackward = false; break;
    case 'A': m_moveLeft     = false; break;
    case 'D': m_moveRight    = false; break;
    default:  break;
    }
}

XMMATRIX Camera::GetViewMatrix() const
{
    const float cp = cosf(m_pitch), sp = sinf(m_pitch);
    const float cy = cosf(m_yaw),   sy = sinf(m_yaw);
    const XMVECTOR forward = XMVectorSet(cp * sy, sp, cp * cy, 0.0f);
    const XMVECTOR up      = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    const XMVECTOR target  = XMVectorAdd(m_position, forward);
    return XMMatrixLookAtLH(m_position, target, up);
}
```

- [ ] Commit:

```bash
git add src/camera.h src/camera.cpp
git commit -m "refactor: extract Camera class to camera.h/.cpp"
```

---

### Task 3: d3d_context.h / d3d_context.cpp

**Files:**
- Create: `src/d3d_context.h`
- Create: `src/d3d_context.cpp`

- [ ] Create `src/d3d_context.h`:

```cpp
#pragma once
#include "d3d_helpers.h"
#include <dxgi1_4.h>

struct D3DContext
{
    ComPtr<ID3D12Device>              device;
    ComPtr<ID3D12CommandQueue>        commandQueue;
    ComPtr<IDXGISwapChain3>           swapChain;
    ComPtr<ID3D12GraphicsCommandList> commandList;
    ComPtr<ID3D12CommandAllocator>    commandAllocators[FrameCount];
    ComPtr<ID3D12DescriptorHeap>      rtvHeap;
    ComPtr<ID3D12Resource>            renderTargets[FrameCount];
    ComPtr<ID3D12Fence>               fence;
    HANDLE                            fenceEvent        = nullptr;
    UINT64                            fenceValue        = 1;
    UINT                              frameIndex        = 0;
    UINT                              rtvDescriptorSize = 0;
    UINT                              width             = 0;
    UINT                              height            = 0;

    void Initialize(HWND hwnd, UINT w, UINT h);
    void WaitForPreviousFrame();
};
```

- [ ] Create `src/d3d_context.cpp`:

```cpp
#include "d3d_context.h"

void D3DContext::Initialize(HWND hwnd, UINT w, UINT h)
{
    width  = w;
    height = h;

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
    for (UINT i = 0; DXGI_ERROR_NOT_FOUND != factory->EnumAdapters1(i, &hardwareAdapter); ++i)
    {
        DXGI_ADAPTER_DESC1 desc;
        hardwareAdapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        if (SUCCEEDED(D3D12CreateDevice(hardwareAdapter.Get(),
                D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr))) break;
    }

    ThrowIfFailed(D3D12CreateDevice(hardwareAdapter.Get(),
        D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type  = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ThrowIfFailed(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue)));

    DXGI_SWAP_CHAIN_DESC1 scDesc = {};
    scDesc.BufferCount      = FrameCount;
    scDesc.Width            = width;
    scDesc.Height           = height;
    scDesc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
    scDesc.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scDesc.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> sc1;
    ThrowIfFailed(factory->CreateSwapChainForHwnd(
        commandQueue.Get(), hwnd, &scDesc, nullptr, nullptr, &sc1));
    ThrowIfFailed(factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER));
    ThrowIfFailed(sc1.As(&swapChain));
    frameIndex = swapChain->GetCurrentBackBufferIndex();

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = FrameCount;
    rtvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap)));
    rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT n = 0; n < FrameCount; ++n)
    {
        ThrowIfFailed(swapChain->GetBuffer(n, IID_PPV_ARGS(&renderTargets[n])));
        device->CreateRenderTargetView(renderTargets[n].Get(), nullptr, rtvHandle);
        rtvHandle.ptr += rtvDescriptorSize;
    }

    for (UINT n = 0; n < FrameCount; ++n)
        ThrowIfFailed(device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocators[n])));

    ThrowIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent)
        ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));

    ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        commandAllocators[frameIndex].Get(), nullptr, IID_PPV_ARGS(&commandList)));
    ThrowIfFailed(commandList->Close());
}

void D3DContext::WaitForPreviousFrame()
{
    const UINT64 val = fenceValue;
    ThrowIfFailed(commandQueue->Signal(fence.Get(), val));
    ++fenceValue;

    if (fence->GetCompletedValue() < val)
    {
        ThrowIfFailed(fence->SetEventOnCompletion(val, fenceEvent));
        WaitForSingleObject(fenceEvent, INFINITE);
    }

    frameIndex = swapChain->GetCurrentBackBufferIndex();
}
```

- [ ] Commit:

```bash
git add src/d3d_context.h src/d3d_context.cpp
git commit -m "refactor: extract D3DContext (device/swapchain/fence/command infra)"
```

---

### Task 4: passes/render_pass.h

**Files:**
- Create: `src/passes/render_pass.h`

- [ ] Create the `src/passes/` directory and `render_pass.h`:

```cpp
#pragma once
#include <d3d12.h>

struct D3DContext;

struct RenderPass
{
    virtual void Execute(D3DContext& ctx, ID3D12GraphicsCommandList* cmd) = 0;
    virtual ~RenderPass() = default;
};
```

- [ ] Commit:

```bash
git add src/passes/render_pass.h
git commit -m "refactor: add RenderPass base interface"
```

---

### Task 5: passes/scene_pass.h / scene_pass.cpp

**Files:**
- Create: `src/passes/scene_pass.h`
- Create: `src/passes/scene_pass.cpp`

`Execute` renders the cube to the swapchain back buffer (no SSAA). `ExecuteToTarget` renders to a caller-provided RTV at a given resolution (used by Renderer when SSAA is active).

- [ ] Create `src/passes/scene_pass.h`:

```cpp
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

    void ExecuteToTarget(D3DContext& ctx, ID3D12GraphicsCommandList* cmd,
                         D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle, UINT w, UINT h);

private:
    void DrawScene(ID3D12GraphicsCommandList* cmd,
                   D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle, UINT w, UINT h);

    ComPtr<ID3D12RootSignature> m_rootSig;
    ComPtr<ID3D12PipelineState> m_pso;
    ComPtr<ID3D12Resource>      m_vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW    m_vbv = {};
    ComPtr<ID3D12Resource>      m_indexBuffer;
    D3D12_INDEX_BUFFER_VIEW     m_ibv = {};
    ComPtr<ID3D12Resource>      m_constantBuffer;
    UINT8*                      m_cbDataBegin = nullptr;
};
```

- [ ] Create `src/passes/scene_pass.cpp`:

```cpp
#include "scene_pass.h"
#include "../d3d_context.h"

void ScenePass::Initialize(D3DContext& ctx, const std::wstring& shaderPath)
{
    // Root signature: one CBV at register b0, vertex-visible
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

    // Shaders
    ComPtr<ID3DBlob> vs, ps, errBlob;
    ThrowIfFailed(D3DCompileFromFile(shaderPath.c_str(), nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE, "VSMain", "vs_5_0", 0, 0, &vs, &errBlob));
    ThrowIfFailed(D3DCompileFromFile(shaderPath.c_str(), nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE, "PSMain", "ps_5_0", 0, 0, &ps, &errBlob));

    // PSO
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
    dsDesc.DepthEnable   = FALSE;
    dsDesc.StencilEnable = FALSE;

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
    psoDesc.SampleDesc.Count      = 1;
    ThrowIfFailed(ctx.device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pso)));

    // Vertex buffer
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
    UINT8* pData = nullptr;

    auto vbDesc = MakeBufferDesc(vbSize);
    ThrowIfFailed(ctx.device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE,
        &vbDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_vertexBuffer)));
    ThrowIfFailed(m_vertexBuffer->Map(0, &noRead, reinterpret_cast<void**>(&pData)));
    memcpy(pData, vertices, vbSize);
    m_vertexBuffer->Unmap(0, nullptr);

    m_vbv.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vbv.StrideInBytes  = sizeof(Vertex);
    m_vbv.SizeInBytes    = vbSize;

    // Index buffer
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
    ThrowIfFailed(m_indexBuffer->Map(0, &noRead, reinterpret_cast<void**>(&pData)));
    memcpy(pData, indices, ibSize);
    m_indexBuffer->Unmap(0, nullptr);

    m_ibv.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
    m_ibv.Format         = DXGI_FORMAT_R16_UINT;
    m_ibv.SizeInBytes    = ibSize;

    // Constant buffer
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
    const XMMATRIX mvp = XMMatrixTranspose(XMMatrixIdentity() * view * proj);

    ConstantBufferData cb;
    XMStoreFloat4x4(&cb.mvp, mvp);
    memcpy(m_cbDataBegin, &cb, sizeof(cb));
}

void ScenePass::DrawScene(ID3D12GraphicsCommandList* cmd,
                          D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle, UINT w, UINT h)
{
    constexpr float clearColor[] = { 0.1f, 0.15f, 0.25f, 1.0f };

    D3D12_VIEWPORT vp = {};
    vp.Width    = static_cast<float>(w);
    vp.Height   = static_cast<float>(h);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    cmd->RSSetViewports(1, &vp);

    D3D12_RECT scissor = { 0, 0, static_cast<LONG>(w), static_cast<LONG>(h) };
    cmd->RSSetScissorRects(1, &scissor);

    cmd->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
    cmd->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
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

    DrawScene(cmd, rtvHandle, ctx.width, ctx.height);

    auto toPresent = TransitionBarrier(ctx.renderTargets[ctx.frameIndex].Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    cmd->ResourceBarrier(1, &toPresent);
}

void ScenePass::ExecuteToTarget(D3DContext& ctx, ID3D12GraphicsCommandList* cmd,
                                D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle, UINT w, UINT h)
{
    DrawScene(cmd, rtvHandle, w, h);
}
```

- [ ] Commit:

```bash
git add src/passes/scene_pass.h src/passes/scene_pass.cpp
git commit -m "refactor: extract ScenePass (cube root sig, PSO, buffers, draw)"
```

---

### Task 6: passes/downsample_pass.h / downsample_pass.cpp

**Files:**
- Create: `src/passes/downsample_pass.h`
- Create: `src/passes/downsample_pass.cpp`

Owns the 1600×900 offscreen RT. `Execute` transitions it to SRV, blits to the swapchain, then transitions back to RT for the next frame.

- [ ] Create `src/passes/downsample_pass.h`:

```cpp
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
```

- [ ] Create `src/passes/downsample_pass.cpp`:

```cpp
#include "downsample_pass.h"
#include "../d3d_context.h"
#include <d3dcompiler.h>

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
    clearVal.Color[0] = 0.1f; clearVal.Color[1] = 0.15f;
    clearVal.Color[2] = 0.25f; clearVal.Color[3] = 1.0f;

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
    ComPtr<ID3DBlob> vs, ps, errBlob;
    ThrowIfFailed(D3DCompileFromFile(shaderPath.c_str(), nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE, "QuadVS", "vs_5_0", 0, 0, &vs, &errBlob));
    ThrowIfFailed(D3DCompileFromFile(shaderPath.c_str(), nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE, "QuadPS", "ps_5_0", 0, 0, &ps, &errBlob));

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
```

- [ ] Commit:

```bash
git add src/passes/downsample_pass.h src/passes/downsample_pass.cpp
git commit -m "refactor: extract DownsamplePass (SSAA offscreen RT and blit)"
```

---

### Task 7: renderer.h / renderer.cpp

**Files:**
- Create: `src/renderer.h`
- Create: `src/renderer.cpp`

Renderer owns `D3DContext`, `Camera`, `ScenePass`, and an optional `DownsamplePass`. It drives the frame loop; `main.cpp` delegates all events to it.

- [ ] Create `src/renderer.h`:

```cpp
#pragma once
#include "d3d_context.h"
#include "camera.h"
#include "passes/scene_pass.h"
#include "passes/downsample_pass.h"
#include <memory>
#include <filesystem>
#include <string>

class Renderer
{
public:
    void Initialize(HWND hwnd, UINT width, UINT height);
    void Update();
    void Render();

    void OnKeyDown(WPARAM key) { if (key == VK_ESCAPE) m_camera.DisableCapture(); m_camera.OnKeyDown(key); }
    void OnKeyUp(WPARAM key)   { m_camera.OnKeyUp(key); }
    void OnLButtonDown()       { m_camera.EnableCapture(m_hwnd); }
    void OnKillFocus()         { m_camera.DisableCapture(); }

private:
    std::wstring GetShaderPath() const;

    HWND        m_hwnd = nullptr;
    D3DContext  m_ctx;
    Camera      m_camera;
    ScenePass   m_scenePass;
    std::unique_ptr<DownsamplePass> m_downsamplePass;
};
```

- [ ] Create `src/renderer.cpp`:

```cpp
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
        m_scenePass.ExecuteToTarget(m_ctx, cmd,
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
```

- [ ] Commit:

```bash
git add src/renderer.h src/renderer.cpp
git commit -m "refactor: add Renderer orchestrating context, camera, and passes"
```

---

### Task 8: Integration — update CMakeLists.txt and rewrite main.cpp

The project only compiles correctly after **both** files are updated in this task. Update CMakeLists first, then main.cpp.

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `src/main.cpp`

- [ ] In `CMakeLists.txt`, replace:

```cmake
add_executable(DirectX12Cube WIN32 src/main.cpp)
```

with:

```cmake
add_executable(DirectX12Cube WIN32
    src/main.cpp
    src/camera.cpp
    src/d3d_context.cpp
    src/passes/scene_pass.cpp
    src/passes/downsample_pass.cpp
    src/renderer.cpp
)
```

- [ ] Replace the entire contents of `src/main.cpp` with:

```cpp
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "renderer.h"

static Renderer* g_renderer = nullptr;

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_LBUTTONDOWN:
        if (g_renderer) g_renderer->OnLButtonDown();
        return 0;
    case WM_KEYDOWN:
        if (g_renderer) g_renderer->OnKeyDown(wParam);
        return 0;
    case WM_KEYUP:
        if (g_renderer) g_renderer->OnKeyUp(wParam);
        return 0;
    case WM_KILLFOCUS:
        if (g_renderer) g_renderer->OnKillFocus();
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
    const wchar_t className[] = L"DirectX12CubeWindowClass";

    WNDCLASSEX wc = {};
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = className;
    if (!RegisterClassEx(&wc)) return -1;

    constexpr UINT width = 800, height = 450;
    RECT rect = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hwnd = CreateWindowEx(0, className, L"DX12 Cube - WASD Camera",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) return -1;

    ShowWindow(hwnd, nCmdShow);

    Renderer renderer;
    g_renderer = &renderer;

    try { renderer.Initialize(hwnd, width, height); }
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
            try { renderer.Update(); renderer.Render(); }
            catch (const std::exception& ex)
            {
                MessageBoxA(hwnd, ex.what(), "Runtime Error", MB_OK | MB_ICONERROR);
                return -1;
            }
        }
    }
    return static_cast<int>(msg.wParam);
}
```

- [ ] Build:

```
cmake --build build
```

Expected: Build succeeds with no errors.

- [ ] Run `build\DirectX12Cube.exe`. Verify:
  - Cube renders on a dark blue background
  - WASD moves the camera
  - Left-click captures the mouse; mouse movement rotates the camera
  - ESC or alt-tab releases the mouse

- [ ] Commit:

```bash
git add CMakeLists.txt src/main.cpp
git commit -m "refactor: integrate modular structure, strip main.cpp to WinMain + WindowProc"
```
