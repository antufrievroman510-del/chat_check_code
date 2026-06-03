#pragma once

#include "WinHeaders.h"

#include <string>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <thread>

// Глобальные переменные состояния кнопок для аппаратного режима
// Объявляются здесь, определяются в MakcuUART.cpp
namespace pwnz_ai {
    extern std::atomic<bool> g_makcu_aiming;    // Состояние ПКМ (прицеливание)
    extern std::atomic<bool> g_makcu_shooting;  // Состояние ЛКМ (стрельба)
    extern std::atomic<bool> g_makcu_zooming;   // Состояние СКМ (зум)
}

// Контроллер для работы с платой Makcu через UART (COM-порт)
// Протокол: ТЕКСТОВЫЙ согласно https://github.com/K4HVH/makcu-cpp и https://www.makcu.com/en/api
// 
// ВАЖНО: Устройство Makcu работает на скорости 4 Мбит/с (4000000 бод)
// VID:PID устройства = 1A86:55D3 (CH340/CH343)
//
// Команды (текстовый ASCII протокол, завершается \r\n):
//   km.version()\r\n          - запрос версии прошивки
//   km.move(dx,dy)\r\n        - движение мыши (dx, dy - int16)
//   km.left(1)\r\n            - нажать ЛКМ
//   km.left(0)\r\n            - отпустить ЛКМ
//   km.right(1)\r\n           - нажать ПКМ
//   km.right(0)\r\n           - отпустить ПКМ
//   km.middle(1)\r\n         - нажать СКМ
//   km.middle(0)\r\n         - отпустить СКМ
//   km.ms1(1)\r\n             - нажать боковую кнопку 1
//   km.ms1(0)\r\n             - отпустить боковую кнопку 1
//   km.ms2(1)\r\n             - нажать боковую кнопку 2
//   km.ms2(0)\r\n             - отпустить боковую кнопку 2
//   km.buttons(1)\r\n        - включить стрим состояния кнопок
//   km.buttons(0)\r\n        - выключить стрим состояния кнопок
//
// Ответы устройства:
//   - Все ответы заканчиваются промптом ">>> "
//   - Пример: "km.version()\r\nMAKCU v3.7\r\n>>> "
//   - События кнопок: префикс "km." (0x6B 0x6D 0x2E) + 1 байт маска кнопок
//
// Маска кнопок (биты):
//   Бит 0 - левая кнопка (ЛКМ)
//   Бит 1 - правая кнопка (ПКМ)  
//   Бит 2 - средняя кнопка (СКМ)
//   Бит 3 - боковая 1
//   Бит 4 - боковая 2
//
// Последовательность подключения (согласно makcu-cpp):
//   1. Открыть порт на 4 Мбит/с (4000000)
//   2. Отправить km.version()\r\n
//   3. Если в ответе есть "MAKCU" - связь установлена
//   4. Если нет - выполнить смену скорости:
//      a. Открыть порт на 115200
//      b. Отправить бинарный фрейм смены скорости: DE AD 05 00 A5 00 09 3D 00
//      c. Ждать 100 мс
//      d. Закрыть порт
//      e. Открыть порт на 4 Мбит/с
//      f. Ждать 50 мс, сбросить входной буфер
//      g. Отправить km.version()\r\n
//      h. Если ответа нет - подключение не удалось
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
    bool Connect(const std::string& portName, int baudRate = 4000000);
    
    // Закрытие соединения
    void Disconnect();

    // Проверка подключения
    bool IsConnected() const;

    // Отправка движения мыши (текстовый протокол)
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
    
    void SetLeftButtonPressed(bool pressed) { 
        m_lmb_pressed.store(pressed);
        pwnz_ai::g_makcu_shooting.store(pressed);  // Синхронизация с глобальной переменной
    }
    void SetRightButtonPressed(bool pressed) { 
        m_rmb_pressed.store(pressed);
        pwnz_ai::g_makcu_aiming.store(pressed);  // Синхронизация с глобальной переменной
    }
    void SetMiddleButtonPressed(bool pressed) { 
        m_mmb_pressed.store(pressed);
        pwnz_ai::g_makcu_zooming.store(pressed);  // Синхронизация с глобальной переменной
    }
    
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
    bool WriteCommand(const char* command);
    void ParseResponse(const char* buffer, size_t length);
    void UpdateButtonState(bool lmb, bool rmb, bool mmb);  // Обновление состояния кнопок
    
    // Внутренний метод для переключения скорости
    bool PerformBaudRateSwitch(const std::string& portName);
};
