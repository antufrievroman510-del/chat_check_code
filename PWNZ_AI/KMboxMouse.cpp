#include "KMboxMouse.h"
#include "KMBoxNet.h"
#include <iostream>

namespace pwnz_ai {

KMboxMouse::KMboxMouse(const std::string& ip, int port)
    : m_ip(ip), m_port(port), m_initialized(false) {
    m_kmbox = std::make_unique<KMBoxNet>();
}

KMboxMouse::~KMboxMouse() {
    Shutdown();
}

bool KMboxMouse::Init(const std::string& /*port*/) {
    if (m_initialized) {
        return true;
    }

    if (!m_kmbox) {
        return false;
    }

    // Инициализация соединения с KMBox через прямой вызов метода подключения
    // Передаем актуальные IP и порт из настроек обертки
    bool success = m_kmbox->ConnectToDevice(m_ip, m_port);
    
    if (success) {
        m_initialized = true;
        std::cout << "[KMboxMouse] Connected to " << m_ip << ":" << m_port << std::endl;
    } else {
        std::cerr << "[KMboxMouse] Failed to connect to " << m_ip << ":" << m_port << std::endl;
    }

    return success;
}

void KMboxMouse::Move(int dx, int dy) {
    if (!m_initialized || !m_kmbox) {
        return;
    }

    // Отправка команды перемещения мыши в KMBox
    m_kmbox->Move(dx, dy);
}

void KMboxMouse::Click(MouseButton button) {
    if (!m_initialized || !m_kmbox) {
        return;
    }

    // Эмуляция нажатия кнопки через интерфейс KMBoxNet
    int btn = static_cast<int>(button) - 1; // Преобразование enum в int (0-based)
    m_kmbox->Click(btn);
}

void KMboxMouse::Press(MouseButton button) {
    if (!m_initialized || !m_kmbox) {
        return;
    }

    // Отправка команды нажатия кнопки (без отпускания)
    uint8_t btn = 0;
    switch (button) {
        case MouseButton::LEFT: btn = KMBoxButton::LEFT; break;
        case MouseButton::RIGHT: btn = KMBoxButton::RIGHT; break;
        case MouseButton::MIDDLE: btn = KMBoxButton::MIDDLE; break;
        default: btn = KMBoxButton::LEFT; break;
    }
    
    m_kmbox->MouseButton(btn, true);
    std::cout << "[KMboxMouse] Press button=" << static_cast<int>(button) << std::endl;
}

void KMboxMouse::Release(MouseButton button) {
    if (!m_initialized || !m_kmbox) {
        return;
    }

    // Отправка команды отпускания кнопки
    uint8_t btn = 0;
    switch (button) {
        case MouseButton::LEFT: btn = KMBoxButton::LEFT; break;
        case MouseButton::RIGHT: btn = KMBoxButton::RIGHT; break;
        case MouseButton::MIDDLE: btn = KMBoxButton::MIDDLE; break;
        default: btn = KMBoxButton::LEFT; break;
    }
    
    m_kmbox->MouseButton(btn, false);
    std::cout << "[KMboxMouse] Release button=" << static_cast<int>(button) << std::endl;
}

void KMboxMouse::Shutdown() {
    if (m_initialized && m_kmbox) {
        m_kmbox->Disconnect();
        m_initialized = false;
        std::cout << "[KMboxMouse] Disconnected." << std::endl;
    }
}

bool KMboxMouse::IsConnected() const {
    return m_initialized && m_kmbox && m_kmbox->IsConnected();
}

void KMboxMouse::SetConnectionInfo(const std::string& ip, int port) {
    // Если соединение активно, нужно переподключиться с новыми параметрами
    bool was_initialized = m_initialized;
    if (was_initialized) {
        Shutdown();
    }
    
    m_ip = ip;
    m_port = port;
    
    // Автоматически не подключаем здесь, это сделает вызывающий код через Init()
    if (was_initialized) {
        Init();
    }
}

} // namespace pwnz_ai
