#include "SendInputMouse.h"

namespace pwnz_ai {

bool SendInputMouse::Init(const std::string& /*port*/) {
    // Для SendInput не требуется специальной инициализации
    // Функция SendInput доступна напрямую из user32.dll
    m_initialized = true;
    return true;
}

void SendInputMouse::Move(int dx, int dy) {
    if (!m_initialized) {
        return;
    }
    
    // Отправляем событие перемещения мыши
    SendMouseInput(MOUSEEVENTF_MOVE, dx, dy);
}

void SendInputMouse::Click(MouseButton button) {
    if (!m_initialized) {
        return;
    }

    DWORD downFlag = 0;
    DWORD upFlag = 0;

    // Определяем флаги для нажатия и отпускания кнопки
    switch (button) {
        case MouseButton::LEFT:
            downFlag = MOUSEEVENTF_LEFTDOWN;
            upFlag = MOUSEEVENTF_LEFTUP;
            break;
        case MouseButton::RIGHT:
            downFlag = MOUSEEVENTF_RIGHTDOWN;
            upFlag = MOUSEEVENTF_RIGHTUP;
            break;
        case MouseButton::MIDDLE:
            downFlag = MOUSEEVENTF_MIDDLEDOWN;
            upFlag = MOUSEEVENTF_MIDDLEUP;
            break;
        default:
            return; // Неизвестная кнопка
    }

    // Эмулируем нажатие и отпускание
    SendMouseInput(downFlag);
    SendMouseInput(upFlag);
}

void SendInputMouse::Press(MouseButton button) {
    if (!m_initialized) {
        return;
    }

    DWORD downFlag = 0;

    // Определяем флаг нажатия кнопки
    switch (button) {
        case MouseButton::LEFT:
            downFlag = MOUSEEVENTF_LEFTDOWN;
            break;
        case MouseButton::RIGHT:
            downFlag = MOUSEEVENTF_RIGHTDOWN;
            break;
        case MouseButton::MIDDLE:
            downFlag = MOUSEEVENTF_MIDDLEDOWN;
            break;
        default:
            return; // Неизвестная кнопка
    }

    // Эмулируем только нажатие (удержание)
    SendMouseInput(downFlag);
}

void SendInputMouse::Release(MouseButton button) {
    if (!m_initialized) {
        return;
    }

    DWORD upFlag = 0;

    // Определяем флаг отпускания кнопки
    switch (button) {
        case MouseButton::LEFT:
            upFlag = MOUSEEVENTF_LEFTUP;
            break;
        case MouseButton::RIGHT:
            upFlag = MOUSEEVENTF_RIGHTUP;
            break;
        case MouseButton::MIDDLE:
            upFlag = MOUSEEVENTF_MIDDLEUP;
            break;
        default:
            return; // Неизвестная кнопка
    }

    // Эмулируем только отпускание
    SendMouseInput(upFlag);
}

void SendInputMouse::Shutdown() {
    m_initialized = false;
    // Для SendInput не требуется освобождения ресурсов
}

void SendInputMouse::SendMouseInput(DWORD flags, int dx, int dy) {
    INPUT input = {0};
    input.type = INPUT_MOUSE;
    input.mi.dx = dx;
    input.mi.dy = dy;
    input.mi.dwFlags = flags;
    input.mi.time = 0;
    input.mi.dwExtraInfo = 0;

    // Отправляем событие через SendInput
    ::SendInput(1, &input, sizeof(INPUT));
}

} // namespace pwnz_ai
