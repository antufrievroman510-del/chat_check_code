#include "KMBoxNet.h"
// Winsock заголовки уже подключены в KMBoxNet.h
#include <thread>
#include <chrono>
#include <cstring>

// Формат пакета KMbox Net (примерный, может требовать уточнения по документации):
// [Header: 2 bytes] [Command: 1 byte] [Data: N bytes] [Checksum: 1 byte]
// Header: 0xEB 0x90 (магические байты)
// Checksum: XOR всех байтов между header и checksum

constexpr unsigned char KMBOX_HEADER_1 = 0xEB;
constexpr unsigned char KMBOX_HEADER_2 = 0x90;

KMBoxNet::KMBoxNet() 
    : hSocket(INVALID_SOCKET), isConnected(false), packetDelayMs(1), winsockInitialized(false) {}

KMBoxNet::~KMBoxNet() {
    Disconnect();
}

bool KMBoxNet::InitializeWinSock() {
    if (winsockInitialized) {
        return true;
    }

    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        return false;
    }

    winsockInitialized = true;
    return true;
}

bool KMBoxNet::ConnectToDevice(const std::string& ipAddress, int port) {
    std::lock_guard<std::mutex> lock(mtx);

    if (isConnected) {
        Disconnect();
    }

    if (!InitializeWinSock()) {
        return false;
    }

    hSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (hSocket == INVALID_SOCKET) {
        return false;
    }

    // Настройка адреса
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    
    // Преобразование IP адреса
    if (inet_pton(AF_INET, ipAddress.c_str(), &serverAddr.sin_addr) <= 0) {
        closesocket(hSocket);
        hSocket = INVALID_SOCKET;
        return false;
    }

    // Подключение
    if (::connect(hSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) != 0) {
        closesocket(hSocket);
        hSocket = INVALID_SOCKET;
        return false;
    }

    // Установка таймаута на отправку (опционально)
    struct timeval timeout;
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;
    setsockopt(hSocket, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));

    isConnected = true;
    return true;
}

void KMBoxNet::Disconnect() {
    std::lock_guard<std::mutex> lock(mtx);

    if (hSocket != INVALID_SOCKET) {
        shutdown(hSocket, SD_SEND);
        closesocket(hSocket);
        hSocket = INVALID_SOCKET;
    }
    isConnected = false;
}

bool KMBoxNet::IsConnected() const {
    return isConnected;
}

std::vector<unsigned char> KMBoxNet::BuildPacket(uint8_t command, const unsigned char* data, size_t dataLength) {
    std::vector<unsigned char> packet;
    
    // Header
    packet.push_back(KMBOX_HEADER_1);
    packet.push_back(KMBOX_HEADER_2);
    
    // Command
    packet.push_back(command);
    
    // Data
    for (size_t i = 0; i < dataLength; ++i) {
        packet.push_back(data[i]);
    }
    
    // Checksum (XOR всех байтов после header)
    unsigned char checksum = command;
    for (size_t i = 0; i < dataLength; ++i) {
        checksum ^= data[i];
    }
    packet.push_back(checksum);
    
    return packet;
}

bool KMBoxNet::SendData(const unsigned char* data, size_t length) {
    if (!isConnected || hSocket == INVALID_SOCKET) {
        return false;
    }

    int totalSent = 0;
    while (totalSent < static_cast<int>(length)) {
        int result = send(hSocket, (const char*)(data + totalSent), length - totalSent, 0);
        if (result == SOCKET_ERROR || result == 0) {
            Disconnect();
            return false;
        }
        totalSent += result;
    }

    return true;
}

bool KMBoxNet::MoveMouse(int dx, int dy) {
    if (!isConnected || hSocket == INVALID_SOCKET) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mtx);

    // Формируем данные: DX (int16, little-endian), DY (int16, little-endian)
    unsigned char data[4];
    data[0] = static_cast<unsigned char>(dx & 0xFF);
    data[1] = static_cast<unsigned char>((dx >> 8) & 0xFF);
    data[2] = static_cast<unsigned char>(dy & 0xFF);
    data[3] = static_cast<unsigned char>((dy >> 8) & 0xFF);

    auto packet = BuildPacket(KMBoxCmd::MOVE_REL, data, sizeof(data));
    bool result = SendData(packet.data(), packet.size());

    if (result && packetDelayMs > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(packetDelayMs));
    }

    return result;
}

bool KMBoxNet::MoveMouseAbsolute(int x, int y) {
    if (!isConnected || hSocket == INVALID_SOCKET) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mtx);

    // Абсолютные координаты (uint16, 0-65535)
    unsigned char data[4];
    data[0] = static_cast<unsigned char>(x & 0xFF);
    data[1] = static_cast<unsigned char>((x >> 8) & 0xFF);
    data[2] = static_cast<unsigned char>(y & 0xFF);
    data[3] = static_cast<unsigned char>((y >> 8) & 0xFF);

    auto packet = BuildPacket(KMBoxCmd::MOVE_ABS, data, sizeof(data));
    bool result = SendData(packet.data(), packet.size());

    if (result && packetDelayMs > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(packetDelayMs));
    }

    return result;
}

bool KMBoxNet::MouseButton(uint8_t button, bool isPressed) {
    if (!isConnected || hSocket == INVALID_SOCKET) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mtx);

    // Данные: Button ID, State (0=release, 1=press)
    unsigned char data[2];
    data[0] = button;
    data[1] = isPressed ? 0x01 : 0x00;

    auto packet = BuildPacket(KMBoxCmd::BUTTON, data, sizeof(data));
    bool result = SendData(packet.data(), packet.size());

    if (result && packetDelayMs > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(packetDelayMs));
    }

    return result;
}

void KMBoxNet::SetPacketDelayMs(int ms) {
    packetDelayMs = (ms < 0) ? 0 : ms;
}
