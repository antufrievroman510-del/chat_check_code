#include "MakcuUART.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <sstream>

// Определение глобальных переменных состояния кнопок
namespace pwnz_ai {
    std::atomic<bool> g_makcu_aiming{false};    // Состояние ПКМ (прицеливание) - для 2PC
    std::atomic<bool> g_makcu_shooting{false};  // Состояние ЛКМ (стрельба) - для 2PC
    std::atomic<bool> g_makcu_zooming{false};   // Состояние СКМ (зум) - для 2PC
}

// Глобальная переменная для 2PC-связки (объявлена в main.cpp)
extern std::atomic<bool> g_remote_aim_key;

// Протокол Makcu Text Protocol согласно https://github.com/K4HVH/makcu и https://www.makcu.com/en/api
// Команды:
//   km.move(dx,dy)\r\n      - движение мыши
//   km.left(1)\r\n           - нажать ЛКМ
//   km.left(0)\r\n           - отпустить ЛКМ
//   km.right(1)\r\n          - нажать ПКМ
//   km.right(0)\r\n          - отпустить ПКМ
//   km.middle(1)\r\n         - нажать СКМ
//   km.middle(0)\r\n         - отпустить СКМ
//   km.buttons(1)\r\n        - включить стрим состояния кнопок
// Ответы при включенном стриме кнопок:
//   btn:1\r\n  - ЛКМ нажата
//   btn:2\r\n  - ПКМ нажата
//   btn:4\r\n  - СКМ нажата
//   btn:0\r\n  - все кнопки отпущены

MakcuUART::MakcuUART() 
    : hComPort(nullptr), isConnected(false), packetDelayMs(1), m_portName("COM3"), m_baudRate(115200) {}

MakcuUART::~MakcuUART() {
    Shutdown();
}

bool MakcuUART::Init() {
    return Connect(m_portName, m_baudRate);
}

void MakcuUART::Move(int dx, int dy) {
    MoveMouse(dx, dy);
}

void MakcuUART::Click(int button) {
    // Для текстового протокола клик = press + release с небольшой задержкой
    PressButton(button);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ReleaseButton(button);
}

void MakcuUART::Shutdown() {
    Disconnect();
}

bool MakcuUART::Connect(const std::string& portName, int baudRate) {
    std::lock_guard<std::mutex> lock(mtx);
    
    if (isConnected) {
        Disconnect();
    }

    // Формируем имя порта для Windows
    std::string fullPortName = "\\\\.\\" + portName;
    
    std::cout << "[MakcuUART] Connecting to: " << fullPortName 
              << " at " << baudRate << " baud..." << std::endl;
    
    hComPort = CreateFileA(
        fullPortName.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,          // No sharing
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr
    );

    if (hComPort == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        hComPort = nullptr;
        std::cerr << "[MakcuUART] Failed to open COM port: " << portName 
                  << " (Error: " << err << ")" << std::endl;
        if (err == ERROR_ACCESS_DENIED) {
            std::cerr << "[MakcuUART] Port is busy or access denied. Run as Administrator." << std::endl;
        } else if (err == ERROR_FILE_NOT_FOUND) {
            std::cerr << "[MakcuUART] Port does not exist. Check Device Manager." << std::endl;
        }
        return false;
    }

    // Настройка параметров COM-порта
    DCB dcbSerialParams = { 0 };
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

    if (!GetCommState(hComPort, &dcbSerialParams)) {
        Disconnect();
        return false;
    }

    dcbSerialParams.BaudRate = baudRate;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;

    if (!SetCommState(hComPort, &dcbSerialParams)) {
        Disconnect();
        return false;
    }

    // Таймауты для записи
    COMMTIMEOUTS timeouts = { 0 };
    timeouts.WriteTotalTimeoutConstant = 500;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    SetCommTimeouts(hComPort, &timeouts);

    // Очистка буферов
    PurgeComm(hComPort, PURGE_TXCLEAR | PURGE_RXCLEAR);
    
    // Включаем DTR и RTS
    dcbSerialParams.fDtrControl = DTR_CONTROL_ENABLE;
    dcbSerialParams.fRtsControl = RTS_CONTROL_ENABLE;
    SetCommState(hComPort, &dcbSerialParams);
    
    Sleep(100); // Задержка для стабилизации

    isConnected = true;
    std::cout << "[MakcuUART] Connected successfully!" << std::endl;
    
    // === КРИТИЧНО: Сначала очищаем буфер от старых данных перед отправкой команд ===
    PurgeComm(hComPort, PURGE_RXCLEAR);
    Sleep(50);
    
    // Отправляем команду включения стрима кнопок
    // Устройство начнёт отправлять btn:X\r\n при изменении состояния кнопок
    WriteCommand("km.buttons(1)\r\n");
    std::cout << "[MakcuUART] Sent button stream enable command (km.buttons(1))" << std::endl;
    
    // Даём устройству время на ответ
    Sleep(100);
    
    // Очищаем буфер ещё раз после команды
    PurgeComm(hComPort, PURGE_RXCLEAR);
    
    // Запускаем поток мониторинга для чтения состояния кнопок
    StartMonitoring();
    
    return true;
}

