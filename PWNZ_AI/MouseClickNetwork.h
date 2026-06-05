#pragma once

#include "WinHeaders.h"

#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <cstring>
#include <iostream>

/**
 * @brief Класс для передачи нажатий кнопок мыши между ПК через UDP
 * 
 * Архитектура 2PC:
 * - ПК1 (Игровой): Перехват хуком -> Отправляет события по сети
 * - ПК2 (Чит): MouseClickServer получает -> Эмулирует через SendInput
 * 
 * Поддерживаемые события:
 * - ЛКМ, ПКМ, СКМ (нажатие/отпускание)
 * - Колесо прокрутки (вверх/вниз)
 * - Боковые кнопки X1 (Назад), X2 (Вперед)
 */
class MouseClickNetwork {
private:
    SOCKET sock = INVALID_SOCKET;
    sockaddr_in server_addr{};
    std::atomic<bool> is_connected{false};
    std::atomic<bool> should_run{false};
    std::string target_ip = "192.168.1.100";
    int target_port = 5556;

    std::mutex send_mutex;

    // Типы событий (должны совпадать с MouseClickServer.h)
    #define MOUSE_EVENT_LMB_DOWN   0x01
    #define MOUSE_EVENT_LMB_UP     0x02
    #define MOUSE_EVENT_RMB_DOWN   0x03
    #define MOUSE_EVENT_RMB_UP     0x04
    #define MOUSE_EVENT_MMB_DOWN   0x05
    #define MOUSE_EVENT_MMB_UP     0x06
    #define MOUSE_EVENT_WHEEL_UP   0x07
    #define MOUSE_EVENT_WHEEL_DOWN 0x08
    #define MOUSE_EVENT_X1_DOWN    0x09
    #define MOUSE_EVENT_X1_UP      0x0A
    #define MOUSE_EVENT_X2_DOWN    0x0B
    #define MOUSE_EVENT_X2_UP      0x0C

    struct ClickPacket {
        uint8_t event_type;
        uint8_t reserved;
        int16_t wheel_delta;
        int32_t extra;
    };

public:
    MouseClickNetwork() {
#ifdef _WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    }

    ~MouseClickNetwork() {
        disconnect();
#ifdef _WIN32
        WSACleanup();
#endif
    }

    /**
     * @brief Подключение к серверу на втором ПК
     * @param ip IP адрес второго ПК (где чит)
     * @param port Порт для приема кликов (по умолчанию 5556)
     */
    bool connect(const std::string& ip, int port = 5556) {
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
        ClickPacket test_pkt = {0x00, 0, 0, 0};
        sendto(sock, (const char*)&test_pkt, sizeof(test_pkt), 0, 
               (struct sockaddr*)&server_addr, sizeof(server_addr));

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

    void send_event(uint8_t event_type, int16_t wheel_delta = 0) {
        if (!is_connected.load() || sock == INVALID_SOCKET) return;

        ClickPacket pkt;
        pkt.event_type = event_type;
        pkt.reserved = 0;
        pkt.wheel_delta = htons(wheel_delta);
        pkt.extra = 0;

        std::lock_guard<std::mutex> lock(send_mutex);
        sendto(sock, (const char*)&pkt, sizeof(pkt), 0, 
               (struct sockaddr*)&server_addr, sizeof(server_addr));
    }

    // Удобные методы для отправки конкретных событий
    void send_lmb_down() { send_event(MOUSE_EVENT_LMB_DOWN); }
    void send_lmb_up() { send_event(MOUSE_EVENT_LMB_UP); }
    void send_rmb_down() { send_event(MOUSE_EVENT_RMB_DOWN); }
    void send_rmb_up() { send_event(MOUSE_EVENT_RMB_UP); }
    void send_mmb_down() { send_event(MOUSE_EVENT_MMB_DOWN); }
    void send_mmb_up() { send_event(MOUSE_EVENT_MMB_UP); }
    void send_wheel_up() { send_event(MOUSE_EVENT_WHEEL_UP, 120); }
    void send_wheel_down() { send_event(MOUSE_EVENT_WHEEL_DOWN, -120); }
    void send_x1_down() { send_event(MOUSE_EVENT_X1_DOWN); }
    void send_x1_up() { send_event(MOUSE_EVENT_X1_UP); }
    void send_x2_down() { send_event(MOUSE_EVENT_X2_DOWN); }
    void send_x2_up() { send_event(MOUSE_EVENT_X2_UP); }

    bool is_connected_status() const {
        return is_connected.load();
    }

    void set_ip(const std::string& ip) { target_ip = ip; }
    void set_port(int port) { target_port = port; }

    std::string get_ip() const { return target_ip; }
    int get_port() const { return target_port; }

    void reconnect_if_needed() {
        if (!is_connected.load() && sock == INVALID_SOCKET) {
            connect(target_ip, target_port);
        }
    }
};
