#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <mutex>
#include <vector>

// Контроллер для работы с платой KMbox Net через TCP/IP сеть
// Протокол: Бинарный пакет с checksum
class KMBoxNet {
public:
    KMBoxNet();
    ~KMBoxNet();

    // Инициализация сетевого подключения
    bool Connect(const std::string& ipAddress, int port = 5555);
    
    // Закрытие соединения
    void Disconnect();

    // Проверка подключения
    bool IsConnected() const;

    // Отправка движения мыши (относительное)
    bool MoveMouse(int dx, int dy);

    // Отправка абсолютного положения
    bool MoveMouseAbsolute(int x, int y);

    // Отправка нажатия кнопки мыши
    bool MouseButton(uint8_t button, bool isPressed);

    // Настройка таймингов
    void SetPacketDelayMs(int ms);

private:
    SOCKET hSocket;
    bool isConnected;
    int packetDelayMs;
    std::mutex mtx;
    bool winsockInitialized;

    // Инициализация WinSock
    bool InitializeWinSock();

    // Формирование пакета с checksum
    std::vector<unsigned char> BuildPacket(uint8_t command, const unsigned char* data, size_t dataLength);

    // Отправка данных
    bool SendData(const unsigned char* data, size_t length);
};

// Команды протокола KMbox
namespace KMBoxCmd {
    constexpr uint8_t MOVE_REL = 0x01;      // Относительное движение
    constexpr uint8_t MOVE_ABS = 0x02;      // Абсолютное позиционирование
    constexpr uint8_t BUTTON = 0x03;        // Кнопка мыши
    constexpr uint8_t SCROLL = 0x04;        // Скролл
}

// Кнопки мыши
namespace KMBoxButton {
    constexpr uint8_t LEFT = 0x01;
    constexpr uint8_t RIGHT = 0x02;
    constexpr uint8_t MIDDLE = 0x04;
}
