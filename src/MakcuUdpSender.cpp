// Программа для ПК №1 (с мышью и Makcu)
// Перехватывает нажатия мыши и отправляет их по UDP на ПК №2

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <atomic>

#pragma comment(lib, "ws2_32.lib")

// Конфигурация
const char* TARGET_IP = "192.168.1.100"; // IP адрес ПК №2 (где запущен чит)
const int TARGET_PORT = 9999;

// Протокол: Байт 1 = тип кнопки, Байт 2 = состояние (1=нажато, 0=отпущено)
constexpr uint8_t BTN_LMB = 0x01;
constexpr uint8_t BTN_RMB = 0x02;
constexpr uint8_t BTN_WHEEL = 0x04;
constexpr uint8_t BTN_SIDE1 = 0x08;
constexpr uint8_t BTN_SIDE2 = 0x10;

std::atomic<bool> g_running(true);
SOCKET g_sock = INVALID_SOCKET;

// Функция отправки события
void SendButtonEvent(uint8_t button, uint8_t state) {
    if (g_sock == INVALID_SOCKET) return;

    sockaddr_in target_addr{};
    target_addr.sin_family = AF_INET;
    inet_pton(AF_INET, TARGET_IP, &target_addr.sin_addr);
    target_addr.sin_port = htons(TARGET_PORT);

    char buffer[2] = { (char)button, (char)state };
    
    int result = sendto(g_sock, buffer, 2, 0, (sockaddr*)&target_addr, sizeof(target_addr));
    if (result == SOCKET_ERROR) {
        std::cerr << "[SENDER] Ошибка отправки: " << WSAGetLastError() << "\n";
    } else {
        const char* btn_name = "";
        switch(button) {
            case BTN_LMB: btn_name = "ЛКМ"; break;
            case BTN_RMB: btn_name = "ПКМ"; break;
            case BTN_WHEEL: btn_name = "Колесо"; break;
            case BTN_SIDE1: btn_name = "Side1"; break;
            case BTN_SIDE2: btn_name = "Side2"; break;
        }
        std::cout << "[SENDER] Отправлено: " << btn_name << " -> " << (state ? "НАЖАТА" : "ОТПУЩЕНА") << "\n";
    }
}

// Глобальный хук мыши
HHOOK g_mouse_hook = NULL;

// Предыдущие состояния кнопок для детектирования изменений
bool g_prev_lmb = false;
bool g_prev_rmb = false;
bool g_prev_wheel = false;
bool g_prev_side1 = false;
bool g_prev_side2 = false;

LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        MSLLHOOKSTRUCT* pMouseStruct = (MSLLHOOKSTRUCT*)lParam;
        
        // Получаем текущее состояние всех кнопок
        bool lmb_pressed = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        bool rmb_pressed = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
        bool wheel_pressed = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
        bool side1_pressed = (GetAsyncKeyState(VK_XBUTTON1) & 0x8000) != 0;
        bool side2_pressed = (GetAsyncKeyState(VK_XBUTTON2) & 0x8000) != 0;

        // Отправляем событие только при ИЗМЕНЕНИИ состояния
        if (lmb_pressed != g_prev_lmb) {
            SendButtonEvent(BTN_LMB, lmb_pressed ? 0x01 : 0x00);
            g_prev_lmb = lmb_pressed;
        }
        if (rmb_pressed != g_prev_rmb) {
            SendButtonEvent(BTN_RMB, rmb_pressed ? 0x01 : 0x00);
            g_prev_rmb = rmb_pressed;
        }
        if (wheel_pressed != g_prev_wheel) {
            SendButtonEvent(BTN_WHEEL, wheel_pressed ? 0x01 : 0x00);
            g_prev_wheel = wheel_pressed;
        }
        if (side1_pressed != g_prev_side1) {
            SendButtonEvent(BTN_SIDE1, side1_pressed ? 0x01 : 0x00);
            g_prev_side1 = side1_pressed;
        }
        if (side2_pressed != g_prev_side2) {
            SendButtonEvent(BTN_SIDE2, side2_pressed ? 0x01 : 0x00);
            g_prev_side2 = side2_pressed;
        }
    }

    return CallNextHookEx(g_mouse_hook, nCode, wParam, lParam);
}

int main() {
    std::cout << "=== MAKCU UDP SENDER (ПК №1) ===\n";
    std::cout << "Отправка нажатий мыши на: " << TARGET_IP << ":" << TARGET_PORT << "\n\n";

    // Инициализация Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "[SENDER] Ошибка WSAStartup: " << WSAGetLastError() << "\n";
        return 1;
    }

    // Создаем UDP сокет
    g_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_sock == INVALID_SOCKET) {
        std::cerr << "[SENDER] Ошибка создания сокета: " << WSAGetLastError() << "\n";
        WSACleanup();
        return 1;
    }

    // Устанавливаем глобальный хук мыши
    g_mouse_hook = SetWindowsHookEx(WH_MOUSE_LL, MouseHookProc, NULL, 0);
    if (!g_mouse_hook) {
        std::cerr << "[SENDER] Ошибка установки хука: " << GetLastError() << "\n";
        closesocket(g_sock);
        WSACleanup();
        return 1;
    }

    std::cout << "[SENDER] Хук установлен. Ожидание нажатий...\n";
    std::cout << "Нажмите Ctrl+C для выхода.\n\n";

    // Цикл обработки сообщений
    MSG msg;
    while (g_running && GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Очистка
    UnhookWindowsHookEx(g_mouse_hook);
    closesocket(g_sock);
    WSACleanup();

    std::cout << "[SENDER] Завершено\n";
    return 0;
}
