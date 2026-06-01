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
            return false;
        }

        // Вызов реального метода подключения из MakcuUART
        if (!m_uart.Connect(m_com_port, m_baud_rate)) {
            std::cerr << "[MakcuMouse] Failed to connect to " << m_com_port << std::endl;
            return false;
        }

        m_initialized = true;
        return true;
    }

    void MakcuMouse::Move(int dx, int dy) {
        if (!m_initialized) return;

        // Относительное перемещение мыши через MakcuUART
        m_uart.MoveMouse(dx, dy);
    }

    void MakcuMouse::Click(int button) {
        if (!m_initialized) return;

        // Эмуляция нажатия кнопки
        // Примечание: MakcuUART может не поддерживать клики напрямую.
        // Если поддержка нужна, потребуется доработка MakcuUART или эмуляция через SendInput.
        // В данной реализации считаем, что клик обрабатывается драйвером устройства или игнорируется.
        // Для полной поддержки можно добавить метод в MakcuUART.
        
        // Заглушка: пока просто логируем (или можно вызвать MoveMouse(0,0) как heart-beat)
        // std::cout << "[MakcuMouse] Click: button=" << button << " (Not fully supported by UART yet)" << std::endl;
        
        // Если в будущем добавим поддержку в MakcuUART:
        // m_uart.Click(button); 
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
