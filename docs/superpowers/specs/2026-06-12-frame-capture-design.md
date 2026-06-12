# Frame Capture for Dataset Generation — Design

**Date:** 2026-06-12
**Status:** Approved

## Goal

Capture a single rendered frame on demand and write three pixel-aligned
800×450 outputs to disk, as the first step toward building a dataset for
DLAA (deep-learning anti-aliasing) training:

1. **SSAA color** (`*_ssaa.png`) — the anti-aliased target: the existing
   supersampled 1600×900 render downsampled to 800×450.
2. **Aliased color** (`*_aliased.png`) — the training input: a native
   800×450 render at 1 sample per pixel.
3. **Depth** (`*_depth.npy`) — float32 depth of the native (aliased) render,
   aligned pixel-for-pixel with the aliased input.

The capture is triggered interactively by pressing **C**, so the camera can
frame the shot first. The module is built so that capturing a **sequence of
frames** and adding **more buffers** (normals, motion vectors, ...) are
natural extensions later.

## Context

- `dlaa-env` renders a single rotating cube. With supersampling the scene
  pass renders into a 1600×900 offscreen RT owned by `DownsamplePass`, which
  then blits the downsampled 800×450 result to the swapchain. A depth buffer
  at render resolution already exists via `FrameBuffers` (see the
  2026-06-10 depth-buffer design).
- The render loop (`main.cpp`) already routes keyboard input through
  `Renderer::OnKeyDown(WPARAM)`, and already performs a GPU wait
  (`D3DContext::WaitForPreviousFrame`) after every `Present`. Capture readback
  can piggyback on that existing wait.
- The MVP constant buffer uses a 16:9 aspect ratio, identical at 1600×900 and
  800×450, so the native capture pass reuses the same constants as the live
  render — no camera or constant changes needed.

## Decisions (from brainstorming)

- **Aliased input is rendered separately** (render-twice): the "raw" image is
  a true 1-spp native render, not the anti-aliased downsample. This is the
  standard aliased→anti-aliased training pair.
- **All three outputs at 800×450**, pixel-aligned (SSAA target saved as the
  downsampled image, not the full 1600×900 buffer).
- **Trigger:** keypress `C`, one frame per press.
- **Formats:** color → PNG (inspectable, loads in PIL); depth → NumPy `.npy`
  float32 (full precision, `np.load`-ready).
- **Supersampling is always on.** The `ENABLE_SUPERSAMPLING` option and the
  `debug-ssaa` / `release-ssaa` presets are removed; the SSAA pipeline becomes
  the single code path. This simplifies `Renderer` (no `#ifdef`, no optional
  `DownsamplePass`) and does not reduce capture capability, since the aliased
  input is its own native pass regardless.

## Design

### 1. Build / preset simplification

- `CMakeLists.txt`: remove the `ENABLE_SUPERSAMPLING` `option(...)` and the
  associated `target_compile_definitions`. No SSAA compile flag remains.
- `CMakePresets.json`: remove the `debug-ssaa` and `release-ssaa` configure
  and build presets. Only `debug` and `release` remain.
- Add `frame_capture.cpp` to the `add_executable` source list, and the
  third-party `stb_image_write.h` to the include path (vendored under
  `src/third_party/`).

### 2. Renderer simplification (`src/renderer.h/.cpp`)

- `DownsamplePass m_downsamplePass;` becomes a plain member (not
  `std::unique_ptr`); remove all `#ifdef ENABLE_SUPERSAMPLING` branches.
- `Renderer::Initialize` always: initializes `DownsamplePass`, creates the
  display depth buffer (`FrameBuffers`) at `DownsamplePass::SSWidth/SSHeight`
  (1600×900), and wires the scene-pass DSV.
- `Renderer::Update` always computes render resolution as the SS resolution.
- `Renderer::Render` always runs `ScenePass::ExecuteToTarget` into the
  offscreen RT followed by `DownsamplePass::Execute`.

### 3. New module: `FrameCapture` (`src/frame_capture.h/.cpp`)

Owns all capture state, isolated from the live render. Resources are created
once in `Initialize`.

**Owned resources:**

- `m_aliasedRT` — 800×450 `R8G8B8A8_UNORM`, default heap, flag
  `ALLOW_RENDER_TARGET`, plus a 1-descriptor RTV heap. The native pass renders
  the aliased color here.
- `m_captureDepth` — 800×450 `R32_TYPELESS`, flag `ALLOW_DEPTH_STENCIL`, plus a
  1-descriptor DSV heap viewing it as `D32_FLOAT`. Mirrors the `FrameBuffers`
  depth pattern. This is both the native pass's depth target and the captured
  depth source.
- Three READBACK-heap buffers (`m_readbackSSAA`, `m_readbackAliased`,
  `m_readbackDepth`), each sized to the copyable footprint
  (row pitch aligned up to `D3D12_TEXTURE_DATA_PITCH_ALIGNMENT` = 256,
  times height). Color buffers store RGBA8; depth stores float32.
- `m_frameIndex` — running counter for output filenames.
- `m_pending` — set by `RequestCapture()`, read in `Render` to decide whether
  to capture this frame, and cleared in `WriteToDisk`.

**Public interface:**

