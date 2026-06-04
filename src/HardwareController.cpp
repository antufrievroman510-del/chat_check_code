#include "HardwareController.h"
#include "MakcuWrapper.h"
#include "KMBoxNet.h"
#include <iostream>

// Заглушки для HIDAPI (будут реализованы при наличии библиотеки)
#ifdef HAS_HIDAPI
#include <hidapi/hidapi.h>
#endif

// Внутренние экземпляры контроллеров
static MakcuWrapper g_makcu;
static KMBoxNet g_kmbox;

HardwareController& HardwareController::Instance() {
    static HardwareController instance;
    return instance;
}

bool HardwareController::Initialize(const ::HardwareConfig& config) {
    EnterCriticalSection(&cs_);
    
    current_config_ = config;
    current_mode_ = static_cast<HardwareMode>(config.mode);
    
    bool success = true;

    switch (current_mode_) {
        case HardwareMode::LocalMouse:
            connected_ = true; // Всегда доступно
            std::cout << "[HW] Mode: Local Mouse (SendInput)" << std::endl;
            break;

        case HardwareMode::GHub:
#ifdef HAS_HIDAPI
            // Попытка инициализации HID для GHub
            if (hid_init() == 0) {
                // Поиск устройства Logitech
                // hid_device* handle = hid_open(0x046d, ...); 
                connected_ = true;
                std::cout << "[HW] Mode: GHub Bypass (HID initialized)" << std::endl;
            } else {
                connected_ = false;
                std::cerr << "[HW] GHub HID init failed" << std::endl;
            }
#else
            connected_ = false;
            std::cerr << "[HW] GHub mode requires HIDAPI library!" << std::endl;
#endif
            break;

        case HardwareMode::Razer:
#ifdef HAS_HIDAPI
            // Аналогично для Razer
            if (hid_init() == 0) {
                connected_ = true;
                std::cout << "[HW] Mode: Razer Bypass (HID initialized)" << std::endl;
            } else {
                connected_ = false;
            }
#else
            connected_ = false;
            std::cerr << "[HW] Razer mode requires HIDAPI library!" << std::endl;
#endif
            break;

        case HardwareMode::MackuUART:
            {
                // Подключение через MakcuWrapper класс (автопоиск по VID:PID)
                if (g_makcu.Connect()) {
                    connected_ = true;
                    std::cout << "[HW] Mode: Macku UART (VID:PID 1A86:55D3)" << std::endl;
                    // Мониторинг кнопок запускается автоматически внутри Initialize()
                } else {
                    connected_ = false;
                    std::cerr << "[HW] Macku connection failed" << std::endl;
                }
            }
            break;

        case HardwareMode::KMboxNet:
            {
                // Подключение через KMBoxNet класс
                if (g_kmbox.ConnectToDevice(config.kmbox_ip, config.kmbox_port)) {
                    connected_ = true;
                    std::cout << "[HW] Mode: KMbox Net connected to " << config.kmbox_ip << ":" << config.kmbox_port << std::endl;
                } else {
                    connected_ = false;
                    std::cerr << "[HW] KMbox connection failed" << std::endl;
                }
            }
            break;
    }

    LeaveCriticalSection(&cs_);
    return connected_;
}

void HardwareController::Shutdown() {
    EnterCriticalSection(&cs_);
    
    // Отключение всех устройств (мониторинг останавливается автоматически в Shutdown())
    g_makcu.Disconnect();
    g_kmbox.Disconnect();

    connected_ = false;
    LeaveCriticalSection(&cs_);
}

void HardwareController::MoveMouse(int dx, int dy) {
    if (!connected_) return;

    EnterCriticalSection(&cs_);
    
    switch (current_mode_) {
        case HardwareMode::LocalMouse:
            MoveLocal(dx, dy);
            break;
        case HardwareMode::GHub:
            MoveGHub(dx, dy);
            break;
        case HardwareMode::Razer:
            MoveRazer(dx, dy);
            break;
        case HardwareMode::MackuUART:
            MoveMakcu(dx, dy);
            break;
        case HardwareMode::KMboxNet:
            MoveKMBox(dx, dy);
            break;
    }
    
    LeaveCriticalSection(&cs_);
}

void HardwareController::ClickButton(bool left, bool down) {
    // Реализация кликов для разных режимов
    // Пока используем стандартный метод для всех
    INPUT input = {0};
    input.type = INPUT_MOUSE;
    
    if (left) {
        input.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    } else {
        input.mi.dwFlags = down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
    }
    
    SendInput(1, &input, sizeof(INPUT));
}

void HardwareController::MoveLocal(int dx, int dy) {
    // Стандартное движение через SendInput
    INPUT input = {0};
    input.type = INPUT_MOUSE;
    input.mi.dx = dx;
    input.mi.dy = dy;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;
    
    SendInput(1, &input, sizeof(INPUT));
}

void HardwareController::MoveGHub(int dx, int dy) {
#ifdef HAS_HIDAPI
    // Формат отчета HID для Logitech (примерный)
    // Требуется точная спецификация протокола GHub
    unsigned char report[4];
    report[0] = 0x02; // ID отчета движения
    report[1] = (unsigned char)dx;
    report[2] = (unsigned char)dy;
    report[3] = 0x00;
    
    // hid_write(handle, report, sizeof(report));
#else
    // Fallback на Local если нет HIDAPI
    MoveLocal(dx, dy);
#endif
}

void HardwareController::MoveRazer(int dx, int dy) {
#ifdef HAS_HIDAPI
    // Аналогично для Razer
    MoveLocal(dx, dy); // Fallback
#else
    MoveLocal(dx, dy);
#endif
}

void HardwareController::MoveMakcu(int dx, int dy) {
    // Отправка движения через MakcuWrapper класс
    g_makcu.Move(dx, dy);
}

void HardwareController::MoveKMBox(int dx, int dy) {
    // Отправка движения через KMBoxNet класс
    g_kmbox.MoveMouse(dx, dy);
}

void HardwareController::UpdateConfig(const ::HardwareConfig& config) {
    // Если режим изменился - переинициализация
    if (static_cast<HardwareMode>(config.mode) != current_mode_) {
        Shutdown();
        Initialize(config);
    } else {
        // Обновление параметров без переподключения
        EnterCriticalSection(&cs_);
        current_config_ = config;
        LeaveCriticalSection(&cs_);
    }
}

HardwareController::~HardwareController() {
    Shutdown();
    DeleteCriticalSection(&cs_);
}
