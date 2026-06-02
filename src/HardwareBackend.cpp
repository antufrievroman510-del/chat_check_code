// Включаем WinHeaders.h ПЕРЕД HardwareBackend.h чтобы все типы Windows были определены
#include "WinHeaders.h"
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
    // Определяем ssize_t для Windows если еще не определен
    #ifndef _SSIZE_T_DEFINED
        #ifdef _WIN64
            typedef signed __int64 ssize_t;
        #else
            typedef signed int ssize_t;
        #endif
        #define _SSIZE_T_DEFINED
    #endif
#endif

HardwareBackend& HardwareBackend::Instance() {
    static HardwareBackend instance;
    return instance;
}

HardwareBackend::HardwareBackend() 
    : hComPort(nullptr), udpSocket(0), kmboxAddrPtr(nullptr), mackuConnected(false), kmboxConnected(false) {
}

HardwareBackend::~HardwareBackend() {
    DisconnectMacku();
    DisconnectKMbox();
}

bool HardwareBackend::ConnectMacku(const std::string& port, int baud) {
#ifdef _WIN32
    // Проверка на пустое имя порта
    if (port.empty()) {
        std::cerr << "Empty COM port name" << std::endl;
        return false;
    }
    
    DisconnectMacku();
    
    // Формируем имя порта для Windows (\\.\COM3)
    std::string fullPort = "\\\\.\\" + port;
    
    hComPort = CreateFileA(
        fullPort.c_str(),
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
        std::cerr << "Failed to open COM port: " << port << " (Error: " << GetLastError() << ")" << std::endl;
        return false;
    }
    
    DCB dcb = {};
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(hComPort, &dcb)) {
        CloseHandle(hComPort);
        hComPort = nullptr;
        std::cerr << "Failed to get COM state: " << GetLastError() << std::endl;
        return false;
    }
    
    dcb.BaudRate = baud;
    dcb.ByteSize = 8;
    dcb.StopBits = ONESTOPBIT;
    dcb.Parity = NOPARITY;
    
    if (!SetCommState(hComPort, &dcb)) {
        CloseHandle(hComPort);
        hComPort = nullptr;
        std::cerr << "Failed to set COM state: " << GetLastError() << std::endl;
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
    if (!mackuConnected.load() || hComPort == nullptr) return false;
    
#ifdef _WIN32
    // Формат протокола Makcu ESP32S3 (совместим с прошивкой MAKCM):
    // Текстовая команда: "km.move(x,y)\r\n"
    // См: https://github.com/terrafirma2021/MAKCM
    std::string command = "km.move(" + std::to_string(x) + "," + std::to_string(y) + ")\r\n";
    
    DWORD bytesWritten;
    BOOL result = WriteFile(hComPort, command.c_str(), static_cast<DWORD>(command.length()), &bytesWritten, nullptr);
    
    if (!result || bytesWritten != command.length()) {
        std::cerr << "[HardwareBackend] SendMackuMove failed: " << GetLastError() << std::endl;
        return false;
    }
    return true;
#else
    return false;
#endif
}

