#include "MakcuUART.h"
// Winsock заголовки уже подключены в MakcuUART.h
#include <iostream>
#include <thread>
#include <chrono>
#include <sstream>

// Протокол Makcu ESP32S3 (прошивка MAKCM) использует текстовые команды формата:
// km.move(x,y)      - движение мыши
// km.press(button)  - нажать кнопку (button: L=ЛКМ, R=ПКМ, M=колесо, 4=боковая1, 5=боковая2)
// km.release(button)- отпустить кнопку
// Важно: Для корректного клика нужно отправить пару команд press -> release

MakcuUART::MakcuUART() 
    : hComPort(nullptr), isConnected(false), packetDelayMs(5), m_portName("COM3"), m_baudRate(115200) {}

MakcuUART::~MakcuUART() {
    Shutdown();
}

// Реализация интерфейса IMouseInput
bool MakcuUART::Init() {
    return Connect(m_portName, m_baudRate);
}

void MakcuUART::Move(int dx, int dy) {
    MoveMouse(dx, dy);
}

void MakcuUART::Click(int button) {
    ClickMouse(button);
}

bool MakcuUART::ClickMouse(int button) {
    if (!isConnected || hComPort == nullptr) {
        std::cerr << "[MakcuUART] ClickMouse: Not connected!" << std::endl;
        return false;
    }

    std::lock_guard<std::mutex> lock(mtx);

    // Формируем обозначение кнопки
    // button: 0=ЛКМ (L), 1=ПКМ (R), 2=Колесо (M), 3=Боковая1 (4), 4=Боковая2 (5)
    std::string buttonStr;
    switch (button) {
        case 0: buttonStr = "L"; break;   // ЛКМ
        case 1: buttonStr = "R"; break;   // ПКМ
        case 2: buttonStr = "M"; break;   // Колесо (нажатие)
        case 3: buttonStr = "4"; break;   // Боковая кнопка 1
        case 4: buttonStr = "5"; break;   // Боковая кнопка 2
        default: buttonStr = "L"; break;  // По умолчанию ЛКМ
    }
    
    // Прошивка MAKCM требует раздельные команды press и release для корректного клика
    // Формируем команду нажатия: km.press(button)\r\n
    std::ostringstream pressCmd;
    pressCmd << "km.press(" << buttonStr << ")\r\n";
    std::string pressCommand = pressCmd.str();
    
    // Формируем команду отпускания: km.release(button)\r\n
    std::ostringstream releaseCmd;
    releaseCmd << "km.release(" << buttonStr << ")\r\n";
    std::string releaseCommand = releaseCmd.str();

    std::cout << "[MakcuUART] Sending click: press(" << buttonStr << ") -> release(" << buttonStr << ")" << std::endl;
    
    // Отправляем нажатие
    bool pressResult = WriteBytes(reinterpret_cast<const unsigned char*>(pressCommand.c_str()), pressCommand.length());
    if (!pressResult) {
        std::cerr << "[MakcuUART] Failed to send press command" << std::endl;
        return false;
    }
    
    // Небольшая задержка между нажатием и отпусканием (имитация реального клика)
    // 50ms достаточно для регистрации клика в игре
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Отправляем отпускание
    bool releaseResult = WriteBytes(reinterpret_cast<const unsigned char*>(releaseCommand.c_str()), releaseCommand.length());
    if (!releaseResult) {
        std::cerr << "[MakcuUART] Failed to send release command" << std::endl;
        return false;
    }
    
    // Дополнительная задержка после клика
    if (packetDelayMs > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(packetDelayMs));
    }

    return true;
}

void MakcuUART::Shutdown() {
    Disconnect();
}

bool MakcuUART::Connect(const std::string& portName, int baudRate) {
    std::lock_guard<std::mutex> lock(mtx);
    
    if (isConnected) {
        Disconnect();
    }

    // Формируем имя порта для Windows (\\.\COM3)
    // Правильное экранирование: четыре обратных слэша дают два в строке, плюс точка и ещё два слэша
    std::string fullPortName = "\\\\.\\" + portName;
    
    hComPort = CreateFileA(
        fullPortName.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,          // No sharing
        nullptr,    // Default security attributes
        OPEN_EXISTING,
        0,          // No flags or attributes
        nullptr     // No template file
    );

    // Проверка на INVALID_HANDLE_VALUE, а не на nullptr
    if (hComPort == INVALID_HANDLE_VALUE) {
        hComPort = nullptr;
        std::cerr << "[MakcuUART] Failed to open COM port: " << portName 
                  << " (Error: " << GetLastError() << ")" << std::endl;
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

    // Установка таймаутов (опционально, для надежности)
    COMMTIMEOUTS timeouts = { 0 };
    timeouts.WriteTotalTimeoutConstant = 500;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    SetCommTimeouts(hComPort, &timeouts);

    isConnected = true;
    return true;
}

void MakcuUART::Disconnect() {
    std::lock_guard<std::mutex> lock(mtx);
    
    if (hComPort != nullptr) {
        CloseHandle(hComPort);
        hComPort = nullptr;
    }
    isConnected = false;
}

bool MakcuUART::IsConnected() const {
    return isConnected;
}

bool MakcuUART::MoveMouse(int dx, int dy) {
    if (!isConnected || hComPort == nullptr) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mtx);

    // Формируем текстовую команду: km.move(x,y)\n
    // ESP32S3 прошивка ожидает именно такой формат
    std::ostringstream cmd;
    cmd << "km.move(" << dx << "," << dy << ")\r\n";
    std::string command = cmd.str();

    bool result = WriteBytes(reinterpret_cast<const unsigned char*>(command.c_str()), command.length());
    
    if (result && packetDelayMs > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(packetDelayMs));
    }

    return result;
}

bool MakcuUART::MoveMouseAbsolute(int x, int y, int /*width*/, int /*height*/) {
    if (!isConnected || hComPort == nullptr) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mtx);

    // Для абсолютного позиционирования используем ту же команду
    // Прошивка сама масштабирует координаты если нужно
    std::ostringstream cmd;
    cmd << "km.move(" << x << "," << y << ")\r\n";
    std::string command = cmd.str();

    bool result = WriteBytes(reinterpret_cast<const unsigned char*>(command.c_str()), command.length());
    
    if (result && packetDelayMs > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(packetDelayMs));
    }

    return result;
}

void MakcuUART::SetPacketDelayMs(int ms) {
    packetDelayMs = (ms < 0) ? 0 : ms;
}

bool MakcuUART::WriteBytes(const unsigned char* data, size_t length) {
    DWORD bytesWritten;
    BOOL result = WriteFile(hComPort, data, static_cast<DWORD>(length), &bytesWritten, nullptr);
    
    if (!result || bytesWritten != length) {
        // Ошибка записи или таймаут
        Disconnect();
        return false;
    }
    
    return true;
}
