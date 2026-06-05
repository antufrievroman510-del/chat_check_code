#include "MakcuInput.h"
#include <iostream>
#include <algorithm>

// Глобальные переменные определены в main.cpp (namespace pwnz_ai)
// Здесь мы только используем их через extern объявления из MakcuInput.h

namespace pwnz_ai {

MakcuInput::MakcuInput() {
    std::cout << "[MakcuInput] Constructor called" << std::endl;
}

MakcuInput::~MakcuInput() {
    std::cout << "[MakcuInput] Destructor called, shutting down..." << std::endl;
    Shutdown();
}

bool MakcuInput::Init(const std::string& port) {
    std::cout << "[MakcuInput] Init started with port: " << port << std::endl;

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

    std::cout << "[MakcuInput] Successfully connected to Makcu on " << port << std::endl;
    return true;
}

void MakcuInput::Shutdown() {
    std::cout << "[MakcuInput] Shutdown called" << std::endl;
    if (m_device && m_device->isConnected()) {
        m_device->disconnect();
        std::cout << "[MakcuInput] Disconnected from device" << std::endl;
    }
    m_device.reset();
    std::cout << "[MakcuInput] Device object destroyed" << std::endl;
}

void MakcuInput::Move(int dx, int dy) {
    if (!m_device || !m_device->isConnected()) {
        std::cerr << "[MakcuInput] Cannot move: not connected" << std::endl;
        return;
    }
    
    // Отправляем движение через makcu
    m_device->mouseMove(dx, dy);
}

int MakcuInput::buttonToInt(MouseButton button) const {
    switch (button) {
        case MouseButton::LEFT:   return 0;
        case MouseButton::RIGHT:  return 1;
        case MouseButton::MIDDLE: return 2;
        case MouseButton::SIDE1:  return 3;
        case MouseButton::SIDE2:  return 4;
        default:                  return -1;
    }
}

void MakcuInput::Click(MouseButton button) {
    makcu::MouseButton btn = static_cast<makcu::MouseButton>(button);
    
    if (!m_device || !m_device->isConnected()) {
        std::cerr << "[MakcuInput] Cannot click: not connected" << std::endl;
        return;
    }
    
    // Эмулируем клик как нажатие + отпускание
    m_device->mouseDown(btn);
    m_device->mouseUp(btn);
}

void MakcuInput::Press(MouseButton button) {
    makcu::MouseButton btn = static_cast<makcu::MouseButton>(button);
    
    if (!m_device || !m_device->isConnected()) {
        std::cerr << "[MakcuInput] Cannot press: not connected" << std::endl;
        return;
    }
    
    m_device->mouseDown(btn);
}

void MakcuInput::Release(MouseButton button) {
    makcu::MouseButton btn = static_cast<makcu::MouseButton>(button);
    
    if (!m_device || !m_device->isConnected()) {
        std::cerr << "[MakcuInput] Cannot release: not connected" << std::endl;
        return;
    }
    
    m_device->mouseUp(btn);
}

bool MakcuInput::IsButtonPressed(MouseButton button) const {
    switch (button) {
        case MouseButton::LEFT:   return m_btnLmb.load();
        case MouseButton::RIGHT:  return m_btnRmb.load();
        case MouseButton::MIDDLE: return m_btnMmb.load();
        case MouseButton::SIDE1:  return m_btnSide1.load();
        case MouseButton::SIDE2:  return m_btnSide2.load();
        default:                  return false;
    }
}

bool MakcuInput::IsConnected() const {
    return m_device && m_device->isConnected();
}

void MakcuInput::onMouseButton(makcu::MouseButton button, bool pressed) {
    // Обновляем атомарные флаги состояния кнопок (локальные и глобальные)
    switch (button) {
        case makcu::MouseButton::LEFT:
            m_btnLmb.store(pressed);
            pwnz_ai::shooting.store(pressed);  // LMB = стрельба
            std::cout << "[MakcuInput] LMB " << (pressed ? "PRESSED" : "RELEASED") << std::endl;
            break;
        case makcu::MouseButton::RIGHT:
            m_btnRmb.store(pressed);
            pwnz_ai::zooming.store(pressed);   // RMB = зум/прицеливание
            pwnz_ai::aiming.store(pressed);    // RMB = прицеливание
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
            pwnz_ai::aiming.store(pressed);    // SIDE2 = прицеливание
            std::cout << "[MakcuInput] SIDE2 " << (pressed ? "PRESSED" : "RELEASED") << std::endl;
            break;
        default:
            std::cout << "[MakcuInput] Unknown button event: " << static_cast<int>(button)
                      << " pressed=" << pressed << std::endl;
            break;
    }
}

} // namespace pwnz_ai
