#pragma once
#ifndef MAKCU_INPUT_H
#define MAKCU_INPUT_H

#include "macku2pc/include/makcu.h"
#include "IMouseInput.h"
#include <atomic>
#include <memory>
#include <functional>
#include <string>
#include <makcu.h>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>

// Глобальные атомарные переменные для состояния кнопок Makcu (2PC режим)
// Эти переменные обновляются в коллбэке и читаются из aimbot.cpp
// naming как в source_logic/source_logic/sunone_aimbot_2.h
// Определения находятся в main.cpp в namespace pwnz_ai
namespace pwnz_ai {
    extern std::atomic<bool>& aiming;    // RMB/SIDE2 - прицеливание (основная клавиша аима)
    extern std::atomic<bool>& shooting;  // LMB - стрельба
    extern std::atomic<bool>& zooming;   // RMB - зум/прицеливание
}

namespace pwnz_ai {

// Команда для отправки в UART поток
struct UartCommand {
    enum class Type { Move, Press, Release, Click } type;
    int x = 0, y = 0;      // для Move
    int button = 0;        // для Press/Release/Click
};

/**
 * @brief Класс для работы с Makcu (ESP32S3) в режиме 2PC
 * 
 * Архитектура:
 * - ПК1 (Игровой): Физическая мышь подключена к MAKCU (правый разъём), устройство считывает нажатия кнопок
 * - ПК2 (Чит): Этот класс получает события от MAKCU через средний USB и эмулирует движения/клики
 * 
 * Порядок инициализации (СТРОГО как в source_logic/source_logic/mouse/Makcu.cpp):
 * 1. Создание объекта makcu::Device
 * 2. Установка коллбэка setMouseButtonCallback()
 * 3. Включение мониторинга enableButtonMonitoring(true) - ДО подключения!
 * 4. Подключение connect(port)
 * 
 * ВАЖНО: Все операции записи в COM-порт выполняются в отдельном потоке UART.
 * Методы Move/Press/Release только ставят команды в очередь и возвращаются сразу.
 */
class MakcuInput : public IMouseInput {
public:
    MakcuInput();
    ~MakcuInput() override;

    /**
     * @brief Инициализация подключения к Makcu
     * @param port COM-порт (например "COM5")
     * @return true если успешно подключено
     */
    bool Init(const std::string& port) override;

    /**
     * @brief Завершение работы и отключение от устройства
     */
    void Shutdown() override;

    /**
     * @brief Движение мыши через Makcu (асинхронно, через очередь UART)
     * @param dx Смещение по X (в counts)
     * @param dy Смещение по Y (в counts)
     */
    void Move(int dx, int dy) override;

    /**
     * @brief Клик кнопкой мыши (асинхронно, через очередь UART)
     * @param button Кнопка мыши
     */
    void Click(MouseButton button) override;

    /**
     * @brief Нажатие кнопки (удержание) (асинхронно, через очередь UART)
     * @param button Кнопка мыши
     */
    void Press(MouseButton button) override;

    /**
     * @brief Отпускание кнопки (асинхронно, через очередь UART)
     * @param button Кнопка мыши
     */
    void Release(MouseButton button) override;

    /**
     * @brief Получить текущее состояние кнопки (нажата/отпущена)
     * @param button Кнопка мыши
     * @return true если кнопка нажата
     */
    bool IsButtonPressed(MouseButton button) const;

    /**
     * @brief Проверка подключения к устройству
     */
    bool IsConnected() const override;

private:
    std::unique_ptr<makcu::Device> m_device;
    
    // Атомарные флаги состояния кнопок (потокобезопасность)
    std::atomic<bool> m_btnLmb{false};
    std::atomic<bool> m_btnRmb{false};
    std::atomic<bool> m_btnMmb{false};
    std::atomic<bool> m_btnSide1{false};
    std::atomic<bool> m_btnSide2{false};

    // === UART ПОТОК ===
    std::thread m_uartThread;
    std::atomic<bool> m_uartRunning{false};
    std::queue<UartCommand> m_uartQueue;
    mutable std::mutex m_uartMutex;
    std::condition_variable m_uartCV;

    // Поток обработки UART команд
    void UartThreadFunc();
    
    // Коллбэк для обработки событий кнопок
    void onMouseButton(makcu::MouseButton button, bool pressed);
    
    // Конвертация MouseButton в int для makcu::Device
    int buttonToInt(MouseButton button) const;
};

} // namespace pwnz_ai

#endif // MAKCU_INPUT_H
