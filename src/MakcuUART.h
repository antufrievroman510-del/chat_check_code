#pragma once

#include "WinHeaders.h"

#include <string>
#include <mutex>

// Контроллер для работы с платой Makcu через UART (COM-порт)
// Протокол: Текстовые команды "km.move(x,y)\r\n" и "km.click(b)\r\n"
// Совместим с прошивкой https://github.com/terrafirma2021/MAKCM
class MakcuUART {
public:
    MakcuUART();
    ~MakcuUART();

    // Методы интерфейса IMouseInput (для использования как самостоятельный бэкенд)
    bool Init();
    void Move(int dx, int dy);
    void Click(int button);  // 0=ЛКМ, 1=ПКМ, 2=Колесо, 3=Боковая1, 4=Боковая2
    void Shutdown();

    // Инициализация COM-порта
    bool Connect(const std::string& portName, int baudRate = 115200);
    
    // Закрытие соединения
    void Disconnect();

    // Проверка подключения
    bool IsConnected() const;

    // Отправка движения мыши
    // dx, dy - относительное смещение (как в MouseMove)
    bool MoveMouse(int dx, int dy);

    // Отправка абсолютного положения (если поддерживается прошивкой)
    bool MoveMouseAbsolute(int x, int y, int width, int height);
    
    // Отправка клика мышью
    // button: 0=ЛКМ, 1=ПКМ, 2=Колесо (нажатие), 3=Боковая кнопка 1, 4=Боковая кнопка 2
    bool ClickMouse(int button);

    // Отправка нажатия кнопки (удержание)
    bool PressButton(int button);

    // Отпускание кнопки
    bool ReleaseButton(int button);

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
