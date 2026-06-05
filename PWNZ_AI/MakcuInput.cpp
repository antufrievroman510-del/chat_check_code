#include "MakcuInput.h"
#include <iostream>
#include <algorithm>

namespace pwnz_ai {

MakcuInput::MakcuInput() {
    std::cout << "[MakcuInput] Constructor called" << std::endl;
}

MakcuInput::~MakcuInput() {
    std::cout << "[MakcuInput] Destructor called, shutting down..." << std::endl;
    Shutdown();
}

bool MakcuInput::Initialize(const std::string& port) {
    std::cout << "[MakcuInput] Initialize started with port: " << port << std::endl;

    // 1. Создание объекта устройства
    m_device = std::make_unique<makcu::Device>();
    if (!m_device) {
        std::cerr << "[MakcuInput] Failed to create Device object!" << std::endl;
        return false;
    }
    std::cout << "[MakcuInput] Device object created" << std::endl;

    // 2. Установка коллбэка ДО подключения (СТРОГО! как в source_logic/source_logic/mouse/Makcu.cpp)
    m_device->setMouseButtonCallback([this](makcu::MouseButton button, bool pressed) {
        this->onMouseButton(button, pressed);
    });
    std::cout << "[MakcuInput] Mouse button callback set" << std::endl;

    // 3. Включение мониторинга кнопок ПОСЛЕ установки коллбэка но ДО подключения
    bool monitoringEnabled = m_device->enableButtonMonitoring(true);
    if (!monitoringEnabled) {
        std::cerr << "[MakcuInput] Warning: Failed to enable button monitoring before connect!" << std::endl;
        // Продолжаем - мониторинг может включиться после подключения
    } else {
        std::cout << "[MakcuInput] Button monitoring enabled before connect" << std::endl;
    }

    // 4. Подключение к устройству
    std::cout << "[MakcuInput] Connecting to port: " << port << std::endl;
    bool connected = m_device->connect(port);  // highSpeed по умолчанию true
    
    if (!connected) {
        std::cerr << "[MakcuInput] Connection failed!" << std::endl;
        std::cerr << "[MakcuInput] Status: " << static_cast<int>(m_device->getStatus()) << std::endl;
        return false;
    }

    std::cout << "[MakcuInput] SUCCESS: Connected to Makcu on " << port << std::endl;
    std::cout << "[MakcuInput] Device version: " << m_device->getVersion() << std::endl;

    return true;
}

void MakcuInput::Shutdown() {
    std::cout << "[MakcuInput] Shutdown called" << std::endl;
    
    if (m_device) {
        // Отключаем мониторинг
        (void)m_device->enableButtonMonitoring(false);
        std::cout << "[MakcuInput] Button monitoring disabled" << std::endl;
        
        // Отключаемся от устройства
        m_device->disconnect();
        std::cout << "[MakcuInput] Device disconnected" << std::endl;
        
        m_device.reset();
        std::cout << "[MakcuInput] Device object destroyed" << std::endl;
    }
}

bool MakcuInput::Move(int32_t x, int32_t y) {
    if (!m_device || !m_device->isConnected()) {
        return false;
    }
    
    bool result = m_device->mouseMove(x, y);
    if (!result) {
        std::cerr << "[MakcuInput] Move failed: x=" << x << " y=" << y << std::endl;
    }
    return result;
}

bool MakcuInput::Click(int button) {
    if (!m_device || !m_device->isConnected()) {
        return false;
    }

    makcu::MouseButton mb = makcu::MouseButton::LEFT;
    switch (button) {
        case 0: mb = makcu::MouseButton::LEFT; break;
        case 1: mb = makcu::MouseButton::RIGHT; break;
        case 2: mb = makcu::MouseButton::MIDDLE; break;
        case 3: mb = makcu::MouseButton::SIDE1; break;
        case 4: mb = makcu::MouseButton::SIDE2; break;
        default: 
            std::cerr << "[MakcuInput] Invalid button: " << button << std::endl;
            return false;
    }

    bool result = m_device->click(mb);
    if (!result) {
        std::cerr << "[MakcuInput] Click failed: button=" << button << std::endl;
    }
    return result;
}

bool MakcuInput::Press(int button) {
    if (!m_device || !m_device->isConnected()) {
        return false;
    }

    makcu::MouseButton mb = makcu::MouseButton::LEFT;
    switch (button) {
        case 0: mb = makcu::MouseButton::LEFT; break;
        case 1: mb = makcu::MouseButton::RIGHT; break;
        case 2: mb = makcu::MouseButton::MIDDLE; break;
        case 3: mb = makcu::MouseButton::SIDE1; break;
        case 4: mb = makcu::MouseButton::SIDE2; break;
        default: 
            std::cerr << "[MakcuInput] Invalid button: " << button << std::endl;
            return false;
    }

    bool result = m_device->mouseDown(mb);
    if (!result) {
        std::cerr << "[MakcuInput] Press failed: button=" << button << std::endl;
    }
    return result;
}

bool MakcuInput::Release(int button) {
    if (!m_device || !m_device->isConnected()) {
        return false;
    }

    makcu::MouseButton mb = makcu::MouseButton::LEFT;
    switch (button) {
        case 0: mb = makcu::MouseButton::LEFT; break;
        case 1: mb = makcu::MouseButton::RIGHT; break;
        case 2: mb = makcu::MouseButton::MIDDLE; break;
        case 3: mb = makcu::MouseButton::SIDE1; break;
        case 4: mb = makcu::MouseButton::SIDE2; break;
        default: 
            std::cerr << "[MakcuInput] Invalid button: " << button << std::endl;
            return false;
    }

    bool result = m_device->mouseUp(mb);
    if (!result) {
        std::cerr << "[MakcuInput] Release failed: button=" << button << std::endl;
    }
    return result;
}

bool MakcuInput::IsButtonPressed(int button) const {
    switch (button) {
        case 0: return m_btnLmb.load();
        case 1: return m_btnRmb.load();
        case 2: return m_btnMmb.load();
        case 3: return m_btnSide1.load();
        case 4: return m_btnSide2.load();
        default: return false;
    }
}

bool MakcuInput::IsConnected() const {
    return m_device && m_device->isConnected();
}

void MakcuInput::onMouseButton(makcu::MouseButton button, bool pressed) {
    // Обновляем атомарные флаги состояния кнопок (локальные)
    switch (button) {
        case makcu::MouseButton::LEFT:
            m_btnLmb.store(pressed);
            shooting.store(pressed);  // LMB = стрельба (как в source_logic/source_logic/mouse/Makcu.cpp)
            std::cout << "[MakcuInput] LMB " << (pressed ? "PRESSED" : "RELEASED") << std::endl;
            break;
        case makcu::MouseButton::RIGHT:
            m_btnRmb.store(pressed);
            zooming.store(pressed);   // RMB = зум/прицеливание (как в source_logic)
            aiming.store(pressed);    // RMB = прицеливание (дублируем для совместимости)
            std::cout << "[MakcuInput] RMB " << (pressed ? "PRESSED" : "RELEASED") << std::endl;
            break;
        case makcu::MouseButton::MIDDLE:
            m_btnMmb.store(pressed);
            std::cout << "[MakcuInput] MMB " << (pressed ? "PRESSED" : "RELEASED") << std::endl;
            break;
        case makcu::MouseButton::SIDE1:
            m_btnSide1.store(pressed);
            std::cout << "[MakcuInput] SIDE1 " << (pressed ? "PRESSED" : "RELEASED") << std::endl;
            break;
        case makcu::MouseButton::SIDE2:
            m_btnSide2.store(pressed);
            aiming.store(pressed);    // SIDE2 = прицеливание (как в source_logic/source_logic/mouse/Makcu.cpp)
            std::cout << "[MakcuInput] SIDE2 " << (pressed ? "PRESSED" : "RELEASED") << std::endl;
            break;
        default:
            std::cout << "[MakcuInput] Unknown button event: " << static_cast<int>(button) 
                      << " pressed=" << pressed << std::endl;
            break;
    }
}

} // namespace pwnz_ai
