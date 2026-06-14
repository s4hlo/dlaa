#define _CRT_SECURE_NO_WARNINGS
#include "frame_capture.h"
#include "d3d_context.h"
#include "passes/scene_pass.h"

#include <vector>
#include <string>
#include <fstream>
#include <cstdint>
#include <filesystem>

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

    // --- Motion vector RT (800x450, R16G16_FLOAT) ---
    D3D12_RESOURCE_DESC motionDesc = {};
    motionDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    motionDesc.Width            = Width;
    motionDesc.Height           = Height;
    motionDesc.DepthOrArraySize = 1;
    motionDesc.MipLevels        = 1;
    motionDesc.Format           = DXGI_FORMAT_R16G16_FLOAT;
    motionDesc.SampleDesc.Count = 1;
    motionDesc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    motionDesc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE motionClear = {};
    motionClear.Format = DXGI_FORMAT_R16G16_FLOAT;

    ThrowIfFailed(ctx.device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE,
        &motionDesc, D3D12_RESOURCE_STATE_RENDER_TARGET, &motionClear,
        IID_PPV_ARGS(&m_motionRT)));

    D3D12_DESCRIPTOR_HEAP_DESC motionRTVHeapDesc = {};
    motionRTVHeapDesc.NumDescriptors = 1;
    motionRTVHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    ThrowIfFailed(ctx.device->CreateDescriptorHeap(&motionRTVHeapDesc, IID_PPV_ARGS(&m_motionRTVHeap)));
    ctx.device->CreateRenderTargetView(m_motionRT.Get(), nullptr,
        m_motionRTVHeap->GetCPUDescriptorHandleForHeapStart());

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

    // Use a plain typeless color desc so the footprint format matches the source resource.
    D3D12_RESOURCE_DESC depthTyped = depthDesc;
    depthTyped.Format = DXGI_FORMAT_R32_TYPELESS;
    depthTyped.Flags  = D3D12_RESOURCE_FLAG_NONE;
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
    UINT64 motionTotal = 0;
    ctx.device->GetCopyableFootprints(&motionDesc, 0, 1, 0,
        &m_motionLayout, nullptr, nullptr, &motionTotal);

    MakeReadback(colorTotal,  m_readbackSSAA);
    MakeReadback(colorTotal,  m_readbackAliased);
    MakeReadback(depthTotal,  m_readbackDepth);
    MakeReadback(motionTotal, m_readbackMotion);
}

D3D12_CPU_DESCRIPTOR_HANDLE FrameCapture::AliasedRTV() const
{
    return m_aliasedRTVHeap->GetCPUDescriptorHandleForHeapStart();
}

D3D12_CPU_DESCRIPTOR_HANDLE FrameCapture::MotionRTV() const
{
    return m_motionRTVHeap->GetCPUDescriptorHandleForHeapStart();
}

