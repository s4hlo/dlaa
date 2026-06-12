# Depth Buffer (FrameBuffers Module) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a D32 depth buffer with depth testing to the scene pass, owned by a new `FrameBuffers` module designed to host future dataset buffers (normals, motion vectors).

**Architecture:** A new `FrameBuffers` class owns the depth resource (`R32_TYPELESS`, viewed as `D32_FLOAT`) and its DSV heap at render resolution (1600×900 with SSAA, window size otherwise). `Renderer` creates it and hands the stable DSV handle to `ScenePass` once via a setter; `ScenePass` enables depth test/write in its PSO and binds/clears the DSV every frame.

**Tech Stack:** C++17, D3D12, CMake + Ninja presets, MSVC `/W4 /WX`.

**Spec:** `docs/superpowers/specs/2026-06-10-depth-buffer-design.md`

> **IMPORTANT — NO COMMITS:** The user explicitly instructed that nothing be committed to git. Skip all commits; leave everything in the working tree. Where a TDD plan would normally commit, this plan verifies with a build instead.

> **Testing note:** This repo has no automated test harness (graphics app). Verification = clean build under `/W4 /WX` on both presets + manual run. The cube is a single convex mesh, so rendering must look identical to before; the change is verified by the build, the debug layer staying silent, and the app not crashing.

---

### Task 1: FrameBuffers module

**Files:**
- Create: `src/frame_buffers.h`
- Create: `src/frame_buffers.cpp`
- Modify: `CMakeLists.txt:10-17` (add source file)

- [ ] **Step 1: Create `src/frame_buffers.h`**

```cpp
#pragma once
#include "d3d_helpers.h"

struct D3DContext;

// Owns render-resolution per-frame buffers used by the passes and, later,
// by dataset capture. Currently: depth only. Future: normals, motion vectors.
class FrameBuffers
{
public:
    void Initialize(D3DContext& ctx, UINT width, UINT height);

    D3D12_CPU_DESCRIPTOR_HANDLE GetDSV() const;
    ID3D12Resource*             GetDepthResource() const { return m_depthBuffer.Get(); }

private:
    ComPtr<ID3D12Resource>       m_depthBuffer;
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
};
```

- [ ] **Step 2: Create `src/frame_buffers.cpp`**

The resource is `R32_TYPELESS` (so a future dataset-capture step can view it
as `R32_FLOAT` SRV without recreating it); the DSV must therefore use an
explicit `D3D12_DEPTH_STENCIL_VIEW_DESC` with `D32_FLOAT` — a `nullptr` desc
does not work on typeless resources.

```cpp
#include "frame_buffers.h"
#include "d3d_context.h"

void FrameBuffers::Initialize(D3DContext& ctx, UINT width, UINT height)
{
    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type                 = D3D12_HEAP_TYPE_DEFAULT;
    defaultHeap.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    defaultHeap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    defaultHeap.CreationNodeMask     = 1;
    defaultHeap.VisibleNodeMask      = 1;

    D3D12_RESOURCE_DESC depthDesc = {};
    depthDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Width            = width;
    depthDesc.Height           = height;
    depthDesc.DepthOrArraySize = 1;
    depthDesc.MipLevels        = 1;
    depthDesc.Format           = DXGI_FORMAT_R32_TYPELESS;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    depthDesc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearVal = {};
    clearVal.Format               = DXGI_FORMAT_D32_FLOAT;
    clearVal.DepthStencil.Depth   = 1.0f;
    clearVal.DepthStencil.Stencil = 0;

    ThrowIfFailed(ctx.device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE,
        &depthDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearVal,
        IID_PPV_ARGS(&m_depthBuffer)));

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(ctx.device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap)));

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format        = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Flags         = D3D12_DSV_FLAG_NONE;
    ctx.device->CreateDepthStencilView(m_depthBuffer.Get(), &dsvDesc,
        m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
}

D3D12_CPU_DESCRIPTOR_HANDLE FrameBuffers::GetDSV() const
{
    return m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
}
```

- [ ] **Step 3: Add the new source to `CMakeLists.txt`**

In the `add_executable` block, add `src/frame_buffers.cpp` after
`src/d3d_context.cpp`:

```cmake
add_executable(DirectX12Cube WIN32
    src/main.cpp
    src/camera.cpp
    src/d3d_context.cpp
    src/frame_buffers.cpp
    src/passes/scene_pass.cpp
    src/passes/downsample_pass.cpp
    src/renderer.cpp
)
```

- [ ] **Step 4: Build to verify the new module compiles**

Run: `cmake --preset debug` then `cmake --build --preset debug`
Expected: configure + build succeed, zero warnings (`/W4 /WX`). The module
is not referenced yet — this only proves it compiles standalone.

**No commit (user instruction: do not commit anything).**

---

### Task 2: Enable depth in ScenePass

