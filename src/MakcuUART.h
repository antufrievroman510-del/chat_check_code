#pragma once

#include "WinHeaders.h"

#include <string>
#include <mutex>
#include <atomic>

// Глобальные атомарные переменные для состояния кнопок (как в reference проекте)
namespace pwnz_ai {
    extern std::atomic<bool> g_makcu_aiming;      // RMB - прицеливание
    extern std::atomic<bool> g_makcu_shooting;    // LMB - стрельба
    extern std::atomic<bool> g_makcu_zooming;     // MMB - зум (если нужно)
}

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
    
    // === НОВОЕ: Мониторинг состояния кнопок для аппаратного режима ===
    // Эти методы возвращают состояние кнопок, полученное от устройства macku
    // (если прошивка поддерживает обратную связь)
    bool IsLeftButtonPressed() const { return m_lmb_pressed.load(); }
    bool IsRightButtonPressed() const { return m_rmb_pressed.load(); }
    bool IsMiddleButtonPressed() const { return m_mmb_pressed.load(); }
    
    // Установка состояния кнопок (используется при получении данных от устройства)
    void SetLeftButtonPressed(bool pressed) { m_lmb_pressed.store(pressed); }
    void SetRightButtonPressed(bool pressed) { m_rmb_pressed.store(pressed); }
    void SetMiddleButtonPressed(bool pressed) { m_mmb_pressed.store(pressed); }

private:
    HANDLE hComPort;
    bool isConnected;
    int packetDelayMs;
    std::mutex mtx;
    std::string m_portName;   // Имя порта для инициализации через Init()
    int m_baudRate;           // Скорость для инициализации через Init()
    
    // === Состояние кнопок для аппаратного режима ===
    std::atomic<bool> m_lmb_pressed{false};
    std::atomic<bool> m_rmb_pressed{false};
    std::atomic<bool> m_mmb_pressed{false};

    // Внутренняя отправка байтов
    bool WriteBytes(const unsigned char* data, size_t length);
};
