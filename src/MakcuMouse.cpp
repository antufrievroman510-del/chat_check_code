#include "MakcuMouse.h"
#include <iostream>
#include <thread>
#include <chrono>

// ============================================
// РЕАЛИЗАЦИЯ C API ДЛЯ MAKCU
// ============================================
// Это реализация C API функций которые объявлены в заголовке
// В реальном проекте эти функции будут вызывать библиотеку makcu-cpp
// ============================================

// Внутреннее состояние подключения
static bool g_makcu_connected = false;
static std::string g_makcu_port = "";

// Глобальные переменные для 2PC синхронизации (определения)
namespace pwnz_ai {
    std::atomic<bool> g_makcu_aiming{false};    // ПКМ - прицеливание
    std::atomic<bool> g_makcu_shooting{false};  // ЛКМ - стрельба
    std::atomic<bool> g_makcu_zooming{false};   // СКМ - зум
}

// ============================================
// C API ФУНКЦИИ - РЕАЛИЗАЦИЯ
// ============================================

extern "C" {

/**
 * @brief Инициализация подключения к Makcu
 * @param port_name Имя COM-порта (например "COM3")
 * @return 0 при успехе, отрицательное значение при ошибке
 */
int makcu_init(const char* port_name) {
    if (!port_name) {
        std::cerr << "[MAKCU] Error: null port name" << std::endl;
        return -1;
    }

    std::cout << "[MAKCU] Initializing connection to " << port_name << std::endl;

    // Здесь будет вызов реальной библиотеки makcu-cpp
    // Для примера эмулируем успешное подключение
    // В реальности здесь будет:
    // makcu::Device device;
    // device.connect(port_name);
    
    // Эмуляция подключения для демонстрации
    g_makcu_port = port_name;
    g_makcu_connected = true;
    
    std::cout << "[MAKCU] Successfully connected to " << port_name << std::endl;
    std::cout << "[MAKCU] Device VID:PID = 1A86:55D3" << std::endl;
    std::cout << "[MAKCU] Using C API wrapper for C++17 compatibility" << std::endl;
    
    return 0;
}

/**
 * @brief Закрытие соединения с Makcu
 * @return 0 при успехе
 */
int makcu_deinit() {
    std::cout << "[MAKCU] Disconnecting from " << g_makcu_port << std::endl;
    
    // Здесь будет вызов реальной библиотеки makcu-cpp для отключения
    
    g_makcu_connected = false;
    g_makcu_port = "";
    
    std::cout << "[MAKCU] Disconnected" << std::endl;
    return 0;
}

/**
 * @brief Проверка состояния подключения
 * @return 1 если подключено, 0 если нет
 */
int makcu_is_connected() {
    return g_makcu_connected ? 1 : 0;
}

/**
 * @brief Перемещение мыши
 * @param dx Смещение по X
 * @param dy Смещение по Y
 * @return 0 при успехе
 */
int makcu_move(int dx, int dy) {
    if (!g_makcu_connected) {
        std::cerr << "[MAKCU] Error: not connected, cannot move" << std::endl;
        return -1;
    }

    // Здесь будет вызов реальной библиотеки makcu-cpp
    // device.mouseMove(dx, dy);
    
    // Эмуляция для демонстрации
#ifdef _DEBUG
    std::cout << "[MAKCU] Move: (" << dx << ", " << dy << ")" << std::endl;
#endif
    
    return 0;
}

/**
 * @brief Нажатие кнопки мыши
 * @param button Кнопка (MAKCU_MOUSE_BUTTON_*)
 * @return 0 при успехе
 */
int makcu_press(MakcuMouseButton button) {
    if (!g_makcu_connected) {
        std::cerr << "[MAKCU] Error: not connected, cannot press" << std::endl;
        return -1;
    }

    // Здесь будет вызов реальной библиотеки makcu-cpp
    // device.buttonPress(button);
    
    // Обновляем глобальные переменные для 2PC синхронизации
    switch (button) {
        case MAKCU_MOUSE_BUTTON_LEFT:
            pwnz_ai::g_makcu_shooting.store(true);
            std::cout << "[MAKCU] Press LEFT (shooting=true)" << std::endl;
            break;
        case MAKCU_MOUSE_BUTTON_RIGHT:
            pwnz_ai::g_makcu_aiming.store(true);
            std::cout << "[MAKCU] Press RIGHT (aiming=true)" << std::endl;
            break;
        case MAKCU_MOUSE_BUTTON_MIDDLE:
            pwnz_ai::g_makcu_zooming.store(true);
            std::cout << "[MAKCU] Press MIDDLE (zooming=true)" << std::endl;
            break;
        default:
            std::cout << "[MAKCU] Press button " << button << std::endl;
            break;
    }
    
    return 0;
}

/**
 * @brief Отпускание кнопки мыши
 * @param button Кнопка (MAKCU_MOUSE_BUTTON_*)
 * @return 0 при успехе
 */
int makcu_release(MakcuMouseButton button) {
    if (!g_makcu_connected) {
        std::cerr << "[MAKCU] Error: not connected, cannot release" << std::endl;
        return -1;
    }

    // Здесь будет вызов реальной библиотеки makcu-cpp
    // device.buttonRelease(button);
    
    // Обновляем глобальные переменные для 2PC синхронизации
    switch (button) {
        case MAKCU_MOUSE_BUTTON_LEFT:
            pwnz_ai::g_makcu_shooting.store(false);
            std::cout << "[MAKCU] Release LEFT (shooting=false)" << std::endl;
            break;
        case MAKCU_MOUSE_BUTTON_RIGHT:
            pwnz_ai::g_makcu_aiming.store(false);
            std::cout << "[MAKCU] Release RIGHT (aiming=false)" << std::endl;
            break;
        case MAKCU_MOUSE_BUTTON_MIDDLE:
            pwnz_ai::g_makcu_zooming.store(false);
            std::cout << "[MAKCU] Release MIDDLE (zooming=false)" << std::endl;
            break;
        default:
            std::cout << "[MAKCU] Release button " << button << std::endl;
            break;
    }
    
    return 0;
}

/**
 * @brief Клик (нажатие + отпускание)
 * @param button Кнопка (MAKCU_MOUSE_BUTTON_*)
 * @return 0 при успехе
 */
int makcu_click(MakcuMouseButton button) {
    makcu_press(button);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    makcu_release(button);
    return 0;
}

/**
 * @brief Получение состояния кнопки
 * @param button Кнопка (MAKCU_MOUSE_BUTTON_*)
 * @return 1 если нажата, 0 если нет
 */
int makcu_get_button_state(MakcuMouseButton button) {
    switch (button) {
        case MAKCU_MOUSE_BUTTON_LEFT:
            return pwnz_ai::g_makcu_shooting.load() ? 1 : 0;
        case MAKCU_MOUSE_BUTTON_RIGHT:
            return pwnz_ai::g_makcu_aiming.load() ? 1 : 0;
        case MAKCU_MOUSE_BUTTON_MIDDLE:
            return pwnz_ai::g_makcu_zooming.load() ? 1 : 0;
        default:
            return 0;
    }
}

} // extern "C"

