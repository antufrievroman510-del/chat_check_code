#pragma once

// ============================================
// MakcuWrapper - Обертка для работы с Makcu через C API
// Совместимо с C++17 для проекта PWNZ AI
// ============================================

#include <string>
#include <atomic>
#include <thread>
#include <memory>
#include <functional>

// Подключаем C API библиотеки makcu-cpp
extern "C" {
#include <makcu/makcu_c.h>
}

namespace pwnz_ai {

/**
 * @brief Глобальные переменные для синхронизации с 2PC режимом
 * 
 * Эти переменные обновляются в реальном времени при получении событий от Makcu
 * и используются логикой аимбота для активации функций.
 */
extern std::atomic<bool> g_makcu_aiming;    // ПКМ - прицеливание
extern std::atomic<bool> g_makcu_shooting;  // ЛКМ - стрельба
extern std::atomic<bool> g_makcu_zooming;   // СКМ - зум
extern std::atomic<bool> g_makcu_side1;     // Боковая кнопка 1
extern std::atomic<bool> g_makcu_side2;     // Боковая кнопка 2

/**
 * @brief Конфигурация для подключения Makcu
 */
struct MakcuConfig {
    std::string com_port = "";          // Если пусто, будет автопоиск
    uint16_t vid = 0x1A86;              // VID устройства (CH341 chipset)
    uint16_t pid = 0x55D3;              // PID устройства
    bool enable_monitoring = true;      // Включить мониторинг кнопок
    int polling_interval_ms = 1;        // Интервал опроса (мс)
};

/**
 * @brief Основной класс для работы с устройством Makcu
 * 
 * Реализует:
 * - Автопоиск устройства по VID:PID
 * - Мониторинг состояния кнопок в отдельном потоке
 * - Управление движением мыши и кликами
 * - Обработку ошибок подключения
 */
class MakcuWrapper {
public:
    using ButtonCallback = std::function<void(makcu_mouse_button_t button, bool pressed)>;

    explicit MakcuWrapper(const MakcuConfig& config = MakcuConfig{});
    ~MakcuWrapper();

    // Запрет копирования
    MakcuWrapper(const MakcuWrapper&) = delete;
    MakcuWrapper& operator=(const MakcuWrapper&) = delete;

    /**
     * @brief Инициализация подключения к устройству
     * @return true если успешно подключено
     */
    bool Initialize();

    /**
     * @brief Завершение работы и отключение
     */
    void Shutdown();

    /**
     * @brief Удобная обертка для Initialize()
     * @return true если успешно подключено
     */
    bool Connect() { return Initialize(); }

    /**
     * @brief Удобная обертка для Shutdown()
     */
    void Disconnect() { Shutdown(); }

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
     * @param out_port Буфер для имени COM-порта
     * @param port_size Размер буфера
     * @return true если устройство найдено
     */
    static bool FindDeviceByVidPid(uint16_t vid, uint16_t pid, char* out_port, size_t port_size);

    /**
     * @brief Найти все устройства Makcu в системе
     * @return Вектор с информацией о найденных устройствах
     */
    static std::vector<std::string> FindAllDevices();

private:
    MakcuConfig m_config;
    makcu_device_t* m_device;
    std::atomic<bool> m_initialized;
    std::atomic<bool> m_running;
    std::unique_ptr<std::thread> m_monitor_thread;
    ButtonCallback m_button_callback;

    /**
     * @brief Поток мониторинга состояния кнопок
     */
    void MonitorThreadFunc();

    /**
     * @brief Обновление глобальных переменных состояния кнопок
     */
    void UpdateGlobalButtonState(makcu_mouse_button_t button, bool pressed);

    /**
     * @brief Конвертация номера кнопки в тип C API
     */
    static makcu_mouse_button_t IntToButton(int button);
};

} // namespace pwnz_ai
