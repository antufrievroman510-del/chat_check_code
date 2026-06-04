#include "MakcuWrapper.h"
#include <iostream>
#include <algorithm>

namespace pwnz_ai {

// ============================================
// Глобальные переменные для 2PC синхронизации
// ============================================
std::atomic<bool> g_makcu_aiming{false};    // Mouse5 (Side2) - прицеливание
std::atomic<bool> g_makcu_shooting{false};  // ЛКМ - стрельба
std::atomic<bool> g_makcu_zooming{false};   // ПКМ - зум
std::atomic<bool> g_makcu_side1{false};     // Mouse4 (Side1)
std::atomic<bool> g_makcu_side2{false};     // Side2 (дублирование для aiming)

// ============================================
// Реализация MakcuWrapper
// ============================================

MakcuWrapper::MakcuWrapper(const MakcuConfig& config)
    : m_config(config)
    , m_device(nullptr)
    , m_initialized(false)
    , m_connected(false)
    , m_button_callback(nullptr)
{
}

MakcuWrapper::~MakcuWrapper() {
    if (m_initialized.load()) {
        ShutdownInternal();
    }
}

std::optional<std::string> MakcuWrapper::FindDeviceByVidPid(uint16_t vid, uint16_t pid) {
    try {
        auto devices = makcu::Device::findDevices();
        
        // Ищем устройство с нужными VID:PID
        for (const auto& device : devices) {
            if (device.vid == vid && device.pid == pid) {
                std::cout << "[MakcuWrapper] Found device at " << device.port 
                          << " (VID:0x" << std::hex << vid << ", PID:0x" << pid << std::dec << ")" << std::endl;
                return device.port;
            }
        }

        // Если не нашли по точному совпадению, пробуем первое доступное устройство Makcu
        if (!devices.empty()) {
            std::cout << "[MakcuWrapper] Device with exact VID:PID not found, using first available: " 
                      << devices[0].port << std::endl;
            return devices[0].port;
        }
    } catch (const std::exception& e) {
        std::cerr << "[MakcuWrapper] Error finding device: " << e.what() << std::endl;
    }

    return std::nullopt;
}

std::vector<std::string> MakcuWrapper::FindAllDevices() {
    std::vector<std::string> result;
    
    try {
        auto devices = makcu::Device::findDevices();
        
        for (const auto& device : devices) {
            result.push_back(device.port);
            std::cout << "[MakcuWrapper] Found device: " << device.port
                      << " (VID:0x" << std::hex << device.vid 
                      << ", PID:0x" << device.pid << std::dec << ")" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "[MakcuWrapper] Error finding devices: " << e.what() << std::endl;
    }

    return result;
}

std::expected<void, std::string> MakcuWrapper::Initialize() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::cout << "[MakcuWrapper] Initializing Makcu device..." << std::endl;

    if (m_initialized.load()) {
        std::cout << "[MakcuWrapper] Already initialized!" << std::endl;
        return {};
    }

    try {
        // Создаем устройство
        m_device = std::make_unique<makcu::Device>();
        
        // Определяем COM-порт
        std::string port = m_config.com_port;
        
        if (port.empty()) {
            // Автопоиск устройства
            auto found_port = FindDeviceByVidPid(m_config.vid, m_config.pid);
            if (!found_port.has_value()) {
                std::string error_msg = "No Makcu device found with VID:PID " + 
                    std::to_string(m_config.vid) + ":" + std::to_string(m_config.pid);
                std::cerr << "[MakcuWrapper] ERROR: " << error_msg << std::endl;
                m_device.reset();
                return std::unexpected(error_msg);
            }
            port = found_port.value();
            m_config.com_port = port;
        }

        std::cout << "[MakcuWrapper] Using COM port: " << port << std::endl;

        // ============================================
        // КРИТИЧНО: Порядок инициализации как в референсе!
        // 1. Сначала устанавливаем callback
        // 2. Включаем мониторинг кнопок
        // 3. Только потом подключаемся
        // ============================================

        // Устанавливаем callback ДО подключения для обработки событий кнопок
        m_device->setMouseButtonCallback([this](makcu::MouseButton button, bool pressed) {
            OnButtonEvent(button, pressed);
        });
        std::cout << "[MakcuWrapper] Button callback installed" << std::endl;

        // Включаем мониторинг кнопок ДО подключения - КРИТИЧНО для 2PC!
        if (m_config.enable_monitoring) {
            std::cout << "[MakcuWrapper] Enabling button monitoring BEFORE connect..." << std::endl;
            bool monitor_result = m_device->enableButtonMonitoring(true);
            if (monitor_result) {
                std::cout << "[MakcuWrapper] Button monitoring ENABLED before connect" << std::endl;
            } else {
                std::cerr << "[MakcuWrapper] WARNING: Could not enable button monitoring before connect!" << std::endl;
            }
        }

        // Подключаемся к устройству (используем signature из референса: connect(port) без второго аргумента)
        std::cout << "[MakcuWrapper] Connecting to device..." << std::endl;
        if (!m_device->connect(port)) {
            std::string error_msg = "Failed to connect to Makcu on " + port;
            std::cerr << "[MakcuWrapper] ERROR: " << error_msg << std::endl;
            m_device.reset();
            return std::unexpected(error_msg);
        }
        std::cout << "[MakcuWrapper] Connect() returned success" << std::endl;

        // Проверяем подключение
        if (!m_device->isConnected()) {
            std::string error_msg = "Device reports not connected after connect()";
            std::cerr << "[MakcuWrapper] ERROR: " << error_msg << std::endl;
            m_device->disconnect();
            m_device.reset();
            return std::unexpected(error_msg);
        }
        std::cout << "[MakcuWrapper] Device isConnected() = true" << std::endl;

        // Получаем версию устройства
        try {
            std::string version = m_device->getVersion();
            std::cout << "[MakcuWrapper] Successfully connected! Device version: " << version << std::endl;
        } catch (...) {
            std::cout << "[MakcuWrapper] Successfully connected! (version query failed)" << std::endl;
        }

        // Проверяем статус мониторинга после подключения
        if (m_config.enable_monitoring) {
            std::cout << "[MakcuWrapper] Checking button monitoring status after connect..." << std::endl;
            
            // Проверяем несколько раз чтобы убедиться что мониторинг активен
            for (int i = 0; i < 5; i++) {
                bool monitoring_enabled = m_device->isButtonMonitoringEnabled();
                std::cout << "[MakcuWrapper] Button monitoring check " << (i+1) << "/5: " 
                          << (monitoring_enabled ? "ACTIVE" : "NOT ACTIVE") << std::endl;
                
                if (monitoring_enabled) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            // Финальная проверка
            if (!m_device->isButtonMonitoringEnabled()) {
                std::cerr << "[MakcuWrapper] CRITICAL: button monitoring is NOT active after connect! 2PC will NOT work!" << std::endl;
            } else {
                std::cout << "[MakcuWrapper] CONFIRMED: Button monitoring is ACTIVE - 2PC ready!" << std::endl;
            }
        }

        m_initialized.store(true);
        m_connected.store(true);
        
        // Включаем высокопроизводительный режим ПОСЛЕ успешного подключения
        try {
            m_device->enableHighPerformanceMode(true);
            std::cout << "[MakcuWrapper] High performance mode enabled" << std::endl;
        } catch (...) {
            std::cout << "[MakcuWrapper] Warning: Could not enable high performance mode" << std::endl;
        }
        
        // Устанавливаем baud rate если указан (только после подключения)
        if (m_config.baud_rate > 0 && m_config.baud_rate != 115200) {
            try {
                if (!m_device->setBaudRate(static_cast<uint32_t>(m_config.baud_rate), true)) {
                    std::cout << "[MakcuWrapper] Warning: Could not set baud rate to " << m_config.baud_rate << std::endl;
                } else {
                    std::cout << "[MakcuWrapper] Baud rate set to " << m_config.baud_rate << std::endl;
                }
            } catch (...) {
                std::cout << "[MakcuWrapper] Warning: Could not set baud rate" << std::endl;
            }
        }
        
        std::cout << "[MakcuWrapper] ==============================================" << std::endl;
        std::cout << "[MakcuWrapper] 2PC MODE READY - Waiting for button presses..." << std::endl;
        std::cout << "[MakcuWrapper] ==============================================" << std::endl;
        std::cout << "[MakcuWrapper] Initialization complete!" << std::endl;
        
        return {};
        
    } catch (const makcu::ConnectionException& e) {
        std::string error_msg = "Connection error: " + std::string(e.what());
        std::cerr << "[MakcuWrapper] ERROR: " << error_msg << std::endl;
        m_device.reset();
        return std::unexpected(error_msg);
    } catch (const makcu::MakcuException& e) {
        std::string error_msg = "Makcu error: " + std::string(e.what());
        std::cerr << "[MakcuWrapper] ERROR: " << error_msg << std::endl;
        m_device.reset();
        return std::unexpected(error_msg);
    } catch (const std::exception& e) {
        std::string error_msg = "Unexpected error: " + std::string(e.what());
        std::cerr << "[MakcuWrapper] ERROR: " << error_msg << std::endl;
        m_device.reset();
        return std::unexpected(error_msg);
    }
}

void MakcuWrapper::ShutdownInternal() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_initialized.load()) {
        return;
    }

