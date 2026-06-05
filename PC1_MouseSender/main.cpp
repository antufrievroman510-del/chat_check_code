// PWNZ VISION PRO (UDP ULTIMATE) - PC1 Mouse Click Sender GUI
// Single file C++ implementation using Dear ImGui
// Compile with: cl /EHsc main.cpp imgui_impl_dx11.cpp imgui_impl_win32.cpp d3d11.lib user32.lib ws2_32.lib /Fe:PWNZ_Vision_Pro.exe

#include <windows.h>
#include <d3d11.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "ws2_32.lib")

// Forward declarations
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Global state
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static UINT g_ResizeWidth = 0, g_ResizeHeight = 0;
static D3D_FEATURE_LEVEL g_featureLevel;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

// Application state
struct AppState {
    bool autoconnect = true;
    bool debug_mode = false;
    char local_ip[64] = "192.168.1.105";
    char hostname[128] = "MY-DESKTOP-01";
    char target_ip[64] = "10.0.0.120";
    char port[16] = "8080";
    bool connected = false;
    std::string status = "IDLE - Waiting for connection.";
    std::vector<std::string> logs;
} g_app;

// Helper to get local IP
std::string GetLocalIP() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return "Unknown";
    
    char hostbuf[256];
    if (gethostname(hostbuf, sizeof(hostbuf)) == SOCKET_ERROR) {
        WSACleanup();
        return "Unknown";
    }
    
    struct addrinfo hints = {}, *addrs;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    
    if (getaddrinfo(hostbuf, NULL, &hints, &addrs) != 0) {
        WSACleanup();
        return "Unknown";
    }
    
    std::string ip = "Unknown";
    for (auto* addr = addrs; addr != nullptr; addr = addr->ai_next) {
        sockaddr_in* ipv4 = reinterpret_cast<sockaddr_in*>(addr->ai_addr);
        inet_ntop(AF_INET, &ipv4->sin_addr, hostbuf, sizeof(hostbuf));
        if (std::string(hostbuf).find("127.") != 0) {
            ip = hostbuf;
            break;
        }
    }
    
    freeaddrinfo(addrs);
    WSACleanup();
    return ip;
}

// Helper to get hostname
std::string GetHostname() {
    char hostbuf[256];
    if (gethostname(hostbuf, sizeof(hostbuf)) == 0) {
        return std::string(hostbuf);
    }
    return "Unknown";
}

// Draw shield logo using ImDrawList
void DrawShieldLogo(ImDrawList* draw_list, ImVec2 pos, float size) {
    // Shield shape points
    ImVec2 top_center = ImVec2(pos.x + size/2, pos.y);
    ImVec2 top_left = ImVec2(pos.x + size*0.1f, pos.y + size*0.15f);
    ImVec2 mid_left = ImVec2(pos.x, pos.y + size*0.5f);
    ImVec2 bottom_point = ImVec2(pos.x + size/2, pos.y + size*0.95f);
    ImVec2 mid_right = ImVec2(pos.x + size, pos.y + size*0.5f);
    ImVec2 top_right = ImVec2(pos.x + size*0.9f, pos.y + size*0.15f);
    
    // Fill color (light blue)
    ImU32 fill_color = IM_COL32(60, 140, 220, 255);
    // Border color (dark blue)
    ImU32 border_color = IM_COL32(20, 60, 120, 255);
    
    // Draw filled shield
    draw_list->AddConvexPolyFilled(
        &top_center, 6, 
        fill_color
    );
    
    // Manual polygon for shield outline
    draw_list->AddLine(top_center, top_right, border_color, 2.0f);
    draw_list->AddLine(top_right, mid_right, border_color, 2.0f);
    draw_list->AddLine(mid_right, bottom_point, border_color, 2.0f);
    draw_list->AddLine(bottom_point, mid_left, border_color, 2.0f);
    draw_list->AddLine(mid_left, top_left, border_color, 2.0f);
    draw_list->AddLine(top_left, top_center, border_color, 2.0f);
    
    // Draw "PV" text inside shield
    ImVec2 text_pos = ImVec2(pos.x + size*0.25f, pos.y + size*0.35f);
    draw_list->AddText(nullptr, size*0.4f, text_pos, IM_COL32(255, 255, 255, 255), "PV");
}