**Files:**
- Modify: `src/passes/scene_pass.h`
- Modify: `src/passes/scene_pass.cpp:55-71` (PSO) and `:163-185` (`DrawScene`)

- [ ] **Step 1: Add the DSV setter and member to `src/passes/scene_pass.h`**

Add after the `ExecuteToTarget` declaration (public section):

```cpp
    void SetDepthStencilView(D3D12_CPU_DESCRIPTOR_HANDLE dsv) { m_dsvHandle = dsv; }
```

Add at the end of the private members:

```cpp
    D3D12_CPU_DESCRIPTOR_HANDLE m_dsvHandle = {};
```

- [ ] **Step 2: Enable depth in the PSO (`src/passes/scene_pass.cpp`)**

Replace the existing `dsDesc` block (currently `DepthEnable = FALSE`):

```cpp
    D3D12_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable    = TRUE;
    dsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    dsDesc.DepthFunc      = D3D12_COMPARISON_FUNC_LESS;
    dsDesc.StencilEnable  = FALSE;
```

And in `psoDesc`, after `psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;`
add:

```cpp
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
```

- [ ] **Step 3: Bind and clear the DSV in `DrawScene`**

Replace these two lines in `DrawScene`:

```cpp
    cmd->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
    cmd->ClearRenderTargetView(rtvHandle, kClearColor, 0, nullptr);
```

with:

```cpp
    cmd->OMSetRenderTargets(1, &rtvHandle, FALSE, &m_dsvHandle);
    cmd->ClearRenderTargetView(rtvHandle, kClearColor, 0, nullptr);
    cmd->ClearDepthStencilView(m_dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
```

- [ ] **Step 4: Build to verify**

Run: `cmake --build --preset debug`
Expected: clean build. (Running the exe now would crash or trip the debug
layer — `m_dsvHandle` is still null until Task 3 wires it. Do not run yet.)

**No commit (user instruction: do not commit anything).**

---

### Task 3: Wire FrameBuffers in Renderer

**Files:**
- Modify: `src/renderer.h`
- Modify: `src/renderer.cpp:3-15` (`Initialize`)

- [ ] **Step 1: Add the member to `src/renderer.h`**

Add the include after `#include "camera.h"`:

```cpp
#include "frame_buffers.h"
```

Add the member after `Camera m_camera;`:

```cpp
    FrameBuffers m_frameBuffers;
```

- [ ] **Step 2: Create FrameBuffers at render resolution in `Renderer::Initialize`**

Replace the body of `Renderer::Initialize` in `src/renderer.cpp` with:

```cpp
void Renderer::Initialize(HWND hwnd, UINT width, UINT height)
{
    m_hwnd = hwnd;
    m_ctx.Initialize(hwnd, width, height);

    const std::wstring shaderPath = GetShaderPath();
    m_scenePass.Initialize(m_ctx, shaderPath);

#ifdef ENABLE_SUPERSAMPLING
    m_downsamplePass = std::make_unique<DownsamplePass>();
    m_downsamplePass->Initialize(m_ctx, shaderPath);
    m_frameBuffers.Initialize(m_ctx, DownsamplePass::SSWidth, DownsamplePass::SSHeight);
#else
    m_frameBuffers.Initialize(m_ctx, width, height);
#endif
    m_scenePass.SetDepthStencilView(m_frameBuffers.GetDSV());
}
```

`Renderer::Render` needs no changes — the scene pass binds the stored DSV
internally in both the SSAA and direct paths.

- [ ] **Step 3: Build to verify**

Run: `cmake --build --preset debug`
Expected: clean build, zero warnings.

**No commit (user instruction: do not commit anything).**

---

### Task 4: Verify both presets build and run

- [ ] **Step 1: Build the non-SSAA debug preset**

Run: `cmake --preset debug; cmake --build --preset debug`
Expected: clean build.

- [ ] **Step 2: Run the non-SSAA exe**

Run: `.\build\debug\DirectX12Cube.exe`
Expected: window opens at 800×450, cube renders exactly as before (colors,
background `kClearColor`), WASD/mouse camera works, no crash, clean exit.

- [ ] **Step 3: Build the SSAA debug preset**

Run: `cmake --preset debug-ssaa; cmake --build --preset debug-ssaa`
Expected: clean build.

- [ ] **Step 4: Run the SSAA exe**

Run: `.\build\debug-ssaa\DirectX12Cube.exe`
Expected: same as Step 2 but via the 1600×900 → 800×450 downsample path;
edges look antialiased as before. The depth buffer here is 1600×900,
matching the offscreen RT.

- [ ] **Step 5: Confirm nothing was committed**

Run: `git log --oneline -1; git status --short`
Expected: HEAD is still `0c0e376 launch and CLAUDE.md`; new/modified files
(`src/frame_buffers.*`, `scene_pass.*`, `renderer.*`, `CMakeLists.txt`,
docs) appear as uncommitted working-tree changes.

**No commit (user instruction: do not commit anything).**
