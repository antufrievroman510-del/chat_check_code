#include "MakcuWrapper.h"
#include <iostream>
#include <algorithm>

namespace pwnz_ai {

// ============================================
// Реализация MakcuWrapper (архитектура как в референсе)
// ============================================

MakcuWrapper::MakcuWrapper(const std::string& port, unsigned int baud_rate)
    : m_device(nullptr)
    , m_is_open(false)
    , m_aiming_active(false)
    , m_shooting_active(false)
    , m_zooming_active(false)
{
    // Пустой конструктор - инициализация в Connect()
}

MakcuWrapper::~MakcuWrapper() {
    Disconnect();
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

bool MakcuWrapper::Connect() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_is_open.load()) {
        std::cout << "[MakcuWrapper] Already connected!" << std::endl;
        return true;
    }

    try {
        // Создаем устройство
        m_device = std::make_unique<makcu::Device>();
        
        // Определяем COM-порт
        std::string port = FindDeviceByVidPid().value_or("");
        
        if (port.empty()) {
            std::cerr << "[MakcuWrapper] ERROR: No Makcu device found!" << std::endl;
            m_device.reset();
            return false;
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
            onButtonCallback(button, pressed);
        });
        std::cout << "[MakcuWrapper] Button callback installed" << std::endl;

        // Включаем мониторинг кнопок ДО подключения - КРИТИЧНО для 2PC!
        std::cout << "[MakcuWrapper] Enabling button monitoring BEFORE connect..." << std::endl;
        bool monitor_result = m_device->enableButtonMonitoring(true);
        if (monitor_result) {
            std::cout << "[MakcuWrapper] Button monitoring ENABLED before connect" << std::endl;
        } else {
            std::cerr << "[MakcuWrapper] WARNING: Could not enable button monitoring before connect!" << std::endl;
        }

        // Подключаемся к устройству
        std::cout << "[MakcuWrapper] Connecting to device..." << std::endl;
        if (!m_device->connect(port)) {
            std::cerr << "[MakcuWrapper] ERROR: Failed to connect to Makcu on " << port << std::endl;
            m_device.reset();
            return false;
        }
        std::cout << "[MakcuWrapper] Connect() returned success" << std::endl;

        // Проверяем подключение
        if (!m_device->isConnected()) {
            std::cerr << "[MakcuWrapper] ERROR: Device reports not connected after connect()" << std::endl;
            m_device->disconnect();
            m_device.reset();
            return false;
        }
        std::cout << "[MakcuWrapper] Device isConnected() = true" << std::endl;

        // Получаем версию устройства
        try {
            std::string version = m_device->getVersion();
            std::cout << "[MakcuWrapper] Successfully connected! Device version: " << version << std::endl;
        } catch (...) {
            std::cout << "[MakcuWrapper] Successfully connected!" << std::endl;
        }

        m_is_open.store(true);
        
        // Включаем высокопроизводительный режим ПОСЛЕ успешного подключения
        try {
            m_device->enableHighPerformanceMode(true);
            std::cout << "[MakcuWrapper] High performance mode enabled" << std::endl;
        } catch (...) {
            std::cout << "[MakcuWrapper] Warning: Could not enable high performance mode" << std::endl;
        }
        
        std::cout << "[MakcuWrapper] ==============================================" << std::endl;
        std::cout << "[MakcuWrapper] 2PC MODE READY - Waiting for button presses..." << std::endl;
        std::cout << "[MakcuWrapper] ==============================================" << std::endl;
        
        return true;
        
    } catch (const makcu::ConnectionException& e) {
        std::cerr << "[MakcuWrapper] Connection error: " << e.what() << std::endl;
        m_device.reset();
        return false;
    } catch (const makcu::MakcuException& e) {
        std::cerr << "[MakcuWrapper] Makcu error: " << e.what() << std::endl;
        m_device.reset();
        return false;
    } catch (const std::exception& e) {
        std::cerr << "[MakcuWrapper] Unexpected error: " << e.what() << std::endl;
        m_device.reset();
        return false;
    }
}

void MakcuWrapper::Disconnect() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_is_open.load()) {
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

    // Сбрасываем состояние кнопок
    m_aiming_active.store(false);
    m_shooting_active.store(false);
    m_zooming_active.store(false);

    m_is_open.store(false);
    
    std::cout << "[MakcuWrapper] Shutdown complete" << std::endl;
}

