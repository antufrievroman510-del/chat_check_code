// MouseClickSender.cpp - Отдельная программа для ПК1 (Игровой)
// Компилировать: cl /EHsc MouseClickSender.cpp Ws2_32.lib /Fe:MouseClickSender.exe

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
SOCKET g_socket = INVALID_SOCKET;
sockaddr_in g_serverAddr{};
bool g_connected = false;

// Функция отправки пакета
void send_click(uint8_t type, uint8_t state) {
    if (!g_connected || g_socket == INVALID_SOCKET) return;

    char buffer[4];
    buffer[0] = type;   // 0x01 = ЛКМ, 0x02 = ПКМ
    buffer[1] = state;  // 1 = нажато, 0 = отпущено
    buffer[2] = 0;
    buffer[3] = 0;

    sendto(g_socket, buffer, sizeof(buffer), 0, (sockaddr*)&g_serverAddr, sizeof(g_serverAddr));
}

// Хук для перехвата нажатий мыши (низкоуровневый)
HHOOK g_mouse_hook = NULL;

LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        MSLLHOOKSTRUCT* pMouseStruct = (MSLLHOOKSTRUCT*)lParam;

        if (wParam == WM_LBUTTONDOWN) {
            g_lmb_pressed = true;
            send_click(0x01, 1); // ЛКМ нажата
            std::cout << "[SEND] LMB PRESSED\n";
        }
        else if (wParam == WM_LBUTTONUP) {
            g_lmb_pressed = false;
            send_click(0x01, 0); // ЛКМ отпущена
            std::cout << "[SEND] LMB RELEASED\n";
        }
        else if (wParam == WM_RBUTTONDOWN) {
            g_rmb_pressed = true;
            send_click(0x02, 1); // ПКМ нажата
            std::cout << "[SEND] RMB PRESSED\n";
        }
        else if (wParam == WM_RBUTTONUP) {
            g_rmb_pressed = false;
            send_click(0x02, 0); // ПКМ отпущена
            std::cout << "[SEND] RMB RELEASED\n";
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

    // Тестовый пакет
    char test_buf[4] = { 0x00, 0x00, 0x00, 0x00 };
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