    std::cout << "[MakcuWrapper] Shutting down..." << std::endl;

    if (m_device) {
        try {
            // Отключаем мониторинг кнопок
            m_device->enableButtonMonitoring(false);
            
            // Отключаемся и уничтожаем устройство
            m_device->disconnect();
        } catch (...) {
            // Игнорируем ошибки при отключении
        }
        m_device.reset();
    }

    // Сбрасываем глобальные переменные
    g_makcu_aiming.store(false);
    g_makcu_shooting.store(false);
    g_makcu_zooming.store(false);
    g_makcu_side1.store(false);
    g_makcu_side2.store(false);

    m_initialized.store(false);
    m_connected.store(false);
    
    std::cout << "[MakcuWrapper] Shutdown complete" << std::endl;
}

bool MakcuWrapper::Connect() {
    auto result = Initialize();
    if (result.has_value()) {
        return true;
    }
    return false;
}

bool MakcuWrapper::IsConnected() const {
    if (!m_initialized.load() || !m_connected.load()) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_device) {
        return false;
    }
    
    return m_device->isConnected();
}

std::string MakcuWrapper::GetDeviceInfo() const {
    if (!m_initialized.load() || !m_connected.load()) {
        return "Not initialized";
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_device) {
        return "No device";
    }

    try {
        auto info = m_device->getDeviceInfo();
        return info.port + " (VID:" + std::to_string(info.vid) + 
               ", PID:" + std::to_string(info.pid) + ")";
    } catch (...) {
        return "Unknown";
    }
}

