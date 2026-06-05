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

// Структура пакета клика (должна совпадать с MouseClickServer.h)
#pragma pack(push, 1)
struct ClickPacket {
    uint8_t event_type; // Тип события (должен совпадать с константами в сервере)
    uint8_t reserved;   // Выравнивание
    int16_t wheel_delta; // Для колеса прокрутки (+120/-120), для кнопок = 0
};
#pragma pack(pop)

// Типы событий (должны точно совпадать с MouseClickServer.h строки 48-59)
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
bool send_click(uint8_t event_type, int16_t wheel_delta = 0) {
    if (g_socket == INVALID_SOCKET) return false;

    ClickPacket packet;
    packet.event_type = event_type;
    packet.reserved = 0;
    packet.wheel_delta = htons(wheel_delta); // Преобразуем в network byte order

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
        
        uint8_t event_type = 0;

        switch (wParam) {
            case WM_LBUTTONDOWN:
                event_type = MOUSE_EVENT_LMB_DOWN;
                break;
            case WM_LBUTTONUP:
                event_type = MOUSE_EVENT_LMB_UP;
                break;
            case WM_RBUTTONDOWN:
                event_type = MOUSE_EVENT_RMB_DOWN;
                break;
            case WM_RBUTTONUP:
                event_type = MOUSE_EVENT_RMB_UP;
                break;
            case WM_MBUTTONDOWN:
                event_type = MOUSE_EVENT_MMB_DOWN;
                break;
            case WM_MBUTTONUP:
                event_type = MOUSE_EVENT_MMB_UP;
                break;
            case WM_MOUSEWHEEL:
                // Определяем направление прокрутки
                if (GET_WHEEL_DELTA_WPARAM(pMouseStruct->mouseData) > 0) {
                    event_type = MOUSE_EVENT_WHEEL_UP;
                } else {
                    event_type = MOUSE_EVENT_WHEEL_DOWN;
                }
                break;
            case WM_XBUTTONDOWN:
                if (HIWORD(pMouseStruct->mouseData) == XBUTTON1) {
                    event_type = MOUSE_EVENT_X1_DOWN;
                } else if (HIWORD(pMouseStruct->mouseData) == XBUTTON2) {
                    event_type = MOUSE_EVENT_X2_DOWN;
                }
                break;
            case WM_XBUTTONUP:
                if (HIWORD(pMouseStruct->mouseData) == XBUTTON1) {
                    event_type = MOUSE_EVENT_X1_UP;
                } else if (HIWORD(pMouseStruct->mouseData) == XBUTTON2) {
                    event_type = MOUSE_EVENT_X2_UP;
                }
                break;
        }

        if (event_type != 0) {
            if (!send_click(event_type)) {
                // Попытка переподключения при ошибке
                std::cout << "[WARN] Ошибка отправки, попытка переподключения..." << std::endl;
                closesocket(g_socket);
                g_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            } else {
                const char* event_name = "UNKNOWN";
                switch (event_type) {
                    case MOUSE_EVENT_LMB_DOWN: event_name = "ЛКМ НАЖАТА"; break;
                    case MOUSE_EVENT_LMB_UP: event_name = "ЛКМ ОТПУЩЕНА"; break;
                    case MOUSE_EVENT_RMB_DOWN: event_name = "ПКМ НАЖАТА"; break;
                    case MOUSE_EVENT_RMB_UP: event_name = "ПКМ ОТПУЩЕНА"; break;
                    case MOUSE_EVENT_MMB_DOWN: event_name = "СКМ НАЖАТА"; break;
                    case MOUSE_EVENT_MMB_UP: event_name = "СКМ ОТПУЩЕНА"; break;
                    case MOUSE_EVENT_WHEEL_UP: event_name = "КОЛЕСО ВВЕРХ"; break;
                    case MOUSE_EVENT_WHEEL_DOWN: event_name = "КОЛЕСО ВНИЗ"; break;
                    case MOUSE_EVENT_X1_DOWN: event_name = "X1 (НАЗАД) НАЖАТА"; break;
                    case MOUSE_EVENT_X1_UP: event_name = "X1 (НАЗАД) ОТПУЩЕНА"; break;
                    case MOUSE_EVENT_X2_DOWN: event_name = "X2 (ВПЕРЕД) НАЖАТА"; break;
                    case MOUSE_EVENT_X2_UP: event_name = "X2 (ВПЕРЕД) ОТПУЩЕНА"; break;
                }
                std::cout << "[CLICK] " << event_name << std::endl;
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
