#pragma once
#include <windows.h>
#include <DirectXMath.h>
#include <chrono>

class Camera
{
public:
    void EnableCapture(HWND hwnd);
    void DisableCapture();
    void Update(HWND hwnd);
    void OnKeyDown(WPARAM key);
    void OnKeyUp(WPARAM key);
    DirectX::XMMATRIX GetViewMatrix() const;
    bool IsCapturing() const { return m_mouseCapture; }

private:
    DirectX::XMVECTOR m_position     = DirectX::XMVectorSet(0.0f, 0.0f, -6.0f, 0.0f);
    float             m_yaw          = 0.0f;
    float             m_pitch        = 0.0f;
    float             m_speed        = 3.0f;
    float             m_sensitivity  = 0.002f;
    bool              m_mouseCapture = false;
    bool              m_moveForward  = false;
    bool              m_moveBackward = false;
    bool              m_moveLeft     = false;
    bool              m_moveRight    = false;
    std::chrono::steady_clock::time_point m_lastTime =
        std::chrono::steady_clock::now();
};