// Render frame
void RenderFrame() {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    
    // Setup style
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 5.0f;
    style.FrameRounding = 4.0f;
    style.FramePadding = ImVec2(8, 6);
    style.ItemSpacing = ImVec2(10, 8);
    
    // Colors
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.12f, 0.12f, 0.12f, 1.0f); // ~#1E1E1E
    style.Colors[ImGuiCol_Header] = ImVec4(0.0f, 0.48f, 0.8f, 1.0f);     // Active blue ~#007ACC
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.0f, 0.4f, 0.7f, 1.0f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.0f, 0.55f, 0.9f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.0f, 0.48f, 0.8f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.0f, 0.35f, 0.6f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.0f, 0.55f, 0.9f, 1.0f);
    style.Colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.67f, 0.67f, 0.67f, 1.0f); // ~#AAAAAA
    style.Colors[ImGuiCol_Border] = ImVec4(0.25f, 0.25f, 0.25f, 1.0f);
    style.Colors[ImGuiCol_Tab] = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
    style.Colors[ImGuiCol_TabActive] = ImVec4(0.0f, 0.48f, 0.8f, 1.0f);
    style.Colors[ImGuiCol_TabHovered] = ImVec4(0.3f, 0.3f, 0.3f, 1.0f);
    
    // Main window flags
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;
    
    ImGui::SetNextWindowSize(ImVec2(900, 600));
    ImGui::Begin("PWNZ VISION PRO (UDP ULTIMATE)", nullptr, window_flags);
    
    // Tab bar
    if (ImGui::BeginTabBar("MainTabs")) {
        // NETWORK tab (active by default)
        if (ImGui::BeginTabItem("NETWORK")) {
            ImGui::EndTabBar();
            
            // === HEADER SECTION ===
            ImGui::BeginGroup();
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
            DrawShieldLogo(draw_list, cursor_pos, 50.0f);
            ImGui::Dummy(ImVec2(60, 60));
            ImGui::EndGroup();
            
            ImGui::SameLine();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 10);
            ImGui::TextUnformatted("PWNZ VISION PRO");
            ImGui::SameLine();
            ImGui::TextDisabled("(UDP ULTIMATE)");
            
            // Right side checkboxes
            float content_width = ImGui::GetContentRegionAvail().x;
            float checkbox_width = 250.0f;
            ImGui::SameLine(content_width - checkbox_width);
            
            ImGui::Checkbox("Autoconnect", &g_app.autoconnect);
            ImGui::SameLine();
            ImGui::Checkbox("Debug Mode", &g_app.debug_mode);
            
            ImGui::Separator();
            ImGui::Spacing();
            
            // === INFO PANELS ===
            float panel_width = (ImGui::GetContentRegionAvail().x - 10) / 2.0f;
            
            // Left Panel - LOCAL SYSTEM INFO
            ImGui::BeginChild("LocalPanel", ImVec2(panel_width, 150), true);
            ImGui::TextDisabled("LOCAL SYSTEM INFO");
            ImGui::Separator();
            ImGui::Spacing();
            
            ImGui::TextDisabled("Current IP:");
            strncpy_s(g_app.local_ip, GetLocalIP().c_str(), sizeof(g_app.local_ip) - 1);
            ImGui::Text("%s (automatically detected)", g_app.local_ip);
            ImGui::Spacing();
            
            ImGui::TextDisabled("Hostname:");
            strncpy_s(g_app.hostname, GetHostname().c_str(), sizeof(g_app.hostname) - 1);
            ImGui::Text("%s", g_app.hostname);
            
            ImGui::EndChild();
            
            // Right Panel - TARGET MACHINE SETUP
            ImGui::SameLine();
            ImGui::BeginChild("TargetPanel", ImVec2(panel_width, 150), true);
            ImGui::TextDisabled("TARGET MACHINE SETUP");
            ImGui::Separator();
            ImGui::Spacing();
            
            ImGui::TextDisabled("Remote IP (Target PC):");
            ImGui::InputText("##TargetIP", g_app.target_ip, sizeof(g_app.target_ip));
            ImGui::Spacing();
            
            ImGui::TextDisabled("Port:");
            ImGui::InputText("##Port", g_app.port, sizeof(g_app.port));
            ImGui::Spacing();
            
            // Buttons
            float btn_width = panel_width - 20;
            
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.48f, 0.8f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.6f, 1.0f, 1.0f));
            if (ImGui::Button("[🔗 CONNECT]", ImVec2(btn_width, 35))) {
                g_app.connected = true;
                g_app.status = "CONNECTED - Sending mouse clicks to " + std::string(g_app.target_ip) + ":" + g_app.port;
                g_app.logs.push_back("[INFO] Connected to " + std::string(g_app.target_ip) + ":" + g_app.port);
            }
            ImGui::PopStyleColor(2);
            
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
            if (!g_app.connected) ImGui::BeginDisabled();
            if (ImGui::Button("[DISCONNECT]", ImVec2(btn_width, 35))) {
                g_app.connected = false;
                g_app.status = "IDLE - Waiting for connection.";
                g_app.logs.push_back("[INFO] Disconnected");
            }
            if (!g_app.connected) ImGui::EndDisabled();
            ImGui::PopStyleColor(2);
            
            ImGui::EndChild();
            
            ImGui::Spacing();
            
            // === TABLE SECTION ===
            if (ImGui::BeginTable("Connections", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Name");
                ImGui::TableSetupColumn("Address");
                ImGui::TableSetupColumn("Description");
                
                ImGui::TableHeadersRow();
                
                // Empty row for structure
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("No active connections...");
                
                ImGui::EndTable();
            }
            
            ImGui::Spacing();
            
            // === STATUS BAR ===
            ImGui::TextDisabled("Status: %s", g_app.status.c_str());
            
            ImGui::EndTabItem();
        } else {
            ImGui::EndTabItem();
        }
        
        // Other tabs (placeholders)
        if (ImGui::BeginTabItem("SYSTEM")) {
            ImGui::Text("System settings placeholder");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("SETTINGS")) {
            ImGui::Text("Settings placeholder");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("LOGS")) {
            if (ImGui::BeginChild("LogWindow", ImVec2(0, 0), true)) {
                for (const auto& log : g_app.logs) {
                    ImGui::Text("%s", log.c_str());
                }
                ImGui::EndChild();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("ABOUT")) {
            ImGui::Text("PWNZ VISION PRO v1.0");
            ImGui::Text("UDP Ultimate Mouse Click Sender");
            ImGui::EndTabItem();
        }
        
        ImGui::EndTabBar();
    }
    
    ImGui::End();
}

