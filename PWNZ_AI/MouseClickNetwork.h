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
 * @brief Класс для передачи ТОЛЬКО нажатий кнопок мыши между ПК
 * 
 * Архитектура 2PC без LAN кабеля:
 * - ПК1 (Игровой): Физическая мышь -> Makcu плата -> Читает нажатия кнопок
 * - ПК2 (Чит): Этот класс получает события кнопок по WiFi/интернету
 * 
 * Важно: 
 * - Движения мыши передаются через Makcu (COM-порт)
 * - Этот класс передает ТОЛЬКО нажатия кнопок (LMB/RMB) для аимбота
 * - Использует UDP для минимальной задержки
 * - Античит не видит сетевое соединение т.к. оно идет от чита, а не от игры
 */
class MouseClickNetwork {
private:
    SOCKET sock = INVALID_SOCKET;
    sockaddr_in server_addr{};
    std::atomic<bool> is_connected{false};
    std::atomic<bool> should_run{false};
    std::string target_ip = "192.168.1.100";
    int target_port = 5556; // Отдельный порт для кликов (не путать с aim_data)

    std::mutex send_mutex;

    // Пакет содержит ТОЛЬКО состояние кнопок
    struct ClickPacket {
        bool lmb_pressed;    // Левая кнопка (стрельба)
        bool rmb_pressed;    // Правая кнопка (прицеливание)
        bool mmb_pressed;    // Средняя кнопка (опционально)
        uint8_t reserved;    // Выравнивание
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
        ClickPacket test_pkt = {false, false, false, 0};
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

    /**
     * @brief Отправка состояния кнопок мыши
     * @param lmb Левая кнопка нажата
     * @param rmb Правая кнопка нажата
     * @param mmb Средняя кнопка нажата (опционально)
     */
    bool send_click_state(bool lmb, bool rmb, bool mmb = false) {
        if (!is_connected.load() || sock == INVALID_SOCKET) return false;

        ClickPacket pkt;
        pkt.lmb_pressed = lmb;
        pkt.rmb_pressed = rmb;
        pkt.mmb_pressed = mmb;
        pkt.reserved = 0;

        std::lock_guard<std::mutex> lock(send_mutex);
        int result = sendto(sock, (const char*)&pkt, sizeof(pkt), 0, 
                           (struct sockaddr*)&server_addr, sizeof(server_addr));

        return (result != SOCKET_ERROR);
    }

    /**
     * @brief Быстрая отправка только LMB (для стрельбы)
     */
    bool send_lmb(bool pressed) {
        return send_click_state(pressed, false, false);
    }

    /**
     * @brief Быстрая отправка только RMB (для прицеливания)
     */
    bool send_rmb(bool pressed) {
        return send_click_state(false, pressed, false);
    }

    bool is_connected_status() const {
        return is_connected.load();
    }

    void set_ip(const std::string& ip) { target_ip = ip; }
    void set_port(int port) { target_port = port; }

    std::string get_ip() const { return target_ip; }
    int get_port() const { return target_port; }
};
