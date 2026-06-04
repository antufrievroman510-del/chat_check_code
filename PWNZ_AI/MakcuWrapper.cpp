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
        Shutdown();
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

        // Устанавливаем callback ДО подключения для обработки событий кнопок
        m_device->setMouseButtonCallback([this](makcu::MouseButton button, bool pressed) {
            OnButtonEvent(button, pressed);
        });

        // Подключаемся к устройству
        if (!m_device->connect(port, true)) {
            std::string error_msg = "Failed to connect to Makcu on " + port;
            std::cerr << "[MakcuWrapper] ERROR: " << error_msg << std::endl;
            m_device.reset();
            return std::unexpected(error_msg);
        }

        // Проверяем подключение
        if (!m_device->isConnected()) {
            std::string error_msg = "Device reports not connected after connect()";
            std::cerr << "[MakcuWrapper] ERROR: " << error_msg << std::endl;
            m_device->disconnect();
            m_device.reset();
            return std::unexpected(error_msg);
        }

        // Получаем версию устройства
        try {
            std::string version = m_device->getVersion();
            std::cout << "[MakcuWrapper] Successfully connected! Device version: " << version << std::endl;
        } catch (...) {
            std::cout << "[MakcuWrapper] Successfully connected! (version query failed)" << std::endl;
        }

        // Включаем мониторинг кнопок если требуется
        if (m_config.enable_monitoring) {
            if (m_device->enableButtonMonitoring(true)) {
                std::cout << "[MakcuWrapper] Button monitoring ENABLED for 2PC sync" << std::endl;
                
                if (m_device->isButtonMonitoringEnabled()) {
                    std::cout << "[MakcuWrapper] Confirmed: button monitoring is ACTIVE" << std::endl;
                } else {
                    std::cerr << "[MakcuWrapper] WARNING: button monitoring status check failed or disabled" << std::endl;
                }
            } else {
                std::cerr << "[MakcuWrapper] WARNING: Could not enable button monitoring" << std::endl;
            }
        }

        // Включаем высокопроизводительный режим
        m_device->enableHighPerformanceMode(true);
        
        // Устанавливаем baud rate если указан
        if (m_config.baud_rate > 0 && m_config.baud_rate != 115200) {
            if (!m_device->setBaudRate(static_cast<uint32_t>(m_config.baud_rate), true)) {
                std::cout << "[MakcuWrapper] Warning: Could not set baud rate to " << m_config.baud_rate << std::endl;
            }
        }

        m_initialized.store(true);
        m_connected.store(true);
        
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

void MakcuWrapper::Shutdown() {
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
    Shutdown();
    
    // Пробуем заново инициализировать
    return Connect();
}

void MakcuWrapper::OnButtonEvent(makcu::MouseButton button, bool pressed) {
    // Логирование всех событий для отладки 2PC
#ifdef _DEBUG
    std::cout << "[MakcuWrapper] CALLBACK: Button " << static_cast<int>(button) 
              << " " << (pressed ? "PRESSED" : "RELEASED") << std::endl;
#endif
    
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
    switch (button) {
        case makcu::MouseButton::LEFT:
            // LMB = shooting
            g_makcu_shooting.store(pressed);
            break;
            
        case makcu::MouseButton::RIGHT:
            // RMB = zooming/aiming
            g_makcu_zooming.store(pressed);
            break;
            
        case makcu::MouseButton::MIDDLE:
            // MMB - not used for now
            break;
            
        case makcu::MouseButton::SIDE1:
            // Mouse4 (Side1)
            g_makcu_side1.store(pressed);
            break;
            
        case makcu::MouseButton::SIDE2:
            // Mouse5 (Side2) = aiming
            g_makcu_aiming.store(pressed);
            g_makcu_side2.store(pressed);
            break;
            
        default:
            break;
    }
}

} // namespace pwnz_ai
