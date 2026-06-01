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

    // Инициализация соединения с KMBox через метод интерфейса
    bool success = m_kmbox->Init();
    
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
    m_ip = ip;
    m_port = port;
    // Если устройство уже инициализировано, может потребоваться переподключение
    // Это можно реализовать вызвав Shutdown() и затем Init(), если нужно применить настройки на лету
}

} // namespace pwnz_ai
