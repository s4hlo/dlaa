# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

Prerequisites: Windows 10/11 SDK, Visual Studio (MSVC), Ninja.

```powershell
# Configure
cmake --preset debug          # or: release, debug-ssaa, release-ssaa

# Build
cmake --build --preset debug  # or matching preset

# Run
.\build\debug\DirectX12Cube.exe
```

The `debug-ssaa` / `release-ssaa` presets set `ENABLE_SUPERSAMPLING=ON`, which activates the 2× SSAA path (renders at 1600×900, downsamples to 800×450). MSVC `/W4 /WX` is enforced — all warnings are errors.

Shaders (`src/shaders.hlsl`) are compiled at runtime via `D3DCompileFromFile`; the file is copied to the binary directory by CMake. There are no offline shader compilation steps.

## Architecture

```
main.cpp        — WinMain + WindowProc only; no D3D12 logic
renderer.h/.cpp — owns D3DContext + passes; drives Update/Render
d3d_context.h/.cpp — raw D3D12 device, queue, swapchain, fence, command allocators, RTV heap
d3d_helpers.h   — ThrowIfFailed, TransitionBarrier, Vertex, ConstantBufferData, FrameCount
camera.h/.cpp   — WASD + mouse-capture camera; zero D3D12 dependency
passes/
  render_pass.h           — pure virtual base: Execute(D3DContext&, cmd*)
  scene_pass.h/.cpp       — cube: root sig, PSO, VB/IB/CB; can render to an explicit RTV for SSAA
  downsample_pass.h/.cpp  — offscreen RT (SS resolution) + SRV heap + fullscreen blit to swapchain
```

### Key patterns

- **D3DContext** is created once in `Renderer::Initialize` and passed by reference to every pass. It holds all shared D3D12 state (device, queue, swapchain, fence, per-frame allocators, RTV heap).
- **RenderPass** is the extension point for new techniques. Add a `*.h/.cpp` pair under `src/passes/`, inherit `RenderPass`, implement `Execute`, and register it in `Renderer`.
- **SSAA path**: when `m_downsamplePass` is non-null, `ScenePass::ExecuteToTarget` renders into `DownsamplePass`'s offscreen RT, then `DownsamplePass::Execute` blits it to the swapchain RT. Without SSAA, `ScenePass::Execute` writes directly to the swapchain RT.
- **Error handling**: all D3D12 calls are wrapped in `ThrowIfFailed`; exceptions bubble to `WinMain` which shows a `MessageBox`.
- **Double buffering**: `FrameCount = 2`; `D3DContext::WaitForPreviousFrame()` synchronises the CPU before reusing a frame's command allocator.

### Shaders

`src/shaders.hlsl` contains all shaders in one file:
- `VSMain` / `PSMain` — scene vertex/pixel shaders (MVP transform + vertex color)
- `QuadVS` / `QuadPS` — fullscreen-triangle downsample shaders (used by `DownsamplePass`)
