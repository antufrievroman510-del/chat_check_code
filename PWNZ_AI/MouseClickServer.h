#pragma once

#include "WinHeaders.h"

#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <iostream>
#include <functional>

/**
 * @brief Сервер для приема нажатий кнопок мыши с первого ПК
 * 
 * Архитектура 2PC:
 * - ПК1 (Игровой): Физическая мышь -> Makcu плата -> Отправляет клики по сети
 * - ПК2 (Чит): Этот сервер получает клики и эмулирует их через SendInput
 * 
 * Важно:
 * - Движения мыши обрабатываются отдельно через MakcuInput (COM-порт)
 * - Этот сервер принимает ТОЛЬКО нажатия кнопок
 * - Использует UDP порт 5556 (отдельно от aim_data порта 5555)
 */
class MouseClickServer {
private:
    SOCKET sock = INVALID_SOCKET;
    sockaddr_in local_addr{};
    std::atomic<bool> is_running{false};
    std::thread receive_thread;
    int listen_port = 5556;

    // Коллбэки для обработки нажатий
    std::function<void(bool)> on_lmb_pressed = nullptr;
    std::function<void(bool)> on_rmb_pressed = nullptr;
    std::function<void(bool)> on_mmb_pressed = nullptr;

    struct ClickPacket {
        bool lmb_pressed;
        bool rmb_pressed;
        bool mmb_pressed;
        uint8_t reserved;
    };

    void receive_loop() {
        std::cout << "[MouseClickServer] Listening on port " << listen_port << std::endl;

        while (is_running.load()) {
            ClickPacket pkt;
            sockaddr_in client_addr;
            int client_addr_size = sizeof(client_addr);

            int result = recvfrom(
                sock,
                (char*)&pkt,
                sizeof(pkt),
                0,
                (sockaddr*)&client_addr,
                &client_addr_size
            );

            if (result == SOCKET_ERROR || result != sizeof(pkt)) {
                continue;
            }

            // Обработка полученного пакета
            if (on_lmb_pressed && pkt.lmb_pressed) {
                on_lmb_pressed(true);
            }
            if (on_rmb_pressed && pkt.rmb_pressed) {
                on_rmb_pressed(true);
            }
            if (on_mmb_pressed && pkt.mmb_pressed) {
                on_mmb_pressed(true);
            }

            // Эмуляция отпускания кнопки через небольшую задержку
            // Это нужно т.к. мы получаем только факт нажатия, а не состояние
            if (result == sizeof(pkt)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                
                if (on_lmb_pressed && pkt.lmb_pressed) {
                    on_lmb_pressed(false);
                }
                if (on_rmb_pressed && pkt.rmb_pressed) {
                    on_rmb_pressed(false);
                }
                if (on_mmb_pressed && pkt.mmb_pressed) {
                    on_mmb_pressed(false);
                }
            }
        }
    }

public:
    MouseClickServer() = default;

    ~MouseClickServer() {
        stop();
    }

    /**
     * @brief Запуск сервера
     * @param port Порт для прослушивания (по умолчанию 5556)
     * @return true если успешно запущено
     */
    bool start(int port = 5556) {
        if (is_running.load()) {
            return true; // Уже запущен
        }

        listen_port = port;

#ifdef _WIN32
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            return false;
        }
#endif

        sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock == INVALID_SOCKET) {
#ifdef _WIN32
            WSACleanup();
#endif
            return false;
        }

        // Разрешаем несколько процессам binding на один порт (опционально)
        int reuse = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

        local_addr.sin_family = AF_INET;
        local_addr.sin_addr.s_addr = htonl(INADDR_ANY); // Слушаем все интерфейсы
        local_addr.sin_port = htons(listen_port);

        if (bind(sock, (sockaddr*)&local_addr, sizeof(local_addr)) == SOCKET_ERROR) {
            closesocket(sock);
            sock = INVALID_SOCKET;
#ifdef _WIN32
            WSACleanup();
#endif
            return false;
        }

        is_running.store(true);
        receive_thread = std::thread(&MouseClickServer::receive_loop, this);
        
        std::cout << "[MouseClickServer] Started successfully on port " << listen_port << std::endl;
        return true;
    }

    /**
     * @brief Остановка сервера
     */
    void stop() {
        if (!is_running.load()) {
            return;
        }

        is_running.store(false);
        
        if (receive_thread.joinable()) {
            receive_thread.join();
        }

        if (sock != INVALID_SOCKET) {
            closesocket(sock);
            sock = INVALID_SOCKET;
        }

#ifdef _WIN32
        WSACleanup();
#endif

        std::cout << "[MouseClickServer] Stopped" << std::endl;
    }

    /**
     * @brief Установка коллбэка для LMB
     */
    void set_lmb_callback(std::function<void(bool)> callback) {
        on_lmb_pressed = callback;
    }

    /**
     * @brief Установка коллбэка для RMB
     */
    void set_rmb_callback(std::function<void(bool)> callback) {
        on_rmb_pressed = callback;
    }

    /**
     * @brief Установка коллбэка для MMB
     */
    void set_mmb_callback(std::function<void(bool)> callback) {
        on_mmb_pressed = callback;
    }

    bool is_running_status() const {
        return is_running.load();
    }
};
