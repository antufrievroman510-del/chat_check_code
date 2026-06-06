#include "capture.h"
#include <iostream>
#include <algorithm>

DXGICapture::DXGICapture() {}
DXGICapture::~DXGICapture() { Cleanup(); }

bool DXGICapture::Initialize() {
    m_ScreenWidth = GetSystemMetrics(SM_CXSCREEN);
    m_ScreenHeight = GetSystemMetrics(SM_CYSCREEN);

    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory))) return false;

    IDXGIAdapter1* adapter = nullptr;
    IDXGIOutput* output = nullptr;
    bool found_output = false;

    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        if (SUCCEEDED(adapter->EnumOutputs(0, &output))) {
            found_output = true;
            break;
        }
        adapter->Release();
    }
    factory->Release();

    if (!found_output) return false;

    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
    if (FAILED(D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, featureLevels, 1, D3D11_SDK_VERSION, &m_Device, nullptr, &m_Context))) {
        adapter->Release(); output->Release(); return false;
    }

    IDXGIOutput1* output1 = nullptr;
    output->QueryInterface(__uuidof(IDXGIOutput1), (void**)&output1);
    output->Release(); adapter->Release();

    HRESULT hr = output1->DuplicateOutput(m_Device, &m_DeskDupl);
    output1->Release();

    if (FAILED(hr)) return false;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = m_ScreenWidth;
    desc.Height = m_ScreenHeight;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    m_Device->CreateTexture2D(&desc, nullptr, &m_StagingTex);

    std::cout << "[+] Hardware Capture        : " << m_ScreenWidth << "x" << m_ScreenHeight << std::endl;
    return true;
}

bool DXGICapture::GetHardwareROIFrame(unsigned char* out_pixels, int roi_x, int roi_y, int roi_w, int roi_h) {
    std::lock_guard<std::mutex> lock(m_CaptureMutex);
    if (!m_DeskDupl || !m_StagingTex) return false;

    roi_w = (std::min)(roi_w, m_ScreenWidth);
    roi_h = (std::min)(roi_h, m_ScreenHeight);
    int max_x = (std::max)(0, m_ScreenWidth - roi_w);
    int max_y = (std::max)(0, m_ScreenHeight - roi_h);
    roi_x = (std::max)(0, (std::min)(roi_x, max_x));
    roi_y = (std::max)(0, (std::min)(roi_y, max_y));

    IDXGIResource* desktopRes = nullptr;
    DXGI_OUTDUPL_FRAME_INFO frameInfo;
    HRESULT hr = m_DeskDupl->AcquireNextFrame(0, &frameInfo, &desktopRes);
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) return false;
    if (FAILED(hr)) { ResetDuplicator(); return false; }

    ID3D11Texture2D* gpuTex = nullptr;
    desktopRes->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&gpuTex);
    desktopRes->Release();

    if (!gpuTex) { m_DeskDupl->ReleaseFrame(); return false; }

    D3D11_BOX sourceRegion;
    sourceRegion.left = roi_x;
    sourceRegion.right = roi_x + roi_w;
    sourceRegion.top = roi_y;
    sourceRegion.bottom = roi_y + roi_h;
    sourceRegion.front = 0;
    sourceRegion.back = 1;

    m_Context->CopySubresourceRegion(m_StagingTex, 0, 0, 0, 0, gpuTex, 0, &sourceRegion);
    gpuTex->Release();

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(m_Context->Map(m_StagingTex, 0, D3D11_MAP_READ, 0, &mapped))) {
        unsigned char* src = (unsigned char*)mapped.pData;
        int pitch = mapped.RowPitch;
        for (int y = 0; y < roi_h; ++y) {
            memcpy(out_pixels + y * roi_w * 4, src + y * pitch, roi_w * 4);
        }
        m_Context->Unmap(m_StagingTex, 0);
        m_DeskDupl->ReleaseFrame();
        return true;
    }

    m_DeskDupl->ReleaseFrame();
    return false;
}

void DXGICapture::ResetDuplicator() {
    if (m_DeskDupl) { m_DeskDupl->Release(); m_DeskDupl = nullptr; }
    Initialize();
}

void DXGICapture::Cleanup() {
    if (m_StagingTex) { m_StagingTex->Release(); m_StagingTex = nullptr; }
    if (m_DeskDupl) { m_DeskDupl->Release(); m_DeskDupl = nullptr; }
    if (m_Context) { m_Context->Release(); m_Context = nullptr; }
    if (m_Device) { m_Device->Release(); m_Device = nullptr; }
}
