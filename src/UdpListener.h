#pragma once

#include <atomic>
#include <thread>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

class UdpMouseListener {
public:
    UdpMouseListener();
    ~UdpMouseListener();

    // Запуск потока прослушивания
    void Start(int port = 9999);
    // Остановка
    void Stop();

    // Получение состояния кнопок (возвращает true, если кнопка сейчас нажата)
    // Используем простую логику: получили байт нажатия -> флаг true, получили байт отпускания -> флаг false
    bool IsAimKeyPressed() const { return m_aim_pressed.load(); }
    bool IsShootKeyPressed() const { return m_shoot_pressed.load(); }
    bool IsZoomKeyPressed() const { return m_zoom_pressed.load(); }
    bool IsSide1Pressed() const { return m_side1_pressed.load(); }
    bool IsSide2Pressed() const { return m_side2_pressed.load(); }

private:
    void ListenThreadFunc();

    SOCKET m_sock;
    std::thread m_thread;
    std::atomic<bool> m_running;
    
    // Флаги состояний кнопок
    std::atomic<bool> m_aim_pressed;   // ЛКМ
    std::atomic<bool> m_shoot_pressed; // ПКМ
    std::atomic<bool> m_zoom_pressed;  // Колесо (опционально)
    std::atomic<bool> m_side1_pressed; // Боковая 1
    std::atomic<bool> m_side2_pressed; // Боковая 2
};