void MakcuWrapper::MoveSmooth(int dx, int dy, uint32_t segments) {
    if (!m_initialized.load() || !m_connected.load()) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_device) {
        return;
    }
    
    try {
        m_device->mouseMoveSmooth(dx, dy, segments);
    } catch (...) {
#ifdef _DEBUG
        std::cerr << "[MakcuWrapper] MoveSmooth failed" << std::endl;
#endif
    }
}

void MakcuWrapper::Move(int dx, int dy) {
    if (!m_initialized.load() || !m_connected.load()) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_device) {
        return;
    }
    
    try {
        m_device->mouseMove(dx, dy);
    } catch (...) {
#ifdef _DEBUG
        std::cerr << "[MakcuWrapper] Move failed" << std::endl;
#endif
    }
}

void MakcuWrapper::Click(int button) {
    if (!m_initialized.load() || !m_connected.load()) {
        std::cerr << "[MakcuWrapper] Click called but not initialized!" << std::endl;
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_device) {
        return;
    }

    try {
        makcu::MouseButton btn = IntToButton(button);
        m_device->click(btn);
    } catch (...) {
        std::cerr << "[MakcuWrapper] Click failed" << std::endl;
    }
}

void MakcuWrapper::Press(int button) {
    if (!m_initialized.load() || !m_connected.load()) {
        std::cerr << "[MakcuWrapper] Press called but not initialized!" << std::endl;
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_device) {
        return;
    }

    try {
        makcu::MouseButton btn = IntToButton(button);
        m_device->mouseDown(btn);
        
        // Обновляем глобальное состояние
        UpdateGlobalButtonState(btn, true);
    } catch (...) {
        std::cerr << "[MakcuWrapper] Press failed" << std::endl;
    }
}

