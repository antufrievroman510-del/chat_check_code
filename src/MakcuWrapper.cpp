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
        // Проверяем, включен ли режим UDP
        if (m_config.enable_udp_listener) {
            std::cout << "[MakcuWrapper] Using UDP LISTENER mode for 2PC sync" << std::endl;
            
            // Создаем и запускаем UDP слушатель
            m_udp_listener = std::make_unique<UdpMouseListener>();
            m_udp_listener->Start(m_config.udp_port);
            
            // Запускаем поток мониторинга UDP
            m_running.store(true);
            m_monitor_thread = std::make_unique<std::thread>(&MakcuWrapper::UdpMonitorThreadFunc, this);
            std::cout << "[MakcuWrapper] ✓ UDP Monitor thread STARTED on port " << m_config.udp_port << std::endl;
        } else {
            // COM-порт режим
            // Сначала включаем отправку данных о кнопках с устройства
            // Это критически важно для 2PC режима - устройство должно отправлять байты при нажатии
            makcu_error_t error = makcu_enable_button_monitoring(m_device, true);
            if (error != MAKCU_SUCCESS) {
                std::cerr << "[MakcuWrapper] ERROR: Could not enable button monitoring: " 
                          << makcu_error_string(error) << std::endl;
            } else {
                std::cout << "[MakcuWrapper] Button monitoring ENABLED for 2PC sync" << std::endl;
            }

            // Проверяем, включился ли мониторинг
            bool monitoring_enabled = false;
            error = makcu_is_button_monitoring_enabled(m_device, &monitoring_enabled);
            if (error == MAKCU_SUCCESS && monitoring_enabled) {
                std::cout << "[MakcuWrapper] ✓ Confirmed: button monitoring is ACTIVE" << std::endl;
            } else {
                std::cerr << "[MakcuWrapper] WARNING: button monitoring status check failed or disabled" << std::endl;
            }

            // Получаем текущую маску кнопок для начального состояния
            uint8_t initial_mask = 0;
            error = makcu_get_button_mask(m_device, &initial_mask);
            if (error == MAKCU_SUCCESS) {
                std::cout << "[MakcuWrapper] Initial button mask: 0x" << std::hex << (int)initial_mask << std::dec << std::endl;
            }

            // Устанавливаем callback через C API для обработки событий кнопок
            // Библиотека будет вызывать этот callback когда получит байты от устройства
            auto c_callback = [](makcu_mouse_button_t button, bool pressed, void* user_data) {
                if (!user_data) return;
                auto* wrapper = static_cast<MakcuWrapper*>(user_data);
                
                // Логирование ВСЕХ событий для отладки 2PC
                std::cout << "[MakcuWrapper] CALLBACK: Button " << static_cast<int>(button) 
                          << " " << (pressed ? "PRESSED" : "RELEASED") << std::endl;
                
                wrapper->UpdateGlobalButtonState(button, pressed);
            };
            
            error = makcu_set_mouse_button_callback(m_device, c_callback, this);
            if (error != MAKCU_SUCCESS) {
                std::cerr << "[MakcuWrapper] ERROR: Could not set mouse button callback: " 
                          << makcu_error_string(error) << std::endl;
            } else {
                std::cout << "[MakcuWrapper] ✓ Mouse button callback REGISTERED" << std::endl;
            }

            // Запускаем поток мониторинга (POLLING MODE с makcu_get_button_mask)
            m_running.store(true);
            m_monitor_thread = std::make_unique<std::thread>(&MakcuWrapper::MonitorThreadFunc, this);
            std::cout << "[MakcuWrapper] ✓ Monitor thread STARTED (polling with get_button_mask)" << std::endl;
        }
        
        std::cout << "[MakcuWrapper] ==============================================" << std::endl;
        std::cout << "[MakcuWrapper] 2PC MODE READY - Waiting for button presses..." << std::endl;
        std::cout << "[MakcuWrapper] ==============================================" << std::endl;
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

    // Останавливаем UDP слушатель если был запущен
    if (m_udp_listener) {
        m_udp_listener->Stop();
        m_udp_listener.reset();
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

    // Для 2PC режима: опрашиваем устройство через makcu_get_button_mask
    // Это читает последние полученные байты от устройства Makcu на ПК№1
    
    uint8_t prev_mask = 0;
    int poll_count = 0;
    int error_count = 0;
    int success_count = 0;
    
    while (m_running.load()) {
        if (!m_device || !makcu_is_connected(m_device)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        
        poll_count++;
        
        // === СПОСОБ 1: Используем makcu_get_button_mask ===
        // Эта функция возвращает маску всех кнопок одним вызовом
        uint8_t button_mask = 0;
        makcu_error_t err = makcu_get_button_mask(m_device, &button_mask);
        
        if (err != MAKCU_SUCCESS) {
            error_count++;
            if (error_count % 100 == 1) {
                std::cout << "[MakcuWrapper] get_button_mask error: " << makcu_error_string(err) 
                          << " (error_count=" << error_count << ")" << std::endl;
            }
            // При ошибке продолжаем опрос, возможно временная проблема
            std::this_thread::sleep_for(std::chrono::milliseconds(m_config.polling_interval_ms));
            continue;
        }
        
        success_count++;
        
        // Интерпретируем маску кнопок
        // Биты: 0=ЛКМ, 1=ПКМ, 2=СКМ, 3=Side1, 4=Side2
        bool curr_left   = (button_mask & 0x01) != 0;
        bool curr_right  = (button_mask & 0x02) != 0;
        bool curr_middle = (button_mask & 0x04) != 0;
        bool curr_side1  = (button_mask & 0x08) != 0;
        bool curr_side2  = (button_mask & 0x10) != 0;
        
        // Детектируем изменения состояний (фронты сигналов)
        bool left_changed   = ((button_mask & 0x01) != (prev_mask & 0x01));
        bool right_changed  = ((button_mask & 0x02) != (prev_mask & 0x02));
        bool middle_changed = ((button_mask & 0x04) != (prev_mask & 0x04));
        bool side1_changed  = ((button_mask & 0x08) != (prev_mask & 0x08));
        bool side2_changed  = ((button_mask & 0x10) != (prev_mask & 0x10));
        
        // Обработка изменений ЛКМ
        if (left_changed) {
#ifdef _DEBUG
            std::cout << "[MakcuWrapper] LEFT button " << (curr_left ? "PRESSED" : "RELEASED") 
                      << " (mask=0x" << std::hex << (int)button_mask << std::dec << ")" << std::endl;
#endif
            UpdateGlobalButtonState(MAKCU_MOUSE_LEFT, curr_left);
            if (m_button_callback) m_button_callback(MAKCU_MOUSE_LEFT, curr_left);
        }
        
        // Обработка изменений ПКМ
        if (right_changed) {
#ifdef _DEBUG
            std::cout << "[MakcuWrapper] RIGHT button " << (curr_right ? "PRESSED" : "RELEASED")
                      << " (mask=0x" << std::hex << (int)button_mask << std::dec << ")" << std::endl;
#endif
            UpdateGlobalButtonState(MAKCU_MOUSE_RIGHT, curr_right);
            if (m_button_callback) m_button_callback(MAKCU_MOUSE_RIGHT, curr_right);
        }
        
        // Обработка изменений СКМ
        if (middle_changed) {
#ifdef _DEBUG
            std::cout << "[MakcuWrapper] MIDDLE button " << (curr_middle ? "PRESSED" : "RELEASED")
                      << " (mask=0x" << std::hex << (int)button_mask << std::dec << ")" << std::endl;
#endif
            UpdateGlobalButtonState(MAKCU_MOUSE_MIDDLE, curr_middle);
            if (m_button_callback) m_button_callback(MAKCU_MOUSE_MIDDLE, curr_middle);
        }
        
        // Обработка изменений Side1
        if (side1_changed) {
#ifdef _DEBUG
            std::cout << "[MakcuWrapper] SIDE1 button " << (curr_side1 ? "PRESSED" : "RELEASED")
                      << " (mask=0x" << std::hex << (int)button_mask << std::dec << ")" << std::endl;
#endif
            UpdateGlobalButtonState(MAKCU_MOUSE_SIDE1, curr_side1);
            if (m_button_callback) m_button_callback(MAKCU_MOUSE_SIDE1, curr_side1);
        }
        
        // Обработка изменений Side2
        if (side2_changed) {
#ifdef _DEBUG
            std::cout << "[MakcuWrapper] SIDE2 button " << (curr_side2 ? "PRESSED" : "RELEASED")
                      << " (mask=0x" << std::hex << (int)button_mask << std::dec << ")" << std::endl;
#endif
            UpdateGlobalButtonState(MAKCU_MOUSE_SIDE2, curr_side2);
            if (m_button_callback) m_button_callback(MAKCU_MOUSE_SIDE2, curr_side2);
        }
        
        // Сохраняем предыдущее состояние
        prev_mask = button_mask;
        
        // Периодический статус (каждые 500 успешных опросов ~ каждые 0.5 сек при 1ms интервале)
        if (success_count % 500 == 1) {
            std::cout << "[MakcuWrapper] Monitor alive. Buttons: L=" << curr_left 
                      << " R=" << curr_right << " M=" << curr_middle
                      << " S1=" << curr_side1 << " S2=" << curr_side2
                      << " (mask=0x" << std::hex << (int)button_mask << std::dec 
                      << ", success=" << success_count << ", errors=" << error_count << ")" << std::endl;
        }
        
        // Опрос с минимальной задержкой для sub-millisecond реакции
        std::this_thread::sleep_for(std::chrono::milliseconds(m_config.polling_interval_ms));
    }

    std::cout << "[MakcuWrapper] Monitor thread stopped. Total polls: " << poll_count 
              << ", Success: " << success_count << ", Errors: " << error_count << std::endl;
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

// ============================================
// UDP мониторинг для 2PC режима
// ============================================
void MakcuWrapper::UdpMonitorThreadFunc() {
    std::cout << "[MakcuWrapper] UDP Monitor thread running..." << std::endl;
    
    // Предыдущие состояния для детектирования изменений
    bool prev_aim = false;
    bool prev_shoot = false;
    bool prev_zoom = false;
    bool prev_side1 = false;
    bool prev_side2 = false;
    
    int check_count = 0;
    
    while (m_running.load()) {
        if (!m_udp_listener) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        
        check_count++;
        
        // Получаем текущие состояния кнопок от UDP слушателя
        bool curr_aim = m_udp_listener->IsAimKeyPressed();      // ПКМ
        bool curr_shoot = m_udp_listener->IsShootKeyPressed();  // ЛКМ
        bool curr_zoom = m_udp_listener->IsZoomKeyPressed();    // СКМ
        bool curr_side1 = m_udp_listener->IsSide1Pressed();     // Side1
        bool curr_side2 = m_udp_listener->IsSide2Pressed();     // Side2
        
        // Детектируем изменения и обновляем глобальные переменные
        if (curr_aim != prev_aim) {
            std::cout << "[UDP Monitor] ПКМ (aim): " << (curr_aim ? "НАЖАТА" : "ОТПУЩЕНА") << std::endl;
            g_makcu_aiming.store(curr_aim);
            if (m_button_callback) m_button_callback(MAKCU_MOUSE_RIGHT, curr_aim);
            prev_aim = curr_aim;
        }
        
        if (curr_shoot != prev_shoot) {
            std::cout << "[UDP Monitor] ЛКМ (shoot): " << (curr_shoot ? "НАЖАТА" : "ОТПУЩЕНА") << std::endl;
            g_makcu_shooting.store(curr_shoot);
            if (m_button_callback) m_button_callback(MAKCU_MOUSE_LEFT, curr_shoot);
            prev_shoot = curr_shoot;
        }
        
        if (curr_zoom != prev_zoom) {
            std::cout << "[UDP Monitor] СКМ (zoom): " << (curr_zoom ? "НАЖАТА" : "ОТПУЩЕНА") << std::endl;
            g_makcu_zooming.store(curr_zoom);
            if (m_button_callback) m_button_callback(MAKCU_MOUSE_MIDDLE, curr_zoom);
            prev_zoom = curr_zoom;
        }
        
        if (curr_side1 != prev_side1) {
            std::cout << "[UDP Monitor] Side1: " << (curr_side1 ? "НАЖАТА" : "ОТПУЩЕНА") << std::endl;
            g_makcu_side1.store(curr_side1);
            if (m_button_callback) m_button_callback(MAKCU_MOUSE_SIDE1, curr_side1);
            prev_side1 = curr_side1;
        }
        
        if (curr_side2 != prev_side2) {
            std::cout << "[UDP Monitor] Side2: " << (curr_side2 ? "НАЖАТА" : "ОТПУЩЕНА") << std::endl;
            g_makcu_side2.store(curr_side2);
            if (m_button_callback) m_button_callback(MAKCU_MOUSE_SIDE2, curr_side2);
            prev_side2 = curr_side2;
        }
        
        // Периодический статус
        if (check_count % 500 == 1) {
            std::cout << "[UDP Monitor] Alive. Aim=" << curr_aim << " Shoot=" << curr_shoot 
                      << " Zoom=" << curr_zoom << " S1=" << curr_side1 << " S2=" << curr_side2 << std::endl;
        }
        
        // Опрос с минимальной задержкой
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    std::cout << "[UDP Monitor] Thread stopped after " << check_count << " checks" << std::endl;
}

} // namespace pwnz_ai
