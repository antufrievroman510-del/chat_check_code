#pragma once
#include "WinHeaders.h"
#include "AimMath.h"
#include "MakcuInput.h"
#include "KMBoxNet.h"
#include "HardwareBackend.h"  // Для HardwareConfig

// Перечисление методов ввода для Mouse Bypass
enum class MouseMethod {
    Standard,      // Стандартный SendInput (может детектиться)
    GHub_Spoof,    // Эмуляция через Logitech G Hub (скрытый ввод)
    Razer_Spoof,   // Эмуляция через Razer Synapse (скрытый ввод)
    Driver,        // Прямой драйвер (требует подписанного драйвера)
    Makcu_UART,    // Плата Makcu через COM-порт
    KMBox_Net      // Плата KMbox через сеть
};

class MouseController {
public:
    static MouseController& GetInstance();

    // Инициализация (вызывается один раз при старте)
    void Initialize(MouseMethod method = MouseMethod::Standard);

    // Основное движение мыши
    // deltaX, deltaY - смещение в пикселях (уже рассчитанное AimMath)
    void MoveMouse(float deltaX, float deltaY);

    // Нажатие кнопки мыши
    void PressButton(int buttonCode); // VK_LBUTTON, VK_RBUTTON и т.д.
    void ReleaseButton(int buttonCode);

    // Смена метода ввода на лету
    void SetMethod(MouseMethod method);
    
    // Обновление конфигурации HardwareBackend
    void UpdateHardwareConfig(const HardwareConfig& config);

private:
    MouseController() = default;
    ~MouseController() = default;
    MouseController(const MouseController&) = delete;
    MouseController& operator=(const MouseController&) = delete;

    // Реализации методов движения
    void MoveStandard(int dx, int dy);
    void MoveGHubSpoof(int dx, int dy);
    void MoveRazerSpoof(int dx, int dy);
    void MoveDriver(int dx, int dy);
    void MoveMakcu(int dx, int dy);
    void MoveKMBox(int dx, int dy);

    MouseMethod currentMethod = MouseMethod::Standard;
    bool isInitialized = false;
};