// ============================================
// РЕАЛИЗАЦИЯ КЛАССА MakcuMouse
// ============================================

namespace pwnz_ai {

MakcuMouse::MakcuMouse(const std::string& com_port)
    : m_com_port(com_port), m_initialized(false) {
}

MakcuMouse::~MakcuMouse() {
    if (m_initialized) {
        Shutdown();
    }
}

bool MakcuMouse::Init() {
    std::cout << "[MakcuMouse] Initializing with port: " << m_com_port << std::endl;
    
    if (m_com_port.empty()) {
        std::cerr << "[MakcuMouse] ERROR: Empty COM port name!" << std::endl;
        return false;
    }

    int result = makcu_init(m_com_port.c_str());
    
    if (result != 0) {
        std::cerr << "[MakcuMouse] Failed to initialize Makcu (error code: " << result << ")" << std::endl;
        return false;
    }

    m_initialized = true;
    std::cout << "[MakcuMouse] Successfully initialized!" << std::endl;
    return true;
}

void MakcuMouse::Move(int dx, int dy) {
    if (!m_initialized) {
        return;
    }
    
    makcu_move(dx, dy);
}

void MakcuMouse::Click(int button) {
    if (!m_initialized) {
        std::cerr << "[MakcuMouse] Click called but not initialized!" << std::endl;
        return;
    }

    MakcuMouseButton btn = IntToButton(button);
    makcu_click(btn);
}

void MakcuMouse::Press(int button) {
    if (!m_initialized) {
        std::cerr << "[MakcuMouse] Press called but not initialized!" << std::endl;
        return;
    }

    MakcuMouseButton btn = IntToButton(button);
    makcu_press(btn);
}

void MakcuMouse::Release(int button) {
    if (!m_initialized) {
        std::cerr << "[MakcuMouse] Release called but not initialized!" << std::endl;
        return;
    }

    MakcuMouseButton btn = IntToButton(button);
    makcu_release(btn);
}

void MakcuMouse::Shutdown() {
    if (!m_initialized) {
        return;
    }

    makcu_deinit();
    m_initialized = false;
    std::cout << "[MakcuMouse] Shutdown complete" << std::endl;
}

void MakcuMouse::SetPort(const std::string& port) {
    if (m_initialized) {
        Shutdown();
    }
    m_com_port = port;
}

bool MakcuMouse::IsConnected() const {
    return m_initialized && (makcu_is_connected() != 0);
}

MakcuMouse::MakcuMouseButton MakcuMouse::IntToButton(int button) {
    switch (button) {
        case 0: return MAKCU_MOUSE_BUTTON_LEFT;
        case 1: return MAKCU_MOUSE_BUTTON_RIGHT;
        case 2: return MAKCU_MOUSE_BUTTON_MIDDLE;
        case 3: return MAKCU_MOUSE_BUTTON_SIDE1;
        case 4: return MAKCU_MOUSE_BUTTON_SIDE2;
        default: return MAKCU_MOUSE_BUTTON_LEFT;
    }
}

} // namespace pwnz_ai
