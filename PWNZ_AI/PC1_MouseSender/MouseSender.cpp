/*
 * MouseClickSender - Программа для отправки нажатий мыши на второй ПК
 * Устанавливается на ПК1 (Игровой)
 * 
 * Компиляция: cl /EHsc MouseSender.cpp Ws2_32.lib /Fe:MouseSender.exe
 */

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <fstream>

#pragma comment(lib, "Ws2_32.lib")

// Глобальные переменные
std::atomic<bool> g_running(true);
SOCKET g_socket = INVALID_SOCKET;
std::string g_target_ip = "192.168.1.100"; // IP второго ПК (читового)
int g_target_port = 5556;

// Структура пакета клика
#pragma pack(push, 1)
struct ClickPacket {
    uint8_t button; // 0 = ЛКМ, 1 = ПКМ
    uint8_t pressed; // 1 = нажато, 0 = отпущено
};
#pragma pack(pop)

// Чтение конфигурации из файла
void load_config() {
    std::ifstream config("config.txt");
    if (config.is_open()) {
        std::string line;
        while (std::getline(config, line)) {
            if (line.find("IP=") == 0) {
                g_target_ip = line.substr(3);
            } else if (line.find("PORT=") == 0) {
                g_target_port = std::stoi(line.substr(5));
            }
        }
        config.close();
        std::cout << "[INFO] Конфигурация загружена: " << g_target_ip << ":" << g_target_port << std::endl;
    } else {
        std::cout << "[INFO] Файл config.txt не найден, используются значения по умолчанию." << std::endl;
    }
}

// Отправка пакета
bool send_click(uint8_t button, uint8_t pressed) {
    if (g_socket == INVALID_SOCKET) return false;

    ClickPacket packet;
    packet.button = button;
    packet.pressed = pressed;

    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(g_target_port);
    inet_pton(AF_INET, g_target_ip.c_str(), &server_addr.sin_addr);

    int result = sendto(g_socket, (const char*)&packet, sizeof(packet), 0, 
                        (struct sockaddr*)&server_addr, sizeof(server_addr));
    
    return result > 0;
}

// Глобальный хук для перехвата нажатий мыши
HHOOK g_mouse_hook = NULL;

LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        MSLLHOOKSTRUCT* pMouseStruct = (MSLLHOOKSTRUCT*)lParam;
        
        uint8_t button = 0xFF;
        uint8_t pressed = 0;

        switch (wParam) {
            case WM_LBUTTONDOWN:
                button = 0; // ЛКМ
                pressed = 1;
                break;
            case WM_LBUTTONUP:
                button = 0;
                pressed = 0;
                break;
            case WM_RBUTTONDOWN:
                button = 1; // ПКМ
                pressed = 1;
                break;
            case WM_RBUTTONUP:
                button = 1;
                pressed = 0;
                break;
        }

        if (button != 0xFF) {
            if (!send_click(button, pressed)) {
                // Попытка переподключения при ошибке
                std::cout << "[WARN] Ошибка отправки, попытка переподключения..." << std::endl;
                closesocket(g_socket);
                g_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            } else {
                const char* btn_name = (button == 0) ? "ЛКМ" : "ПКМ";
                const char* action = pressed ? "НАЖАТА" : "ОТПУЩЕНА";
                std::cout << "[CLICK] " << btn_name << " " << action << std::endl;
            }
        }
    }
    return CallNextHookEx(g_mouse_hook, nCode, wParam, lParam);
}

// Установка глобального хука
void install_hook() {
    g_mouse_hook = SetWindowsHookEx(WH_MOUSE_LL, MouseHookProc, NULL, 0);
    if (!g_mouse_hook) {
        std::cerr << "[ERROR] Не удалось установить хук! Код ошибки: " << GetLastError() << std::endl;
        exit(1);
    }
    std::cout << "[OK] Хук мыши установлен успешно." << std::endl;
}

// Инициализация Winsock и сокета
void init_network() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "[ERROR] Ошибка инициализации Winsock!" << std::endl;
        exit(1);
    }

    g_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_socket == INVALID_SOCKET) {
        std::cerr << "[ERROR] Ошибка создания сокета!" << std::endl;
        WSACleanup();
        exit(1);
    }

    std::cout << "[OK] Сетевой интерфейс инициализирован." << std::endl;
}

// Обработка Ctrl+C
BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType) {
    if (dwCtrlType == CTRL_C_EVENT || dwCtrlType == CTRL_CLOSE_EVENT) {
        std::cout << "\n[INFO] Завершение работы..." << std::endl;
        g_running = false;
        return TRUE;
    }
    return FALSE;
}

int main() {
    SetConsoleTitleA("PWNZ AI - Mouse Click Sender (PC1)");
    std::cout << "========================================" << std::endl;
    std::cout << "   PWNZ AI - Mouse Click Sender (PC1)   " << std::endl;
    std::cout << "========================================" << std::endl;

    // Загрузка конфига
    load_config();

    // Инициализация сети
    init_network();

    // Установка обработчика Ctrl+C
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    // Установка хука
    install_hook();

    std::cout << "\n[INFO] Ожидание нажатий мыши..." << std::endl;
    std::cout << "[INFO] Нажмите Ctrl+C для выхода." << std::endl;

    // Цикл обработки сообщений
    MSG msg;
    while (g_running && GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Очистка
    if (g_mouse_hook) UnhookWindowsHookEx(g_mouse_hook);
    if (g_socket != INVALID_SOCKET) closesocket(g_socket);
    WSACleanup();

    std::cout << "[INFO] Программа завершена." << std::endl;
    return 0;
}