bool MakcuWrapper::IsConnected() const {
    if (!m_is_open.load()) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_device) {
        return false;
    }
    
    return m_device->isConnected();
}

std::string MakcuWrapper::GetDeviceInfo() const {
    if (!m_is_open.load()) {
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
    if (!m_is_open.load()) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(m_write_mutex);
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
    if (!m_is_open.load()) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(m_write_mutex);
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
    if (!m_is_open.load()) {
        std::cerr << "[MakcuWrapper] Click called but not initialized!" << std::endl;
        return;
    }

    std::lock_guard<std::mutex> lock(m_write_mutex);
    if (!m_device) {
        return;
    }

    try {
        makcu::MouseButton btn = static_cast<makcu::MouseButton>(button);
        m_device->click(btn);
    } catch (...) {
        std::cerr << "[MakcuWrapper] Click failed" << std::endl;
    }
}

void MakcuWrapper::Press(int button) {
    if (!m_is_open.load()) {
        std::cerr << "[MakcuWrapper] Press called but not initialized!" << std::endl;
        return;
    }

    std::lock_guard<std::mutex> lock(m_write_mutex);
    if (!m_device) {
        return;
    }

    try {
        makcu::MouseButton btn = static_cast<makcu::MouseButton>(button);
        m_device->mouseDown(btn);
    } catch (...) {
        std::cerr << "[MakcuWrapper] Press failed" << std::endl;
    }
}

void MakcuWrapper::Release(int button) {
    if (!m_is_open.load()) {
        std::cerr << "[MakcuWrapper] Release called but not initialized!" << std::endl;
        return;
    }

    std::lock_guard<std::mutex> lock(m_write_mutex);
    if (!m_device) {
        return;
    }

    try {
        makcu::MouseButton btn = static_cast<makcu::MouseButton>(button);
        m_device->mouseUp(btn);
    } catch (...) {
        std::cerr << "[MakcuWrapper] Release failed" << std::endl;
    }
}

bool MakcuWrapper::TryReconnect() {
    if (IsConnected()) {
        return true; // Уже подключено
    }

    std::cout << "[MakcuWrapper] Attempting to reconnect..." << std::endl;
    
    // Сначала полностью закрываем старое соединение
    Disconnect();
    
    // Пробуем заново инициализировать
    return Connect();
}

void MakcuWrapper::onButtonCallback(makcu::MouseButton button, bool pressed) {
    // Логирование ВСЕХ событий для отладки 2PC - даже в релизе
    std::cout << "[MakcuWrapper] CALLBACK: Button " << static_cast<int>(button) 
              << " " << (pressed ? "PRESSED" : "RELEASED") << std::endl;
    
    // Обновляем внутреннее состояние (как в референсе)
    switch (button) {
        case makcu::MouseButton::LEFT:
            // LMB = shooting
            std::cout << "[MakcuWrapper] Setting m_shooting_active = " << pressed << std::endl;
            m_shooting_active.store(pressed);
            break;
            
        case makcu::MouseButton::RIGHT:
            // RMB = zooming
            std::cout << "[MakcuWrapper] Setting m_zooming_active = " << pressed << std::endl;
            m_zooming_active.store(pressed);
            break;
            
        case makcu::MouseButton::MIDDLE:
            // MMB - not used for now
            std::cout << "[MakcuWrapper] MIDDLE button event (ignored)" << std::endl;
            break;
            
        case makcu::MouseButton::SIDE1:
            // Mouse4 (Side1)
            std::cout << "[MakcuWrapper] SIDE1 button event (ignored)" << std::endl;
            break;
            
        case makcu::MouseButton::SIDE2:
            // Mouse5 (Side2) = aiming - ЭТО ГЛАВНАЯ КНОПКА ПРИЦЕЛИВАНИЯ!
            std::cout << "[MakcuWrapper] Setting m_aiming_active = " << pressed << " (SIDE2)" << std::endl;
            m_aiming_active.store(pressed);
            break;
            
        default:
            std::cout << "[MakcuWrapper] Unknown button event: " << static_cast<int>(button) << std::endl;
            break;
    }
    
    // Финальный статус всех переменных
    std::cout << "[MakcuWrapper] State: aiming=" << m_aiming_active.load() 
              << " shooting=" << m_shooting_active.load() 
              << " zooming=" << m_zooming_active.load() << std::endl;
}

} // namespace pwnz_ai
