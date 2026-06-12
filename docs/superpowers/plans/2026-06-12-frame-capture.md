# Frame Capture for Dataset Generation — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Press **C** to capture the current frame and write three pixel-aligned 800×450 outputs — anti-aliased PNG (`_ssaa`), aliased PNG (`_aliased`), and float32 depth (`_depth.npy`) — for a DLAA training dataset.

**Architecture:** Supersampling becomes the single always-on path (the `-ssaa` presets and the `ENABLE_SUPERSAMPLING` flag are removed). A new `FrameCapture` module owns capture-only resources (a native 800×450 color RT, a native 800×450 depth buffer, three READBACK buffers) and the file writers. On a capture frame the `Renderer` records, into the live command list, a copy of the finished swapchain image plus a second native 1-spp render of the cube; after the loop's existing GPU wait it reads back the three buffers and writes files.

**Tech Stack:** C++17, D3D12, `stb_image_write.h` (vendored single-header) for PNG, hand-written NumPy `.npy` writer for depth.

---

## Project conventions (read first)

- **You build and run, not the agent.** This project's owner always performs builds/runs themselves. Every "verify" step below is a command **for the human to run** and confirm; the agent must not run builds or launch the app.
- **Commits are optional.** This project keeps work in the working tree, uncommitted, by default. Each task ends with a commit command — run it only if you have opted into committing. Otherwise skip it.
- **Warnings are errors:** MSVC `/W4 /WX`. New code must be warning-clean.
- Build commands: `cmake --preset debug` (configure) then `cmake --build --preset debug`. Run: `.\build\debug\DirectX12Cube.exe`.

## File structure

| File | Responsibility | Action |
|------|----------------|--------|
| `CMakeLists.txt` | Remove SSAA flag; add capture source + third-party include | Modify |
| `CMakePresets.json` | Remove `debug-ssaa` / `release-ssaa` presets | Modify |
| `src/third_party/stb_image_write.h` | Vendored PNG encoder | Create |
| `src/passes/scene_pass.h/.cpp` | Add `DrawTo(rtv, dsv, w, h)` for an explicit depth target | Modify |
| `src/frame_capture.h/.cpp` | All capture state + readback + PNG/.npy writers | Create |
| `src/renderer.h/.cpp` | Always-SSAA; own `FrameCapture`; trigger + orchestration | Modify |

---

## Task 1: Remove the SSAA build option and presets

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `CMakePresets.json`

- [ ] **Step 1: Drop the `ENABLE_SUPERSAMPLING` option and its define**

In `CMakeLists.txt`, delete these lines (the `option(...)` near the top and the `if(ENABLE_SUPERSAMPLING) ... endif()` block):

```cmake
option(ENABLE_SUPERSAMPLING "Enable 2x supersampling (renders at 1600x900, downsamples to 800x450)" OFF)
```

```cmake
if(ENABLE_SUPERSAMPLING)
    target_compile_definitions(DirectX12Cube PRIVATE ENABLE_SUPERSAMPLING)
endif()
```

The `target_compile_definitions(DirectX12Cube PRIVATE WIN32_LEAN_AND_MEAN UNICODE)` line stays.

- [ ] **Step 2: Remove the `-ssaa` presets**

In `CMakePresets.json`, delete the two configure presets named `debug-ssaa` and `release-ssaa` (objects on lines ~31–46) and the two build presets named `debug-ssaa` and `release-ssaa` (objects on lines ~57–64). Keep `base`, `debug`, `release` configure presets and `debug`, `release` build presets. Ensure the JSON has no trailing commas after the last array elements.

- [ ] **Step 3: Verify (you run)**

```
cmake --preset debug
cmake --build --preset debug
```

