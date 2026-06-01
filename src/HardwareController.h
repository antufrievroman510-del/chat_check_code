#pragma once

#include <Windows.h>
#include <string>
#include <atomic>

// Режимы работы оборудования
enum class HardwareMode {
    LocalMouse = 0,       // Стандартная SendInput (ОС видит движение)
    GHubBypass = 1,       // Эмуляция через Logitech GHub (требует DLL)
    RazerBypass = 2,      // Эмуляция через Razer Synapse (требует DLL)
    MakcuUART = 3,        // Плата Makcu через COM-порт/UART
    KMBoxNet = 4          // Плата KMbox через сеть (LAN)
};

struct HardwareConfig {
    HardwareMode mode = HardwareMode::LocalMouse;
    
    // Настройки для Makcu
    std::string makcu_port = "COM3";
    int makcu_baudrate = 9600;

    // Настройки для KMbox Net
    std::string kmbox_ip = "192.168.1.100";
    int kmbox_port = 5555;
    
    // Общие настройки
    bool enable_smoothing = true;
    float smoothing_factor = 5.0f;
};

class HardwareController {
public:
    static HardwareController& Instance();

    // Инициализация (попытка подключения к устройству)
    bool Initialize(const HardwareConfig& config);
    
    // Закрытие соединений
    void Shutdown();

    // Отправка движения мыши
    // dx, dy - дельта движения в пикселях (или единицах устройства)
    void MoveMouse(int dx, int dy);
    
    // Нажатие кнопки мыши
    void ClickButton(bool left, bool down);

    // Проверка статуса подключения
    bool IsConnected() const { return connected_; }
    
    // Получение текущего режима
    HardwareMode GetCurrentMode() const { return current_mode_; }

    // Обновление конфигурации на лету
    void UpdateConfig(const HardwareConfig& config);

private:
    HardwareController() = default;
    ~HardwareController();
    
    // Внутренние методы для разных режимов
    void MoveLocal(int dx, int dy);
    void MoveGHub(int dx, int dy);
    void MoveRazer(int dx, int dy);
    void MoveMakcu(int dx, int dy);
    void MoveKMBox(int dx, int dy);

    HardwareConfig current_config_;
    HardwareMode current_mode_ = HardwareMode::LocalMouse;
    std::atomic<bool> connected_{false};
    
    // Дескрипторы для устройств
    void* hMakcu = nullptr; // Handle для COM порта
    SOCKET hKMBoxSocket = INVALID_SOCKET; // Сокет для KMbox
    
    // Критическая секция для потокобезопасности
    CRITICAL_SECTION cs_;
};
