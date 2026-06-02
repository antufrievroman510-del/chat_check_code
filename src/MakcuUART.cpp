#include "MakcuUART.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstdint>
#include <cstring>

// Протокол Makcu Binary Mouse Stream:
// Фрейм: [0xDE][0xAD][Length][Command][Data...]
// Length = количество байт данных + 1 (байт команды)
// Command 0x01: Движение (4 байта: dx_low, dx_high, dy_low, dy_high) - int16 little-endian
// Command 0x03: Кнопки (1 байт: битовая маска)
//   Бит 0 (0x01): ЛКМ
//   Бит 1 (0x02): ПКМ
//   Бит 2 (0x04): СКМ (колесо)
//   Бит 3 (0x08): Боковая кнопка 1
//   Бит 4 (0x10): Боковая кнопка 2

// Маркеры фрейма
static const uint8_t FRAME_MARKER_1 = 0xDE;
static const uint8_t FRAME_MARKER_2 = 0xAD;

// Команды
static const uint8_t CMD_MOVE_RELATIVE = 0x01;
static const uint8_t CMD_BUTTONS = 0x03;

// Кнопки (битовая маска)
static const uint8_t BTN_LEFT = 0x01;
static const uint8_t BTN_RIGHT = 0x02;
static const uint8_t BTN_MIDDLE = 0x04;

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
    // Для бинарного протокола клик = press + release с небольшой задержкой
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
    
    // Отправляем команду включения бинарного стрима кнопок
    // Прошивка Makcu начинает отправлять бинарные данные о кнопках после этой команды
    uint8_t enable_btn[] = {FRAME_MARKER_1, FRAME_MARKER_2, 0x02, CMD_BUTTONS, 0x01};
    WriteBytes(enable_btn, sizeof(enable_btn));
    std::cout << "[MakcuUART] Sent button stream enable command" << std::endl;
    
    Sleep(50);
    
    return true;
}