void MakcuUART::Disconnect() {
    std::lock_guard<std::mutex> lock(mtx);
    
    StopMonitoring();
    
    if (hComPort != nullptr) {
        // Отключаем стрим кнопок перед закрытием
        WriteCommand("km.buttons(0)\r\n");
        Sleep(50);  // Даём время на отправку команды
        
        CloseHandle(hComPort);
        hComPort = nullptr;
    }
    isConnected = false;
    std::cout << "[MakcuUART] Disconnected" << std::endl;
}

bool MakcuUART::IsConnected() const {
    return isConnected;
}

bool MakcuUART::MoveMouse(int dx, int dy) {
    if (!isConnected || hComPort == nullptr) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mtx);

    // Формируем текстовую команду движения
    std::ostringstream cmd;
    cmd << "km.move(" << dx << "," << dy << ")\r\n";
    
    bool result = WriteCommand(cmd.str().c_str());
    
    if (result && packetDelayMs > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(packetDelayMs));
    }

    return result;
}

bool MakcuUART::PressButton(int button) {
    if (!isConnected || hComPort == nullptr) {
        std::cerr << "[MakcuUART] PressButton: Not connected!" << std::endl;
        return false;
    }

    std::lock_guard<std::mutex> lock(mtx);

    // Определяем команду кнопки
    const char* cmd = nullptr;
    switch (button) {
        case 0: cmd = "km.left(1)\r\n"; break;   // ЛКМ
        case 1: cmd = "km.right(1)\r\n"; break;  // ПКМ
        case 2: cmd = "km.middle(1)\r\n"; break; // СКМ
        default: cmd = "km.left(1)\r\n"; break;
    }
    
    bool result = WriteCommand(cmd);
    
    if (result) {
        // Обновляем локальное состояние
        switch (button) {
            case 0: m_lmb_pressed.store(true); break;
            case 1: m_rmb_pressed.store(true); break;
            case 2: m_mmb_pressed.store(true); break;
        }
        std::cout << "[MakcuUART] Press button " << button << std::endl;
    }
    
    return result;
}

bool MakcuUART::ReleaseButton(int button) {
    if (!isConnected || hComPort == nullptr) {
        std::cerr << "[MakcuUART] ReleaseButton: Not connected!" << std::endl;
        return false;
    }

    std::lock_guard<std::mutex> lock(mtx);

    // Определяем команду отпускания кнопки
    const char* cmd = nullptr;
    switch (button) {
        case 0: cmd = "km.left(0)\r\n"; break;   // ЛКМ
        case 1: cmd = "km.right(0)\r\n"; break;  // ПКМ
        case 2: cmd = "km.middle(0)\r\n"; break; // СКМ
        default: cmd = "km.left(0)\r\n"; break;
    }
    
    bool result = WriteCommand(cmd);
    
    if (result) {
        // Обновляем локальное состояние
        switch (button) {
            case 0: m_lmb_pressed.store(false); break;
            case 1: m_rmb_pressed.store(false); break;
            case 2: m_mmb_pressed.store(false); break;
        }
        std::cout << "[MakcuUART] Release button " << button << std::endl;
    }
    
    return result;
}

void MakcuUART::SetPacketDelayMs(int ms) {
    packetDelayMs = (ms < 0) ? 0 : ms;
}

bool MakcuUART::WriteCommand(const char* command) {
    size_t len = strlen(command);
    DWORD bytesWritten;
    
    // Записываем команду в COM-порт
    BOOL result = WriteFile(hComPort, command, static_cast<DWORD>(len), &bytesWritten, nullptr);
    
    if (!result || bytesWritten != len) {
        DWORD err = GetLastError();
        std::cerr << "[MakcuUART] Write failed: " << err << " (written=" << bytesWritten 
                  << ", expected=" << len << ")" << std::endl;
        Disconnect();
        return false;
    }
    
    // ВАЖНО: НЕ используем FlushFileBuffers здесь!
    // Это блокирует асинхронную отправку и нарушает синхронизацию с устройством.
    // Устройство должно само обработать команду и отправить ответ >>>
    
    return true;
}

void MakcuUART::StartMonitoring() {
    if (m_monitoring.load()) {
        return;
    }
    
    m_stopMonitoring.store(false);
    m_monitorThread = std::thread(&MakcuUART::monitoringLoop, this);
    m_monitoring.store(true);
    
    std::cout << "[MakcuUART] Button monitoring started" << std::endl;
}

