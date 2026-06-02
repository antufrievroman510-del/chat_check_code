#include "MakcuUART.h"
// Winsock заголовки уже подключены в MakcuUART.h
#include <iostream>
#include <thread>
#include <chrono>
#include <sstream>

// Протокол Makcu ESP32S3 использует текстовые команды формата:
// km.move(x,y) - движение мыши
// km.click(button) - клик (button: 0=left, 1=right, 2=middle)
// km.press(button) - нажать кнопку
// km.release(button) - отпустить кнопку

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
    // В текущей прошивке Makcu нет команды клика через UART.
    // Можно расширить протокол или оставить заглушку.
    // Для совместимости с интерфейсом просто возвращаем.
    (void)button; 
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
