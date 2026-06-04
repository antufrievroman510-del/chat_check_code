#include "MakcuWrapper.h"
#include <iostream>
#include <chrono>

namespace pwnz_ai {

// ============================================
// Глобальные переменные для 2PC синхронизации
// ============================================
std::atomic<bool> g_makcu_aiming{false};    // ПКМ - прицеливание
std::atomic<bool> g_makcu_shooting{false};  // ЛКМ - стрельба
std::atomic<bool> g_makcu_zooming{false};   // СКМ - зум
std::atomic<bool> g_makcu_side1{false};     // Боковая кнопка 1
std::atomic<bool> g_makcu_side2{false};     // Боковая кнопка 2

// ============================================
// Реализация MakcuWrapper
// ============================================

MakcuWrapper::MakcuWrapper(const MakcuConfig& config)
    : m_config(config)
    , m_device(nullptr)
    , m_initialized(false)
    , m_running(false)
    , m_button_callback(nullptr)
{
}

MakcuWrapper::~MakcuWrapper() {
    if (m_initialized) {
        Shutdown();
    }
}

bool MakcuWrapper::FindDeviceByVidPid(uint16_t vid, uint16_t pid, char* out_port, size_t port_size) {
    if (!out_port || port_size == 0) {
        return false;
    }

    // Используем функцию поиска первого устройства из C API
    // Примечание: makcu_find_first_device ищет любое устройство Makcu
    // Для точного поиска по VID:PID можно использовать makcu_find_devices
    
    makcu_device_info_t devices[16];
    int count = makcu_find_devices(devices, 16);
    
    if (count <= 0) {
        return false;
    }

    // Ищем устройство с нужными VID:PID
    for (int i = 0; i < count; ++i) {
        if (devices[i].vid == vid && devices[i].pid == pid) {
            strncpy(out_port, devices[i].port, port_size - 1);
            out_port[port_size - 1] = '\0';
            std::cout << "[MakcuWrapper] Found device at " << out_port 
                      << " (VID:0x" << std::hex << vid << ", PID:0x" << pid << std::dec << ")" << std::endl;
            return true;
        }
    }

    // Если не нашли по точному совпадению, пробуем первое доступное устройство
    if (count > 0) {
        strncpy(out_port, devices[0].port, port_size - 1);
        out_port[port_size - 1] = '\0';
        std::cout << "[MakcuWrapper] Device with exact VID:PID not found, using first available: " 
                  << out_port << std::endl;
        return true;
    }

    return false;
}

std::vector<std::string> MakcuWrapper::FindAllDevices() {
    std::vector<std::string> result;
    
    makcu_device_info_t devices[16];
    int count = makcu_find_devices(devices, 16);
    
    if (count <= 0) {
        return result;
    }

    for (int i = 0; i < count; ++i) {
        result.emplace_back(devices[i].port);
        std::cout << "[MakcuWrapper] Found device #" << i << ": " << devices[i].port
                  << " (VID:0x" << std::hex << devices[i].vid 
                  << ", PID:0x" << devices[i].pid << std::dec << ")" << std::endl;
    }

    return result;
}

bool MakcuWrapper::Initialize() {
    std::cout << "[MakcuWrapper] Initializing Makcu device..." << std::endl;

    if (m_initialized.load()) {
        std::cout << "[MakcuWrapper] Already initialized!" << std::endl;
        return true;
    }

    // Определяем COM-порт
    std::string port = m_config.com_port;
    
    if (port.empty()) {
        // Автопоиск устройства
        char found_port[64] = {0};
        if (!FindDeviceByVidPid(m_config.vid, m_config.pid, found_port, sizeof(found_port))) {
            std::cerr << "[MakcuWrapper] ERROR: No Makcu device found with VID:PID " 
                      << std::hex << m_config.vid << ":" << m_config.pid << std::dec << std::endl;
            return false;
        }
        port = found_port;
        m_config.com_port = port;
    }

    std::cout << "[MakcuWrapper] Using COM port: " << port << std::endl;

    // Создаём устройство через C API
    m_device = makcu_device_create();
    if (!m_device) {
        std::cerr << "[MakcuWrapper] ERROR: Failed to create makcu device!" << std::endl;
        return false;
    }

    // Подключаемся к устройству
    makcu_error_t error = makcu_connect(m_device, port.c_str());
    if (error != MAKCU_SUCCESS) {
        std::cerr << "[MakcuWrapper] ERROR: Failed to connect to Makcu on " << port 
                  << " (error: " << makcu_error_string(error) << ")" << std::endl;
        makcu_device_destroy(m_device);
        m_device = nullptr;
        return false;
    }

    // Проверяем подключение
    if (!makcu_is_connected(m_device)) {
        std::cerr << "[MakcuWrapper] ERROR: Device reports not connected after connect()" << std::endl;
        makcu_disconnect(m_device);
        makcu_device_destroy(m_device);
        m_device = nullptr;
        return false;
    }

    // Получаем версию устройства
    char version[64];
    error = makcu_get_version(m_device, version, sizeof(version));
    if (error == MAKCU_SUCCESS) {
        std::cout << "[MakcuWrapper] Successfully connected! Device version: " << version << std::endl;
    } else {
        std::cout << "[MakcuWrapper] Successfully connected! (version query failed)" << std::endl;
    }

    // Включаем мониторинг кнопок если требуется
    if (m_config.enable_monitoring) {
        error = makcu_enable_button_monitoring(m_device, true);
        if (error != MAKCU_SUCCESS) {
            std::cout << "[MakcuWrapper] Warning: Could not enable button monitoring: " 
                      << makcu_error_string(error) << std::endl;
        } else {
            std::cout << "[MakcuWrapper] Button monitoring enabled for 2PC sync" << std::endl;
        }

        // Запускаем поток мониторинга
        m_running.store(true);
        m_monitor_thread = std::make_unique<std::thread>(&MakcuWrapper::MonitorThreadFunc, this);
        std::cout << "[MakcuWrapper] Monitor thread started (interval: " << m_config.polling_interval_ms << "ms)" << std::endl;
    }

    // Включаем высокопроизводительный режим
    error = makcu_enable_high_performance_mode(m_device, true);
    if (error != MAKCU_SUCCESS) {
        std::cout << "[MakcuWrapper] Warning: Could not enable high performance mode" << std::endl;
    }

    m_initialized.store(true);
    std::cout << "[MakcuWrapper] Initialization complete!" << std::endl;
    return true;
}

void MakcuWrapper::Shutdown() {
    if (!m_initialized.load()) {
        return;
    }

    std::cout << "[MakcuWrapper] Shutting down..." << std::endl;

    // Останавливаем поток мониторинга
    m_running.store(false);
    if (m_monitor_thread && m_monitor_thread->joinable()) {
        m_monitor_thread->join();
        m_monitor_thread.reset();
    }

    if (m_device) {
        // Отключаем мониторинг кнопок
        makcu_enable_button_monitoring(m_device, false);
        
        // Отключаемся и уничтожаем устройство
        makcu_disconnect(m_device);
        makcu_device_destroy(m_device);
        m_device = nullptr;
    }

    // Сбрасываем глобальные переменные
    g_makcu_aiming.store(false);
    g_makcu_shooting.store(false);
    g_makcu_zooming.store(false);
    g_makcu_side1.store(false);
    g_makcu_side2.store(false);

    m_initialized.store(false);
    std::cout << "[MakcuWrapper] Shutdown complete" << std::endl;
}

bool MakcuWrapper::IsConnected() const {
    if (!m_initialized.load() || !m_device) {
        return false;
    }
    return makcu_is_connected(m_device);
}

std::string MakcuWrapper::GetDeviceInfo() const {
    if (!m_initialized.load() || !m_device) {
        return "Not initialized";
    }

    makcu_device_info_t info;
    makcu_error_t error = makcu_get_device_info(m_device, &info);
    if (error != MAKCU_SUCCESS) {
        return "Unknown";
    }

    return std::string(info.port) + " (VID:" + std::to_string(info.vid) + 
           ", PID:" + std::to_string(info.pid) + ")";
}

void MakcuWrapper::Move(int dx, int dy) {
    if (!m_initialized.load() || !m_device) {
        return;
    }
    
    makcu_error_t error = makcu_mouse_move(m_device, dx, dy);
    if (error != MAKCU_SUCCESS) {
#ifdef _DEBUG
        std::cerr << "[MakcuWrapper] Move failed: " << makcu_error_string(error) << std::endl;
#endif
    }
}

void MakcuWrapper::MoveSmooth(int dx, int dy, uint32_t segments) {
    if (!m_initialized.load() || !m_device) {
        return;
    }
    
    makcu_error_t error = makcu_mouse_move_smooth(m_device, dx, dy, segments);
    if (error != MAKCU_SUCCESS) {
#ifdef _DEBUG
        std::cerr << "[MakcuWrapper] MoveSmooth failed: " << makcu_error_string(error) << std::endl;
#endif
    }
}

void MakcuWrapper::Click(int button) {
    if (!m_initialized.load() || !m_device) {
        std::cerr << "[MakcuWrapper] Click called but not initialized!" << std::endl;
        return;
    }

    makcu_mouse_button_t btn = IntToButton(button);
    makcu_error_t error = makcu_mouse_click(m_device, btn);
    if (error != MAKCU_SUCCESS) {
        std::cerr << "[MakcuWrapper] Click failed: " << makcu_error_string(error) << std::endl;
    }
}

void MakcuWrapper::Press(int button) {
    if (!m_initialized.load() || !m_device) {
        std::cerr << "[MakcuWrapper] Press called but not initialized!" << std::endl;
        return;
    }

    makcu_mouse_button_t btn = IntToButton(button);
    makcu_error_t error = makcu_mouse_down(m_device, btn);
    if (error != MAKCU_SUCCESS) {
        std::cerr << "[MakcuWrapper] Press failed: " << makcu_error_string(error) << std::endl;
        return;
    }
    
    // Обновляем глобальное состояние
    UpdateGlobalButtonState(btn, true);
}

void MakcuWrapper::Release(int button) {
    if (!m_initialized.load() || !m_device) {
        std::cerr << "[MakcuWrapper] Release called but not initialized!" << std::endl;
        return;
    }

    makcu_mouse_button_t btn = IntToButton(button);
    makcu_error_t error = makcu_mouse_up(m_device, btn);
    if (error != MAKCU_SUCCESS) {
        std::cerr << "[MakcuWrapper] Release failed: " << makcu_error_string(error) << std::endl;
        return;
    }
    
    // Обновляем глобальное состояние
    UpdateGlobalButtonState(btn, false);
}

void MakcuWrapper::SetButtonCallback(ButtonCallback callback) {
    m_button_callback = callback;
}

void MakcuWrapper::MonitorThreadFunc() {
    std::cout << "[MakcuWrapper] Monitor thread running..." << std::endl;

    // Предыдущее состояние кнопок для детектирования изменений
    bool prev_left = false;
    bool prev_right = false;
    bool prev_middle = false;
    bool prev_side1 = false;
    bool prev_side2 = false;

    while (m_running.load()) {
        if (!m_device || !makcu_is_connected(m_device)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // Получаем маску кнопок через C API
        uint8_t mask = 0;
        makcu_error_t error = makcu_get_button_mask(m_device, &mask);
        
        if (error == MAKCU_SUCCESS) {
            // Биты маски: bit0=left, bit1=right, bit2=middle, bit3=side1, bit4=side2
            bool left = (mask & 0x01) != 0;
            bool right = (mask & 0x02) != 0;
            bool middle = (mask & 0x04) != 0;
            bool side1 = (mask & 0x08) != 0;
            bool side2 = (mask & 0x10) != 0;

            // Детектируем изменения и вызываем callback
            if (left != prev_left) {
                UpdateGlobalButtonState(MAKCU_MOUSE_LEFT, left);
                if (m_button_callback) {
                    m_button_callback(MAKCU_MOUSE_LEFT, left);
                }
                prev_left = left;
            }
            
            if (right != prev_right) {
                UpdateGlobalButtonState(MAKCU_MOUSE_RIGHT, right);
                if (m_button_callback) {
                    m_button_callback(MAKCU_MOUSE_RIGHT, right);
                }
                prev_right = right;
            }
            
            if (middle != prev_middle) {
                UpdateGlobalButtonState(MAKCU_MOUSE_MIDDLE, middle);
                if (m_button_callback) {
                    m_button_callback(MAKCU_MOUSE_MIDDLE, middle);
                }
                prev_middle = middle;
            }
            
            if (side1 != prev_side1) {
                UpdateGlobalButtonState(MAKCU_MOUSE_SIDE1, side1);
                if (m_button_callback) {
                    m_button_callback(MAKCU_MOUSE_SIDE1, side1);
                }
                prev_side1 = side1;
            }
            
            if (side2 != prev_side2) {
                UpdateGlobalButtonState(MAKCU_MOUSE_SIDE2, side2);
                if (m_button_callback) {
                    m_button_callback(MAKCU_MOUSE_SIDE2, side2);
                }
                prev_side2 = side2;
            }
        }

        // Ждём следующий цикл опроса
        std::this_thread::sleep_for(std::chrono::milliseconds(m_config.polling_interval_ms));
    }

    std::cout << "[MakcuWrapper] Monitor thread stopped" << std::endl;
}

void MakcuWrapper::UpdateGlobalButtonState(makcu_mouse_button_t button, bool pressed) {
    switch (button) {
        case MAKCU_MOUSE_LEFT:
            g_makcu_shooting.store(pressed);
#ifdef _DEBUG
            std::cout << "[MakcuWrapper] LEFT button " << (pressed ? "pressed" : "released") << std::endl;
#endif
            break;
        case MAKCU_MOUSE_RIGHT:
            g_makcu_aiming.store(pressed);
#ifdef _DEBUG
            std::cout << "[MakcuWrapper] RIGHT button " << (pressed ? "pressed" : "released") << std::endl;
#endif
            break;
        case MAKCU_MOUSE_MIDDLE:
            g_makcu_zooming.store(pressed);
#ifdef _DEBUG
            std::cout << "[MakcuWrapper] MIDDLE button " << (pressed ? "pressed" : "released") << std::endl;
#endif
            break;
        case MAKCU_MOUSE_SIDE1:
            g_makcu_side1.store(pressed);
#ifdef _DEBUG
            std::cout << "[MakcuWrapper] SIDE1 button " << (pressed ? "pressed" : "released") << std::endl;
#endif
            break;
        case MAKCU_MOUSE_SIDE2:
            g_makcu_side2.store(pressed);
#ifdef _DEBUG
            std::cout << "[MakcuWrapper] SIDE2 button " << (pressed ? "pressed" : "released") << std::endl;
#endif
            break;
        default:
            break;
    }
}

makcu_mouse_button_t MakcuWrapper::IntToButton(int button) {
    switch (button) {
        case 0: return MAKCU_MOUSE_LEFT;
        case 1: return MAKCU_MOUSE_RIGHT;
        case 2: return MAKCU_MOUSE_MIDDLE;
        case 3: return MAKCU_MOUSE_SIDE1;
        case 4: return MAKCU_MOUSE_SIDE2;
        default: return MAKCU_MOUSE_LEFT;
    }
}

} // namespace pwnz_ai
