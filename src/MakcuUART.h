#pragma once

// Критически важно: определяем макросы ДО любых заголовков Windows
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <string>
#include <mutex>

// Контроллер для работы с платой Makcu через UART (COM-порт)
// Протокол: Бинарный пакет [0xAA, 0x01, DX_L, DX_H, DY_L, DY_H, 0xBB]
class MakcuUART {
public:
    MakcuUART();
    ~MakcuUART();

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

    // Внутренняя отправка байтов
    bool WriteBytes(const unsigned char* data, size_t length);
};
