#pragma once

#include <string>
#include <vector>
#include <atomic>

#ifdef _WIN32
    #include <windows.h>
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
    HardwareBackend() = default;
    ~HardwareBackend();

#ifdef _WIN32
    HANDLE hComPort = INVALID_HANDLE_VALUE;
#endif
    int udpSocket = -1;
    struct sockaddr_in kmboxAddr {};
    std::atomic<bool> mackuConnected{false};
    std::atomic<bool> kmboxConnected{false};
};
