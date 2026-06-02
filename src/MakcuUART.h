#pragma once

#include "WinHeaders.h"

#include <string>
#include <mutex>
#include <atomic>
#include <cstdint>

// Контроллер для работы с платой Makcu через UART (COM-порт)
// Протокол: БИНАРНЫЙ (Binary Mouse Stream) согласно документации https://www.makcu.com/en/api
// Формат фрейма: [0xDE][0xAD][Length][Command][Data...]
// - Command 0x01: Относительное движение (4 байта: dx_low, dx_high, dy_low, dy_high)
// - Command 0x03: Кнопки (1 байт: битовая маска)
class MakcuUART {
public:
    MakcuUART();
    ~MakcuUART();
    
    // Методы интерфейса IMouseInput
    bool Init();
    void Move(int dx, int dy);
    void Click(int button);  // 0=ЛКМ, 1=ПКМ, 2=Колесо
    void Shutdown();

    // Инициализация COM-порта
    bool Connect(const std::string& portName, int baudRate = 115200);
    
    // Закрытие соединения
    void Disconnect();

    // Проверка подключения
    bool IsConnected() const;

    // Отправка движения мыши (бинарный протокол)
    bool MoveMouse(int dx, int dy);
    
    // Отправка нажатия кнопки (удержание)
    bool PressButton(int button);

    // Отпускание кнопки
    bool ReleaseButton(int button);

    // Настройка таймингов
    void SetPacketDelayMs(int ms);
    
    // === Состояние кнопок для аппаратного режима ===
    bool IsLeftButtonPressed() const { return m_lmb_pressed.load(); }
    bool IsRightButtonPressed() const { return m_rmb_pressed.load(); }
    bool IsMiddleButtonPressed() const { return m_mmb_pressed.load(); }
    
    void SetLeftButtonPressed(bool pressed) { m_lmb_pressed.store(pressed); }
    void SetRightButtonPressed(bool pressed) { m_rmb_pressed.store(pressed); }
    void SetMiddleButtonPressed(bool pressed) { m_mmb_pressed.store(pressed); }
    
    // Запуск/остановка потока мониторинга
    void StartMonitoring();
    void StopMonitoring();
    bool IsMonitoring() const { return m_monitoring.load(); }

private:
    HANDLE hComPort;
    bool isConnected;
    int packetDelayMs;
    std::mutex mtx;
    std::string m_portName;
    int m_baudRate;

    // Состояние кнопок
    std::atomic<bool> m_lmb_pressed{false};
    std::atomic<bool> m_rmb_pressed{false};
    std::atomic<bool> m_mmb_pressed{false};
    
    // Поток мониторинга
    std::thread m_monitorThread;
    std::atomic<bool> m_monitoring{false};
    std::atomic<bool> m_stopMonitoring{false};
    
    void monitoringLoop();
    bool WriteBytes(const uint8_t* data, size_t length);
    bool SendBinaryFrame(uint8_t command, const uint8_t* data, size_t dataLen);
    
    // Парсинг бинарных ответов от устройства
    void ParseBinaryResponse(const uint8_t* buffer, size_t length);
};
