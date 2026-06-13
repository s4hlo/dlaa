# DirectX 12 SSAA Renderer — Architecture Reference

## Overview

A Windows DirectX 12 renderer that draws a colored cube at **2× supersampled resolution** (1600×900) and downsamples to an **800×450 display backbuffer** via a fullscreen blit, producing anti-aliased output without MSAA.

---

## Project Structure

```
src/
  main.cpp               — WinMain + WindowProc; no D3D12 logic
  renderer.h/.cpp        — Central orchestrator; owns all subsystems
  d3d_context.h/.cpp     — Raw D3D12 device, queue, swapchain, fence
  d3d_helpers.h          — ThrowIfFailed, Vertex, ConstantBufferData, TransitionBarrier
  frame_buffers.h/.cpp   — Shared depth buffer for scene rendering
  camera.h/.cpp          — First-person WASD camera
  shaders.hlsl           — All shaders: scene (VSMain/PSMain) + downsample (QuadVS/QuadPS)
  passes/
    render_pass.h        — Pure virtual base: Execute(D3DContext&, cmd*)
    scene_pass.h/.cpp    — Cube: root signature, PSO, VB/IB/CB
    downsample_pass.h/.cpp — Offscreen RT (1600x900) + fullscreen blit to swapchain
```

---

## Component Architecture

### Class Diagram

```mermaid
classDiagram
    class Renderer {
        +Initialize(hwnd, w, h)
        +Update()
        +Render()
        -D3DContext m_ctx
        -Camera m_camera
        -FrameBuffers m_frameBuffers
        -ScenePass m_scenePass
        -DownsamplePass m_downsamplePass
    }

    class D3DContext {
        +ID3D12Device device
        +ID3D12CommandQueue commandQueue
        +IDXGISwapChain3 swapChain
        +ID3D12GraphicsCommandList commandList
        +commandAllocators[2]
        +renderTargets[2]
        +ID3D12Fence fence
        +UINT frameIndex
        +Initialize(hwnd, w, h)
        +WaitForPreviousFrame()
    }

    class RenderPass {
        <<abstract>>
        +Execute(D3DContext, cmd)*
    }

    class ScenePass {
        +Initialize(ctx, shaderPath)
        +UpdateConstants(camera, w, h)
        +Execute(ctx, cmd)
        +ExecuteToTarget(cmd, rtvHandle, w, h)
        -rootSig
        -pso
        -vertexBuffer
        -indexBuffer
        -constantBuffer
        -dsvHandle
    }

    class DownsamplePass {
        +SSWidth = 1600
        +SSHeight = 900
        +Initialize(ctx, shaderPath)
        +Execute(ctx, cmd)
        +GetOffscreenRTV()
        -offscreenRT
        -srvHeap
        -rootSig
        -pso
    }

    class FrameBuffers {
        +Initialize(ctx, w, h)
        +GetDSV()
        -depthBuffer D32_FLOAT 1600x900
        -dsvHeap
    }

    Renderer --> D3DContext : owns
    Renderer --> ScenePass : owns
    Renderer --> DownsamplePass : owns
    Renderer --> FrameBuffers : owns
    ScenePass --|> RenderPass
    DownsamplePass --|> RenderPass
```

### Module Dependency Graph

```mermaid
graph TD
    main["main.cpp\nWinMain + WindowProc"] --> Renderer
    Renderer --> D3DContext
    Renderer --> ScenePass
    Renderer --> DownsamplePass
    Renderer --> FrameBuffers
    ScenePass --> RenderPass["render_pass.h\nRenderPass (abstract)"]
    DownsamplePass --> RenderPass
    ScenePass --> helpers["d3d_helpers.h\nVertex · CBData · ThrowIfFailed · TransitionBarrier"]
    DownsamplePass --> helpers
    D3DContext --> helpers
    ScenePass --> shaders["shaders.hlsl\nVSMain / PSMain\nQuadVS / QuadPS"]
    DownsamplePass --> shaders
```

---

## Initialization

