#include "HardwareBackend.h"
#include <cstring>
#include <iostream>

// WinHeaders.h уже включает всё необходимое для Windows (windows.h, winsock2.h, ws2tcpip.h)
// Поэтому не нужно включать их повторно здесь - это вызовет конфликты переопределения
#ifndef _WIN32
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
#else
    #pragma comment(lib, "ws2_32.lib")
    #ifndef _SSIZE_T_DEFINED
        #define _SSIZE_T_DEFINED
        typedef SSIZE_T ssize_t;
    #endif
#endif

HardwareBackend& HardwareBackend::Instance() {
    static HardwareBackend instance;
    return instance;
}

HardwareBackend::HardwareBackend() : hComPort(nullptr), udpSocket(-1), mackuConnected(false), kmboxConnected(false) {}

HardwareBackend::~HardwareBackend() {
    DisconnectMacku();
    DisconnectKMbox();
}

bool HardwareBackend::ConnectMacku(const std::string& port, int baud) {
#ifdef _WIN32
    DisconnectMacku();
    
    std::string fullPort = "\\\\.\\" + port;
    hComPort = CreateFileA(fullPort.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    
    if (hComPort == (HANDLE)-1) {
        std::cerr << "Failed to open COM port: " << port << std::endl;
        return false;
    }
    
    DCB dcb = {};
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(hComPort, &dcb)) {
        CloseHandle(hComPort);
        hComPort = nullptr;
        return false;
    }
    
    dcb.BaudRate = baud;
    dcb.ByteSize = 8;
    dcb.StopBits = ONESTOPBIT;
    dcb.Parity = NOPARITY;
    
    if (!SetCommState(hComPort, &dcb)) {
        CloseHandle(hComPort);
        hComPort = nullptr;
        return false;
    }
    
    COMMTIMEOUTS timeouts = {};
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant = 50;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    
    SetCommTimeouts(hComPort, &timeouts);
    
    mackuConnected = true;
    std::cout << "Macku connected on " << port << " at " << baud << " baud" << std::endl;
    return true;
#else
    std::cerr << "Macku UART only supported on Windows" << std::endl;
    return false;
#endif
}

void HardwareBackend::DisconnectMacku() {
#ifdef _WIN32
    if (hComPort != nullptr) {
        CloseHandle(hComPort);
        hComPort = nullptr;
    }
#endif
    mackuConnected = false;
}

bool HardwareBackend::IsMackuConnected() const {
    return mackuConnected.load();
}

bool HardwareBackend::SendMackuMove(int x, int y) {
    if (!mackuConnected.load()) return false;
    
#ifdef _WIN32
    // Формат протокола Macku: [0xAA, 0x55, X_low, X_high, Y_low, Y_high, checksum]
    uint8_t buffer[8];
    buffer[0] = 0xAA;
    buffer[1] = 0x55;
    buffer[2] = x & 0xFF;
    buffer[3] = (x >> 8) & 0xFF;
    buffer[4] = y & 0xFF;
    buffer[5] = (y >> 8) & 0xFF;
    buffer[6] = (buffer[0] + buffer[1] + buffer[2] + buffer[3] + buffer[4] + buffer[5]) & 0xFF;
    buffer[7] = 0x0D; // End marker
    
    DWORD bytesWritten;
    WriteFile(hComPort, buffer, 8, &bytesWritten, nullptr);
    return bytesWritten == 8;
#else
    return false;
#endif
}

bool HardwareBackend::SendMackuClick(uint8_t button) {
    if (!mackuConnected.load()) return false;
    
#ifdef _WIN32
    // Формат: [0xAA, 0x55, 0x00, 0x00, BUTTON, 0x00, checksum, 0x0D]
    uint8_t buffer[8];
    buffer[0] = 0xAA;
    buffer[1] = 0x55;
    buffer[2] = 0x00;
    buffer[3] = 0x00;
    buffer[4] = button;
    buffer[5] = 0x00;
    buffer[6] = (buffer[0] + buffer[1] + buffer[2] + buffer[3] + buffer[4] + buffer[5]) & 0xFF;
    buffer[7] = 0x0D;
    
    DWORD bytesWritten;
    WriteFile(hComPort, buffer, 8, &bytesWritten, nullptr);
    return bytesWritten == 8;
#else
    return false;
#endif
}

bool HardwareBackend::ConnectKMbox(const std::string& ip, int port) {
    DisconnectKMbox();
    
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    
    udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udpSocket < 0) {
        std::cerr << "Failed to create UDP socket" << std::endl;
        return false;
    }
    
    memset(&kmboxAddr, 0, sizeof(kmboxAddr));
    kmboxAddr.sin_family = AF_INET;
    kmboxAddr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, ip.c_str(), &kmboxAddr.sin_addr) <= 0) {
        std::cerr << "Invalid IP address: " << ip << std::endl;
#ifdef _WIN32
        closesocket(udpSocket);
#else
        close(udpSocket);
#endif
        udpSocket = -1;
        return false;
    }
    
    kmboxConnected = true;
    std::cout << "KMbox connected to " << ip << ":" << port << std::endl;
    return true;
}

void HardwareBackend::DisconnectKMbox() {
    if (udpSocket >= 0) {
#ifdef _WIN32
        closesocket(udpSocket);
        WSACleanup();
#else
        close(udpSocket);
#endif
        udpSocket = -1;
    }
    kmboxConnected = false;
}

bool HardwareBackend::IsKMboxConnected() const {
    return kmboxConnected.load();
}

bool HardwareBackend::SendKMboxMove(int x, int y) {
    if (!kmboxConnected.load() || udpSocket < 0) return false;
    
    // Протокол KMbox Net: отправка структуры движения
    struct KMboxMovePacket {
        uint8_t header[2] = {0x5A, 0xA5};
        uint8_t type = 0x01; // Move command
        uint8_t reserved = 0x00;
        int16_t move_x;
        int16_t move_y;
        uint8_t padding[2] = {0x00, 0x00};
    };
    
    KMboxMovePacket packet;
    packet.move_x = static_cast<int16_t>(x);
    packet.move_y = static_cast<int16_t>(y);
    
    ssize_t sent = sendto(udpSocket, reinterpret_cast<char*>(&packet), sizeof(packet), 0,
                          reinterpret_cast<struct sockaddr*>(&kmboxAddr), sizeof(kmboxAddr));
    
    return sent == sizeof(packet);
}

bool HardwareBackend::SendKMboxClick(uint8_t button) {
    if (!kmboxConnected.load() || udpSocket < 0) return false;
    
    // Протокол KMbox Net: клик
    struct KMboxClickPacket {
        uint8_t header[2] = {0x5A, 0xA5};
        uint8_t type = 0x02; // Click command
        uint8_t button;
        uint8_t reserved[4] = {0x00};
    };
    
    KMboxClickPacket packet;
    packet.button = button;
    
    ssize_t sent = sendto(udpSocket, reinterpret_cast<char*>(&packet), sizeof(packet), 0,
                          reinterpret_cast<struct sockaddr*>(&kmboxAddr), sizeof(kmboxAddr));
    
    return sent == sizeof(packet);
}
