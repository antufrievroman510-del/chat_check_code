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
        // Сначала включаем отправку данных о кнопках с устройства
        // Это критически важно для 2PC режима - устройство должно отправлять байты при нажатии
        error = makcu_enable_button_monitoring(m_device, true);
        if (error != MAKCU_SUCCESS) {
            std::cout << "[MakcuWrapper] Warning: Could not enable button monitoring: " 
                      << makcu_error_string(error) << std::endl;
        } else {
            std::cout << "[MakcuWrapper] Button monitoring enabled for 2PC sync" << std::endl;
        }

        // Устанавливаем callback через C API для обработки событий кнопок
        // Библиотека будет вызывать этот callback когда получит байты от устройства
        auto c_callback = [](makcu_mouse_button_t button, bool pressed, void* user_data) {
            if (!user_data) return;
            auto* wrapper = static_cast<MakcuWrapper*>(user_data);
            wrapper->UpdateGlobalButtonState(button, pressed);
#ifdef _DEBUG
            std::cout << "[MakcuWrapper] C Callback: button=" << static_cast<int>(button) 
                      << " pressed=" << pressed << std::endl;
#endif
        };
        
        error = makcu_set_mouse_button_callback(m_device, c_callback, this);
        if (error != MAKCU_SUCCESS) {
            std::cout << "[MakcuWrapper] Warning: Could not set mouse button callback: " 
                      << makcu_error_string(error) << std::endl;
        } else {
            std::cout << "[MakcuWrapper] Mouse button callback registered" << std::endl;
        }

        // Запускаем поток мониторинга (в event-driven режиме он просто ждёт)
        m_running.store(true);
        m_monitor_thread = std::make_unique<std::thread>(&MakcuWrapper::MonitorThreadFunc, this);
        std::cout << "[MakcuWrapper] Monitor thread started (event-driven mode)" << std::endl;
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
    std::cout << "[MakcuWrapper] Monitor thread running (POLLING MODE for 2PC)..." << std::endl;
    std::cout << "[MakcuWrapper] Polling interval: " << m_config.polling_interval_ms << "ms" << std::endl;

    // Для 2PC режима: опрашиваем устройство на предмет изменений кнопок
    // Устройство Makcu на ПК№1 отправляет байты при нажатии кнопок
    // Мы читаем эти байты через последовательный порт
    
    // Состояния кнопок для детектирования изменений (изначально все отпущены)
    struct ButtonState {
        bool left = false;
        bool right = false;
        bool middle = false;
        bool side1 = false;
        bool side2 = false;
    };
    
    ButtonState prev_state;
    int poll_count = 0;
    int error_count = 0;
    
    while (m_running.load()) {
        if (!m_device || !makcu_is_connected(m_device)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        
        poll_count++;
        
        // === СПОСОБ 1: Используем makcu_catch_mouse_* функции ===
        // Эти функции должны читать последние полученные байты от устройства
        uint8_t left_result = 0, right_result = 0, middle_result = 0;
        uint8_t side1_result = 0, side2_result = 0;
        
        makcu_error_t err_l = makcu_catch_mouse_left(m_device, &left_result);
        makcu_error_t err_r = makcu_catch_mouse_right(m_device, &right_result);
        makcu_error_t err_m = makcu_catch_mouse_middle(m_device, &middle_result);
        makcu_error_t err_s1 = makcu_catch_mouse_side1(m_device, &side1_result);
        makcu_error_t err_s2 = makcu_catch_mouse_side2(m_device, &side2_result);
        
        // Логгируем ошибки только периодически
        if (err_l != MAKCU_SUCCESS || err_r != MAKCU_SUCCESS || err_m != MAKCU_SUCCESS ||
            err_s1 != MAKCU_SUCCESS || err_s2 != MAKCU_SUCCESS) {
            error_count++;
            if (error_count % 100 == 1) {
                std::cout << "[MakcuWrapper] catch_mouse errors: L=" << err_l 
                          << " R=" << err_r << " M=" << err_m 
                          << " S1=" << err_s1 << " S2=" << err_s2 << std::endl;
            }
        }
        
        // Интерпретируем результаты
        // Обычно: 0 = отпущена, 1 = нажата (но может зависеть от прошивки)
        bool curr_left = (left_result != 0);
        bool curr_right = (right_result != 0);
        bool curr_middle = (middle_result != 0);
        bool curr_side1 = (side1_result != 0);
        bool curr_side2 = (side2_result != 0);
        
        // Детектируем изменения состояний
        bool changed = false;
        
        if (curr_left != prev_state.left) {
#ifdef _DEBUG
            std::cout << "[MakcuWrapper] LEFT button " << (curr_left ? "PRESSED" : "RELEASED") 
                      << " (result=" << (int)left_result << ")" << std::endl;
#endif
            UpdateGlobalButtonState(MAKCU_MOUSE_LEFT, curr_left);
            if (m_button_callback) m_button_callback(MAKCU_MOUSE_LEFT, curr_left);
            prev_state.left = curr_left;
            changed = true;
        }
        
        if (curr_right != prev_state.right) {
#ifdef _DEBUG
            std::cout << "[MakcuWrapper] RIGHT button " << (curr_right ? "PRESSED" : "RELEASED")
                      << " (result=" << (int)right_result << ")" << std::endl;
#endif
            UpdateGlobalButtonState(MAKCU_MOUSE_RIGHT, curr_right);
            if (m_button_callback) m_button_callback(MAKCU_MOUSE_RIGHT, curr_right);
            prev_state.right = curr_right;
            changed = true;
        }
        
        if (curr_middle != prev_state.middle) {
#ifdef _DEBUG
            std::cout << "[MakcuWrapper] MIDDLE button " << (curr_middle ? "PRESSED" : "RELEASED")
                      << " (result=" << (int)middle_result << ")" << std::endl;
#endif
            UpdateGlobalButtonState(MAKCU_MOUSE_MIDDLE, curr_middle);
            if (m_button_callback) m_button_callback(MAKCU_MOUSE_MIDDLE, curr_middle);
            prev_state.middle = curr_middle;
            changed = true;
        }
        
        if (curr_side1 != prev_state.side1) {
#ifdef _DEBUG
            std::cout << "[MakcuWrapper] SIDE1 button " << (curr_side1 ? "PRESSED" : "RELEASED")
                      << " (result=" << (int)side1_result << ")" << std::endl;
#endif
            UpdateGlobalButtonState(MAKCU_MOUSE_SIDE1, curr_side1);
            if (m_button_callback) m_button_callback(MAKCU_MOUSE_SIDE1, curr_side1);
            prev_state.side1 = curr_side1;
            changed = true;
        }
        
        if (curr_side2 != prev_state.side2) {
#ifdef _DEBUG
            std::cout << "[MakcuWrapper] SIDE2 button " << (curr_side2 ? "PRESSED" : "RELEASED")
                      << " (result=" << (int)side2_result << ")" << std::endl;
#endif
            UpdateGlobalButtonState(MAKCU_MOUSE_SIDE2, curr_side2);
            if (m_button_callback) m_button_callback(MAKCU_MOUSE_SIDE2, curr_side2);
            prev_state.side2 = curr_side2;
            changed = true;
        }
        
        // Периодический статус
        if (poll_count % 500 == 1) {
            std::cout << "[MakcuWrapper] Monitor alive. Buttons: L=" << curr_left 
                      << " R=" << curr_right << " M=" << curr_middle
                      << " S1=" << curr_side1 << " S2=" << curr_side2 << std::endl;
        }
        
        // Опрос с минимальной задержкой для sub-millisecond реакции
        std::this_thread::sleep_for(std::chrono::milliseconds(m_config.polling_interval_ms));
    }

    std::cout << "[MakcuWrapper] Monitor thread stopped. Total polls: " << poll_count 
              << ", Errors: " << error_count << std::endl;
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
