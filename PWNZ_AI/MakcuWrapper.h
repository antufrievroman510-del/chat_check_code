#pragma once

// ============================================
// MakcuWrapper - Обертка для работы с Makcu через C++ API
// Совместимо с C++23 для проекта PWNZ AI
// ============================================

#include <string>
#include <atomic>
#include <memory>
#include <functional>
#include <expected>
#include <mutex>
#include <optional>
#include <vector>

// Подключаем C++ API библиотеки makcu-cpp
#include <makcu.h>

// Подключаем интерфейс IMouseInput для полиморфизма
#include "IMouseInput.h"

namespace pwnz_ai {

/**
 * @brief Глобальные переменные для синхронизации с 2PC режимом
 * 
 * Эти переменные обновляются в реальном времени при получении событий от Makcu
 * и используются логикой аимбота для активации функций.
 */
extern std::atomic<bool> g_makcu_aiming;    // Mouse5 (Side2) - прицеливание
extern std::atomic<bool> g_makcu_shooting;  // ЛКМ - стрельба
extern std::atomic<bool> g_makcu_zooming;   // ПКМ - зум
extern std::atomic<bool> g_makcu_side1;     // Боковая кнопка 1 (Mouse4)
extern std::atomic<bool> g_makcu_side2;     // Боковая кнопка 2 (используется для aiming)

/**
 * @brief Конфигурация для подключения Makcu
 */
struct MakcuConfig {
    std::string com_port = "";          // Если пусто, будет автопоиск
    uint16_t vid = 0x1A86;              // VID устройства (CH341 chipset)
    uint16_t pid = 0x55D3;              // PID устройства
    int baud_rate = 115200;             // Скорость соединения
    bool enable_monitoring = true;      // Включить мониторинг кнопок
    int polling_interval_ms = 1;        // Интервал опроса в мс (для совместимости)
};

/**
 * @brief Основной класс для работы с устройством Makcu
 * 
 * Реализует:
 * - Автопоиск устройства по VID:PID
 * - Мониторинг состояния кнопок через callback (без polling)
 * - Управление движением мыши и кликами
 * - Обработку ошибок подключения через std::expected
 * - Горячее переподключение
 */
class MakcuWrapper : public IMouseInput {
public:
    using ButtonCallback = std::function<void(makcu::MouseButton button, bool pressed)>;

    explicit MakcuWrapper(const MakcuConfig& config = MakcuConfig{});
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
    void Shutdown() override { ShutdownInternal(); }

    /**
     * @brief Инициализация подключения к устройству
     * @return std::expected<void, std::string> - результат или ошибка
     */
    std::expected<void, std::string> Initialize();

    /**
     * @brief Завершение работы и отключение
     */
    void ShutdownInternal();

    /**
     * @brief Удобная обертка для Initialize()
     * @return true если успешно подключено
     */
    bool Connect();

    /**
     * @brief Удобная обертка для ShutdownInternal()
     */
    void Disconnect() { ShutdownInternal(); }

    /**
     * @brief Проверка статуса подключения
     */
    bool IsConnected() const;

    /**
     * @brief Получить информацию об устройстве
     */
    std::string GetDeviceInfo() const;

    /**
     * @brief Движение мыши
     * @param dx Смещение по X
     * @param dy Смещение по Y
     */
    void Move(int dx, int dy);

    /**
     * @brief Плавное движение мыши
     * @param dx Смещение по X
     * @param dy Смещение по Y
     * @param segments Количество сегментов для плавности
     */
    void MoveSmooth(int dx, int dy, uint32_t segments = 10);

    /**
     * @brief Клик кнопкой мыши
     * @param button Номер кнопки (0=ЛКМ, 1=ПКМ, 2=СКМ, 3=Side1, 4=Side2)
     */
    void Click(int button);

    /**
     * @brief Нажатие кнопки (без отпускания)
     * @param button Номер кнопки
     */
    void Press(int button);

    /**
     * @brief Отпускание кнопки
     * @param button Номер кнопки
     */
    void Release(int button);

    /**
     * @brief Установить callback для событий кнопок
     */
    void SetButtonCallback(ButtonCallback callback);

    /**
     * @brief Статический метод для поиска первого устройства Makcu по VID:PID
     * @param vid Vendor ID
     * @param pid Product ID
     * @return std::optional<std::string> - COM-порт или nullopt
     */
    static std::optional<std::string> FindDeviceByVidPid(uint16_t vid, uint16_t pid);

    /**
     * @brief Найти все устройства Makcu в системе
     * @return Вектор с информацией о найденных устройствах
     */
    static std::vector<std::string> FindAllDevices();

    /**
     * @brief Попытка переподключения при потере устройства
     * @return true если успешно переподключено
     */
    bool TryReconnect();

private:
    MakcuConfig m_config;
    std::unique_ptr<makcu::Device> m_device;
    std::atomic<bool> m_initialized;
    std::atomic<bool> m_connected;
    ButtonCallback m_button_callback;
    mutable std::mutex m_mutex;

    /**
     * @brief Обработка событий кнопок из callback
     */
    void OnButtonEvent(makcu::MouseButton button, bool pressed);

    /**
     * @brief Конвертация номера кнопки в тип C++ API
     */
    static makcu::MouseButton IntToButton(int button);

    /**
     * @brief Обновление глобальных переменных состояния кнопок
     */
    void UpdateGlobalButtonState(makcu::MouseButton button, bool pressed);
};

} // namespace pwnz_ai
