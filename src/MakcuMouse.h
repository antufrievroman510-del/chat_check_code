#pragma once
#include "IMouseInput.h"
#include "MakcuUART.h"
#include <string>

namespace pwnz_ai {

    /**
     * @brief Реализация интерфейса IMouseInput для устройства Makcu (через UART/COM).
     * Использует существующий класс MakcuUART для отправки команд.
     */
    class MakcuMouse : public IMouseInput {
    public:
        /**
         * @brief Конструктор.
         * @param com_port Имя COM-порта (например, "COM3").
         * @param baud_rate Скорость передачи (по умолчанию 9600).
         */
        explicit MakcuMouse(const std::string& com_port = "COM3", int baud_rate = 9600);
        ~MakcuMouse() override;

        // Реализация интерфейса IMouseInput
        bool Init() override;
        void Move(int dx, int dy) override;
        void Click(int button) override;
        void Press(int button) override;  // Нажатие кнопки (удержание)
        void Release(int button) override;  // Отпускание кнопки
        void Shutdown() override;

        // Сеттеры для изменения настроек на лету (если потребуется)
        void SetPort(const std::string& port);
        void SetBaudRate(int rate);

    private:
        std::string m_com_port;
        int m_baud_rate;
        MakcuUART m_uart; // Внутренний объект для работы с железом
        bool m_initialized;
    };

} // namespace pwnz_ai
