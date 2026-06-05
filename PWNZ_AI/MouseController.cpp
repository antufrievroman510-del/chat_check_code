#include "MouseController.h"
#include "MakcuInput.h"
#include <iostream>
#include <memory>

namespace pwnz_ai {

// Singleton instance
static MouseController* g_Instance = nullptr;

// Глобальный экземпляр MakcuInput для режима Makcu_UART
static std::unique_ptr<MakcuInput> g_makcuInstance;

MouseController& MouseController::GetInstance() {
    if (!g_Instance) {
        static MouseController instance;
        g_Instance = &instance;
    }
    return *g_Instance;
}

/**
 * @brief Инициализация контроллера мыши
 * Вызывается один раз при старте приложения
 */
void MouseController::Initialize(MouseMethod method) {
    if (isInitialized) {
        return; // Уже инициализировано
    }

    currentMethod = method;
    isInitialized = true;

    std::cout << "[MouseController] Initialized with method: " 
              << (method == MouseMethod::Standard ? "Standard" : 
                  method == MouseMethod::GHub_Spoof ? "GHub Spoof" :
                  method == MouseMethod::Razer_Spoof ? "Razer Spoof" :
                  method == MouseMethod::Driver ? "Driver" :
                  "Makcu UART")
              << std::endl;
}

/**
 * @brief Основное движение мыши
 * Принимает уже рассчитанные дельты от AimMath
 */
void MouseController::MoveMouse(float deltaX, float deltaY) {
    if (!isInitialized) {
        Initialize(); // Авто-инициализация если нужно
    }

    // Округляем до целых пикселей для SendInput
    int dx = static_cast<int>(deltaX);
    int dy = static_cast<int>(deltaY);

    // Если движение слишком маленькое (< 1 пикселя), но не ноль - добавляем минимальное смещение
    if (dx == 0 && deltaX != 0.0f) dx = (deltaX > 0) ? 1 : -1;
    if (dy == 0 && deltaY != 0.0f) dy = (deltaY > 0) ? 1 : -1;

    // Выбор метода движения
    switch (currentMethod) {
        case MouseMethod::Standard:
            MoveStandard(dx, dy);
            break;
        case MouseMethod::GHub_Spoof:
            MoveGHubSpoof(dx, dy);
            break;
        case MouseMethod::Razer_Spoof:
            MoveRazerSpoof(dx, dy);
            break;
        case MouseMethod::Driver:
            MoveDriver(dx, dy);
            break;
        case MouseMethod::Makcu_UART:
            MoveMakcu(dx, dy);
            break;
    }
}

/**
 * @brief Стандартное движение через SendInput
 * Работает всегда, но может детектиться античитами
 */
void MouseController::MoveStandard(int dx, int dy) {
    if (dx == 0 && dy == 0) return;

    INPUT input = {0};
    input.type = INPUT_MOUSE;
    input.mi.dx = dx;
    input.mi.dy = dy;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;

    SendInput(1, &input, sizeof(INPUT));
}

/**
 * @brief Эмуляция через Logitech G Hub
 * Требует установленного G Hub и загрузки его DLL
 * Пока заглушка - будет реализована на Шаге 5
 */
void MouseController::MoveGHubSpoof(int dx, int dy) {
    // TODO: Реализация на Шаге 5
    // 1. Загрузить lgcore.dll / lgghub_hook.dll
    // 2. Найти экспортные функции для эмуляции мыши
    // 3. Вызвать их с нашими координатами
    
    // Временно используем стандартный метод
    MoveStandard(dx, dy);
}

/**
 * @brief Эмуляция через Razer Synapse
 * Требует установленного Razer Synapse
 * Пока заглушка - будет реализована на Шаге 5
 */
void MouseController::MoveRazerSpoof(int dx, int dy) {
    // TODO: Реализация на Шаге 5
    // Аналогично G Hub - загрузка DLL Razer и вызов функций
    
    // Временно используем стандартный метод
    MoveStandard(dx, dy);
}

/**
 * @brief Прямой ввод через драйвер
 * Требует подписанного драйвера (например, RT Core или кастомный)
 * Пока заглушка - будет реализована на Шаге 5
 */
void MouseController::MoveDriver(int dx, int dy) {
    // TODO: Реализация на Шаге 5
    // 1. Открыть-handle драйвера
    // 2. Отправить IOCTL команду с координатами
    // 3. Драйвер эмулирует движение на уровне ядра
    
    // Временно используем стандартный метод
    MoveStandard(dx, dy);
}

/**
 * @brief Движение через плату Makcu (UART/COM)
 */
void MouseController::MoveMakcu(int dx, int dy) {
    if (g_makcuInstance && g_makcuInstance->IsConnected()) {
        g_makcuInstance->Move(dx, dy);
    }
}

/**
 * @brief Нажатие кнопки мыши
 */
void MouseController::PressButton(int buttonCode) {
    INPUT input = {0};
    input.type = INPUT_MOUSE;
    
    switch (buttonCode) {
        case VK_LBUTTON:
            input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
            break;
        case VK_RBUTTON:
            input.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
            break;
        case VK_MBUTTON:
            input.mi.dwFlags = MOUSEEVENTF_MIDDLEDOWN;
            break;
        default:
            return;
    }

    SendInput(1, &input, sizeof(INPUT));
}

/**
 * @brief Отпускание кнопки мыши
 */
void MouseController::ReleaseButton(int buttonCode) {
    INPUT input = {0};
    input.type = INPUT_MOUSE;
    
    switch (buttonCode) {
        case VK_LBUTTON:
            input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
            break;
        case VK_RBUTTON:
            input.mi.dwFlags = MOUSEEVENTF_RIGHTUP;
            break;
        case VK_MBUTTON:
            input.mi.dwFlags = MOUSEEVENTF_MIDDLEUP;
            break;
        default:
            return;
    }

    SendInput(1, &input, sizeof(INPUT));
}

/**
 * @brief Прокрутка колеса мыши
 * @param delta Положительное значение - вверх, отрицательное - вниз
 */
void MouseController::ScrollWheel(int delta) {
    if (delta == 0) return;
    
    INPUT input = {0};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_WHEEL;
    input.mi.mouseData = static_cast<DWORD>(delta);
    
    SendInput(1, &input, sizeof(INPUT));
}

/**
 * @brief Смена метода ввода на лету
 */
void MouseController::SetMethod(MouseMethod method) {
    currentMethod = method;
    
    // Переинициализация устройств если сменился режим
    if (method == MouseMethod::Makcu_UART) {
        // Инициализация Makcu будет вызвана из Aimbot::InitHardware()
        std::cout << "[MouseController] Switched to Makcu UART mode" << std::endl;
    }
}

void InitMakcuDevice(const std::string& port) {
    g_makcuInstance = std::make_unique<MakcuInput>();
    if (!g_makcuInstance->Init(port)) {
        std::cerr << "[MouseController] Failed to initialize Makcu on port " << port << std::endl;
        g_makcuInstance.reset();
    }
}

void ShutdownMakcuDevice() {
    if (g_makcuInstance) {
        g_makcuInstance->Shutdown();
        g_makcuInstance.reset();
    }
}

} // namespace pwnz_ai
