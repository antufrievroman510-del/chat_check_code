#pragma once

#include <atomic>

// Глобальные атомарные переменные для состояния кнопок в аппаратном режиме Makcu
// Эти переменные используются для синхронизации состояния между устройством и логикой аимбота
namespace pwnz_ai {
    extern std::atomic<bool> g_makcu_aiming;      // RMB - прицеливание
    extern std::atomic<bool> g_makcu_shooting;    // LMB - стрельба
    extern std::atomic<bool> g_makcu_zooming;     // MMB - зум
}
