#pragma once

#include "WinHeaders.h"

#include <string>
#include <mutex>

// Контроллер для работы с платой Makcu через UART (COM-порт)
// Протокол: Бинарный пакет [0xAA, 0x01, DX_L, DX_H, DY_L, DY_H, 0xBB]
class MakcuUART {
public:
    MakcuUART();
    ~MakcuUART();

    // Методы интерфейса IMouseInput (для использования как самостоятельный бэкенд)
    bool Init();
    void Move(int dx, int dy);
    void Click(int button);
    void Shutdown();

    // Инициализация COM-порта
    bool Connect(const std::string& portName, int baudRate = 9600);
    
    // Закрытие соединения
    void Disconnect();

    // Проверка подключения
    bool IsConnected() const;

    // Отправка движения мыши
    // dx, dy - относительное смещение (как в MouseMove)
    bool MoveMouse(int dx, int dy);

    // Отправка абсолютного положения (если поддерживается прошивкой)
    bool MoveMouseAbsolute(int x, int y, int width, int height);

    // Настройка таймингов (задержка между пакетами для стабильности)
    void SetPacketDelayMs(int ms);

private:
    HANDLE hComPort;
    bool isConnected;
    int packetDelayMs;
    std::mutex mtx;
    std::string m_portName;   // Имя порта для инициализации через Init()
    int m_baudRate;           // Скорость для инициализации через Init()

    // Внутренняя отправка байтов
    bool WriteBytes(const unsigned char* data, size_t length);
};