Expected: configures and builds clean. The cube still renders (SSAA is now wired unconditionally by Task 2 — at this point the code still has `#ifdef`s, but with the flag gone they compile as the non-SSAA branch; that's fine, the build just needs to succeed). `cmake --preset debug-ssaa` should now error with "no such preset".

- [ ] **Step 4: Commit (optional)**

```
git add CMakeLists.txt CMakePresets.json
git commit -m "build: remove SSAA toggle, make supersampling the only path"
```

---

## Task 2: Make the renderer always run the SSAA path

**Files:**
- Modify: `src/renderer.h`
- Modify: `src/renderer.cpp`

- [ ] **Step 1: Make `DownsamplePass` a plain member**

In `src/renderer.h`, replace the `m_downsamplePass` member and drop the now-unneeded optionality. The member block becomes:

```cpp
    HWND        m_hwnd = nullptr;
    D3DContext  m_ctx;
    Camera       m_camera;
    FrameBuffers m_frameBuffers;
    ScenePass    m_scenePass;
    DownsamplePass m_downsamplePass;
```

`#include <memory>` may now be unused; leave it (harmless) or remove it — your call.

- [ ] **Step 2: Always initialize the SSAA chain**

In `src/renderer.cpp`, replace the body of `Renderer::Initialize` (the `#ifdef ENABLE_SUPERSAMPLING` block) with:

```cpp
void Renderer::Initialize(HWND hwnd, UINT width, UINT height)
{
    m_hwnd = hwnd;
    m_ctx.Initialize(hwnd, width, height);

    const std::wstring shaderPath = GetShaderPath();
    m_scenePass.Initialize(m_ctx, shaderPath);

    m_downsamplePass.Initialize(m_ctx, shaderPath);
    m_frameBuffers.Initialize(m_ctx, DownsamplePass::SSWidth, DownsamplePass::SSHeight);
    m_scenePass.SetDepthStencilView(m_frameBuffers.GetDSV());
}
```

- [ ] **Step 3: Simplify `Update` and `Render`**

Replace `Renderer::Update` with:

```cpp
void Renderer::Update()
{
    m_camera.Update(m_hwnd);
    m_scenePass.UpdateConstants(m_camera,
        DownsamplePass::SSWidth, DownsamplePass::SSHeight);
}
```

Replace the `if (m_downsamplePass) { ... } else { ... }` block inside `Renderer::Render` with the unconditional SSAA path:

```cpp
    m_scenePass.ExecuteToTarget(cmd,
        m_downsamplePass.GetOffscreenRTV(),
        DownsamplePass::SSWidth, DownsamplePass::SSHeight);
    m_downsamplePass.Execute(m_ctx, cmd);
```

- [ ] **Step 4: Verify (you run)**

```
cmake --build --preset debug
.\build\debug\DirectX12Cube.exe
```

Expected: builds clean under `/W4 /WX`; the rotating cube renders exactly as before (anti-aliased via the always-on SSAA path).

- [ ] **Step 5: Commit (optional)**

```
git add src/renderer.h src/renderer.cpp
git commit -m "refactor: drop SSAA conditional, downsample pass is unconditional"
```

---

## Task 3: Add `ScenePass::DrawTo` for an explicit depth target

**Files:**
- Modify: `src/passes/scene_pass.h`
- Modify: `src/passes/scene_pass.cpp`

The capture's native pass renders into a *different* depth buffer than the display DSV, so `DrawScene` must accept a DSV parameter instead of reading the member.

- [ ] **Step 1: Update the header**

In `src/passes/scene_pass.h`, add a public `DrawTo` and change the private `DrawScene` signature:

```cpp
    void ExecuteToTarget(ID3D12GraphicsCommandList* cmd,
                         D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle, UINT w, UINT h);

    // Render the cube to an explicit render target + depth target (used by capture).
    void DrawTo(ID3D12GraphicsCommandList* cmd,
                D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
                D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle, UINT w, UINT h);

    void SetDepthStencilView(D3D12_CPU_DESCRIPTOR_HANDLE dsv) { m_dsvHandle = dsv; }

private:
    void DrawScene(ID3D12GraphicsCommandList* cmd,
                   D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
                   D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle, UINT w, UINT h);
```

- [ ] **Step 2: Thread the DSV through `DrawScene`**

In `src/passes/scene_pass.cpp`, change the `DrawScene` definition to take `dsvHandle` and use it (replace the two spots that referenced `m_dsvHandle`):

```cpp
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
```

- [ ] **Step 3: Update the two callers and add `DrawTo`**

In `ScenePass::Execute`, change the `DrawScene(...)` call to pass the member DSV:

```cpp
    DrawScene(cmd, rtvHandle, m_dsvHandle, ctx.width, ctx.height);
```

In `ScenePass::ExecuteToTarget`, change its `DrawScene(...)` call likewise:

```cpp
    DrawScene(cmd, rtvHandle, m_dsvHandle, w, h);
```

Then add the new method at the end of the file:

```cpp
void ScenePass::DrawTo(ID3D12GraphicsCommandList* cmd,
                       D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
                       D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle, UINT w, UINT h)
{
    DrawScene(cmd, rtvHandle, dsvHandle, w, h);
}
```

- [ ] **Step 4: Verify (you run)**

```
cmake --build --preset debug
.\build\debug\DirectX12Cube.exe
```

Expected: builds clean; cube renders identically (the new method is unused so far).

- [ ] **Step 5: Commit (optional)**

```
git add src/passes/scene_pass.h src/passes/scene_pass.cpp
git commit -m "feat: ScenePass::DrawTo renders to an explicit RTV+DSV"
```

---

## Task 4: Vendor `stb_image_write.h` and register the capture source in CMake

**Files:**
- Create: `src/third_party/stb_image_write.h`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Vendor the header (you run)**

```
New-Item -ItemType Directory -Force src/third_party | Out-Null
Invoke-WebRequest -Uri "https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h" -OutFile "src/third_party/stb_image_write.h"
```

Expected: `src/third_party/stb_image_write.h` exists (~70 KB). (Public domain / MIT — vendoring is fine.)

- [ ] **Step 2: Add the source file and include dir to CMake**

In `CMakeLists.txt`, add `src/frame_capture.cpp` to the `add_executable` source list (place it after `src/d3d_context.cpp`):

```cmake
add_executable(DirectX12Cube WIN32
    src/main.cpp
    src/camera.cpp
    src/d3d_context.cpp
    src/frame_capture.cpp
    src/frame_buffers.cpp
    src/passes/scene_pass.cpp
    src/passes/downsample_pass.cpp
    src/renderer.cpp
)
```

The existing `target_include_directories(DirectX12Cube PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)` already covers `src/third_party`, so `#include "third_party/stb_image_write.h"` will resolve. No change needed there.

> Note: `frame_capture.cpp` does not exist yet, so do **not** build after this step — Task 5 creates the file. Configure can be re-run now (`cmake --preset debug`) but the build will fail until Task 5.

- [ ] **Step 3: Commit (optional)**

```
git add src/third_party/stb_image_write.h CMakeLists.txt
git commit -m "build: vendor stb_image_write, register frame_capture.cpp"
```

---

## Task 5: Create the `FrameCapture` module

**Files:**
- Create: `src/frame_capture.h`
- Create: `src/frame_capture.cpp`

This task creates the whole module. After it, the project builds again, but `FrameCapture` is still unused (Task 6 wires it in).

- [ ] **Step 1: Write the header**

Create `src/frame_capture.h`:

```cpp
#pragma once
#include "d3d_helpers.h"

struct D3DContext;
class ScenePass;

// Owns all frame-capture state: a native-resolution (800x450) color RT and
// depth buffer for the aliased pass, three READBACK buffers, and the file
// writers. Capture is requested with C; one frame is written per press.
class FrameCapture
{
public:
    static constexpr UINT Width  = 800;
    static constexpr UINT Height = 450;

    void Initialize(D3DContext& ctx);

    void RequestCapture()  { m_pending = true; }
    bool IsPending() const { return m_pending; }

    // Records (into the live command list) the swapchain copy + the native
    // aliased render + the two readback copies. Call after DownsamplePass and
    // before cmd->Close(), only when IsPending() is true.
    void RecordCapture(D3DContext& ctx, ID3D12GraphicsCommandList* cmd,
                       ScenePass& scenePass);

    // After the GPU wait: maps the readback buffers, writes the 3 files,
    // increments the frame counter, clears the pending flag.
    void WriteToDisk();

private:
    D3D12_CPU_DESCRIPTOR_HANDLE AliasedRTV() const;
    D3D12_CPU_DESCRIPTOR_HANDLE CaptureDSV() const;

    ComPtr<ID3D12Resource>       m_aliasedRT;
    ComPtr<ID3D12DescriptorHeap> m_aliasedRTVHeap;
    ComPtr<ID3D12Resource>       m_captureDepth;
    ComPtr<ID3D12DescriptorHeap> m_captureDSVHeap;

    ComPtr<ID3D12Resource> m_readbackSSAA;     // swapchain AA copy (RGBA8)
    ComPtr<ID3D12Resource> m_readbackAliased;  // native aliased    (RGBA8)
    ComPtr<ID3D12Resource> m_readbackDepth;    // native depth      (float32)

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT m_colorLayout = {};  // RGBA8 footprint
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT m_depthLayout = {};  // R32_FLOAT footprint

    bool m_pending    = false;
    UINT m_frameIndex = 0;
};
```

- [ ] **Step 2: Write the resource setup (`Initialize`) and accessors**

Create `src/frame_capture.cpp` starting with includes, the stb implementation (warning-suppressed for `/W4 /WX`), and `Initialize`:

```cpp
#define _CRT_SECURE_NO_WARNINGS
#include "frame_capture.h"
#include "d3d_context.h"
#include "passes/scene_pass.h"

#include <vector>
#include <string>
#include <fstream>
#include <cstdint>
#include <filesystem>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#pragma warning(push, 0)
#include "third_party/stb_image_write.h"
#pragma warning(pop)

namespace
{
    D3D12_HEAP_PROPERTIES HeapProps(D3D12_HEAP_TYPE type)
    {
        D3D12_HEAP_PROPERTIES h = {};
        h.Type                 = type;
        h.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        h.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        h.CreationNodeMask     = 1;
        h.VisibleNodeMask      = 1;
        return h;
    }
}

void FrameCapture::Initialize(D3DContext& ctx)
{
    const D3D12_HEAP_PROPERTIES defaultHeap  = HeapProps(D3D12_HEAP_TYPE_DEFAULT);
    const D3D12_HEAP_PROPERTIES readbackHeap = HeapProps(D3D12_HEAP_TYPE_READBACK);

    // --- Aliased color RT (800x450 RGBA8) ---
    D3D12_RESOURCE_DESC colorDesc = {};
    colorDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    colorDesc.Width            = Width;
    colorDesc.Height           = Height;
    colorDesc.DepthOrArraySize = 1;
    colorDesc.MipLevels        = 1;
    colorDesc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
    colorDesc.SampleDesc.Count = 1;
    colorDesc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    colorDesc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE colorClear = {};
    colorClear.Format   = DXGI_FORMAT_R8G8B8A8_UNORM;
    colorClear.Color[0] = kClearColor[0]; colorClear.Color[1] = kClearColor[1];
    colorClear.Color[2] = kClearColor[2]; colorClear.Color[3] = kClearColor[3];

    ThrowIfFailed(ctx.device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE,
        &colorDesc, D3D12_RESOURCE_STATE_RENDER_TARGET, &colorClear,
        IID_PPV_ARGS(&m_aliasedRT)));

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = 1;
    rtvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    ThrowIfFailed(ctx.device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_aliasedRTVHeap)));
    ctx.device->CreateRenderTargetView(m_aliasedRT.Get(), nullptr,
        m_aliasedRTVHeap->GetCPUDescriptorHandleForHeapStart());

    // --- Native depth (800x450, R32_TYPELESS viewed as D32_FLOAT) ---
    D3D12_RESOURCE_DESC depthDesc = {};
    depthDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Width            = Width;
    depthDesc.Height           = Height;
    depthDesc.DepthOrArraySize = 1;
    depthDesc.MipLevels        = 1;
    depthDesc.Format           = DXGI_FORMAT_R32_TYPELESS;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    depthDesc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE depthClear = {};
    depthClear.Format               = DXGI_FORMAT_D32_FLOAT;
    depthClear.DepthStencil.Depth   = 1.0f;
    depthClear.DepthStencil.Stencil = 0;

    ThrowIfFailed(ctx.device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE,
        &depthDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClear,
        IID_PPV_ARGS(&m_captureDepth)));

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    ThrowIfFailed(ctx.device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_captureDSVHeap)));

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format        = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    ctx.device->CreateDepthStencilView(m_captureDepth.Get(), &dsvDesc,
        m_captureDSVHeap->GetCPUDescriptorHandleForHeapStart());

    // --- Copyable footprints + readback buffers ---
    UINT64 colorTotal = 0;
    ctx.device->GetCopyableFootprints(&colorDesc, 0, 1, 0,
        &m_colorLayout, nullptr, nullptr, &colorTotal);

    // Depth must be read back as a fully-typed format; describe it as R32_FLOAT.
    D3D12_RESOURCE_DESC depthTyped = depthDesc;
    depthTyped.Format = DXGI_FORMAT_R32_FLOAT;
    UINT64 depthTotal = 0;
    ctx.device->GetCopyableFootprints(&depthTyped, 0, 1, 0,
        &m_depthLayout, nullptr, nullptr, &depthTotal);

    auto MakeReadback = [&](UINT64 size, ComPtr<ID3D12Resource>& out) {
        D3D12_RESOURCE_DESC b = {};
        b.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        b.Width            = size;
        b.Height           = 1;
        b.DepthOrArraySize = 1;
        b.MipLevels        = 1;
        b.Format           = DXGI_FORMAT_UNKNOWN;
        b.SampleDesc.Count = 1;
        b.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ThrowIfFailed(ctx.device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE,
            &b, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&out)));
    };
    MakeReadback(colorTotal, m_readbackSSAA);
    MakeReadback(colorTotal, m_readbackAliased);
    MakeReadback(depthTotal, m_readbackDepth);
}

D3D12_CPU_DESCRIPTOR_HANDLE FrameCapture::AliasedRTV() const
{
    return m_aliasedRTVHeap->GetCPUDescriptorHandleForHeapStart();
}

D3D12_CPU_DESCRIPTOR_HANDLE FrameCapture::CaptureDSV() const
{
    return m_captureDSVHeap->GetCPUDescriptorHandleForHeapStart();
}
```

- [ ] **Step 3: Add `RecordCapture`**

Append to `src/frame_capture.cpp`. A small local helper records one texture→readback copy:

```cpp
namespace
{
    void CopyToReadback(ID3D12GraphicsCommandList* cmd,
                        ID3D12Resource* src,
                        ID3D12Resource* dst,
                        const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& layout)
    {
        D3D12_TEXTURE_COPY_LOCATION d = {};
        d.pResource       = dst;
        d.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        d.PlacedFootprint = layout;

        D3D12_TEXTURE_COPY_LOCATION s = {};
        s.pResource        = src;
        s.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        s.SubresourceIndex = 0;

        cmd->CopyTextureRegion(&d, 0, 0, 0, &s, nullptr);
    }
}

void FrameCapture::RecordCapture(D3DContext& ctx, ID3D12GraphicsCommandList* cmd,
                                 ScenePass& scenePass)
{
    ID3D12Resource* swap = ctx.renderTargets[ctx.frameIndex].Get();

    // 1. Swapchain (finished AA image) -> readbackSSAA.
    auto toCopy = TransitionBarrier(swap,
        D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
    cmd->ResourceBarrier(1, &toCopy);
    CopyToReadback(cmd, swap, m_readbackSSAA.Get(), m_colorLayout);
    auto toPresent = TransitionBarrier(swap,
        D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
    cmd->ResourceBarrier(1, &toPresent);

    // 2. Native 1-spp render of the cube -> aliased RT + capture depth.
    scenePass.DrawTo(cmd, AliasedRTV(), CaptureDSV(), Width, Height);

    // 3. Aliased RT -> readbackAliased.
    auto aliasedToCopy = TransitionBarrier(m_aliasedRT.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    cmd->ResourceBarrier(1, &aliasedToCopy);
    CopyToReadback(cmd, m_aliasedRT.Get(), m_readbackAliased.Get(), m_colorLayout);
    auto aliasedToRT = TransitionBarrier(m_aliasedRT.Get(),
        D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmd->ResourceBarrier(1, &aliasedToRT);

    // 4. Capture depth -> readbackDepth.
    auto depthToCopy = TransitionBarrier(m_captureDepth.Get(),
        D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);
    cmd->ResourceBarrier(1, &depthToCopy);
    CopyToReadback(cmd, m_captureDepth.Get(), m_readbackDepth.Get(), m_depthLayout);
    auto depthToWrite = TransitionBarrier(m_captureDepth.Get(),
        D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    cmd->ResourceBarrier(1, &depthToWrite);
}
```

- [ ] **Step 4: Add the file writers and `WriteToDisk`**

Append to `src/frame_capture.cpp`. The `.npy` header is padded so the total header length (10 fixed bytes + dict) is a multiple of 64, per the NumPy format spec:

```cpp
namespace
{
    void WriteNpyFloat(const std::filesystem::path& path,
                       const float* data, UINT w, UINT h)
    {
        std::string dict = "{'descr': '<f4', 'fortran_order': False, 'shape': ("
                         + std::to_string(h) + ", " + std::to_string(w) + "), }";
        // 10 = 6 (magic) + 2 (version) + 2 (header-len field). +1 for trailing '\n'.
        size_t unpadded = 10 + dict.size() + 1;
        size_t padded   = ((unpadded + 63) / 64) * 64;
        dict.append(padded - unpadded, ' ');
        dict.push_back('\n');

        std::ofstream f(path, std::ios::binary);
        if (!f) throw std::runtime_error("cannot open " + path.string());
        const unsigned char magic[8] = { 0x93, 'N','U','M','P','Y', 1, 0 };
        f.write(reinterpret_cast<const char*>(magic), 8);
        const uint16_t headerLen = static_cast<uint16_t>(dict.size());
        f.write(reinterpret_cast<const char*>(&headerLen), 2); // little-endian
        f.write(dict.data(), static_cast<std::streamsize>(dict.size()));
        f.write(reinterpret_cast<const char*>(data),
                static_cast<std::streamsize>(sizeof(float) * w * h));
        if (!f) throw std::runtime_error("write failed: " + path.string());
    }

    // Copy padded readback rows into a tight buffer (rowPitch -> w*bytesPerPixel).
    template <typename T>
    std::vector<T> Unpad(ID3D12Resource* readback, UINT rowPitch,
                         UINT w, UINT h, UINT bytesPerPixel)
    {
        D3D12_RANGE readRange = { 0, static_cast<SIZE_T>(rowPitch) * h };
        void* mapped = nullptr;
        ThrowIfFailed(readback->Map(0, &readRange, &mapped));

        const UINT tightRow = w * bytesPerPixel;
        std::vector<T> out(static_cast<size_t>(w) * h * (bytesPerPixel / sizeof(T)));
        auto* dst = reinterpret_cast<unsigned char*>(out.data());
        const auto* src = static_cast<const unsigned char*>(mapped);
        for (UINT y = 0; y < h; ++y)
            memcpy(dst + static_cast<size_t>(y) * tightRow,
                   src + static_cast<size_t>(y) * rowPitch, tightRow);

        const D3D12_RANGE noWrite = { 0, 0 };
        readback->Unmap(0, &noWrite);
        return out;
    }

    void WritePng(const std::filesystem::path& path,
                  const std::vector<unsigned char>& rgba, UINT w, UINT h)
    {
        if (stbi_write_png(path.string().c_str(),
                static_cast<int>(w), static_cast<int>(h), 4,
                rgba.data(), static_cast<int>(w * 4)) == 0)
            throw std::runtime_error("stbi_write_png failed: " + path.string());
    }
}

void FrameCapture::WriteToDisk()
{
    namespace fs = std::filesystem;
    fs::path dir = "captures";
    fs::create_directories(dir);

    char stem[32];
    snprintf(stem, sizeof(stem), "frame_%04u", m_frameIndex);

    auto ssaa = Unpad<unsigned char>(m_readbackSSAA.Get(),
        m_colorLayout.Footprint.RowPitch, Width, Height, 4);
    WritePng(dir / (std::string(stem) + "_ssaa.png"), ssaa, Width, Height);

    auto aliased = Unpad<unsigned char>(m_readbackAliased.Get(),
        m_colorLayout.Footprint.RowPitch, Width, Height, 4);
    WritePng(dir / (std::string(stem) + "_aliased.png"), aliased, Width, Height);

    auto depth = Unpad<float>(m_readbackDepth.Get(),
        m_depthLayout.Footprint.RowPitch, Width, Height, 4);
    WriteNpyFloat(dir / (std::string(stem) + "_depth.npy"), depth.data(), Width, Height);

    ++m_frameIndex;
    m_pending = false;
}
```

- [ ] **Step 5: Verify (you run)**

```
cmake --build --preset debug
```

Expected: builds clean under `/W4 /WX` (the stb implementation is warning-suppressed via `#pragma warning(push, 0)`). `FrameCapture` is compiled but not yet used.

- [ ] **Step 6: Commit (optional)**

```
git add src/frame_capture.h src/frame_capture.cpp
git commit -m "feat: FrameCapture module (resources, readback, PNG/.npy writers)"
```

---

## Task 6: Wire capture into the renderer and the C key

**Files:**
- Modify: `src/renderer.h`
- Modify: `src/renderer.cpp`

- [ ] **Step 1: Add the member and the key trigger**

In `src/renderer.h`, add the include and member, and handle `'C'` in `OnKeyDown`:

```cpp
#include "frame_capture.h"
```

```cpp
    void OnKeyDown(WPARAM key)
    {
        if (key == VK_ESCAPE) m_camera.DisableCapture();
        if (key == 'C')       m_frameCapture.RequestCapture();
        m_camera.OnKeyDown(key);
    }
```

Add the member next to the others:

```cpp
    DownsamplePass m_downsamplePass;
    FrameCapture   m_frameCapture;
```

- [ ] **Step 2: Initialize the capture module**

In `src/renderer.cpp`, at the end of `Renderer::Initialize`, add:

```cpp
    m_frameCapture.Initialize(m_ctx);
```

- [ ] **Step 3: Orchestrate capture in `Render`**

In `src/renderer.cpp`, update `Renderer::Render` so the capture is recorded after the downsample and written after the GPU wait:

```cpp
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

    if (doCapture)
        m_frameCapture.WriteToDisk();
}
```

- [ ] **Step 4: Verify (you run)**

```
cmake --build --preset debug
.\build\debug\DirectX12Cube.exe
```

Then, in the running app, press **C** once. Expected:
- A `captures\` folder appears next to the exe with `frame_0000_ssaa.png`, `frame_0000_aliased.png`, `frame_0000_depth.npy`.
- `frame_0000_ssaa.png` has smooth cube edges; `frame_0000_aliased.png` shows visible jaggies on the same geometry; both are 800×450.
- Pressing **C** again writes `frame_0001_*`.
- No D3D12 debug-layer errors in the Visual Studio output.

- [ ] **Step 5: Verify the depth file (you run, optional)**

```
python -c "import numpy as np; a=np.load(r'.\build\debug\captures\frame_0000_depth.npy'); print(a.shape, a.dtype, a.min(), a.max())"
```

Expected: `(450, 800) float32 <min> 1.0` — values in `[0, 1]`, background `1.0`, lower on the cube.

- [ ] **Step 6: Commit (optional)**

```
git add src/renderer.h src/renderer.cpp
git commit -m "feat: capture a frame on C (ssaa png, aliased png, depth npy)"
```

---

## Self-review notes

- **Spec coverage:** preset/flag removal (Task 1), always-SSAA renderer (Task 2), `DrawTo` explicit DSV (Task 3), stb vendoring + CMake (Task 4), `FrameCapture` resources/readback/writers (Task 5), trigger + orchestration + file output (Task 6). All spec sections (§1–§6) are covered.
- **Depth alignment:** the captured depth is the native 800×450 pass's depth, pixel-aligned with the aliased input, as specified.
- **Type consistency:** `m_colorLayout` / `m_depthLayout`, `AliasedRTV()` / `CaptureDSV()`, `RecordCapture` / `WriteToDisk`, `Width`/`Height` are used identically across header and cpp and across Tasks 5–6.
- **/W4 /WX:** the only third-party warnings come from stb, suppressed with `#pragma warning(push, 0)` + `_CRT_SECURE_NO_WARNINGS`.
- **Known constraint:** capture does a synchronous GPU wait + CPU write on the capture frame (a one-frame hitch). Acceptable for single-frame capture; the spec notes async/ring buffering as a future step for sequences.
```
