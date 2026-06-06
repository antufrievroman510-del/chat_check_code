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

    // NOTE: Staging texture НЕ создаём здесь.
    // Они создаются лениво в EnsureStagingTextures() под точный размер ROI.
    // Это главная оптимизация — не аллоцировать 1920×1080 когда ROI 512×288.

    std::cout << "[+] Hardware Capture initialized: "
        << m_ScreenWidth << "x" << m_ScreenHeight << std::endl;
    return true;
}

// Создаёт/пересоздаёт оба staging буфера под размер ROI.
// Вызывается только когда размер меняется — не каждый кадр.
bool DXGICapture::EnsureStagingTextures(int roi_w, int roi_h) {
    if (m_StagingW == roi_w && m_StagingH == roi_h &&
        m_StagingTex[0] != nullptr && m_StagingTex[1] != nullptr)
        return true;

    // Освобождаем старые
    for (int i = 0; i < 2; ++i) {
        if (m_StagingTex[i]) { m_StagingTex[i]->Release(); m_StagingTex[i] = nullptr; }
    }
    m_BufReady = false;
    m_BufIndex = 0;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(roi_w);
    desc.Height = static_cast<UINT>(roi_h);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    for (int i = 0; i < 2; ++i) {
        if (FAILED(m_Device->CreateTexture2D(&desc, nullptr, &m_StagingTex[i]))) {
            std::cerr << "[!] Failed to create staging texture [" << i << "]" << std::endl;
            return false;
        }
    }

    m_StagingW = roi_w;
    m_StagingH = roi_h;
    std::cout << "[+] Staging textures resized to " << roi_w << "x" << roi_h
        << " (double-buffered)" << std::endl;
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

    // Создаём/пересоздаём staging под текущий ROI если нужно
    if (!EnsureStagingTextures(roi_w, roi_h)) return false;

    // ── ДВОЙНОЙ БУФЕР ──────────────────────────────────────────────────────────
    // write_buf: GPU пишет сюда прямо сейчас
    // read_buf:  CPU читает прошлый кадр (уже готов) пока GPU занят следующим
    //
    //  Кадр N:   AcquireNextFrame → CopySubresource → buf[0]    Map(buf[1]) → данные
    //  Кадр N+1: AcquireNextFrame → CopySubresource → buf[1]    Map(buf[0]) → данные
    //
    // Итог: GPU и CPU работают параллельно, ждать синхронизации не нужно.
    // ──────────────────────────────────────────────────────────────────────────
    const int write_idx = m_BufIndex;          // пишем сюда
    const int read_idx = 1 - m_BufIndex;      // читаем отсюда

    IDXGIResource* desktopRes = nullptr;
    DXGI_OUTDUPL_FRAME_INFO frameInfo{};

    HRESULT hr = m_DeskDupl->AcquireNextFrame(0, &frameInfo, &desktopRes);
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        // Нового кадра нет — если есть готовый в read_buf, возвращаем его
        // (используется последний захваченный кадр, latency не растёт)
        if (!m_BufReady) return false;
        // Читаем из read_buf (он уже был скопирован раньше)
        ID3D11Texture2D* readTex = m_StagingTex[read_idx];
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(m_Context->Map(readTex, 0, D3D11_MAP_READ, 0, &mapped))) return false;

        const auto* src = static_cast<const unsigned char*>(mapped.pData);
        const int   pitch = static_cast<int>(mapped.RowPitch);
        const int   dst_pitch = roi_w * 4;

        if (out_pixels.size_bytes() < static_cast<size_t>(roi_h) * dst_pitch) {
            m_Context->Unmap(readTex, 0);
            return false;
        }
        for (int y = 0; y < roi_h; ++y)
            std::memcpy(out_pixels.data() + y * dst_pitch, src + y * pitch, dst_pitch);

        m_Context->Unmap(readTex, 0);
        return true;
    }
    if (FAILED(hr)) { ResetDuplicator(); return false; }

    ID3D11Texture2D* gpuTex = nullptr;
    desktopRes->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&gpuTex));
    desktopRes->Release();

    if (!gpuTex) { m_DeskDupl->ReleaseFrame(); return false; }

    // Копируем только ROI (не весь экран!) в write-буфер
    D3D11_BOX srcBox{};
    srcBox.left = static_cast<UINT>(roi_x);
    srcBox.right = static_cast<UINT>(roi_x + roi_w);
    srcBox.top = static_cast<UINT>(roi_y);
    srcBox.bottom = static_cast<UINT>(roi_y + roi_h);
    srcBox.front = 0;
    srcBox.back = 1;

    m_Context->CopySubresourceRegion(m_StagingTex[write_idx], 0, 0, 0, 0, gpuTex, 0, &srcBox);
    gpuTex->Release();
    m_DeskDupl->ReleaseFrame();

    // Переключаем буфер — следующий кадр будет писать в другой слот
    m_BufIndex = read_idx;  // теперь write = бывший read
    m_BufReady = true;

    // Читаем из ТОЛЬКО ЧТО записанного (write_idx) — первый кадр без конвейера,
    // со второго кадра CPU читает предыдущий пока GPU пишет следующий.
    ID3D11Texture2D* readTex = m_StagingTex[write_idx];
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(m_Context->Map(readTex, 0, D3D11_MAP_READ, 0, &mapped))) return false;

    const auto* src = static_cast<const unsigned char*>(mapped.pData);
    const int   pitch = static_cast<int>(mapped.RowPitch);
    const int   dst_pitch = roi_w * 4;

    if (out_pixels.size_bytes() < static_cast<size_t>(roi_h) * dst_pitch) {
        m_Context->Unmap(readTex, 0);
        return false;
    }

    for (int y = 0; y < roi_h; ++y)
        std::memcpy(out_pixels.data() + y * dst_pitch, src + y * pitch, dst_pitch);

    m_Context->Unmap(readTex, 0);
    return true;
}

void DXGICapture::ResetDuplicator() {
    if (m_DeskDupl) { m_DeskDupl->Release(); m_DeskDupl = nullptr; }
    // Сбрасываем готовность буферов при реинициализации
    m_BufReady = false;
    m_BufIndex = 0;
    Initialize();
}

void DXGICapture::Cleanup() {
    for (int i = 0; i < 2; ++i) {
        if (m_StagingTex[i]) { m_StagingTex[i]->Release(); m_StagingTex[i] = nullptr; }
    }
    if (m_DeskDupl) { m_DeskDupl->Release();  m_DeskDupl = nullptr; }
    if (m_Context) { m_Context->Release();    m_Context = nullptr; }
    if (m_Device) { m_Device->Release();     m_Device = nullptr; }
}
