#pragma once

#include "WinHeaders.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <mutex>
#include <span>

class DXGICapture {
public:
    DXGICapture();
    ~DXGICapture();

    bool Initialize();
    bool GetHardwareROIFrame(std::span<std::byte> out_pixels, int roi_x, int roi_y, int roi_w, int roi_h);
    void Cleanup();

private:
    void ResetDuplicator();

    ID3D11Device* m_Device = nullptr;
    ID3D11DeviceContext* m_Context = nullptr;
    IDXGIOutputDuplication* m_DeskDupl = nullptr;

    // Одиночная staging texture под размер ROI (создается один раз при Initialize)
    ID3D11Texture2D* m_StagingTex = nullptr;
    int m_StagingW = 0;
    int m_StagingH = 0;

    std::mutex m_CaptureMutex;
    int m_ScreenWidth = 0;
    int m_ScreenHeight = 0;
};