- `void Initialize(D3DContext& ctx);`
- `void RequestCapture();` — sets `m_pending = true`.
- `bool IsPending() const;`
- `void RecordCapture(D3DContext& ctx, ID3D12GraphicsCommandList* cmd,
   ScenePass& scenePass);` — records, into the live command list:
  1. swapchain `PRESENT → COPY_SOURCE`, `CopyTextureRegion` swapchain →
     `m_readbackSSAA`, swapchain `COPY_SOURCE → PRESENT`;
  2. native aliased render: `scenePass.DrawTo(cmd, m_aliasedRT RTV,
     m_captureDepth DSV, 800, 450)` (see §4);
  3. `m_aliasedRT` `RENDER_TARGET → COPY_SOURCE`, copy → `m_readbackAliased`,
     back to `RENDER_TARGET`;
  4. `m_captureDepth` `DEPTH_WRITE → COPY_SOURCE`, copy → `m_readbackDepth`,
     back to `DEPTH_WRITE`.
- `void WriteToDisk();` — after the GPU wait, maps the three readback buffers,
  un-pads the 256-aligned rows, and writes the three files. Increments
  `m_frameIndex`. Clears `m_pending`.

**Capture ordering note:** the swapchain copy happens *after*
`DownsamplePass::Execute` (when the swapchain holds the finished AA image) and
*before* `Present`. The native render is independent and recorded in the same
command list.

### 4. ScenePass: render to an explicit depth target (`src/passes/scene_pass.h/.cpp`)

`ScenePass` currently holds a single fixed DSV handle (`m_dsvHandle`) used by
both `Execute` and `ExecuteToTarget`. The native capture pass needs a
*different* DSV (the 800×450 capture depth). Minimal change:

- Generalize the internal `DrawScene` to take a DSV handle parameter instead
  of reading the member.
- Keep `Execute` / `ExecuteToTarget` passing the stored `m_dsvHandle` (display
  depth), so existing behavior is unchanged.
- Add `void DrawTo(ID3D12GraphicsCommandList* cmd,
  D3D12_CPU_DESCRIPTOR_HANDLE rtv, D3D12_CPU_DESCRIPTOR_HANDLE dsv,
  UINT w, UINT h);` that renders the cube to an explicit RTV+DSV at the given
  resolution. The capture native pass calls this.

The PSO is unchanged: it already targets `RTVFormats[0] = R8G8B8A8_UNORM` and
`DSVFormat = D32_FLOAT`, which both the display and capture targets match.

### 5. Trigger wiring (`src/renderer.h`, `src/main.cpp`)

- `Renderer` gains a `FrameCapture m_frameCapture;` member, initialized in
  `Renderer::Initialize`.
- `Renderer::OnKeyDown`: on `key == 'C'`, call `m_frameCapture.RequestCapture()`
  (in addition to the existing camera handling).
- `Renderer::Render`: after `DownsamplePass::Execute` and before `cmd->Close()`,
  `if (m_frameCapture.IsPending()) m_frameCapture.RecordCapture(m_ctx, cmd,
  m_scenePass);`. After the existing `Present` + `WaitForPreviousFrame`,
  `if (... captured this frame) m_frameCapture.WriteToDisk();`.

### 6. File output

- Directory `captures/` next to the executable (created if missing via
  `std::filesystem::create_directories`).
- Filenames: zero-padded counter, e.g. `frame_0000_ssaa.png`,
  `frame_0000_aliased.png`, `frame_0000_depth.npy`.
- PNG: written with `stb_image_write` (`stbi_write_png`), 4 channels, from the
  un-padded RGBA8 rows.
- `.npy`: hand-written header — magic `\x93NUMPY`, version `1.0`, an ASCII
  dict `{'descr': '<f4', 'fortran_order': False, 'shape': (450, 800), }`
  padded with spaces so the header length is a multiple of 64, then the raw
  little-endian float32 data (row-major, 450×800).

## Future-proofing (designed for, not built now)

- **Sequences:** `WriteToDisk` is per-frame and keyed by `m_frameIndex`.
  A future "record mode" keeps `m_pending` true across N frames; no interface
  change. The current synchronous wait-then-write is fine for one frame; a
  ring of readback buffers can make sequences async later without changing the
  public API.
- **More buffers:** adding normals / motion vectors = one more owned target +
  one more readback + one more write call, all localized in `FrameCapture`
  (and a matching G-buffer output from the scene pass when those exist).

## Out of scope (YAGNI)

- No sequence/record mode yet (single frame per keypress).
- No normals / motion-vector buffers yet.
- No async / double-buffered readback yet.
- No window-resize handling (the app does not handle resize today).
- No capture in a hypothetical non-SSAA build — SSAA is now the only path.

## Error handling

All new D3D12 calls wrapped in `ThrowIfFailed`, consistent with the codebase;
exceptions bubble to `WinMain`'s `MessageBox`. File-write failures (e.g.
`stbi_write_png` returning 0, or an `ofstream` failure) throw a
`std::runtime_error` so they surface the same way.

## Testing / acceptance

No automated test harness exists. Acceptance:

1. `cmake --build --preset debug` and `--preset release` build clean under
   `/W4 /WX`. (The `-ssaa` presets no longer exist.)
2. The app runs and renders the cube as before (SSAA always on).
3. Pressing `C` writes `captures/frame_0000_ssaa.png`,
   `frame_0000_aliased.png`, `frame_0000_depth.npy`; pressing again writes
   `frame_0001_*`.
4. Visual check: `_ssaa.png` shows smooth edges, `_aliased.png` shows
   jaggies on the same geometry, both at 800×450.
5. `np.load('frame_0000_depth.npy')` returns a `(450, 800)` float32 array
   with values in `[0, 1]` (1.0 background, < 1 on the cube).
6. PIX / debug layer clean: no errors about resource states or copy
   footprints.

## Process note

Per the user's established preference on this project, design and
implementation artifacts are left in the working tree and **not committed to
git** unless the user explicitly asks.
