#include "camera.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;

void Camera::EnableCapture(HWND hwnd)
{
    m_mouseCapture = true;
    ShowCursor(FALSE);
    RECT rect;
    GetClientRect(hwnd, &rect);
    POINT center = { (rect.right - rect.left) / 2, (rect.bottom - rect.top) / 2 };
    ClientToScreen(hwnd, &center);
    SetCursorPos(center.x, center.y);
}

void Camera::DisableCapture()
{
    m_mouseCapture = false;
    ShowCursor(TRUE);
}

void Camera::Update(HWND hwnd)
{
    const auto  now = std::chrono::steady_clock::now();
    const float dt  = std::chrono::duration<float>(now - m_lastTime).count();
    m_lastTime = now;

    if (m_mouseCapture)
    {
        RECT rect;
        GetClientRect(hwnd, &rect);
        POINT center = { (rect.right - rect.left) / 2, (rect.bottom - rect.top) / 2 };
        ClientToScreen(hwnd, &center);
        POINT pt;
        GetCursorPos(&pt);
        m_yaw   += static_cast<float>(pt.x - center.x) * m_sensitivity;
        m_pitch -= static_cast<float>(pt.y - center.y) * m_sensitivity;
        m_pitch  = std::clamp(m_pitch, -XM_PIDIV2 + 0.01f, XM_PIDIV2 - 0.01f);
        SetCursorPos(center.x, center.y);
    }

    const float cy = cosf(m_yaw), sy = sinf(m_yaw);
    const XMVECTOR flatFwd = XMVector3Normalize(XMVectorSet(sy, 0.0f, cy, 0.0f));
    const XMVECTOR up      = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    const XMVECTOR right   = XMVector3Cross(up, flatFwd);

    XMVECTOR move = XMVectorZero();
    if (m_moveForward)  move = XMVectorAdd(move, flatFwd);
    if (m_moveBackward) move = XMVectorSubtract(move, flatFwd);
    if (m_moveRight)    move = XMVectorAdd(move, right);
    if (m_moveLeft)     move = XMVectorSubtract(move, right);

    if (XMVector3LengthSq(move).m128_f32[0] > 0.0f)
        m_position = XMVectorAdd(m_position,
            XMVector3Normalize(move) * (m_speed * dt));
}

void Camera::OnKeyDown(WPARAM key)
{
    switch (key)
    {
    case 'W': m_moveForward  = true;  break;
    case 'S': m_moveBackward = true;  break;
    case 'A': m_moveLeft     = true;  break;
    case 'D': m_moveRight    = true;  break;
    default:  break;
    }
}

void Camera::OnKeyUp(WPARAM key)
{
    switch (key)
    {
    case 'W': m_moveForward  = false; break;
    case 'S': m_moveBackward = false; break;
    case 'A': m_moveLeft     = false; break;
    case 'D': m_moveRight    = false; break;
    default:  break;
    }
}

XMMATRIX Camera::GetViewMatrix() const
{
    const float cp = cosf(m_pitch), sp = sinf(m_pitch);
    const float cy = cosf(m_yaw),   sy = sinf(m_yaw);
    const XMVECTOR forward = XMVectorSet(cp * sy, sp, cp * cy, 0.0f);
    const XMVECTOR up      = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    const XMVECTOR target  = XMVectorAdd(m_position, forward);
    return XMMatrixLookAtLH(m_position, target, up);
}
