#include "MakcuMouse.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>

// Глобальные переменные для 2PC синхронизации (определения)
namespace pwnz_ai {
    std::atomic<bool> g_makcu_aiming{false};    // ПКМ - прицеливание
    std::atomic<bool> g_makcu_shooting{false};  // ЛКМ - стрельба
    std::atomic<bool> g_makcu_zooming{false};   // СКМ - зум
}

namespace pwnz_ai {

MakcuMouse::MakcuMouse(const std::string& com_port)
    : m_com_port(com_port), m_device(nullptr), m_initialized(false) {
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

    // Создаём устройство через C API
    m_device = makcu_device_create();
    if (!m_device) {
        std::cerr << "[MakcuMouse] ERROR: Failed to create makcu device!" << std::endl;
        return false;
    }

    // Подключаемся к устройству
    makcu_error_t error = makcu_connect(m_device, m_com_port.c_str());
    if (error != MAKCU_SUCCESS) {
        std::cerr << "[MakcuMouse] ERROR: Failed to connect to Makcu on " << m_com_port 
                  << " (error: " << makcu_error_string(error) << ")" << std::endl;
        makcu_device_destroy(m_device);
        m_device = nullptr;
        return false;
    }

    // Проверяем подключение
    if (!makcu_is_connected(m_device)) {
        std::cerr << "[MakcuMouse] ERROR: Device reports not connected after connect()" << std::endl;
        makcu_disconnect(m_device);
        makcu_device_destroy(m_device);
        m_device = nullptr;
        return false;
    }

    // Получаем версию устройства для проверки
    char version[64];
    error = makcu_get_version(m_device, version, sizeof(version));
    if (error == MAKCU_SUCCESS) {
        std::cout << "[MakcuMouse] Successfully connected! Device version: " << version << std::endl;
    } else {
        std::cout << "[MakcuMouse] Successfully connected! (version query failed: " 
                  << makcu_error_string(error) << ")" << std::endl;
    }

    // Включаем мониторинг кнопок для 2PC синхронизации
    error = makcu_enable_button_monitoring(m_device, true);
    if (error != MAKCU_SUCCESS) {
        std::cout << "[MakcuMouse] Warning: Could not enable button monitoring: " 
                  << makcu_error_string(error) << std::endl;
    } else {
        std::cout << "[MakcuMouse] Button monitoring enabled for 2PC sync" << std::endl;
    }

    m_initialized = true;
    std::cout << "[MakcuMouse] Initialization complete!" << std::endl;
    return true;
}

void MakcuMouse::Move(int dx, int dy) {
    if (!m_initialized || !m_device) {
        return;
    }
    
    makcu_error_t error = makcu_mouse_move(m_device, dx, dy);
    if (error != MAKCU_SUCCESS) {
#ifdef _DEBUG
        std::cerr << "[MakcuMouse] Move failed: " << makcu_error_string(error) << std::endl;
#endif
    }
}

void MakcuMouse::Click(int button) {
    if (!m_initialized || !m_device) {
        std::cerr << "[MakcuMouse] Click called but not initialized!" << std::endl;
        return;
    }

    makcu_mouse_button_t btn = IntToButton(button);
    makcu_error_t error = makcu_mouse_click(m_device, btn);
    if (error != MAKCU_SUCCESS) {
        std::cerr << "[MakcuMouse] Click failed: " << makcu_error_string(error) << std::endl;
    }
    
    // Для 2PC синхронизации обновляем состояние (кратковременно)
    UpdateButtonState(btn, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    UpdateButtonState(btn, false);
}

void MakcuMouse::Press(int button) {
    if (!m_initialized || !m_device) {
        std::cerr << "[MakcuMouse] Press called but not initialized!" << std::endl;
        return;
    }

    makcu_mouse_button_t btn = IntToButton(button);
    makcu_error_t error = makcu_mouse_down(m_device, btn);
    if (error != MAKCU_SUCCESS) {
        std::cerr << "[MakcuMouse] Press failed: " << makcu_error_string(error) << std::endl;
        return;
    }
    
    // Обновляем глобальные переменные для 2PC синхронизации
    UpdateButtonState(btn, true);
}

void MakcuMouse::Release(int button) {
    if (!m_initialized || !m_device) {
        std::cerr << "[MakcuMouse] Release called but not initialized!" << std::endl;
        return;
    }

    makcu_mouse_button_t btn = IntToButton(button);
    makcu_error_t error = makcu_mouse_up(m_device, btn);
    if (error != MAKCU_SUCCESS) {
        std::cerr << "[MakcuMouse] Release failed: " << makcu_error_string(error) << std::endl;
        return;
    }
    
    // Обновляем глобальные переменные для 2PC синхронизации
    UpdateButtonState(btn, false);
}

void MakcuMouse::Shutdown() {
    if (!m_initialized || !m_device) {
        return;
    }

    std::cout << "[MakcuMouse] Shutting down..." << std::endl;
    
    // Отключаем мониторинг кнопок
    makcu_enable_button_monitoring(m_device, false);
    
    // Отключаемся и уничтожаем устройство
    makcu_disconnect(m_device);
    makcu_device_destroy(m_device);
    
    m_device = nullptr;
    m_initialized = false;
    
    // Сбрасываем глобальные переменные
    g_makcu_aiming.store(false);
    g_makcu_shooting.store(false);
    g_makcu_zooming.store(false);
    
    std::cout << "[MakcuMouse] Shutdown complete" << std::endl;
}

void MakcuMouse::SetPort(const std::string& port) {
    if (m_initialized) {
        Shutdown();
    }
    m_com_port = port;
}

bool MakcuMouse::IsConnected() const {
    if (!m_initialized || !m_device) {
        return false;
    }
    return makcu_is_connected(m_device);
}

makcu_mouse_button_t MakcuMouse::IntToButton(int button) {
    switch (button) {
        case 0: return MAKCU_MOUSE_LEFT;
        case 1: return MAKCU_MOUSE_RIGHT;
        case 2: return MAKCU_MOUSE_MIDDLE;
        case 3: return MAKCU_MOUSE_SIDE1;
        case 4: return MAKCU_MOUSE_SIDE2;
        default: return MAKCU_MOUSE_LEFT;
    }
}

void MakcuMouse::UpdateButtonState(makcu_mouse_button_t button, bool pressed) {
    switch (button) {
        case MAKCU_MOUSE_LEFT:
            g_makcu_shooting.store(pressed);
#ifdef _DEBUG
            std::cout << "[MakcuMouse] LEFT button " << (pressed ? "pressed" : "released") 
                      << " (shooting=" << pressed << ")" << std::endl;
#endif
            break;
        case MAKCU_MOUSE_RIGHT:
            g_makcu_aiming.store(pressed);
#ifdef _DEBUG
            std::cout << "[MakcuMouse] RIGHT button " << (pressed ? "pressed" : "released") 
                      << " (aiming=" << pressed << ")" << std::endl;
#endif
            break;
        case MAKCU_MOUSE_MIDDLE:
            g_makcu_zooming.store(pressed);
#ifdef _DEBUG
            std::cout << "[MakcuMouse] MIDDLE button " << (pressed ? "pressed" : "released") 
                      << " (zooming=" << pressed << ")" << std::endl;
#endif
            break;
        default:
            break;
    }
}

} // namespace pwnz_ai
