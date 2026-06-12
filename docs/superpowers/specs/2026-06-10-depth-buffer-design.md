# Depth Buffer via FrameBuffers Module — Design

**Date:** 2026-06-10
**Status:** Approved

## Goal

Add a depth buffer to the D3D12 renderer, with depth testing enabled in the
scene pass. The buffer is the first of several render-resolution buffers
(normals, motion vectors, ...) that will later be captured as a dataset for
SSAA-targeting work, so it lives in a new module designed to host the future
buffers too.

## Context

- The scene pass currently renders with `DepthEnable = FALSE`
  (`src/passes/scene_pass.cpp`); the cube only looks correct because of
  backface culling. There is no depth resource or DSV heap anywhere.
- With SSAA enabled, the scene renders into a 1600×900 offscreen RT owned by
  `DownsamplePass`; without SSAA it renders directly to the 800×450 swapchain
  RT. The depth buffer must match whichever render resolution is active.

## Design

### 1. New module: `src/frame_buffers.h/.cpp`

A `FrameBuffers` class owning all render-resolution per-frame buffers,
starting with depth only:

- `void Initialize(D3DContext& ctx, UINT width, UINT height)` — creates a
  DSV descriptor heap (1 descriptor for now) and the depth resource.
- Depth resource: `DXGI_FORMAT_R32_TYPELESS`, flag
  `D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL`, committed in a default heap,
  initial state `D3D12_RESOURCE_STATE_DEPTH_WRITE`, clear value
  `D3D12_CLEAR_VALUE{ DXGI_FORMAT_D32_FLOAT, depth = 1.0f }`.
  The DSV views it as `DXGI_FORMAT_D32_FLOAT`.
  Typeless creation is deliberate: a future dataset-capture step can create
  an `R32_FLOAT` SRV or copy the resource out without recreating it.
- Accessors: `D3D12_CPU_DESCRIPTOR_HANDLE GetDSV() const`,
  `ID3D12Resource* GetDepthResource() const`.
- Future buffers (normals, motion vectors, ...) are added here as new
  resources + accessors, keeping a single place for dataset capture to read.
- No per-frame state transitions: the resource stays in `DEPTH_WRITE` until
  capture is implemented later.

### 2. Renderer wiring (`src/renderer.h/.cpp`)

- `Renderer` gains a `FrameBuffers m_frameBuffers` member.
- `Renderer::Initialize` creates it at **render resolution**:
  `DownsamplePass::SSWidth/SSHeight` (1600×900) when SSAA is on,
  `ctx.width/height` (800×450) otherwise.
- `Renderer::Initialize` hands the DSV to the scene pass once via
  `m_scenePass.SetDepthStencilView(m_frameBuffers.GetDSV())`. The handle is
  stable for the app's lifetime (the DSV heap never moves), so per-call
  parameters are unnecessary — and `ScenePass::Execute` is an `override` of
  `RenderPass::Execute(D3DContext&, cmd)`, whose signature should not change
  for a scene-pass-only need.

### 3. ScenePass changes (`src/passes/scene_pass.h/.cpp`)

- PSO: `DepthEnable = TRUE`, `DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL`,
  `DepthFunc = D3D12_COMPARISON_FUNC_LESS`,
  `psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT`.
- New `void SetDepthStencilView(D3D12_CPU_DESCRIPTOR_HANDLE dsv)` stores the
  handle in a `m_dsvHandle` member; `Execute`/`ExecuteToTarget`/`DrawScene`
  signatures are unchanged.
- Each frame in `DrawScene`: `OMSetRenderTargets(1, &rtv, FALSE, &m_dsvHandle)`
  and `ClearDepthStencilView(m_dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0,
  nullptr)`.

### 4. Out of scope (YAGNI)

- No readback / dataset-capture code.
- No SRV creation for the depth buffer.
- No normals / motion-vector buffers yet.
- No shader changes (`src/shaders.hlsl` untouched).
- No window-resize handling (the app does not handle resize today).

## Error handling

All new D3D12 calls wrapped in `ThrowIfFailed`, consistent with the rest of
the codebase; exceptions bubble to `WinMain`.

## Testing / acceptance

No automated test harness exists. Acceptance:

1. `cmake --build --preset debug` and `cmake --build --preset debug-ssaa`
   both build clean under `/W4 /WX`.
2. Both executables run and render the cube visually identical to today
   (single convex cube, so enabling depth test changes no pixels).
3. PIX/debug-layer clean: no D3D12 debug-layer errors about missing DSV or
   mismatched PSO `DSVFormat`.

## Process note

Per user instruction, nothing in this work is committed to git — files are
left in the working tree only.