```mermaid
sequenceDiagram
    participant M as main.cpp
    participant R as Renderer
    participant CTX as D3DContext
    participant SP as ScenePass
    participant DP as DownsamplePass
    participant FB as FrameBuffers

    M->>R: Initialize(hwnd, 800, 450)

    R->>CTX: Initialize(hwnd, 800, 450)
    Note over CTX: Enumerate hardware adapter<br/>Create ID3D12Device (FL 11.0)<br/>Create direct CommandQueue<br/>Create SwapChain3 (2 buffers, 800x450, RGBA8)<br/>Create RTV heap + RTVs for each backbuffer<br/>Create 2x CommandAllocator<br/>Create Fence + event<br/>Create CommandList (closed)

    R->>SP: Initialize(ctx, shaderPath)
    Note over SP: Compile VSMain (vs_5_0) + PSMain (ps_5_0)<br/>Build root signature: 1 CBV at b0 (VS-visible)<br/>Build PSO: depth-enabled, back-face cull<br/>Upload VB (8 vertices) to UPLOAD heap<br/>Upload IB (36 indices) to UPLOAD heap<br/>Map CB (256B) persistently

    R->>DP: Initialize(ctx, shaderPath)
    Note over DP: Create offscreen RT (RGBA8, 1600x900, DEFAULT heap)<br/>Create RTV heap for offscreen RT<br/>Create SRV heap (shader-visible) for offscreen RT<br/>Compile QuadVS + QuadPS (vs_5_0 / ps_5_0)<br/>Build root signature: SRV table + linear-clamp static sampler<br/>Build PSO: depth-disabled, no cull, no input layout

    R->>FB: Initialize(ctx, 1600, 900)
    Note over FB: Create depth buffer (R32_TYPELESS → D32_FLOAT, 1600x900)<br/>Create DSV heap + DSV

    R->>SP: SetDepthStencilView(FB.GetDSV())
```

---

## Per-Frame Render Loop

```mermaid
flowchart TD
    A([WinMain message loop]) --> B{PeekMessage?}
    B -- message available --> C[TranslateMessage\nDispatchMessage]
    C --> B
    B -- no message --> D

    subgraph Update
        D[Camera::Update\ncompute view matrix from input state]
        D --> E[ScenePass::UpdateConstants\nbuild MVP → memcpy to mapped CB]
    end

    E --> F

    subgraph Render
        F[Reset CommandAllocator for frameIndex\nReset CommandList]
        F --> G[ScenePass::ExecuteToTarget\nrender cube to offscreen RT 1600x900]
        G --> H[DownsamplePass::Execute\nblit 1600x900 → swapchain backbuffer 800x450]
        H --> I[cmd Close]
        I --> J[ExecuteCommandLists]
        J --> K[SwapChain Present vsync=1]
        K --> L[WaitForPreviousFrame\nsignal fence · wait · update frameIndex]
    end

    L --> B
```

---

## SSAA Rendering Pipeline

The scene is rendered at **2× linear resolution** (1600×900 = 4× the pixel count of 800×450), then linearly downsampled. This is 2× SSAA — each display pixel averages a 2×2 block of rendered samples.

```mermaid
graph LR
    subgraph Geometry Data
        VB["Vertex Buffer\n8 vertices\nposition·color"]
        IB["Index Buffer\n36 indices\n12 triangles"]
        CB["Constant Buffer\nMVP 4×4 float32\n256B aligned"]
    end

    subgraph ScenePass  - 1600x900
        VS["VSMain\npos = mul(pos, mvp)\ncolor = color"]
        PS["PSMain\nreturn color"]
        DSV["Depth Buffer\nD32_FLOAT 1600x900"]
    end

    subgraph DownsamplePass  - 800x450
        QVS["QuadVS\nSV_VertexID → UV + clip pos\nno vertex buffer"]
        QPS["QuadPS\nSceneTexture.Sample\nLinearSampler"]
        OFFRT["Offscreen RT\nRGBA8 1600x900\nbound as SRV"]
    end

    subgraph Display
        SC["Swapchain Backbuffer\nRGBA8 800x450"]
    end

    VB --> VS
    IB --> VS
    CB --> VS
    VS --> PS
    DSV --> PS
    PS -- "write pixels" --> OFFRT

    OFFRT --> QPS
    QVS --> QPS
    QPS -- "write pixels" --> SC
```

### Fullscreen Triangle Trick

`QuadVS` generates clip-space positions and UVs purely from `SV_VertexID` — no vertex buffer is needed:

| VertexID | UV | Clip position |
|---|---|---|
| 0 | (0, 0) | (-1, -1) |
| 1 | (0, 2) | (-1,  3) |
| 2 | (2, 0) | ( 3, -1) |

The triangle over-covers the screen; the rasterizer clips it to the viewport automatically.

---

## Resource State Transitions

D3D12 requires explicit barriers before changing how a resource is used. The diagram below shows every barrier issued in a typical frame.

