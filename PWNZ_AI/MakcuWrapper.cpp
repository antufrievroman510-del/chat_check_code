#include "MakcuWrapper.h"

namespace pwnz_ai {

// ============================================================
// Глобальные переменные состояния кнопок
// ============================================================
std::atomic<bool> g_makcu_aiming{false};    // SIDE2 (Mouse5)
std::atomic<bool> g_makcu_zooming{false};   // RMB
std::atomic<bool> g_makcu_shooting{false};  // LMB

// ============================================================
// Конструктор / Деструктор
// ============================================================
MakcuWrapper::MakcuWrapper(const MakcuConfig& config)
    : m_config(config) {
    std::cout << "[MakcuWrapper] Constructor called with VID:PID " 
              << std::hex << config.vid << ":" << config.pid << std::dec << std::endl;
}

MakcuWrapper::~MakcuWrapper() {
    Shutdown();
}

// ============================================================
// Инициализация - КРИТИЧНЫЙ ПОРЯДОК как в референсе
// ============================================================
bool MakcuWrapper::Initialize() {
    std::cout << "[MakcuWrapper] Starting initialization..." << std::endl;

    try {
        // ШАГ 1: Создание объекта устройства
        std::cout << "[MakcuWrapper] Step 1: Creating makcu::Device object..." << std::endl;
        m_device = std::make_unique<makcu::Device>();
        if (!m_device) {
            std::cerr << "[MakcuWrapper] Failed to create Device object!" << std::endl;
            return false;
        }
        std::cout << "[MakcuWrapper] Device object created successfully." << std::endl;

        // ШАГ 2: Установка коллбэка ДО подключения
        std::cout << "[MakcuWrapper] Step 2: Setting mouse button callback..." << std::endl;
        m_device->setMouseButtonCallback([this](makcu::MouseButton button, bool isPressed) {
            OnButtonEvent(button, isPressed);
        });
        std::cout << "[MakcuWrapper] Callback set successfully." << std::endl;

        // ШАГ 3: Включение мониторинга кнопок ДО подключения
        std::cout << "[MakcuWrapper] Step 3: Enabling button monitoring..." << std::endl;
        if (!m_device->enableButtonMonitoring(true)) {
            std::cerr << "[MakcuWrapper] Failed to enable button monitoring!" << std::endl;
            return false;
        }
        std::cout << "[MakcuWrapper] Button monitoring enabled." << std::endl;

        // ШАГ 4: Поиск устройства и подключение
        std::cout << "[MakcuWrapper] Step 4: Finding and connecting to device..." << std::endl;
        auto devices = makcu::Device::findDevices();
        if (devices.empty()) {
            std::cerr << "[MakcuWrapper] No Makcu devices found!" << std::endl;
            return false;
        }

        std::cout << "[MakcuWrapper] Found " << devices.size() << " device(s):" << std::endl;
        for (const auto& dev : devices) {
            std::cout << "  - Port: " << dev.port 
                      << ", VID:PID=" << std::hex << dev.vid << ":" << dev.pid << std::dec
                      << ", Desc: " << dev.description << std::endl;
        }

        // Пытаемся подключиться к первому найденному устройству
        std::string port = devices[0].port;
        std::cout << "[MakcuWrapper] Attempting to connect to port: " << port << std::endl;
        
        bool connected = m_device->connect(port, m_config.high_speed_mode);
        if (!connected) {
            std::cerr << "[MakcuWrapper] Connection failed!" << std::endl;
            return false;
        }
        std::cout << "[MakcuWrapper] Device connected successfully!" << std::endl;

        // Проверка статуса подключения
        if (!m_device->isConnected()) {
            std::cerr << "[MakcuWrapper] Device reports not connected after connect()!" << std::endl;
            return false;
        }
        std::cout << "[MakcuWrapper] Connection verified." << std::endl;

        // Получение версии устройства для лога
        try {
            std::string version = m_device->getVersion();
            std::cout << "[MakcuWrapper] Device version: " << version << std::endl;
        } catch (...) {
            std::cout << "[MakcuWrapper] Could not get device version." << std::endl;
        }

        // ШАГ 5: Запуск потока обработки событий
        std::cout << "[MakcuWrapper] Step 5: Starting monitor thread..." << std::endl;
        m_running.store(true);
        m_monitorThread = std::thread(&MakcuWrapper::MonitorThreadFunc, this);
        
        m_initialized.store(true);
        std::cout << "[MakcuWrapper] === INITIALIZATION COMPLETE ===" << std::endl;
        return true;

    } catch (const std::exception& e) {
        std::cerr << "[MakcuWrapper] Exception during initialization: " << e.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "[MakcuWrapper] Unknown exception during initialization!" << std::endl;
        return false;
    }
}

// ============================================================
// Завершение работы
// ============================================================
void MakcuWrapper::Shutdown() {
    std::cout << "[MakcuWrapper] Shutting down..." << std::endl;

    // Останавливаем поток
    m_running.store(false);
    if (m_monitorThread.joinable()) {
        m_monitorThread.join();
    }

    // Отключаем устройство
    if (m_device) {
        try {
            m_device->disconnect();
            std::cout << "[MakcuWrapper] Device disconnected." << std::endl;
        } catch (...) {
            std::cerr << "[MakcuWrapper] Error disconnecting device." << std::endl;
        }
        m_device.reset();
    }

    // Сбрасываем глобальные флаги
    g_makcu_aiming.store(false);
    g_makcu_zooming.store(false);
    g_makcu_shooting.store(false);

    m_initialized.store(false);
    std::cout << "[MakcuWrapper] Shutdown complete." << std::endl;
}

// ============================================================
// Методы управления мышью
// ============================================================
void MakcuWrapper::Move(int x, int y) {
    if (!m_initialized.load() || !m_device) {
        return;
    }
    m_device->mouseMove(x, y);
}

void MakcuWrapper::Click(int button) {
    if (!m_initialized.load() || !m_device) {
        return;
    }
    
    makcu::MouseButton mb = makcu::MouseButton::LEFT;
    switch (button) {
        case 0: mb = makcu::MouseButton::LEFT; break;
        case 1: mb = makcu::MouseButton::RIGHT; break;
        case 2: mb = makcu::MouseButton::MIDDLE; break;
        case 3: mb = makcu::MouseButton::SIDE1; break;
        case 4: mb = makcu::MouseButton::SIDE2; break;
        default: mb = makcu::MouseButton::LEFT; break;
    }
    
    m_device->click(mb);
}

void MakcuWrapper::Press(int button) {
    if (!m_initialized.load() || !m_device) {
        return;
    }
    
    makcu::MouseButton mb = makcu::MouseButton::LEFT;
    switch (button) {
        case 0: mb = makcu::MouseButton::LEFT; break;
        case 1: mb = makcu::MouseButton::RIGHT; break;
        case 2: mb = makcu::MouseButton::MIDDLE; break;
        case 3: mb = makcu::MouseButton::SIDE1; break;
        case 4: mb = makcu::MouseButton::SIDE2; break;
        default: mb = makcu::MouseButton::LEFT; break;
    }
    
    m_device->mouseDown(mb);
}

void MakcuWrapper::Release(int button) {
    if (!m_initialized.load() || !m_device) {
        return;
    }
    
    makcu::MouseButton mb = makcu::MouseButton::LEFT;
    switch (button) {
        case 0: mb = makcu::MouseButton::LEFT; break;
        case 1: mb = makcu::MouseButton::RIGHT; break;
        case 2: mb = makcu::MouseButton::MIDDLE; break;
        case 3: mb = makcu::MouseButton::SIDE1; break;
        case 4: mb = makcu::MouseButton::SIDE2; break;
        default: mb = makcu::MouseButton::LEFT; break;
    }
    
    m_device->mouseUp(mb);
}

bool MakcuWrapper::IsConnected() const {
    if (!m_device) return false;
    return m_device->isConnected();
}

// ============================================================
// Обработка событий кнопок
// ============================================================
void MakcuWrapper::OnButtonEvent(makcu::MouseButton button, bool isPressed) {
    // Логирование события
    std::string buttonName;
    switch (button) {
        case makcu::MouseButton::LEFT: buttonName = "LMB"; break;
        case makcu::MouseButton::RIGHT: buttonName = "RMB"; break;
        case makcu::MouseButton::MIDDLE: buttonName = "MMB"; break;
        case makcu::MouseButton::SIDE1: buttonName = "SIDE1"; break;
        case makcu::MouseButton::SIDE2: buttonName = "SIDE2"; break;
        default: buttonName = "UNKNOWN"; break;
    }
    
    std::cout << "[MakcuWrapper] Button Event: " << buttonName 
              << " -> " << (isPressed ? "PRESSED" : "RELEASED") << std::endl;

    // Обновляем глобальные атомарные флаги
    switch (button) {
        case makcu::MouseButton::LEFT:
            g_makcu_shooting.store(isPressed);
            std::cout << "[MakcuWrapper] g_makcu_shooting = " << (isPressed ? "true" : "false") << std::endl;
            break;
            
        case makcu::MouseButton::RIGHT:
            g_makcu_zooming.store(isPressed);
            std::cout << "[MakcuWrapper] g_makcu_zooming = " << (isPressed ? "true" : "false") << std::endl;
            break;
            
        case makcu::MouseButton::SIDE2:
            g_makcu_aiming.store(isPressed);
            std::cout << "[MakcuWrapper] g_makcu_aiming = " << (isPressed ? "true" : "false") << std::endl;
            break;
            
        default:
            break;
    }
}

// ============================================================
// Поток обработки событий (опционально для доп. логики)
// ============================================================
void MakcuWrapper::MonitorThreadFunc() {
    std::cout << "[MakcuWrapper] Monitor thread started." << std::endl;
    
    while (m_running.load()) {
        // В данной реализации все события обрабатываются через коллбэк
        // Этот поток может использоваться для дополнительной логики
        // Например, периодической проверки состояния подключения
        
        std::this_thread::sleep_for(std::chrono::milliseconds(m_config.polling_interval_ms * 10));
        
        // Проверка подключения
        if (m_device && !m_device->isConnected()) {
            std::cerr << "[MakcuWrapper] WARNING: Device disconnected during operation!" << std::endl;
            // Можно добавить логику переподключения
        }
    }
    
    std::cout << "[MakcuWrapper] Monitor thread stopped." << std::endl;
}

} // namespace pwnz_ai