void MakcuUART::Disconnect() {
    std::lock_guard<std::mutex> lock(mtx);
    
    StopMonitoring();
    
    if (hComPort != nullptr) {
        // Отключаем стрим кнопок
        uint8_t disable_btn[] = {FRAME_MARKER_1, FRAME_MARKER_2, 0x02, CMD_BUTTONS, 0x00};
        WriteBytes(disable_btn, sizeof(disable_btn));
        
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

    // Формируем бинарный фрейм для движения
    // Данные: dx (int16 LE), dy (int16 LE) = 4 байта
    uint8_t data[4];
    data[0] = static_cast<uint8_t>(dx & 0xFF);
    data[1] = static_cast<uint8_t>((dx >> 8) & 0xFF);
    data[2] = static_cast<uint8_t>(dy & 0xFF);
    data[3] = static_cast<uint8_t>((dy >> 8) & 0xFF);
    
    bool result = SendBinaryFrame(CMD_MOVE_RELATIVE, data, 4);
    
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

    // Определяем бит кнопки
    uint8_t btnMask = 0;
    switch (button) {
        case 0: btnMask = BTN_LEFT; break;   // ЛКМ
        case 1: btnMask = BTN_RIGHT; break;  // ПКМ
        case 2: btnMask = BTN_MIDDLE; break; // СКМ
        default: btnMask = BTN_LEFT; break;
    }
    
    // Отправляем бинарную команду кнопки
    // Данные: 1 байт - битовая маска
    bool result = SendBinaryFrame(CMD_BUTTONS, &btnMask, 1);
    
    if (result) {
        // Обновляем локальное состояние
        switch (button) {
            case 0: m_lmb_pressed.store(true); break;
            case 1: m_rmb_pressed.store(true); break;
            case 2: m_mmb_pressed.store(true); break;
        }
        std::cout << "[MakcuUART] Press button " << button << " (mask=0x" << std::hex << (int)btnMask << std::dec << ")" << std::endl;
    }
    
    return result;
}

bool MakcuUART::ReleaseButton(int button) {
    if (!isConnected || hComPort == nullptr) {
        std::cerr << "[MakcuUART] ReleaseButton: Not connected!" << std::endl;
        return false;
    }

    std::lock_guard<std::mutex> lock(mtx);

    // Для отпускания отправляем маску со сброшенным битом
    // Но в протоколе Makcu команда 0x03 с маской устанавливает состояние кнопок
    // Поэтому отправляем 0x00 для отпускания всех кнопок, или конкретную маску без нужного бита
    
    // Получаем текущее состояние и сбрасываем нужный бит
    uint8_t currentMask = 0;
    if (m_lmb_pressed.load()) currentMask |= BTN_LEFT;
    if (m_rmb_pressed.load()) currentMask |= BTN_RIGHT;
    if (m_mmb_pressed.load()) currentMask |= BTN_MIDDLE;
    
    // Сбрасываем бит отпускаемой кнопки
    switch (button) {
        case 0: currentMask &= ~BTN_LEFT; break;
        case 1: currentMask &= ~BTN_RIGHT; break;
        case 2: currentMask &= ~BTN_MIDDLE; break;
    }
    
    bool result = SendBinaryFrame(CMD_BUTTONS, &currentMask, 1);
    
    if (result) {
        // Обновляем локальное состояние
        switch (button) {
            case 0: m_lmb_pressed.store(false); break;
            case 1: m_rmb_pressed.store(false); break;
            case 2: m_mmb_pressed.store(false); break;
        }
        std::cout << "[MakcuUART] Release button " << button << " (mask=0x" << std::hex << (int)currentMask << std::dec << ")" << std::endl;
    }
    
    return result;
}

void MakcuUART::SetPacketDelayMs(int ms) {
    packetDelayMs = (ms < 0) ? 0 : ms;
}

bool MakcuUART::SendBinaryFrame(uint8_t command, const uint8_t* data, size_t dataLen) {
    // Формируем фрейм: [0xDE][0xAD][Length][Command][Data...]
    // Length = dataLen + 1 (байт команды)
    uint8_t frame[256];
    frame[0] = FRAME_MARKER_1;
    frame[1] = FRAME_MARKER_2;
    frame[2] = static_cast<uint8_t>(dataLen + 1); // Length includes command byte
    frame[3] = command;
    
    if (dataLen > 0 && data != nullptr) {
        memcpy(frame + 4, data, dataLen);
    }
    
    size_t totalLen = 4 + dataLen;
    
    bool result = WriteBytes(frame, totalLen);
    
    if (result) {
        FlushFileBuffers(hComPort);
    }
    
    return result;
}

bool MakcuUART::WriteBytes(const uint8_t* data, size_t length) {
    DWORD bytesWritten;
    BOOL result = WriteFile(hComPort, data, static_cast<DWORD>(length), &bytesWritten, nullptr);
    
    if (!result || bytesWritten != length) {
        DWORD err = GetLastError();
        std::cerr << "[MakcuUART] Write failed: " << err << " (written=" << bytesWritten << ", expected=" << length << ")" << std::endl;
        Disconnect();
        return false;
    }
    
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
    
    uint8_t buffer[256];
    size_t bufferPos = 0;
    
    while (!m_stopMonitoring.load() && isConnected && hComPort != nullptr) {
        DWORD bytesRead = 0;
        BOOL readResult = ReadFile(hComPort, buffer, sizeof(buffer), &bytesRead, nullptr);
        
        if (readResult && bytesRead > 0) {
            // Парсим бинарные данные
            ParseBinaryResponse(buffer, bytesRead);
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    std::cout << "[MakcuUART] Monitoring loop ended" << std::endl;
}

void MakcuUART::ParseBinaryResponse(const uint8_t* buffer, size_t length) {
    // Ищем маркеры фрейма 0xDE 0xAD
    for (size_t i = 0; i + 3 < length; ++i) {
        if (buffer[i] == FRAME_MARKER_1 && buffer[i+1] == FRAME_MARKER_2) {
            uint8_t frameLen = buffer[i+2];
            uint8_t command = buffer[i+3];
            
            // Проверяем, что у нас достаточно данных
            if (i + 4 + (frameLen - 1) <= length) {
                if (command == CMD_BUTTONS && frameLen >= 2) {
                    // Данные о кнопках: 1 байт маски
                    uint8_t btnMask = buffer[i+4];
                    
                    bool lmb = (btnMask & BTN_LEFT) != 0;
                    bool rmb = (btnMask & BTN_RIGHT) != 0;
                    bool mmb = (btnMask & BTN_MIDDLE) != 0;
                    
                    // Обновляем состояние только если изменилось
                    if (m_lmb_pressed.load() != lmb) {
                        m_lmb_pressed.store(lmb);
                        std::cout << "[MakcuUART] LMB state changed: " << (lmb ? "pressed" : "released") << std::endl;
                    }
                    if (m_rmb_pressed.load() != rmb) {
                        m_rmb_pressed.store(rmb);
                        std::cout << "[MakcuUART] RMB state changed: " << (rmb ? "pressed" : "released") << std::endl;
                    }
                    if (m_mmb_pressed.load() != mmb) {
                        m_mmb_pressed.store(mmb);
                        std::cout << "[MakcuUART] MMB state changed: " << (mmb ? "pressed" : "released") << std::endl;
                    }
                }
                
                // Пропускаем этот фрейм
                i += (3 + frameLen);
            }
        }
    }
}
