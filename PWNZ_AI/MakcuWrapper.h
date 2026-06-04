#pragma once

// ============================================
// MakcuWrapper - Обертка для работы с Makcu через C++ API
// Совместимо с C++23 для проекта PWNZ AI
// Архитектура как в референсе /source_logic/source_logic/mouse/Makcu.h
// ============================================

#include <string>
#include <atomic>
#include <memory>
#include <functional>
#include <mutex>

// Подключаем C++ API библиотеки makcu-cpp
#include <makcu.h>

// Подключаем интерфейс IMouseInput для полиморфизма
#include "IMouseInput.h"

namespace pwnz_ai {

// ============================================
// ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ДЛЯ 2PC РЕЖИМА
// Эти переменные обновляются из callback и используются в aimbot.cpp
// ============================================
extern std::atomic<bool> g_makcu_aiming;      // SIDE2 (Mouse5) - прицеливание
extern std::atomic<bool> g_makcu_shooting;    // LMB - стрельба
extern std::atomic<bool> g_makcu_zooming;     // RMB - зум

/**
 * @brief Основной класс для работы с устройством Makcu
 * 
 * Архитектура как в референсе:
 * - Callback обновляет глобальные переменные g_makcu_*
 * - Методы aimingActive(), shootingActive(), zoomingActive() возвращают состояние
 */
class MakcuWrapper : public IMouseInput {
public:
    explicit MakcuWrapper(const std::string& port = "", unsigned int baud_rate = 115200);
    ~MakcuWrapper() override;

    // Запрет копирования
    MakcuWrapper(const MakcuWrapper&) = delete;
    MakcuWrapper& operator=(const MakcuWrapper&) = delete;

    // Реализация интерфейса IMouseInput
    bool Init() override { return Connect(); }
    void Move(int dx, int dy) override;
    void Click(int button) override;
    void Press(int button) override;
    void Release(int button) override;
    void Shutdown() override { Disconnect(); }
    
    // Методы для получения состояния кнопок (дублируют глобальные переменные)
    bool aimingActive() const { return g_makcu_aiming.load(); }
    bool shootingActive() const { return g_makcu_shooting.load(); }
    bool zoomingActive() const { return g_makcu_zooming.load(); }

    /**
     * @brief Инициализация подключения к устройству
     * @return true если успешно подключено
     */
    bool Connect();

    /**
     * @brief Завершение работы и отключение
     */
    void Disconnect();

    /**
     * @brief Проверка статуса подключения
     */
    bool IsConnected() const;

    /**
     * @brief Получить информацию об устройстве
     */
    std::string GetDeviceInfo() const;

    /**
     * @brief Плавное движение мыши
     */
    void MoveSmooth(int dx, int dy, uint32_t segments = 10);

    /**
     * @brief Статический метод для поиска первого устройства Makcu по VID:PID
     */
    static std::optional<std::string> FindDeviceByVidPid(uint16_t vid = 0x1A86, uint16_t pid = 0x55D3);

    /**
     * @brief Попытка переподключения при потере устройства
     */
    bool TryReconnect();

private:
    std::unique_ptr<makcu::Device> m_device;
    std::atomic<bool> m_is_open{false};
    std::atomic<bool> m_aiming_active{false};   // Mouse5 (Side2)
    std::atomic<bool> m_shooting_active{false}; // LMB
    std::atomic<bool> m_zooming_active{false};  // RMB
    mutable std::mutex m_mutex;
    mutable std::mutex m_write_mutex;

    /**
     * @brief Обработка событий кнопок из callback (как в референсе)
     */
    void onButtonCallback(makcu::MouseButton button, bool pressed);
};

} // namespace pwnz_ai
