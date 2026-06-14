#pragma once
#include "d3d_helpers.h"
#include <DirectXMath.h>
#include <vector>

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

    void RequestCapture()  { if (m_phase == Phase::Idle) m_phase = Phase::StorePrev; }
    bool IsPending() const { return m_phase != Phase::Idle; }

    // Call before WriteToDisk with the jitter offsets (in pixels) from ScenePass.
    void SetJitter(DirectX::XMFLOAT2 curr, DirectX::XMFLOAT2 prev)
    {
        m_jitterCurr = curr;
        m_jitterPrev = prev;
    }

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
    D3D12_CPU_DESCRIPTOR_HANDLE MotionRTV()  const;
    D3D12_CPU_DESCRIPTOR_HANDLE CaptureDSV() const;

    ComPtr<ID3D12Resource>       m_aliasedRT;
    ComPtr<ID3D12DescriptorHeap> m_aliasedRTVHeap;
    ComPtr<ID3D12Resource>       m_motionRT;
    ComPtr<ID3D12DescriptorHeap> m_motionRTVHeap;
    ComPtr<ID3D12Resource>       m_captureDepth;
    ComPtr<ID3D12DescriptorHeap> m_captureDSVHeap;

    ComPtr<ID3D12Resource> m_readbackSSAA;     // swapchain AA copy (RGBA8)
    ComPtr<ID3D12Resource> m_readbackAliased;  // native aliased    (RGBA8)
    ComPtr<ID3D12Resource> m_readbackDepth;    // native depth      (float32)
    ComPtr<ID3D12Resource> m_readbackMotion;   // motion vectors    (RG16F)

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT m_colorLayout  = {};  // RGBA8 footprint
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT m_depthLayout  = {};  // R32_FLOAT footprint
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT m_motionLayout = {};  // RG16F footprint

    std::vector<unsigned char> m_prevSSAA;
    DirectX::XMFLOAT2          m_jitterCurr = {};
    DirectX::XMFLOAT2          m_jitterPrev = {};

    enum class Phase : uint8_t { Idle, StorePrev, CaptureAll };
    Phase m_phase     = Phase::Idle;
    UINT  m_frameIndex = 0;
};
