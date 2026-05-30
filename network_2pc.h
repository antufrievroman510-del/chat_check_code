#pragma once

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "Ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #define SOCKET int
    #define INVALID_SOCKET -1
    #define closesocket close
#endif

#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <cstring>
#include <iostream>

class Network2PC {
private:
    SOCKET sock = INVALID_SOCKET;
    sockaddr_in server_addr{};
    std::atomic<bool> is_connected{false};
    std::atomic<bool> should_run{false};
    std::string target_ip = "192.168.1.100";
    int target_port = 5555;

    std::mutex send_mutex;

    struct AimPacket {
        float x;
        float y;
        bool shoot;
        float confidence;
    };

public:
    Network2PC() {
#ifdef _WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    }

    ~Network2PC() {
        disconnect();
#ifdef _WIN32
        WSACleanup();
#endif
    }

    bool connect(const std::string& ip, int port) {
        if (is_connected.load()) return true;

        target_ip = ip;
        target_port = port;

        sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock == INVALID_SOCKET) {
            return false;
        }

        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(target_port);

#ifdef _WIN32
        inet_pton(AF_INET, target_ip.c_str(), &server_addr.sin_addr);
#else
        inet_pton(AF_INET, target_ip.c_str(), &server_addr.sin_addr);
#endif

        // Отправляем тестовый пакет
        AimPacket test_pkt = {0, 0, false, 0};
        sendto(sock, (const char*)&test_pkt, sizeof(test_pkt), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));

        is_connected.store(true);
        should_run.store(true);
        return true;
    }

    void disconnect() {
        should_run.store(false);
        is_connected.store(false);
        if (sock != INVALID_SOCKET) {
            closesocket(sock);
            sock = INVALID_SOCKET;
        }
    }

    bool send_aim_data(float x, float y, bool shoot, float confidence) {
        if (!is_connected.load() || sock == INVALID_SOCKET) return false;

        AimPacket pkt;
        pkt.x = x;
        pkt.y = y;
        pkt.shoot = shoot;
        pkt.confidence = confidence;

        std::lock_guard<std::mutex> lock(send_mutex);
        int result = sendto(sock, (const char*)&pkt, sizeof(pkt), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));

        return (result != SOCKET_ERROR);
    }

    bool is_connected_status() const {
        return is_connected.load();
    }

    void set_ip(const std::string& ip) { target_ip = ip; }
    void set_port(int port) { target_port = port; }

    std::string get_ip() const { return target_ip; }
    int get_port() const { return target_port; }
};