// Device creation and cleanup
bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    
    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    
    if (D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
        featureLevels, 3, D3D11_SDK_VERSION, &sd, &g_pSwapChain,
        &g_pd3dDevice, &g_featureLevel, &g_pd3dDeviceContext
    ) < 0) {
        return false;
    }
    
    return true;
}

void CleanupDeviceD3D() {
    if (g_pSwapChain) g_pSwapChain->Release();
    if (g_pd3dDeviceContext) g_pd3dDeviceContext->Release();
    if (g_pd3dDevice) g_pd3dDevice->Release();
}

void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Initialize Winsock
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    
    // Register window class
    WNDCLASSEX wc = {
        sizeof(WNDCLASSEX),
        CS_CLASSDC,
        WndProc,
        0L, 0L,
        GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr,
        "PWNZVisionPro", nullptr
    };
    RegisterClassEx(&wc);
    
    // Create window
    HWND hwnd = CreateWindow(
        wc.lpszClassName, "PWNZ VISION PRO (UDP ULTIMATE)",
        WS_OVERLAPPEDWINDOW, 100, 100, 900, 600,
        nullptr, nullptr, wc.hInstance, nullptr
    );
    
    // Initialize Direct3D
    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        UnregisterClass(wc.lpszClassName, wc.hInstance);
        return 1;
    }
    
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);
    
    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
    
    // Main loop
    MSG msg;
    ZeroMemory(&msg, sizeof(msg));
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            continue;
        }
        
        // Handle resize
        if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }
        
        RenderFrame();
        
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        static const float clear_color[] = {0.12f, 0.12f, 0.12f, 1.0f};
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        
        g_pSwapChain->Present(1, 0);
    }
    
    // Cleanup
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    
    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClass(wc.lpszClassName, wc.hInstance);
    
    WSACleanup();
    
    return 0;
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;
    
    switch (msg) {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) return 0;
        g_ResizeWidth = LOWORD(lParam);
        g_ResizeHeight = HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}
