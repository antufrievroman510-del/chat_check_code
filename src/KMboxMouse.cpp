#include "KMboxMouse.h"
#include <iostream>

namespace pwnz_ai {

KMboxMouse::KMboxMouse(const std::string& ip, int port)
    : m_ip(ip), m_port(port), m_initialized(false) {
    m_kmbox = std::make_unique<KMBoxNet>();
}

KMboxMouse::~KMboxMouse() {
    Shutdown();
}

bool KMboxMouse::Init() {
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

void KMboxMouse::Click(int button) {
    if (!m_initialized || !m_kmbox) {
        return;
    }

    // Эмуляция нажатия кнопки через интерфейс KMBoxNet
    m_kmbox->Click(button);
}

void KMboxMouse::Shutdown() {
    if (m_initialized && m_kmbox) {
        m_kmbox->Disconnect();
        m_initialized = false;
        std::cout << "[KMboxMouse] Disconnected." << std::endl;
    }
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
