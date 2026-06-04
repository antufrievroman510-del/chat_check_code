#include "UdpListener.h"
#include <iostream>

// Простой протокол:
// Байт 1: Тип события (0x01 = ЛКМ, 0x02 = ПКМ, 0x04 = Колесо, 0x08 = Side1, 0x10 = Side2)
// Байт 2: Состояние (0x01 = нажато, 0x00 = отпущено)
// Пример: {0x01, 0x01} = ЛКМ нажата, {0x01, 0x00} = ЛКМ отпущена

constexpr int UDP_PORT = 9999;
constexpr int BUFFER_SIZE = 64;

UdpMouseListener::UdpMouseListener() 
    : m_sock(INVALID_SOCKET), m_running(false),
      m_aim_pressed(false), m_shoot_pressed(false), m_zoom_pressed(false),
      m_side1_pressed(false), m_side2_pressed(false) {
    
    // Инициализация Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "[UDP] Ошибка инициализации Winsock\n";
    }
}

UdpMouseListener::~UdpMouseListener() {
    Stop();
    WSACleanup();
}

void UdpMouseListener::Start(int port) {
    if (m_running.load()) {
        return;
    }

    // Создаем сокет
    m_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_sock == INVALID_SOCKET) {
        std::cerr << "[UDP] Ошибка создания сокета: " << WSAGetLastError() << "\n";
        return;
    }

    // Разрешаем повторное использование порта
    int opt = 1;
    setsockopt(m_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    // Настраиваем адрес для прослушивания
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY); // Слушаем все интерфейсы
    addr.sin_port = htons(port);

    if (bind(m_sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "[UDP] Ошибка bind: " << WSAGetLastError() << "\n";
        closesocket(m_sock);
        m_sock = INVALID_SOCKET;
        return;
    }

    std::cout << "[UDP] Слушаем порт " << port << "...\n";

    m_running = true;
    m_thread = std::thread(&UdpMouseListener::ListenThreadFunc, this);
}

void UdpMouseListener::Stop() {
    if (!m_running.load()) {
        return;
    }

    m_running = false;
    
    // Отправляем dummy пакет на localhost чтобы разблокировать recvfrom
    sockaddr_in dummy_addr{};
    dummy_addr.sin_family = AF_INET;
    dummy_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    dummy_addr.sin_port = htons(UDP_PORT);
    
    const char* dummy_msg = "\xFF\xFF";
    sendto(m_sock, dummy_msg, 2, 0, (sockaddr*)&dummy_addr, sizeof(dummy_addr));

    if (m_thread.joinable()) {
        m_thread.join();
    }

    if (m_sock != INVALID_SOCKET) {
        closesocket(m_sock);
        m_sock = INVALID_SOCKET;
    }

    std::cout << "[UDP] Остановлено\n";
}

void UdpMouseListener::ListenThreadFunc() {
    char buffer[BUFFER_SIZE];
    sockaddr_in client_addr{};
    int client_addr_len = sizeof(client_addr);

    while (m_running.load()) {
        // Устанавливаем таймаут 100ms чтобы можно было проверить флаг m_running
        DWORD timeout = 100;
        setsockopt(m_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

        int bytes_received = recvfrom(m_sock, buffer, BUFFER_SIZE, 0, 
                                       (sockaddr*)&client_addr, &client_addr_len);

        if (bytes_received == SOCKET_ERROR) {
            if (WSAGetLastError() == WSAETIMEDOUT) {
                continue; // Таймаут, проверяем флаг и продолжаем
            }
            std::cerr << "[UDP] Ошибка recvfrom: " << WSAGetLastError() << "\n";
            break;
        }

        if (bytes_received >= 2) {
            uint8_t button_type = (uint8_t)buffer[0];
            uint8_t state = (uint8_t)buffer[1];
            bool pressed = (state == 0x01);

            // Обновляем флаги в зависимости от типа кнопки
            switch (button_type) {
                case 0x01: // ЛКМ
                    m_aim_pressed.store(pressed);
                    std::cout << "[UDP] ЛКМ: " << (pressed ? "НАЖАТА" : "ОТПУЩЕНА") << "\n";
                    break;
                case 0x02: // ПКМ
                    m_shoot_pressed.store(pressed);
                    std::cout << "[UDP] ПКМ: " << (pressed ? "НАЖАТА" : "ОТПУЩЕНА") << "\n";
                    break;
                case 0x04: // Колесо
                    m_zoom_pressed.store(pressed);
                    std::cout << "[UDP] Колесо: " << (pressed ? "НАЖАТА" : "ОТПУЩЕНА") << "\n";
                    break;
                case 0x08: // Side1
                    m_side1_pressed.store(pressed);
                    std::cout << "[UDP] Side1: " << (pressed ? "НАЖАТА" : "ОТПУЩЕНА") << "\n";
                    break;
                case 0x10: // Side2
                    m_side2_pressed.store(pressed);
                    std::cout << "[UDP] Side2: " << (pressed ? "НАЖАТА" : "ОТПУЩЕНА") << "\n";
                    break;
                default:
                    std::cout << "[UDP] Неизвестная кнопка: " << (int)button_type << ", состояние: " << (int)state << "\n";
                    break;
            }
        }
    }
}
