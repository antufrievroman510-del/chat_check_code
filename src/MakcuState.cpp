#include "MakcuState.h"

namespace pwnz_ai {
    // Определение и инициализация глобальных атомарных переменных
    std::atomic<bool> g_makcu_aiming(false);      // RMB - прицеливание
    std::atomic<bool> g_makcu_shooting(false);    // LMB - стрельба
    std::atomic<bool> g_makcu_zooming(false);     // MMB - зум
}
