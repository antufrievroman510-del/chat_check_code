#ifndef KMBOXNET_H
#define KMBOXNET_H

#include "WinHeaders.h"

#include <string>
#include <mutex>
#include <vector>
#include <cstdint>

namespace pwnz_ai {

// Контроллер для работы с платой KMbox Net через TCP/IP сеть
// Протокол: Бинарный пакет с checksum
class KMBoxNet {
public:
    KMBoxNet();
    ~KMBoxNet();

    // Методы интерфейса IMouseInput (для использования как самостоятельный бэкенд)
    bool Init();
    void Move(int dx, int dy);
    void Click(int button);
    void Shutdown();

    // Инициализация сетевого подключения
    bool ConnectToDevice(const std::string& ipAddress, int port = 5555);
    
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
    std::string m_ipAddress;  // IP для инициализации через Init()
    int m_port;               // Порт для инициализации через Init()

    // Инициализация WinSock
    bool InitializeWinSock();

    // Формирование пакета с checksum
    std::vector<unsigned char> BuildPacket(uint8_t command, const unsigned char* data, size_t dataLength);

    // Отправка данных
    bool SendData(const unsigned char* data, size_t length);
};

} // namespace pwnz_ai

// Команды протокола KMbox
namespace KMBoxCmd {
    constexpr uint8_t MOVE_REL = 0x01;      // Относительное движение
    constexpr uint8_t MOVE_ABS = 0x02;      // Абсолютное позиционирование
    constexpr uint8_t BUTTON = 0x03;        // Кнопка мыши
    constexpr uint8_t SCROLL = 0x04;        // Скролл
} // namespace KMBoxCmd

// Кнопки мыши
namespace KMBoxButton {
    constexpr uint8_t LEFT = 0x01;
    constexpr uint8_t RIGHT = 0x02;
    constexpr uint8_t MIDDLE = 0x04;
} // namespace KMBoxButton

#endif // KMBOXNET_H
