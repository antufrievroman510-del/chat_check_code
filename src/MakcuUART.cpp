#include "MakcuUART.h"
// Winsock заголовки уже подключены в MakcuUART.h
#include <thread>
#include <chrono>

// Преамбула и постамбула протокола
constexpr unsigned char PACKET_START = 0xAA;
constexpr unsigned char PACKET_END = 0xBB;
constexpr unsigned char PACKET_TYPE_MOVE = 0x01;
constexpr unsigned char PACKET_TYPE_MOVE_ABS = 0x02;

MakcuUART::MakcuUART() 
    : hComPort(nullptr), isConnected(false), packetDelayMs(1) {}

MakcuUART::~MakcuUART() {
    Disconnect();
}

bool MakcuUART::Connect(const std::string& portName, int baudRate) {
    std::lock_guard<std::mutex> lock(mtx);
    
    if (isConnected) {
        Disconnect();
    }

    // Формируем имя порта для Windows (\\.\COM3)
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

    if (hComPort == nullptr) {
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

    // Формируем пакет: [START, TYPE, DX_L, DX_H, DY_L, DY_H, END]
    // Используем little-endian порядок байт для совместимости с Arduino/Makcu
    unsigned char packet[7];
    packet[0] = PACKET_START;
    packet[1] = PACKET_TYPE_MOVE;
    packet[2] = static_cast<unsigned char>(dx & 0xFF);        // DX Low
    packet[3] = static_cast<unsigned char>((dx >> 8) & 0xFF); // DX High
    packet[4] = static_cast<unsigned char>(dy & 0xFF);        // DY Low
    packet[5] = static_cast<unsigned char>((dy >> 8) & 0xFF); // DY High
    packet[6] = PACKET_END;

    bool result = WriteBytes(packet, sizeof(packet));
    
    if (result && packetDelayMs > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(packetDelayMs));
    }

    return result;
}

bool MakcuUART::MoveMouseAbsolute(int x, int y, int width, int height) {
    if (!isConnected || hComPort == nullptr) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mtx);

    // Для абсолютного позиционирования можем масштабировать в диапазон 0-65535 (uint16_t)
    // или передавать как есть, если прошивка поддерживает
    unsigned short scaledX = static_cast<unsigned short>((x * 65535) / width);
    unsigned short scaledY = static_cast<unsigned short>((y * 65535) / height);

    unsigned char packet[7];
    packet[0] = PACKET_START;
    packet[1] = PACKET_TYPE_MOVE_ABS;
    packet[2] = static_cast<unsigned char>(scaledX & 0xFF);
    packet[3] = static_cast<unsigned char>((scaledX >> 8) & 0xFF);
    packet[4] = static_cast<unsigned char>(scaledY & 0xFF);
    packet[5] = static_cast<unsigned char>((scaledY >> 8) & 0xFF);
    packet[6] = PACKET_END;

    bool result = WriteBytes(packet, sizeof(packet));
    
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
