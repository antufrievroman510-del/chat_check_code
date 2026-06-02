#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <cstdint>

// Forward declaration для HANDLE чтобы избежать включения windows.h здесь
#ifdef _WIN32
    typedef void* HANDLE;
#else
    // На не-Windows платформах SOCKET определяется как int
#endif

enum class HardwareMode {
    None = 0,
    LocalMouse, // SendInput
    GHub,       // Logitech G Hub HID
    Razer,      // Razer Synapse HID
    MackuUART,  // COM Port
    KMboxNet    // UDP
};

enum class BypassMode {
    None = 0,
    GHub,
    Razer,
    RandomDelay
};

struct HardwareConfig {
    HardwareMode mode = HardwareMode::LocalMouse;
    BypassMode bypass = BypassMode::None;
    
    // Macku Settings
    std::string com_port = "COM3";
    int baud_rate = 115200;
    
    // KMbox Settings
    std::string kmbox_ip = "192.168.1.100";
    int kmbox_port = 8888;
    
    // Common
    int random_delay_min = 10;
    int random_delay_max = 30;
    bool enabled = false;
};

class HardwareBackend {
public:
    static HardwareBackend& Instance();

    bool ConnectMacku(const std::string& port, int baud);
    void DisconnectMacku();
    bool IsMackuConnected() const;
    bool SendMackuMove(int x, int y);
    bool SendMackuClick(uint8_t button); // 1 = Left, 2 = Right, 3 = Middle

    bool ConnectKMbox(const std::string& ip, int port);
    void DisconnectKMbox();
    bool IsKMboxConnected() const;
    bool SendKMboxMove(int x, int y);
    bool SendKMboxClick(uint8_t button);

private:
    HardwareBackend();  // Добавлен конструктор по умолчанию
    ~HardwareBackend();

#ifdef _WIN32
    HANDLE hComPort;
#else
    void* hComPort;
#endif
#ifdef _WIN32
    unsigned int udpSocket;  // Используем unsigned int вместо SOCKET для избежания конфликта типов
#else
    int udpSocket;
#endif
    // Используем opaque pointer или forward declaration для sockaddr_in
    // Реальное определение будет в .cpp файле где включен WinHeaders.h
    void* kmboxAddrPtr;  // указатель на sockaddr_in
    std::atomic<bool> mackuConnected;
    std::atomic<bool> kmboxConnected;
};
