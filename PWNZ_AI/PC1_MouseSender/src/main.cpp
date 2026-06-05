// PWNZ VISION PRO (UDP ULTIMATE) - Mouse Click Sender GUI
// Главный файл приложения Dear ImGui для ПК1
// Стандарт: C++23

#include <windows.h>
#include <d3d9.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <format>
#include <iostream>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx9.h"

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "imm32.lib")

// Глобальные переменные
static ID3DXFont* g_pFont = NULL;
static ID3DXFont* g_pFontBold = NULL;
static bool g_bAutoConnect = true;
static bool g_bDebugMode = false;
static char g_szRemoteIP[64] = "10.0.0.120";
static char g_szPort[16] = "8080";
static bool g_bConnected = false;
static std::string g_szLocalIP = "192.168.1.105";
static std::string g_szHostname = "MY-DESKTOP-01";
static std::string g_szStatus = "IDLE - Waiting for connection";

// Прототипы функций
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
bool GetLocalIP(std::string& ip);
std::string GetHostname();
void DrawLogo(ImDrawList* draw_list, ImVec2 pos, float size);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Инициализация Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        MessageBox(NULL, "Failed to initialize Winsock", "Error", MB_ICONERROR);
        return 1;
    }

    // Получение локальной информации
    g_szLocalIP = GetLocalIP(g_szLocalIP);
    g_szHostname = GetHostname();

    // Регистрация класса окна
    WNDCLASSEXW wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandleW(nullptr), nullptr, nullptr, nullptr, nullptr, L"PWNZ Vision Pro", nullptr };
    RegisterClassExW(&wc);

    // Создание окна
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"PWNZ VISION PRO (UDP ULTIMATE)", WS_OVERLAPPEDWINDOW, 100, 100, 800, 600, nullptr, nullptr, wc.hInstance, nullptr);

    // Инициализация Direct3D
    LPDIRECT3D9 pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (!pD3D) {
        UnregisterClass(wc.lpszClassName, wc.hInstance);
        WSACleanup();
        return 1;
    }

    D3DPRESENT_PARAMETERS d3dpp{};
    d3dpp.Windowed = TRUE;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;
    d3dpp.EnableAutoDepthStencil = TRUE;
    d3dpp.AutoDepthStencilFormat = D3DFMT_D16;
    d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;

    LPDIRECT3DDEVICE9 pd3dDevice = nullptr;
    if (pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &d3dpp, &pd3dDevice) < 0) {
        pD3D->Release();
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        WSACleanup();
        return 1;
    }

    // Инициализация ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Настройка стиля (темная тема с синим акцентом)
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    
    // Цветовая палитра
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.12f, 0.12f, 0.12f, 1.00f); // ~#1E1E1E
    style.Colors[ImGuiCol_Header] = ImVec4(0.00f, 0.48f, 0.80f, 1.00f);   // #007ACC
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.00f, 0.58f, 0.90f, 1.00f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.00f, 0.38f, 0.70f, 1.00f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.00f, 0.48f, 0.80f, 1.00f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.00f, 0.58f, 0.90f, 1.00f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.00f, 0.38f, 0.70f, 1.00f);
    style.Colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    style.Colors[ImGuiCol_TabHovered] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    style.Colors[ImGuiCol_TabActive] = ImVec4(0.00f, 0.48f, 0.80f, 1.00f); // Активная вкладка синяя
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.00f, 0.48f, 0.80f, 0.50f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.30f, 0.30f, 0.30f, 0.50f);
    style.Colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.67f, 0.67f, 0.67f, 1.00f); // #AAAAAA

    // Переменные стиля
    style.WindowRounding = 5.0f;
    style.FrameRounding = 4.0f;
    style.FramePadding = ImVec2(8, 6);
    style.ItemSpacing = ImVec2(10, 8);
    style.TabRounding = 4.0f;

    // Инициализация бэкендов
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX9_Init(pd3dDevice);

    // Показать окно
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    // Главный цикл
    MSG msg{};
    ZeroMemory(&msg, sizeof(msg));
    while (msg.message != WM_QUIT) {
        if (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            continue;
        }

        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Главное окно
        ImGui::SetNextWindowSize(ImVec2(800, 600));
        ImGui::SetNextWindowPos(ImVec2(100, 100));
        
        if (ImGui::Begin("PWNZ VISION PRO (UDP ULTIMATE)", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse)) {
            
            // Вкладки
            if (ImGui::BeginTabBar("MainTabs")) {
                
                // Вкладка SYSTEM
                if (ImGui::BeginTabItem("SYSTEM")) {
                    ImGui::Text("System settings and information will be here.");
                    ImGui::EndTabItem();
                }
                
                // Вкладка NETWORK (активная по умолчанию)
                if (ImGui::BeginTabItem("NETWORK")) {
                    // Устанавливаем активную вкладку
                    static bool firstTime = true;
                    if (firstTime) {
                        ImGui::SetTabItemClosed("NETWORK"); // Сброс
                        firstTime = false;
                    }
                    
                    // Заголовок с логотипом
                    ImDrawList* draw_list = ImGui::GetWindowDrawList();
                    ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
                    
                    // Рисуем логотип
                    DrawLogo(draw_list, cursor_pos, 40.0f);
                    
                    ImGui::SameLine(60);
                    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "PWNZ VISION PRO");
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.67f, 0.67f, 0.67f, 1.0f), "(UDP ULTIMATE)");
                    
                    // Чекбоксы справа
                    float content_width = ImGui::GetContentRegionAvail().x;
                    float checkbox_width = 250.0f;
                    ImGui::SameLine(content_width - checkbox_width);
                    
                    ImGui::Checkbox("Autoconnect", &g_bAutoConnect);
                    ImGui::SameLine();
                    ImGui::Checkbox("Debug Mode", &g_bDebugMode);
                    
                    ImGui::Separator();
                    ImGui::Spacing();
                    
                    // Две колонки с информацией
                    ImGui::Columns(2, "InfoColumns", false);
                    
                    // Левая колонка: LOCAL SYSTEM INFO
                    ImGui::BeginGroup();
                    ImGui::TextColored(ImVec4(0.67f, 0.67f, 0.67f, 1.0f), "LOCAL SYSTEM INFO");
                    ImGui::Separator();
                    ImGui::Spacing();
                    
                    ImGui::TextDisabled("Current IP:");
                    ImGui::Text("%s (automatically detected)", g_szLocalIP.c_str());
                    ImGui::Spacing();
                    
                    ImGui::TextDisabled("Hostname:");
                    ImGui::Text("%s", g_szHostname.c_str());
                    
                    ImGui::EndGroup();
                    
                    ImGui::NextColumn();
                    
                    // Правая колонка: TARGET MACHINE SETUP
                    ImGui::BeginGroup();
                    ImGui::TextColored(ImVec4(0.67f, 0.67f, 0.67f, 1.0f), "TARGET MACHINE SETUP");
                    ImGui::Separator();
                    ImGui::Spacing();
                    
                    ImGui::TextDisabled("Remote IP (Target PC):");
                    ImGui::PushItemWidth(-1);
                    ImGui::InputText("##RemoteIP", g_szRemoteIP, sizeof(g_szRemoteIP));
                    ImGui::PopItemWidth();
                    ImGui::Spacing();
                    
                    ImGui::TextDisabled("Port:");
                    ImGui::PushItemWidth(-1);
                    ImGui::InputText("##Port", g_szPort, sizeof(g_szPort));
                    ImGui::PopItemWidth();
                    ImGui::Spacing();
                    
                    // Кнопки
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.00f, 0.48f, 0.80f, 1.00f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.00f, 0.58f, 0.90f, 1.00f));
                    if (ImGui::Button("[🔗 CONNECT]", ImVec2(-1, 30))) {
                        g_bConnected = true;
                        g_szStatus = std::format("CONNECTED - Sending mouse clicks to {}:{}" , g_szRemoteIP, g_szPort);
                    }
                    ImGui::PopStyleColor(2);
                    
                    ImGui::Spacing();
                    
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.30f, 0.30f, 1.00f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.40f, 0.40f, 0.40f, 1.00f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 1.00f, 1.00f, 1.00f));
                    if (ImGui::Button("[DISCONNECT]", ImVec2(-1, 30))) {
                        g_bConnected = false;
                        g_szStatus = "IDLE - Waiting for connection";
                    }
                    ImGui::PopStyleColor(3);
                    
                    ImGui::EndGroup();
                    
                    ImGui::Columns(1);
                    
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();
                    
                    // Таблица подключений
                    if (ImGui::BeginTable("Connections", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.3f);
                        ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthStretch, 0.4f);
                        ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch, 0.3f);
                        
                        ImGui::TableHeadersRow();
                        
                        // Пустая строка для демонстрации структуры
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextDisabled("No active connections");
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextDisabled("-");
                        ImGui::TableSetColumnIndex(2);
                        ImGui::TextDisabled("-");
                        
                        ImGui::EndTable();
                    }
                    
                    ImGui::Spacing();
                    
                    // Строка состояния
                    ImGui::TextColored(ImVec4(0.67f, 0.67f, 0.67f, 1.0f), "Status: %s", g_szStatus.c_str());
                    
                    ImGui::EndTabItem();
                }
                
                // Вкладка SETTINGS
                if (ImGui::BeginTabItem("SETTINGS")) {
                    ImGui::Text("Application settings will be here.");
                    ImGui::EndTabItem();
                }
                
                // Вкладка LOGS
                if (ImGui::BeginTabItem("LOGS")) {
                    ImGui::Text("Event logs will be displayed here.");
                    ImGui::EndTabItem();
                }
                
                // Вкладка ABOUT
                if (ImGui::BeginTabItem("ABOUT")) {
                    ImGui::Text("PWNZ VISION PRO (UDP ULTIMATE)");
                    ImGui::Spacing();
                    ImGui::Text("Version: 1.0.0");
                    ImGui::Text("Author: PWNZ AI Team");
                    ImGui::Spacing();
                    ImGui::TextDisabled("This application sends mouse click events via UDP to a remote machine.");
                    ImGui::EndTabItem();
                }
                
                ImGui::EndTabBar();
            }
        }
        ImGui::End();

        // Рендеринг
        pd3dDevice->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_COLORVALUE(0.12f, 0.12f, 0.12f), 1.0f, 0);
        if (pd3dDevice->BeginScene() >= 0) {
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
            pd3dDevice->EndScene();
        }
        pd3dDevice->Present(nullptr, nullptr, nullptr, nullptr);
    }

    // Очистка
    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    pd3dDevice->Release();
    pD3D->Release();
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    WSACleanup();

    return 0;
}

