#pragma once
#ifndef MAKCU_WRAPPER_H
#define MAKCU_WRAPPER_H

#include <atomic>
#include <memory>
#include <thread>
#include <string>
#include <functional>
#include <iostream>

// Подключаем нашу локальную библиотеку makcu из macku2pc/include
#include "macku2pc/include/makcu.h"

namespace pwnz_ai {

// ============================================================
// Глобальные атомарные переменные для состояния кнопок
// Используются в aimbot.cpp для проверки состояния
// ============================================================
extern std::atomic<bool> g_makcu_aiming;    // SIDE2 (Mouse5) - прицеливание
extern std::atomic<bool> g_makcu_zooming;   // RMB - зум
extern std::atomic<bool> g_makcu_shooting;  // LMB - стрельба

// ============================================================
// Конфигурация Makcu устройства
// ============================================================
struct MakcuConfig {
    uint16_t vid = 0x1A86;              // CH341 chipset
    uint16_t pid = 0x55D3;              // Makcu device
    bool enable_monitoring = true;      // Включить мониторинг кнопок
    int polling_interval_ms = 1;        // Интервал опроса (мс)
    bool high_speed_mode = true;        // Высокоскоростной режим
};

// ============================================================
// Класс MakcuWrapper - обертка над библиотекой makcu
// ============================================================
class MakcuWrapper {
public:
    explicit MakcuWrapper(const MakcuConfig& config = MakcuConfig());
    ~MakcuWrapper();

    // Инициализация: поиск устройства, подключение, установка коллбэков
    bool Initialize();
    
    // Завершение работы
    void Shutdown();

    // Методы управления мышью (делегирование на makcu::Device)
    void Move(int x, int y);
    void Click(int button);  // 0=LMB, 1=RMB, 2=MMB, 3=Side1, 4=Side2
    void Press(int button);
    void Release(int button);

    // Проверка состояния
    bool IsConnected() const;
    bool IsInitialized() const { return m_initialized.load(); }

private:
    // Внутренний поток обработки событий от makcu
    void MonitorThreadFunc();

    // Коллбэк для обработки событий кнопок (вызывается из makcu::Device)
    void OnButtonEvent(makcu::MouseButton button, bool isPressed);

    MakcuConfig m_config;
    std::unique_ptr<makcu::Device> m_device;
    std::atomic<bool> m_initialized{false};
    std::atomic<bool> m_running{false};
    std::thread m_monitorThread;
};

} // namespace pwnz_ai

#endif // MAKCU_WRAPPER_H
