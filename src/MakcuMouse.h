#pragma once
#include "IMouseInput.h"
#include <string>
#include <atomic>

// ============================================
// ВСТРОЕННЫЙ C API ЗАГОЛОВОК ДЛЯ MAKCU
// ============================================
// Это упрощённая версия C API из библиотеки makcu-cpp
// которая совместима с C++17 и не требует C++23
// ============================================

extern "C" {
    // Типы данных из C API
    typedef enum {
        MAKCU_MOUSE_BUTTON_LEFT = 0,
        MAKCU_MOUSE_BUTTON_RIGHT = 1,
        MAKCU_MOUSE_BUTTON_MIDDLE = 2,
        MAKCU_MOUSE_BUTTON_SIDE1 = 3,
        MAKCU_MOUSE_BUTTON_SIDE2 = 4
    } MakcuMouseButton;

    typedef struct {
        int x;
        int y;
    } MakcuMouseDelta;

    // Основные функции C API
    // Возвращает 0 при успехе, отрицательное значение при ошибке
    int makcu_init(const char* port_name);
    int makcu_deinit();
    int makcu_is_connected();
    
    // Движение мыши
    int makcu_move(int dx, int dy);
    
    // Кнопки
    int makcu_press(MakcuMouseButton button);
    int makcu_release(MakcuMouseButton button);
    int makcu_click(MakcuMouseButton button);
    
    // Получение состояния кнопок (для 2PC синхронизации)
    int makcu_get_button_state(MakcuMouseButton button);
}

// Глобальные переменные для синхронизации с 2PC режимом
namespace pwnz_ai {
    extern std::atomic<bool> g_makcu_aiming;    // ПКМ
    extern std::atomic<bool> g_makcu_shooting;  // ЛКМ
    extern std::atomic<bool> g_makcu_zooming;   // СКМ
}

namespace pwnz_ai {

    /**
     * @brief Реализация IMouseInput для Makcu через официальное C API
     * 
     * Эта обёртка позволяет использовать библиотеку makcu-cpp из C++17 проекта
     * без необходимости поднимать стандарт до C++23.
     */
    class MakcuMouse : public IMouseInput {
    public:
        explicit MakcuMouse(const std::string& com_port = "COM3");
        ~MakcuMouse() override;

        bool Init() override;
        void Move(int dx, int dy) override;
        void Click(int button) override;
        void Press(int button) override;
        void Release(int button) override;
        void Shutdown() override;

        void SetPort(const std::string& port);
        bool IsConnected() const;

    private:
        std::string m_com_port;
        bool m_initialized;
        
        // Вспомогательный метод для конвертации номера кнопки
        MakcuMouseButton IntToButton(int button);
    };

} // namespace pwnz_ai
