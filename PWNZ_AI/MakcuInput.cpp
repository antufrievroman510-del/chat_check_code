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

    // 5. Запуск UART потока для асинхронной отправки команд
    m_uartRunning = true;
    m_uartThread = std::thread(&MakcuInput::UartThreadFunc, this);
    std::cout << "[MakcuInput] UART thread started" << std::endl;

    return true;
}

void MakcuInput::Shutdown() {
    std::cout << "[MakcuInput] Shutdown called" << std::endl;

    // Останавливаем UART поток
    m_uartRunning = false;
    m_uartCV.notify_one();
    if (m_uartThread.joinable()) {
        m_uartThread.join();
    }
    std::cout << "[MakcuInput] UART thread stopped" << std::endl;

    if (m_device && m_device->isConnected()) {
        m_device->disconnect();
        std::cout << "[MakcuInput] Disconnected from device" << std::endl;
    }
    m_device.reset();
    std::cout << "[MakcuInput] Device object destroyed" << std::endl;
}

// === АСИНХРОННЫЕ МЕТОДЫ — ТОЛЬКО ДОБАВЛЕНИЕ В ОЧЕРЕДЬ ===

void MakcuInput::Move(int dx, int dy) {
    if (!m_uartRunning) return;
    
    UartCommand cmd;
    cmd.type = UartCommand::Type::Move;
    cmd.x = dx;
    cmd.y = dy;

    {
        std::lock_guard<std::mutex> lock(m_uartMutex);
        m_uartQueue.push(cmd);
    }
    m_uartCV.notify_one();
}

void MakcuInput::Click(MouseButton button) {
    if (!m_uartRunning) return;
    
    UartCommand cmd;
    cmd.type = UartCommand::Type::Click;
    cmd.button = buttonToInt(button);

    {
        std::lock_guard<std::mutex> lock(m_uartMutex);
        m_uartQueue.push(cmd);
    }
    m_uartCV.notify_one();
}

void MakcuInput::Press(MouseButton button) {
    if (!m_uartRunning) return;
    
    UartCommand cmd;
    cmd.type = UartCommand::Type::Press;
    cmd.button = buttonToInt(button);

    {
        std::lock_guard<std::mutex> lock(m_uartMutex);
        m_uartQueue.push(cmd);
    }
    m_uartCV.notify_one();
}

void MakcuInput::Release(MouseButton button) {
    if (!m_uartRunning) return;
    
    UartCommand cmd;
    cmd.type = UartCommand::Type::Release;
    cmd.button = buttonToInt(button);

    {
        std::lock_guard<std::mutex> lock(m_uartMutex);
        m_uartQueue.push(cmd);
    }
    m_uartCV.notify_one();
}

// === UART ПОТОК — РЕАЛЬНАЯ ОТПРАВКА НА УСТРОЙСТВО ===

void MakcuInput::UartThreadFunc() {
    std::cout << "[MakcuInput-UART] Thread started" << std::endl;

    while (m_uartRunning) {
        UartCommand cmd;
        
        // Ждём команду или сигнал остановки
        {
            std::unique_lock<std::mutex> lock(m_uartMutex);
            m_uartCV.wait(lock, [this] { 
                return !m_uartRunning || !m_uartQueue.empty(); 
            });
            
            if (!m_uartRunning && m_uartQueue.empty()) break;
            if (m_uartQueue.empty()) continue;
            
            cmd = m_uartQueue.front();
            m_uartQueue.pop();
        }

        // Выполняем команду (без мьютекса — не блокируем основной поток!)
        if (m_device && m_device->isConnected()) {
            switch (cmd.type) {
                case UartCommand::Type::Move:
                    m_device->mouseMove(cmd.x, cmd.y);
                    break;
                case UartCommand::Type::Press:
                    m_device->mouseDown(static_cast<makcu::MouseButton>(cmd.button));
                    break;
                case UartCommand::Type::Release:
                    m_device->mouseUp(static_cast<makcu::MouseButton>(cmd.button));
                    break;
                case UartCommand::Type::Click:
                    m_device->mouseDown(static_cast<makcu::MouseButton>(cmd.button));
                    m_device->mouseUp(static_cast<makcu::MouseButton>(cmd.button));
                    break;
            }
        }

        // Небольшая пауза чтобы не забивать UART
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    std::cout << "[MakcuInput-UART] Thread exiting" << std::endl;
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

} // namespace pwnz_ai