```mermaid
sequenceDiagram
    participant OFF as Offscreen RT
    participant SC as Swapchain Backbuffer
    participant CMD as Command List

    Note over OFF: initial state: RENDER_TARGET
    Note over SC: initial state: PRESENT

    rect rgb(220,235,255)
        Note over CMD: ScenePass::ExecuteToTarget
        CMD->>OFF: draw 36 indices (cube) → write depth + color
    end

    rect rgb(255,240,220)
        Note over CMD: DownsamplePass::Execute — barrier batch 1
        CMD->>OFF: Barrier RENDER_TARGET → PIXEL_SHADER_RESOURCE
        CMD->>SC: Barrier PRESENT → RENDER_TARGET
    end

    rect rgb(220,255,230)
        Note over CMD: DownsamplePass::Execute — draw
        CMD->>SC: DrawInstanced(3) — fullscreen blit sampling OFF
    end

    rect rgb(255,240,220)
        Note over CMD: DownsamplePass::Execute — barrier batch 2
        CMD->>SC: Barrier RENDER_TARGET → PRESENT
        CMD->>OFF: Barrier PIXEL_SHADER_RESOURCE → RENDER_TARGET
    end

    Note over SC: SwapChain::Present()
    Note over OFF: ready for next frame
```

---

## Shaders

All shaders are in a single `shaders.hlsl` file and compiled at runtime via `D3DCompileFromFile`.

### Scene Shaders — `VSMain` / `PSMain`

Used by **ScenePass**. Renders the cube with per-vertex color and depth.

```mermaid
flowchart LR
    IA["Input Assembler\nPOSITION float3\nCOLOR    float4"]
    CB2["cbuffer b0\nmatrix mvp"]
    VS2["VSMain\nout.pos = mul&#40;pos4, mvp&#41;\nout.color = color"]
    PS2["PSMain\nreturn input.color"]
    OUT["Render Target\nRGBA8 pixel\n+ depth write"]

    IA --> VS2
    CB2 --> VS2
    VS2 --> PS2
    PS2 --> OUT
```

### Downsample Shaders — `QuadVS` / `QuadPS`

Used by **DownsamplePass**. Blits the offscreen RT to the swapchain with bilinear filtering.

```mermaid
flowchart LR
    VID["SV_VertexID\n0 · 1 · 2"]
    QVS2["QuadVS\nderive UV from vertex ID\ncompute clip-space pos"]
    TEX["SceneTexture t0\noffscreen RT 1600x900"]
    SAM["LinearSampler s0\nbilinear + clamp"]
    QPS2["QuadPS\nSceneTexture.Sample&#40;sampler, uv&#41;"]
    OUT2["Swapchain RT\nRGBA8 pixel 800x450"]

    VID --> QVS2
    QVS2 --> QPS2
    TEX --> QPS2
    SAM --> QPS2
    QPS2 --> OUT2
```

---

## Double Buffering and CPU/GPU Synchronization

`FrameCount = 2`: two swapchain backbuffers and two command allocators. The CPU must not reset an allocator whose commands the GPU is still executing.

```mermaid
sequenceDiagram
    participant CPU
    participant Queue as CommandQueue
    participant GPU
    participant Fence

    CPU->>CPU: Reset allocator[frameIndex]
    CPU->>CPU: Record commands into CommandList
    CPU->>Queue: ExecuteCommandLists(cmd)
    GPU->>GPU: Execute commands (async)
    CPU->>Queue: SwapChain::Present(1, 0)
    CPU->>Queue: Signal(fence, N)
    CPU->>Fence: Wait until CompletedValue >= N
    Note over CPU,Fence: CPU blocks here until GPU<br/>finishes executing those commands
    Fence-->>CPU: signaled
    CPU->>CPU: frameIndex = SwapChain::GetCurrentBackBufferIndex()
    Note over CPU: Safe to reset allocator[frameIndex]<br/>on the next iteration
```

---

## Pipeline State Objects

| | ScenePass | DownsamplePass |
|---|---|---|
| Vertex shader | `VSMain` | `QuadVS` |
| Pixel shader | `PSMain` | `QuadPS` |
| Input layout | `POSITION` + `COLOR` | none (vertex-ID only) |
| Depth test | Enabled (`LESS`) | Disabled |
| Depth write | Enabled | Disabled |
| Cull mode | Back-face | None |
| Blend | Disabled | Disabled |
| Render target format | `RGBA8_UNORM` | `RGBA8_UNORM` |
| Render target size | 1600 × 900 | 800 × 450 |

---

## Error Handling

All D3D12 API calls go through `ThrowIfFailed`. Any `HRESULT` failure throws `std::runtime_error`, which propagates to `WinMain` and surfaces as a `MessageBox`.

```mermaid
flowchart TD
    A["D3D12 / DXGI API call"] --> B{HRESULT}
    B -- SUCCEEDED --> C[continue]
    B -- FAILED --> D["ThrowIfFailed\nthrow runtime_error\n'HRESULT 0x...'"]
    D --> E["Propagates up call stack"]
    E --> F{"Where caught?"}
    F -- "Initialize" --> G["WinMain catch\nMessageBox 'Initialization Error'\nreturn -1"]
    F -- "Render / Update" --> H["WinMain catch\nMessageBox 'Runtime Error'\nreturn -1"]
```
