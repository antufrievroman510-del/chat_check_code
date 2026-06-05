// MouseClickSender.cpp - Отдельная программа для ПК1 (Игровой)
// Компилировать: cl /EHsc MouseClickSender.cpp Ws2_32.lib /Fe:MouseClickSender.exe
// Поддерживает: ЛКМ, ПКМ, СКМ, Колесо, Боковые кнопки (X1/X2)

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <string>
#include <thread>
#include <atomic>

#pragma comment(lib, "Ws2_32.lib")

// Глобальные переменные для состояния мыши
std::atomic<bool> g_lmb_pressed(false);
std::atomic<bool> g_rmb_pressed(false);
std::atomic<bool> g_mmb_pressed(false);
std::atomic<bool> g_x1_pressed(false);
std::atomic<bool> g_x2_pressed(false);
SOCKET g_socket = INVALID_SOCKET;
sockaddr_in g_serverAddr{};
bool g_connected = false;

// Типы событий в пакете
#define MOUSE_EVENT_LMB_DOWN   0x01
#define MOUSE_EVENT_LMB_UP     0x02
#define MOUSE_EVENT_RMB_DOWN   0x03
#define MOUSE_EVENT_RMB_UP     0x04
#define MOUSE_EVENT_MMB_DOWN   0x05
#define MOUSE_EVENT_MMB_UP     0x06
#define MOUSE_EVENT_WHEEL_UP   0x07
#define MOUSE_EVENT_WHEEL_DOWN 0x08
#define MOUSE_EVENT_X1_DOWN    0x09
#define MOUSE_EVENT_X1_UP      0x0A
#define MOUSE_EVENT_X2_DOWN    0x0B
#define MOUSE_EVENT_X2_UP      0x0C

// Функция отправки пакета
void send_click(uint8_t event_type, int16_t wheel_delta = 0) {
    if (!g_connected || g_socket == INVALID_SOCKET) return;

    char buffer[8];
    buffer[0] = event_type;      // Тип события
    buffer[1] = 0;               // Резерв
    *(int16_t*)&buffer[2] = htons(wheel_delta); // Для колеса
    *(int32_t*)&buffer[4] = 0;   // Резерв

    sendto(g_socket, buffer, sizeof(buffer), 0, (sockaddr*)&g_serverAddr, sizeof(g_serverAddr));
}

// Хук для перехвата нажатий мыши (низкоуровневый)
HHOOK g_mouse_hook = NULL;

LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        MSLLHOOKSTRUCT* pMouseStruct = (MSLLHOOKSTRUCT*)lParam;

        // ЛКМ
        if (wParam == WM_LBUTTONDOWN) {
            g_lmb_pressed = true;
            send_click(MOUSE_EVENT_LMB_DOWN);
            std::cout << "[SEND] LMB PRESSED\n";
        }
        else if (wParam == WM_LBUTTONUP) {
            g_lmb_pressed = false;
            send_click(MOUSE_EVENT_LMB_UP);
            std::cout << "[SEND] LMB RELEASED\n";
        }
        // ПКМ
        else if (wParam == WM_RBUTTONDOWN) {
            g_rmb_pressed = true;
            send_click(MOUSE_EVENT_RMB_DOWN);
            std::cout << "[SEND] RMB PRESSED\n";
        }
        else if (wParam == WM_RBUTTONUP) {
            g_rmb_pressed = false;
            send_click(MOUSE_EVENT_RMB_UP);
            std::cout << "[SEND] RMB RELEASED\n";
        }
        // СКМ (Колесико нажатие)
        else if (wParam == WM_MBUTTONDOWN) {
            g_mmb_pressed = true;
            send_click(MOUSE_EVENT_MMB_DOWN);
            std::cout << "[SEND] MMB PRESSED\n";
        }
        else if (wParam == WM_MBUTTONUP) {
            g_mmb_pressed = false;
            send_click(MOUSE_EVENT_MMB_UP);
            std::cout << "[SEND] MMB RELEASED\n";
        }
        // Колесо прокрутка
        else if (wParam == WM_MOUSEWHEEL) {
            SHORT wheel_delta = GET_WHEEL_DELTA_WPARAM(pMouseStruct->mouseData);
            if (wheel_delta > 0) {
                send_click(MOUSE_EVENT_WHEEL_UP, wheel_delta);
                std::cout << "[SEND] WHEEL UP (" << wheel_delta << ")\n";
            } else if (wheel_delta < 0) {
                send_click(MOUSE_EVENT_WHEEL_DOWN, -wheel_delta);
                std::cout << "[SEND] WHEEL DOWN (" << -wheel_delta << ")\n";
            }
        }
        // Боковая кнопка X1 (Назад)
        else if (wParam == WM_XBUTTONDOWN) {
            if (GET_XBUTTON_WPARAM(pMouseStruct->mouseData) == XBUTTON1) {
                g_x1_pressed = true;
                send_click(MOUSE_EVENT_X1_DOWN);
                std::cout << "[SEND] X1 (BACK) PRESSED\n";
            }
            else if (GET_XBUTTON_WPARAM(pMouseStruct->mouseData) == XBUTTON2) {
                g_x2_pressed = true;
                send_click(MOUSE_EVENT_X2_DOWN);
                std::cout << "[SEND] X2 (FORWARD) PRESSED\n";
            }
        }
        else if (wParam == WM_XBUTTONUP) {
            if (GET_XBUTTON_WPARAM(pMouseStruct->mouseData) == XBUTTON1) {
                g_x1_pressed = false;
                send_click(MOUSE_EVENT_X1_UP);
                std::cout << "[SEND] X1 (BACK) RELEASED\n";
            }
            else if (GET_XBUTTON_WPARAM(pMouseStruct->mouseData) == XBUTTON2) {
                g_x2_pressed = false;
                send_click(MOUSE_EVENT_X2_UP);
                std::cout << "[SEND] X2 (FORWARD) RELEASED\n";
            }
        }
    }
    return CallNextHookEx(g_mouse_hook, nCode, wParam, lParam);
}

