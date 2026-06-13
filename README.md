# DirectX 12 DLAA Environment

Real-time DirectX 12 renderer with SSAA support and dataset capture for Deep Learning Anti-Aliasing (DLAA) training.

## Prerequisites

- Windows 10/11
- Visual Studio Build Tools 2022 (with MSVC and Windows SDK)
- CMake 3.20+
- Ninja

## Build

Open a **Developer Command Prompt for VS** (or any terminal where `cl.exe` is in PATH), then:

```powershell
# Configure (pick one)
cmake --preset debug
cmake --preset debug-ssaa    # enables 2x SSAA path

# Build
cmake --build --preset debug
cmake --build --preset debug-ssaa
```

Release presets (`release`, `release-ssaa`) are also available.

## Run

```powershell
.\build\debug\DirectX12Cube.exe
.\build\debug-ssaa\DirectX12Cube.exe
```

## VS Code

Open the folder in VS Code and press **F5** — the tasks are pre-configured to activate the VS developer environment automatically. Use the launch configuration selector to choose between `Debug`, `Debug SSAA`, and `Release`.

## Controls

| Key | Action |
|-----|--------|
| W A S D | Move camera |
| Mouse | Look around (click to capture) |
| C | Capture dataset sample |

## Dataset Capture

Press **C** once to capture a training sample. The capture spans two consecutive frames:

- Frame 1: stores the current SSAA frame as the "previous" frame
- Frame 2: writes all 5 files to `captures/`

| File | Description |
|------|-------------|
| `frame_XXXX_prev_ssaa.bmp` | Previous frame (clean, SSAA) — temporal context |
| `frame_XXXX_ssaa.bmp` | Current frame ground truth (SSAA) |
| `frame_XXXX_aliased.bmp` | Current frame input (aliased, no AA) |
| `frame_XXXX_depth.npy` | Current frame depth buffer (float32) |

These 4 files form one training sample for a temporal DLAA model.