bool HardwareBackend::SendMackuClick(uint8_t button) {
    if (!mackuConnected.load() || hComPort == nullptr) return false;
    
#ifdef _WIN32
    // Формат протокола Makcu ESP32S3 (совместим с прошивкой MAKCM):
    // Требуется отправка пары команд: нажатие (1) и отпускание (0)
    // km.left(1), km.left(0) - ЛКМ
    // km.right(1), km.right(0) - ПКМ
    // km.middle(1), km.middle(0) - Колесо
    // km.side1(1), km.side1(0) - Боковая 1
    // km.side2(1), km.side2(0) - Боковая 2
    
    std::string buttonName;
    switch (button) {
        case 0: buttonName = "left"; break;    // ЛКМ
        case 1: buttonName = "right"; break;   // ПКМ
        case 2: buttonName = "middle"; break;  // Колесо
        case 3: buttonName = "side1"; break;   // Боковая 1
        case 4: buttonName = "side2"; break;   // Боковая 2
        default: buttonName = "left"; break;
    }
    
    // Команда нажатия
    std::string pressCmd = "km." + buttonName + "(1)\r\n";
    DWORD bytesWritten;
    BOOL result = WriteFile(hComPort, pressCmd.c_str(), static_cast<DWORD>(pressCmd.length()), &bytesWritten, nullptr);
    if (!result || bytesWritten != pressCmd.length()) {
        std::cerr << "[HardwareBackend] SendMackuClick press failed: " << GetLastError() << std::endl;
        return false;
    }
    
    // Задержка между нажатием и отпусканием
    Sleep(50);
    
    // Команда отпускания
    std::string releaseCmd = "km." + buttonName + "(0)\r\n";
    result = WriteFile(hComPort, releaseCmd.c_str(), static_cast<DWORD>(releaseCmd.length()), &bytesWritten, nullptr);
    if (!result || bytesWritten != releaseCmd.length()) {
        std::cerr << "[HardwareBackend] SendMackuClick release failed: " << GetLastError() << std::endl;
        return false;
    }
    
    return true;
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
    if (udpSocket == 0) {
        std::cerr << "Failed to create UDP socket" << std::endl;
        return false;
    }
    
    // Создаем sockaddr_in на куче чтобы избежать проблемы с incomplete type в заголовке
    kmboxAddrPtr = new sockaddr_in();
    sockaddr_in* addr = reinterpret_cast<sockaddr_in*>(kmboxAddrPtr);
    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_port = htons(port);
    
    if (inet_pton(AF_INET, ip.c_str(), &addr->sin_addr) <= 0) {
        std::cerr << "Invalid IP address: " << ip << std::endl;
#ifdef _WIN32
        closesocket(udpSocket);
#else
        close(udpSocket);
#endif
        udpSocket = 0;
        delete addr;
        kmboxAddrPtr = nullptr;
        return false;
    }
    
    kmboxConnected = true;
    std::cout << "KMbox connected to " << ip << ":" << port << std::endl;
    return true;
}

void HardwareBackend::DisconnectKMbox() {
    if (udpSocket != 0) {
#ifdef _WIN32
        closesocket(udpSocket);
        WSACleanup();
#else
        close(udpSocket);
#endif
        udpSocket = 0;
    }
    // Освобождаем память для sockaddr_in
    if (kmboxAddrPtr != nullptr) {
        delete reinterpret_cast<sockaddr_in*>(kmboxAddrPtr);
        kmboxAddrPtr = nullptr;
    }
    kmboxConnected = false;
}

bool HardwareBackend::IsKMboxConnected() const {
    return kmboxConnected.load();
}

bool HardwareBackend::SendKMboxMove(int x, int y) {
    if (!kmboxConnected.load() || udpSocket == 0 || kmboxAddrPtr == nullptr) return false;
    
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
    
    sockaddr_in* addr = reinterpret_cast<sockaddr_in*>(kmboxAddrPtr);
    ssize_t sent = sendto(udpSocket, reinterpret_cast<char*>(&packet), sizeof(packet), 0,
                          reinterpret_cast<struct sockaddr*>(addr), sizeof(*addr));
    
    return sent == sizeof(packet);
}

bool HardwareBackend::SendKMboxClick(uint8_t button) {
    if (!kmboxConnected.load() || udpSocket == 0 || kmboxAddrPtr == nullptr) return false;
    
    // Протокол KMbox Net: клик
    struct KMboxClickPacket {
        uint8_t header[2] = {0x5A, 0xA5};
        uint8_t type = 0x02; // Click command
        uint8_t button;
        uint8_t reserved[4] = {0x00};
    };
    
    KMboxClickPacket packet;
    packet.button = button;
    
    sockaddr_in* addr = reinterpret_cast<sockaddr_in*>(kmboxAddrPtr);
    ssize_t sent = sendto(udpSocket, reinterpret_cast<char*>(&packet), sizeof(packet), 0,
                          reinterpret_cast<struct sockaddr*>(addr), sizeof(*addr));
    
    return sent == sizeof(packet);
}