// Обработчик сообщений окна
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return TRUE;

    switch (msg) {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;
        break;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// Получение локального IP-адреса
bool GetLocalIP(std::string& ip) {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == SOCKET_ERROR) {
        ip = "192.168.1.105"; // Значение по умолчанию
        return false;
    }

    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(hostname, nullptr, &hints, &res) != 0) {
        ip = "192.168.1.105";
        return false;
    }

    struct sockaddr_in* addr = reinterpret_cast<struct sockaddr_in*>(res->ai_addr);
    inet_ntop(AF_INET, &(addr->sin_addr), hostname, sizeof(hostname));
    ip = hostname;

    freeaddrinfo(res);
    return true;
}

// Получение имени хоста
std::string GetHostname() {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == SOCKET_ERROR) {
        return "UNKNOWN-HOST";
    }
    return std::string(hostname);
}

// Отрисовка логотипа (щит с буквами PV)
void DrawLogo(ImDrawList* draw_list, ImVec2 pos, float size) {
    float shield_width = size * 1.2f;
    float shield_height = size * 1.4f;
    
    // Координаты щита
    ImVec2 top_center = ImVec2(pos.x + shield_width / 2, pos.y);
    ImVec2 bottom_left = ImVec2(pos.x, pos.y + shield_height);
    ImVec2 bottom_right = ImVec2(pos.x + shield_width, pos.y + shield_height);
    ImVec2 mid_left = ImVec2(pos.x, pos.y + shield_height * 0.4f);
    ImVec2 mid_right = ImVec2(pos.x + shield_width, pos.y + shield_height * 0.4f);
    
    // Заливка щита (светло-синяя)
    ImColor shield_fill = ImColor(60, 100, 160, 200);
    ImColor shield_border = ImColor(30, 60, 100, 255);
    
    // Рисуем щит (упрощенная форма)
    draw_list->AddQuadFilled(top_center, mid_right, bottom_right, bottom_left, shield_fill);
    draw_list->AddQuad(top_center, mid_right, bottom_right, bottom_left, shield_border, 2.0f);
    
    // Текст "PV" по центру
    ImVec2 text_pos = ImVec2(pos.x + shield_width / 2 - 10, pos.y + shield_height / 2 - 10);
    draw_list->AddText(text_pos, IM_COL32_WHITE, "PV");
}
