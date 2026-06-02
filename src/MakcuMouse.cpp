#include "MakcuMouse.h"
#include <iostream>

namespace pwnz_ai {

    MakcuMouse::MakcuMouse(const std::string& com_port, int baud_rate)
        : m_com_port(com_port), m_baud_rate(baud_rate), m_initialized(false) {
    }

    MakcuMouse::~MakcuMouse() {
        if (m_initialized) {
            Shutdown();
        }
    }

    bool MakcuMouse::Init() {
        // Инициализация UART соединения с параметрами из конструктора
        std::cout << "[MakcuMouse] Initializing COM port: " << m_com_port 
                  << " at " << m_baud_rate << " baud..." << std::endl;
        
        if (m_com_port.empty()) {
            std::cerr << "[MakcuMouse] ERROR: Empty COM port name!" << std::endl;
            return false;
        }

        // Вызов реального метода подключения из MakcuUART
        std::cout << "[MakcuMouse] Calling MakcuUART::Connect..." << std::endl;
        if (!m_uart.Connect(m_com_port, m_baud_rate)) {
            std::cerr << "[MakcuMouse] Failed to connect to " << m_com_port 
                      << " (Error: " << GetLastError() << ")" << std::endl;
            return false;
        }

        std::cout << "[MakcuMouse] Successfully connected!" << std::endl;
        m_initialized = true;
        return true;
    }

    void MakcuMouse::Move(int dx, int dy) {
        if (!m_initialized) return;

        // Относительное перемещение мыши через MakcuUART
        m_uart.MoveMouse(dx, dy);
    }

    void MakcuMouse::Click(int button) {
        if (!m_initialized) {
            std::cerr << "[MakcuMouse] Click called but not initialized!" << std::endl;
            return;
        }

        // Отправка команды клика через UART
        // button: 0=ЛКМ, 1=ПКМ, 2=Колесо (нажатие), 3=Боковая 1, 4=Боковая 2
        std::cout << "[MakcuMouse] Click button=" << button << std::endl;
        m_uart.Click(button);
    }

    void MakcuMouse::Press(int button) {
        if (!m_initialized) {
            std::cerr << "[MakcuMouse] Press called but not initialized!" << std::endl;
            return;
        }

        // Отправка команды нажатия кнопки через UART (без отпускания)
        // button: 0=ЛКМ, 1=ПКМ, 2=Колесо, 3=Боковая 1, 4=Боковая 2
        std::cout << "[MakcuMouse] Press button=" << button << std::endl;
        m_uart.PressButton(button);
    }

    void MakcuMouse::Release(int button) {
        if (!m_initialized) {
            std::cerr << "[MakcuMouse] Release called but not initialized!" << std::endl;
            return;
        }

        // Отправка команды отпускания кнопки через UART
        // button: 0=ЛКМ, 1=ПКМ, 2=Колесо, 3=Боковая 1, 4=Боковая 2
        std::cout << "[MakcuMouse] Release button=" << button << std::endl;
        m_uart.ReleaseButton(button);
    }

    void MakcuMouse::Shutdown() {
        if (!m_initialized) return;

        // Закрытие COM-порта и очистка ресурсов
        m_uart.Disconnect();

        m_initialized = false;
        std::cout << "[MakcuMouse] Shutdown complete." << std::endl;
    }

    void MakcuMouse::SetPort(const std::string& port) {
        if (m_initialized) {
            Shutdown();
        }
        m_com_port = port;
    }

    void MakcuMouse::SetBaudRate(int rate) {
        if (m_initialized) {
            Shutdown();
        }
        m_baud_rate = rate;
    }

} // namespace pwnz_ai