void MakcuWrapper::Release(int button) {
    if (!m_initialized.load() || !m_connected.load()) {
        std::cerr << "[MakcuWrapper] Release called but not initialized!" << std::endl;
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_device) {
        return;
    }

    try {
        makcu::MouseButton btn = IntToButton(button);
        m_device->mouseUp(btn);
        
        // Обновляем глобальное состояние
        UpdateGlobalButtonState(btn, false);
    } catch (...) {
        std::cerr << "[MakcuWrapper] Release failed" << std::endl;
    }
}

void MakcuWrapper::SetButtonCallback(ButtonCallback callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_button_callback = callback;
}

bool MakcuWrapper::TryReconnect() {
    if (IsConnected()) {
        return true; // Уже подключено
    }

    std::cout << "[MakcuWrapper] Attempting to reconnect..." << std::endl;
    
    // Сначала полностью закрываем старое соединение
    ShutdownInternal();
    
    // Пробуем заново инициализировать
    return Connect();
}

void MakcuWrapper::OnButtonEvent(makcu::MouseButton button, bool pressed) {
    // Логирование ВСЕХ событий для отладки 2PC - даже в релизе
    std::cout << "[MakcuWrapper] CALLBACK: Button " << static_cast<int>(button) 
              << " " << (pressed ? "PRESSED" : "RELEASED") << std::endl;
    
    // Обновляем глобальные переменные
    UpdateGlobalButtonState(button, pressed);
    
    // Вызываем пользовательский callback если установлен
    if (m_button_callback) {
        m_button_callback(button, pressed);
    }
}

makcu::MouseButton MakcuWrapper::IntToButton(int button) {
    switch (button) {
        case 0: return makcu::MouseButton::LEFT;
        case 1: return makcu::MouseButton::RIGHT;
        case 2: return makcu::MouseButton::MIDDLE;
        case 3: return makcu::MouseButton::SIDE1;
        case 4: return makcu::MouseButton::SIDE2;
        default: return makcu::MouseButton::LEFT;
    }
}

void MakcuWrapper::UpdateGlobalButtonState(makcu::MouseButton button, bool pressed) {
    // КРИТИЧНО: Логирование для отладки 2PC - показываем какое событие пришло и какую переменную обновляем
    std::cout << "[MakcuWrapper] UpdateGlobalButtonState: Button " << static_cast<int>(button) 
              << " -> " << (pressed ? "TRUE" : "FALSE") << std::endl;

    switch (button) {
        case makcu::MouseButton::LEFT:
            // LMB = shooting
            std::cout << "[MakcuWrapper] Setting g_makcu_shooting = " << pressed << std::endl;
            g_makcu_shooting.store(pressed);
            break;
            
        case makcu::MouseButton::RIGHT:
            // RMB = zooming/aiming - В РЕФЕРЕНСЕ RMB это zooming, а aiming это SIDE2!
            std::cout << "[MakcuWrapper] Setting g_makcu_zooming = " << pressed << std::endl;
            g_makcu_zooming.store(pressed);
            break;
            
        case makcu::MouseButton::MIDDLE:
            // MMB - not used for now
            std::cout << "[MakcuWrapper] MIDDLE button event (ignored)" << std::endl;
            break;
            
        case makcu::MouseButton::SIDE1:
            // Mouse4 (Side1)
            std::cout << "[MakcuWrapper] Setting g_makcu_side1 = " << pressed << std::endl;
            g_makcu_side1.store(pressed);
            break;
            
        case makcu::MouseButton::SIDE2:
            // Mouse5 (Side2) = aiming - ЭТО ГЛАВНАЯ КНОПКА ПРИЦЕЛИВАНИЯ!
            std::cout << "[MakcuWrapper] Setting g_makcu_aiming = " << pressed << " (SIDE2)" << std::endl;
            g_makcu_aiming.store(pressed);
            g_makcu_side2.store(pressed);
            break;
            
        default:
            std::cout << "[MakcuWrapper] Unknown button event: " << static_cast<int>(button) << std::endl;
            break;
    }
    
    // Финальный статус всех переменных
    std::cout << "[MakcuWrapper] Global state: aiming=" << g_makcu_aiming.load() 
              << " shooting=" << g_makcu_shooting.load()
              << " zooming=" << g_makcu_zooming.load()
              << " side1=" << g_makcu_side1.load()
              << " side2=" << g_makcu_side2.load() << std::endl;
}

} // namespace pwnz_ai