void MakcuUART::StopMonitoring() {
    if (!m_monitoring.load()) {
        return;
    }
    
    m_stopMonitoring.store(true);
    
    if (m_monitorThread.joinable()) {
        m_monitorThread.join();
    }
    
    m_monitoring.store(false);
    
    std::cout << "[MakcuUART] Button monitoring stopped" << std::endl;
}

void MakcuUART::monitoringLoop() {
    std::cout << "[MakcuUART] Monitoring loop started" << std::endl;
    
    // Настраиваем таймауты для неблокирующего чтения
    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    SetCommTimeouts(hComPort, &timeouts);
    
    char buffer[256];
    std::string leftover;  // Буфер для неполных пакетов
    
    while (!m_stopMonitoring.load() && isConnected && hComPort != nullptr) {
        DWORD bytesRead = 0;
        BOOL readResult = ReadFile(hComPort, buffer, sizeof(buffer) - 1, &bytesRead, nullptr);
        
        if (readResult && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            
            // Объединяем с предыдущим остатком
            std::string data = leftover + std::string(buffer, bytesRead);
            
            // Ищем полные пакеты, оканчивающиеся на \r\n
            size_t pos = 0;
            while ((pos = data.find("\r\n")) != std::string::npos) {
                std::string line = data.substr(0, pos);
                
                // Пропускаем промпты >>> и пустые строки
                if (line.find(">>>") == std::string::npos && !line.empty()) {
                    // Парсим только значимые данные
                    ParseResponse(line.c_str(), line.length());
                }
                
                // Удаляем обработанную часть
                data = data.substr(pos + 2);
            }
            
            // Сохраняем остаток для следующей итерации
            leftover = data;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(5));  // Уменьшаем задержку для более быстрой реакции
    }
    
    std::cout << "[MakcuUART] Monitoring loop ended" << std::endl;
}

void MakcuUART::ParseResponse(const char* buffer, size_t length) {
    // Парсим ответы вида "btn:X" где X - битовая маска кнопок
    // btn:1 = ЛКМ, btn:2 = ПКМ, btn:4 = СКМ, btn:0 = все отпущены
    
    std::string data(buffer, length);
    
    // Проверяем наличие префикса "btn:"
    size_t pos = data.find("btn:");
    if (pos != std::string::npos) {
        // Извлекаем значение после "btn:"
        size_t endPos = data.find("\r\n", pos);
        if (endPos == std::string::npos) {
            endPos = data.length();  // Если нет \r\n, берём до конца строки
        }
        
        std::string valueStr = data.substr(pos + 4, endPos - pos - 4);
        try {
            int btnValue = std::stoi(valueStr);
            
            bool lmb = (btnValue & 0x01) != 0;
            bool rmb = (btnValue & 0x02) != 0;
            bool mmb = (btnValue & 0x04) != 0;
            
            // Обновляем локальное состояние и глобальные переменные
            if (m_lmb_pressed.load() != lmb) {
                m_lmb_pressed.store(lmb);
                pwnz_ai::g_makcu_shooting.store(lmb);  // Синхронизация
                std::cout << "[MakcuUART] LMB state changed: " << (lmb ? "pressed" : "released") 
                          << " (btnValue=" << btnValue << ")" << std::endl;
            }
            if (m_rmb_pressed.load() != rmb) {
                m_rmb_pressed.store(rmb);
                pwnz_ai::g_makcu_aiming.store(rmb);  // Синхронизация
                std::cout << "[MakcuUART] RMB state changed: " << (rmb ? "pressed" : "released") 
                          << " (btnValue=" << btnValue << ")" << std::endl;
            }
            if (m_mmb_pressed.load() != mmb) {
                m_mmb_pressed.store(mmb);
                pwnz_ai::g_makcu_zooming.store(mmb);  // Синхронизация
                std::cout << "[MakcuUART] MMB state changed: " << (mmb ? "pressed" : "released") 
                          << " (btnValue=" << btnValue << ")" << std::endl;
            }
            
            // === КРИТИЧНО ДЛЯ 2PC: также обновляем g_remote_aim_key для совместимости ===
            // Это позволяет аимботу работать как через g_makcu_aiming, так и через g_remote_aim_key
            if (rmb || lmb) {
                g_remote_aim_key.store(true);
            } else {
                // Только если обе кнопки отпущены, сбрасываем g_remote_aim_key
                // Это нужно чтобы не сбросить если другая кнопка ещё нажата
                if (!rmb && !lmb) {
                    g_remote_aim_key.store(false);
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[MakcuUART] Parse error: " << e.what() << ", data: " << valueStr << std::endl;
        }
    }
    // Игнорируем все остальные данные (промпты >>>, версии и т.д.)
}
