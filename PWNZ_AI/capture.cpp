#include "capture.h"
#include <iostream>
#include <algorithm>
#include <intrin.h>
#include <span>
#include <cstring>

DXGICapture::DXGICapture() {}
DXGICapture::~DXGICapture() { Cleanup(); }

bool DXGICapture::Initialize() {
    m_ScreenWidth = GetSystemMetrics(SM_CXSCREEN);
    m_ScreenHeight = GetSystemMetrics(SM_CYSCREEN);

    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))))
        return false;

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
    if (FAILED(D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0,
        featureLevels, 1, D3D11_SDK_VERSION, &m_Device, nullptr, &m_Context))) {
        adapter->Release(); output->Release(); return false;
    }

    IDXGIOutput1* output1 = nullptr;
    output->QueryInterface(__uuidof(IDXGIOutput1), reinterpret_cast<void**>(&output1));
    output->Release(); adapter->Release();

    HRESULT hr = output1->DuplicateOutput(m_Device, &m_DeskDupl);
    output1->Release();

    if (FAILED(hr)) return false;

    // Staging texture НЕ создаём здесь — она создаётся лениво в GetHardwareROIFrame
    // под точный размер ROI при первом вызове. Это главная оптимизация.

    std::cout << "[+] Hardware Capture initialized: "
        << m_ScreenWidth << "x" << m_ScreenHeight << std::endl;
    return true;
}

bool DXGICapture::GetHardwareROIFrame(std::span<std::byte> out_pixels,
    int roi_x, int roi_y, int roi_w, int roi_h) {
    std::lock_guard<std::mutex> lock(m_CaptureMutex);
    if (!m_DeskDupl) return false;

    // Зажимаем ROI в границы экрана
    roi_w = (std::min)(roi_w, m_ScreenWidth);
    roi_h = (std::min)(roi_h, m_ScreenHeight);
    int max_x = (std::max)(0, m_ScreenWidth - roi_w);
    int max_y = (std::max)(0, m_ScreenHeight - roi_h);
    roi_x = (std::max)(0, (std::min)(roi_x, max_x));
    roi_y = (std::max)(0, (std::min)(roi_y, max_y));

    // Создаём staging texture один раз под размер ROI (ленивая инициализация)
    if (m_StagingW != roi_w || m_StagingH != roi_h || m_StagingTex == nullptr) {
        if (m_StagingTex) { m_StagingTex->Release(); m_StagingTex = nullptr; }

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = static_cast<UINT>(roi_w);
        desc.Height = static_cast<UINT>(roi_h);
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        if (FAILED(m_Device->CreateTexture2D(&desc, nullptr, &m_StagingTex))) {
            std::cerr << "[!] Failed to create staging texture " << roi_w << "x" << roi_h << std::endl;
            return false;
        }
        m_StagingW = roi_w;
        m_StagingH = roi_h;
        std::cout << "[+] Staging texture created: " << roi_w << "x" << roi_h << std::endl;
    }

    IDXGIResource* desktopRes = nullptr;
    DXGI_OUTDUPL_FRAME_INFO frameInfo{};

    // AcquireNextFrame с таймаутом 0 — если кадра нет, сразу возвращаем false
    HRESULT hr = m_DeskDupl->AcquireNextFrame(0, &frameInfo, &desktopRes);
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        return false; // GPU не готов — пропускаем кадр, не ждём
    }
    if (FAILED(hr)) { ResetDuplicator(); return false; }

    ID3D11Texture2D* gpuTex = nullptr;
    desktopRes->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&gpuTex));
    desktopRes->Release();

    if (!gpuTex) { m_DeskDupl->ReleaseFrame(); return false; }

    // Копируем только ROI (не весь экран!) в staging texture
    D3D11_BOX srcBox{};
    srcBox.left = static_cast<UINT>(roi_x);
    srcBox.right = static_cast<UINT>(roi_x + roi_w);
    srcBox.top = static_cast<UINT>(roi_y);
    srcBox.bottom = static_cast<UINT>(roi_y + roi_h);
    srcBox.front = 0;
    srcBox.back = 1;

    m_Context->CopySubresourceRegion(m_StagingTex, 0, 0, 0, 0, gpuTex, 0, &srcBox);
    gpuTex->Release();
    m_DeskDupl->ReleaseFrame();

    // Map с флагом DO_NOT_WAIT — если GPU ещё не закончил, сразу возвращаем false
    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = m_Context->Map(m_StagingTex, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
    if (hr == DXGI_ERROR_WAS_STILL_DRAWING) {
        return false; // GPU ещё рисует — пропускаем кадр
    }
    if (FAILED(hr)) return false;

    const auto* src = static_cast<const unsigned char*>(mapped.pData);
    const int   pitch = static_cast<int>(mapped.RowPitch);
    const int   dst_pitch = roi_w * 4;

    if (out_pixels.size_bytes() < static_cast<size_t>(roi_h) * dst_pitch) {
        m_Context->Unmap(m_StagingTex, 0);
        return false;
    }

    for (int y = 0; y < roi_h; ++y)
        std::memcpy(out_pixels.data() + y * dst_pitch, src + y * pitch, dst_pitch);

    m_Context->Unmap(m_StagingTex, 0);
    return true;
}

void DXGICapture::ResetDuplicator() {
    if (m_DeskDupl) { m_DeskDupl->Release(); m_DeskDupl = nullptr; }
    Initialize();
}

void DXGICapture::Cleanup() {
    if (m_StagingTex) { m_StagingTex->Release(); m_StagingTex = nullptr; }
    if (m_DeskDupl) { m_DeskDupl->Release();  m_DeskDupl = nullptr; }
    if (m_Context) { m_Context->Release();    m_Context = nullptr; }
    if (m_Device) { m_Device->Release();     m_Device = nullptr; }
}