D3D12_CPU_DESCRIPTOR_HANDLE FrameCapture::CaptureDSV() const
{
    return m_captureDSVHeap->GetCPUDescriptorHandleForHeapStart();
}

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

    // Always copy swapchain SSAA (used as prev_ssaa in phase 1, as ssaa in phase 2).
    auto toCopy = TransitionBarrier(swap,
        D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
    cmd->ResourceBarrier(1, &toCopy);
    CopyToReadback(cmd, swap, m_readbackSSAA.Get(), m_colorLayout);
    auto toPresent = TransitionBarrier(swap,
        D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
    cmd->ResourceBarrier(1, &toPresent);

    if (m_phase == Phase::CaptureAll)
    {
        // Native 1-spp render -> aliased RT + motion RT + capture depth (MRT).
        scenePass.DrawToMRT(cmd, AliasedRTV(), MotionRTV(), CaptureDSV(), Width, Height);

        auto aliasedToCopy = TransitionBarrier(m_aliasedRT.Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        cmd->ResourceBarrier(1, &aliasedToCopy);
        CopyToReadback(cmd, m_aliasedRT.Get(), m_readbackAliased.Get(), m_colorLayout);
        auto aliasedToRT = TransitionBarrier(m_aliasedRT.Get(),
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmd->ResourceBarrier(1, &aliasedToRT);

        auto depthToCopy = TransitionBarrier(m_captureDepth.Get(),
            D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);
        cmd->ResourceBarrier(1, &depthToCopy);
        CopyToReadback(cmd, m_captureDepth.Get(), m_readbackDepth.Get(), m_depthLayout);
        auto depthToWrite = TransitionBarrier(m_captureDepth.Get(),
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        cmd->ResourceBarrier(1, &depthToWrite);

        auto motionToCopy = TransitionBarrier(m_motionRT.Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        cmd->ResourceBarrier(1, &motionToCopy);
        CopyToReadback(cmd, m_motionRT.Get(), m_readbackMotion.Get(), m_motionLayout);
        auto motionToRT = TransitionBarrier(m_motionRT.Get(),
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmd->ResourceBarrier(1, &motionToRT);
    }
}

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
        void* mapped = nullptr;
        ThrowIfFailed(readback->Map(0, nullptr, &mapped));

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

    // shape (H, W, 2), dtype float16 little-endian ('<f2')
    void WriteNpyHalf2(const std::filesystem::path& path,
                       const uint16_t* data, UINT w, UINT h)
    {
        std::string dict = "{'descr': '<f2', 'fortran_order': False, 'shape': ("
                         + std::to_string(h) + ", " + std::to_string(w) + ", 2), }";
        size_t unpadded = 10 + dict.size() + 1;
        size_t padded   = ((unpadded + 63) / 64) * 64;
        dict.append(padded - unpadded, ' ');
        dict.push_back('\n');

        std::ofstream f(path, std::ios::binary);
        if (!f) throw std::runtime_error("cannot open " + path.string());
        const unsigned char magic[8] = { 0x93, 'N','U','M','P','Y', 1, 0 };
        f.write(reinterpret_cast<const char*>(magic), 8);
        const uint16_t headerLen = static_cast<uint16_t>(dict.size());
        f.write(reinterpret_cast<const char*>(&headerLen), 2);
        f.write(dict.data(), static_cast<std::streamsize>(dict.size()));
        f.write(reinterpret_cast<const char*>(data),
                static_cast<std::streamsize>(sizeof(uint16_t) * 2 * w * h));
        if (!f) throw std::runtime_error("write failed: " + path.string());
    }

    void WriteBmp(const std::filesystem::path& path,
                  const std::vector<unsigned char>& rgba, UINT w, UINT h)
    {
        const uint32_t rowBytes      = w * 3;
        const uint32_t pixelDataSize = rowBytes * h;
        const uint32_t fileSize      = 14 + 40 + pixelDataSize;

        unsigned char fh[14] = { 'B','M', 0,0,0,0, 0,0, 0,0, 54,0,0,0 };
        memcpy(fh + 2, &fileSize, 4);

        int32_t negH = -static_cast<int32_t>(h);  // top-down storage
        unsigned char ih[40] = {};
        const uint32_t biSize = 40; const uint16_t planes = 1, bpp = 24;
        memcpy(ih,      &biSize, 4); memcpy(ih + 4, &w,     4);
        memcpy(ih + 8,  &negH,   4); memcpy(ih + 12, &planes, 2);
        memcpy(ih + 14, &bpp,    2); memcpy(ih + 20, &pixelDataSize, 4);

        std::vector<unsigned char> bgr(pixelDataSize);
        for (size_t i = 0, j = 0; i < static_cast<size_t>(w) * h; ++i, j += 3) {
            bgr[j] = rgba[i*4+2]; bgr[j+1] = rgba[i*4+1]; bgr[j+2] = rgba[i*4];
        }

        std::ofstream f(path, std::ios::binary);
        if (!f) throw std::runtime_error("cannot open " + path.string());
        f.write(reinterpret_cast<char*>(fh), 14);
        f.write(reinterpret_cast<char*>(ih), 40);
        f.write(reinterpret_cast<char*>(bgr.data()), pixelDataSize);
        if (!f) throw std::runtime_error("write failed: " + path.string());
    }
}

void FrameCapture::WriteToDisk()
{
    if (m_phase == Phase::StorePrev)
    {
        m_prevSSAA = Unpad<unsigned char>(m_readbackSSAA.Get(),
            m_colorLayout.Footprint.RowPitch, Width, Height, 4);
        m_phase = Phase::CaptureAll;
        return;
    }

    namespace fs = std::filesystem;
    fs::path dir = "captures";
    fs::create_directories(dir);

    char stem[32];
    snprintf(stem, sizeof(stem), "frame_%04u", m_frameIndex);

    WriteBmp(dir / (std::string(stem) + "_prev_ssaa.bmp"), m_prevSSAA, Width, Height);

    auto ssaa = Unpad<unsigned char>(m_readbackSSAA.Get(),
        m_colorLayout.Footprint.RowPitch, Width, Height, 4);
    WriteBmp(dir / (std::string(stem) + "_ssaa.bmp"), ssaa, Width, Height);

    auto aliased = Unpad<unsigned char>(m_readbackAliased.Get(),
        m_colorLayout.Footprint.RowPitch, Width, Height, 4);
    WriteBmp(dir / (std::string(stem) + "_aliased.bmp"), aliased, Width, Height);

    auto depth = Unpad<float>(m_readbackDepth.Get(),
        m_depthLayout.Footprint.RowPitch, Width, Height, 4);
    WriteNpyFloat(dir / (std::string(stem) + "_depth.npy"), depth.data(), Width, Height);

    auto motion = Unpad<uint16_t>(m_readbackMotion.Get(),
        m_motionLayout.Footprint.RowPitch, Width, Height, 4);
    WriteNpyHalf2(dir / (std::string(stem) + "_motion.npy"), motion.data(), Width, Height);

    ++m_frameIndex;
    m_prevSSAA.clear();
    m_phase = Phase::Idle;
}
