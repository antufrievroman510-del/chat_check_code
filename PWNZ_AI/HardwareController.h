#pragma once

#include "WinHeaders.h"
#include "HardwareBackend.h"  // Явное включение для HardwareConfig и HardwareMode
#include "MakcuInput.h"
#include "KMBoxNet.h"

#include <string>
#include <atomic>

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

    HardwareConfig current_config_{};
    HardwareMode current_mode_ = HardwareMode::LocalMouse;
    std::atomic<bool> connected_{false};
    
    // Критическая секция для потокобезопасности
    CRITICAL_SECTION cs_;
};
