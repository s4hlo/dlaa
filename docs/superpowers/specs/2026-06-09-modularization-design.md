# Modularization Design — DirectX12Cube

**Date:** 2026-06-09  
**Goal:** Split `src/main.cpp` into focused modules. Improve readability and make it easy to add new rendering techniques (depth, lighting, textures, post-processing) without bloating a single file.

---

## File Structure

```
src/
  d3d_helpers.h                    — ThrowIfFailed, TransitionBarrier, Vertex, ConstantBufferData
  camera.h / camera.cpp            — camera state, Update(float dt), OnKeyDown/Up
  d3d_context.h / d3d_context.cpp  — device, queue, swapchain, fence, command allocators, render targets
  passes/
    render_pass.h                  — pure virtual base: Execute(D3DContext&, ID3D12GraphicsCommandList*)
    scene_pass.h / scene_pass.cpp  — cube root sig, PSO, vertex/index/constant buffer, draw
    downsample_pass.h / .cpp       — quad root sig, PSO, offscreen RT + SRV, blit to swapchain
  renderer.h / renderer.cpp        — owns D3DContext + pass list; drives Update/Render loop
  main.cpp                         — WinMain, WindowProc only — no D3D12 logic
```

---

## Component Responsibilities

### `d3d_helpers.h`
Inline utilities and shared types used across all modules. No .cpp needed.
- `ThrowIfFailed(HRESULT)`
- `TransitionBarrier(resource, before, after) -> D3D12_RESOURCE_BARRIER`
- `struct Vertex { XMFLOAT3 position; XMFLOAT4 color; }`
- `struct alignas(256) ConstantBufferData { XMFLOAT4X4 mvp; }`
- `constexpr UINT FrameCount = 2`

### `Camera` (camera.h / camera.cpp)
Pure logic, zero D3D12 dependency.
- Fields: position, yaw, pitch, movement flags, speed, sensitivity
- `void Update(float deltaTime, HWND hwnd, bool mouseCapture)`
- `XMMATRIX GetViewMatrix() const`
- `void OnKeyDown(WPARAM) / OnKeyUp(WPARAM)`
- `void EnableCapture(HWND) / DisableCapture()`

### `D3DContext` (d3d_context.h / d3d_context.cpp)
Created once in `Renderer::Initialize`. Passed by reference to passes.
```cpp
struct D3DContext {
    ComPtr<ID3D12Device>              device;
    ComPtr<ID3D12CommandQueue>        commandQueue;
    ComPtr<IDXGISwapChain3>           swapChain;
    ComPtr<ID3D12GraphicsCommandList> commandList;
    ComPtr<ID3D12CommandAllocator>    commandAllocators[FrameCount];
    ComPtr<ID3D12DescriptorHeap>      rtvHeap;
    ComPtr<ID3D12Resource>            renderTargets[FrameCount];
    ComPtr<ID3D12Fence>               fence;
    HANDLE                            fenceEvent;
    UINT64                            fenceValue;
    UINT                              frameIndex;
    UINT                              rtvDescriptorSize;
    UINT                              width, height;
};
```
- `void Initialize(HWND, UINT width, UINT height)`
- `void WaitForPreviousFrame()`

### `RenderPass` (passes/render_pass.h)
```cpp
struct RenderPass {
    virtual void Execute(D3DContext& ctx, ID3D12GraphicsCommandList* cmd) = 0;
    virtual ~RenderPass() = default;
};
```

### `ScenePass` (passes/scene_pass.h / .cpp)
Owns: cube root signature, PSO, vertex buffer, index buffer, constant buffer, CBV heap.  
- `void Initialize(D3DContext& ctx)`
- `void UpdateConstants(const Camera& camera, UINT width, UINT height)`
- `void Execute(D3DContext& ctx, ID3D12GraphicsCommandList* cmd)` — optionally takes an explicit RTV handle to support rendering to an offscreen target (needed by DownsamplePass)

### `DownsamplePass` (passes/downsample_pass.h / .cpp)
Owns: offscreen RT (SSWidth×SSHeight), offscreen RTV heap, SRV heap, downsample root sig, downsample PSO.  
- `void Initialize(D3DContext& ctx, const std::wstring& shaderPath)`
- `ID3D12Resource* GetOffscreenRT()` — ScenePass renders into this
- `D3D12_CPU_DESCRIPTOR_HANDLE GetOffscreenRTV()`
- `void Execute(D3DContext& ctx, ID3D12GraphicsCommandList* cmd)` — transitions RT→SRV, blits to swapchain, transitions back

### `Renderer` (renderer.h / renderer.cpp)
Owns `D3DContext` and the pass list. Compiles shaders, wires passes together.
- `void Initialize(HWND, UINT w, UINT h)`
- `void Update(Camera& camera)` — calls camera.Update, calls ScenePass::UpdateConstants
- `void Render()` — resets command list, calls Execute on each pass in order, submits, presents, waits

### `main.cpp`
Only: `WinMain`, `WindowProc`, `g_app` global. Forwards all input to `Renderer`/`Camera`. No D3D12 includes.

---

## SSAA

`ENABLE_SUPERSAMPLING` CMake option is kept. When ON, `Renderer::Initialize` creates `DownsamplePass` and inserts it after `ScenePass`. The `#ifdef` blocks in rendering logic are gone — replaced by the presence or absence of the pass in the list.

---

## CMakeLists.txt

Add all new `.cpp` files to `add_executable`. No other changes needed.
