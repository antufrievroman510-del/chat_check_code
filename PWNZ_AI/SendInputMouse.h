#pragma once
#include "IMouseInput.h"
#include <Windows.h>

namespace pwnz_ai {

/**
 * @brief Реализация программного ввода мыши через Windows API SendInput.
 * 
 * Использует стандартный системный вызов SendInput для эмуляции движения
 * и нажатий мыши. Не требует дополнительного оборудования.
 */
class SendInputMouse : public IMouseInput {
public:
    SendInputMouse() = default;
    ~SendInputMouse() noexcept override = default;

    // Запрет копирования
    SendInputMouse(const SendInputMouse&) = delete;
    SendInputMouse& operator=(const SendInputMouse&) = delete;

    bool Init() override;
    void Move(int dx, int dy) override;
    void Click(int button) override;
    void Press(int button) override;  // Нажатие кнопки (удержание)
    void Release(int button) override;  // Отпускание кнопки
    void Shutdown() override;

private:
    /**
     * @brief Отправка события мыши через SendInput
     * @param flags Флаги события (MOVE, LBUTTONDOWN, LBUTTONUP и т.д.)
     * @param dx Смещение по X (если применимо)
     * @param dy Смещение по Y (если применимо)
     */
    void SendMouseInput(DWORD flags, int dx = 0, int dy = 0);

    bool m_initialized = false;
};

} // namespace pwnz_ai
