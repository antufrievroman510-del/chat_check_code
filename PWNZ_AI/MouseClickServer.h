#pragma once

#include "WinHeaders.h"

#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <iostream>
#include <functional>

namespace pwnz_ai {

/**
 * @brief Сервер для приема нажатий кнопок мыши с первого ПК
 * 
 * Архитектура 2PC:
 * - ПК1 (Игровой): Физическая мышь -> Перехват хуком -> Отправляет клики по сети
 * - ПК2 (Чит): Этот сервер получает клики и эмулирует их через SendInput
 * 
 * Важно:
 * - Движения мыши обрабатываются отдельно через MakcuInput (COM-порт)
 * - Этот сервер принимает ТОЛЬКО нажатия кнопок
 * - Использует UDP порт 5556 (отдельно от aim_data порта 5555)
 * 
 * Поддерживаемые события:
 * - ЛКМ, ПКМ, СКМ (нажатие/отпускание)
 * - Колесо прокрутки (вверх/вниз)
 * - Боковые кнопки X1 (Назад), X2 (Вперед)
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
    std::function<void(int)> on_wheel_scrolled = nullptr; // +120 вверх, -120 вниз
    std::function<void(bool)> on_x1_pressed = nullptr;
    std::function<void(bool)> on_x2_pressed = nullptr;

    // Типы событий (должны совпадать с MouseClickSender.cpp)
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

    void receive_loop() {
        std::cout << "[MouseClickServer] Listening on port " << listen_port << std::endl;

        while (is_running.load()) {
            char buffer[4]; // 4 байта: event_type(1) + reserved(1) + wheel_delta(2)
            sockaddr_in client_addr;
            int client_addr_size = sizeof(client_addr);

            int result = recvfrom(
                sock,
                buffer,
                sizeof(buffer),
                0,
                (sockaddr*)&client_addr,
                &client_addr_size
            );

            if (result == SOCKET_ERROR || result != sizeof(buffer)) {
                continue;
            }

            uint8_t event_type = buffer[0];
            int16_t wheel_delta = ntohs(*(int16_t*)&buffer[2]);

            // Обработка полученного пакета в зависимости от типа события
            switch (event_type) {
                case MOUSE_EVENT_LMB_DOWN:
                    if (on_lmb_pressed) on_lmb_pressed(true);
                    break;
                case MOUSE_EVENT_LMB_UP:
                    if (on_lmb_pressed) on_lmb_pressed(false);
                    break;
                case MOUSE_EVENT_RMB_DOWN:
                    if (on_rmb_pressed) on_rmb_pressed(true);
                    break;
                case MOUSE_EVENT_RMB_UP:
                    if (on_rmb_pressed) on_rmb_pressed(false);
                    break;
                case MOUSE_EVENT_MMB_DOWN:
                    if (on_mmb_pressed) on_mmb_pressed(true);
                    break;
                case MOUSE_EVENT_MMB_UP:
                    if (on_mmb_pressed) on_mmb_pressed(false);
                    break;
                case MOUSE_EVENT_WHEEL_UP:
                    if (on_wheel_scrolled) on_wheel_scrolled(120);
                    break;
                case MOUSE_EVENT_WHEEL_DOWN:
                    if (on_wheel_scrolled) on_wheel_scrolled(-120);
                    break;
                case MOUSE_EVENT_X1_DOWN:
                    if (on_x1_pressed) on_x1_pressed(true);
                    break;
                case MOUSE_EVENT_X1_UP:
                    if (on_x1_pressed) on_x1_pressed(false);
                    break;
                case MOUSE_EVENT_X2_DOWN:
                    if (on_x2_pressed) on_x2_pressed(true);
                    break;
                case MOUSE_EVENT_X2_UP:
                    if (on_x2_pressed) on_x2_pressed(false);
                    break;
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

    /**
     * @brief Установка коллбэка для колеса прокрутки
     * @param callback Функция, принимающая delta (+120 вверх, -120 вниз)
     */
    void set_wheel_callback(std::function<void(int)> callback) {
        on_wheel_scrolled = callback;
    }

    /**
     * @brief Установка коллбэка для боковой кнопки X1 (Назад)
     */
    void set_x1_callback(std::function<void(bool)> callback) {
        on_x1_pressed = callback;
    }

    /**
     * @brief Установка коллбэка для боковой кнопки X2 (Вперед)
     */
    void set_x2_callback(std::function<void(bool)> callback) {
        on_x2_pressed = callback;
    }

    bool is_running_status() const {
        return is_running.load();
    }
};

} // namespace pwnz_ai
