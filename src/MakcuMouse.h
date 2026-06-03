#pragma once
#include "IMouseInput.h"
#include <string>
#include <atomic>

// ============================================
// ПОДКЛЮЧЕНИЕ ОФИЦИАЛЬНОГО C API MAKCU
// ============================================
// Используем C API из библиотеки makcu-cpp для совместимости с C++17
// Заголовочный файл находится в: /workspace/makcu-cpp/makcu-cpp/include/makcu_c.h
// ============================================

extern "C" {
#include <makcu_c.h>
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
        makcu_device_t* m_device;  // Указатель на устройство из C API
        bool m_initialized;
        
        // Вспомогательный метод для конвертации номера кнопки
        makcu_mouse_button_t IntToButton(int button);
        
        // Обновление состояния кнопок для 2PC синхронизации
        void UpdateButtonState(makcu_mouse_button_t button, bool pressed);
    };

} // namespace pwnz_ai