// Попытка переподключения
void try_reconnect(const std::string& ip, int port) {
    if (g_socket != INVALID_SOCKET) {
        closesocket(g_socket);
        g_socket = INVALID_SOCKET;
    }

    g_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_socket == INVALID_SOCKET) {
        std::cout << "[ERROR] Socket creation failed: " << WSAGetLastError() << "\n";
        return;
    }

    g_serverAddr.sin_family = AF_INET;
    g_serverAddr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &g_serverAddr.sin_addr);

    // Тестовый пакет (8 байт для совместимости с новым форматом)
    char test_buf[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    if (sendto(g_socket, test_buf, sizeof(test_buf), 0, (sockaddr*)&g_serverAddr, sizeof(g_serverAddr)) != SOCKET_ERROR) {
        g_connected = true;
        std::cout << "[OK] Connected to " << ip << ":" << port << "\n";
    } else {
        std::cout << "[ERROR] Cannot connect to " << ip << ":" << port << "\n";
        g_connected = false;
    }
}

int main() {
    std::string server_ip;
    int server_port = 5556;

    std::cout << "===========================================\n";
    std::cout << "   Mouse Click Sender for 2PC Setup\n";
    std::cout << "   (C) PWNZ_AI\n";
    std::cout << "===========================================\n\n";

    std::cout << "Enter IP address of second PC (default: 192.168.1.100): ";
    std::getline(std::cin, server_ip);
    if (server_ip.empty()) server_ip = "192.168.1.100";

    std::cout << "Enter port (default: 5556): ";
    std::string port_str;
    std::getline(std::cin, port_str);
    if (!port_str.empty()) server_port = std::stoi(port_str);

    std::cout << "\nInitializing WinSock...\n";
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed!\n";
        return 1;
    }

    // Первая попытка подключения
    try_reconnect(server_ip, server_port);

    // Поток для периодической проверки соединения
    std::thread reconnect_thread([&]() {
        while (true) {
            if (!g_connected) {
                std::cout << "[INFO] Trying to reconnect...\n";
                try_reconnect(server_ip, server_port);
            }
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    });
    reconnect_thread.detach();

    // Установка глобального хука мыши
    std::cout << "Installing mouse hook...\n";
    g_mouse_hook = SetWindowsHookEx(WH_MOUSE_LL, MouseHookProc, NULL, 0);
    if (!g_mouse_hook) {
        std::cerr << "Failed to install mouse hook!\n";
        WSACleanup();
        return 1;
    }

    std::cout << "\n=== LISTENING FOR MOUSE CLICKS ===\n";
    std::cout << "Press Ctrl+C to exit\n\n";

    // Главный цикл сообщений
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Очистка
    UnhookWindowsHookEx(g_mouse_hook);
    closesocket(g_socket);
    WSACleanup();

    return 0;
}
