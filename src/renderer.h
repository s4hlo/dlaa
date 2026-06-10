#pragma once
#include "d3d_context.h"
#include "camera.h"
#include "passes/scene_pass.h"
#include "passes/downsample_pass.h"
#include <memory>
#include <filesystem>
#include <string>

class Renderer
{
public:
    void Initialize(HWND hwnd, UINT width, UINT height);
    void Update();
    void Render();

    void OnKeyDown(WPARAM key) { if (key == VK_ESCAPE) m_camera.DisableCapture(); m_camera.OnKeyDown(key); }
    void OnKeyUp(WPARAM key)   { m_camera.OnKeyUp(key); }
    void OnLButtonDown()       { m_camera.EnableCapture(m_hwnd); }
    void OnKillFocus()         { m_camera.DisableCapture(); }

private:
    std::wstring GetShaderPath() const;

    HWND        m_hwnd = nullptr;
    D3DContext  m_ctx;
    Camera      m_camera;
    ScenePass   m_scenePass;
    std::unique_ptr<DownsamplePass> m_downsamplePass;
};
