#include "HardwareController.h"
#include <iostream>
#include <ws2tcpip.h> // Для сокетов

#pragma comment(lib, "Ws2_32.lib") // Библиотека для сокетов

// Заглушки для HIDAPI (будут реализованы при наличии библиотеки)
#ifdef HAS_HIDAPI
#include <hidapi/hidapi.h>
#endif

HardwareController& HardwareController::Instance() {
    static HardwareController instance;
    return instance;
}

bool HardwareController::Initialize(const HardwareConfig& config) {
    EnterCriticalSection(&cs_);
    
    current_config_ = config;
    current_mode_ = config.mode;
    
    bool success = true;

    switch (current_mode_) {
        case HardwareMode::LocalMouse:
            connected_ = true; // Всегда доступно
            std::cout << "[HW] Mode: Local Mouse (SendInput)" << std::endl;
            break;

        case HardwareMode::GHubBypass:
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

        case HardwareMode::RazerBypass:
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

        case HardwareMode::MakcuUART:
            {
                // Открытие COM порта
                std::string portPath = "\\\\.\\" + config.makcu_port;
                hMakcu = CreateFileA(
                    portPath.c_str(),
                    GENERIC_READ | GENERIC_WRITE,
                    0,
                    nullptr,
                    OPEN_EXISTING,
                    0,
                    nullptr
                );

                if (hMakcu != INVALID_HANDLE_VALUE) {
                    // Настройка baudrate и других параметров
                    DCB dcb = {0};
                    dcb.DCBlength = sizeof(DCB);
                    if (GetCommState((HANDLE)hMakcu, &dcb)) {
                        dcb.BaudRate = config.makcu_baudrate;
                        dcb.ByteSize = 8;
                        dcb.StopBits = ONESTOPBIT;
                        dcb.Parity = NOPARITY;
                        SetCommState((HANDLE)hMakcu, &dcb);
                    }
                    connected_ = true;
                    std::cout << "[HW] Mode: Makcu UART on " << config.makcu_port << std::endl;
                } else {
                    connected_ = false;
                    std::cerr << "[HW] Makcu COM port open failed: " << GetLastError() << std::endl;
                }
            }
            break;

        case HardwareMode::KMBoxNet:
            {
                // Инициализация Winsock
                WSADATA wsaData;
                if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
                    connected_ = false;
                    break;
                }

                struct addrinfo hints = {}, *result = nullptr;
                hints.ai_family = AF_INET;
                hints.ai_socktype = SOCK_STREAM;
                
                std::string portStr = std::to_string(config.kmbox_port);
                if (getaddrinfo(config.kmbox_ip.c_str(), portStr.c_str(), &hints, &result) == 0) {
                    hKMBoxSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
                    if (hKMBoxSocket != INVALID_SOCKET) {
                        if (connect(hKMBoxSocket, result->ai_addr, (int)result->ai_addrlen) == 0) {
                            connected_ = true;
                            std::cout << "[HW] Mode: KMbox Net connected to " << config.kmbox_ip << ":" << config.kmbox_port << std::endl;
                        } else {
                            closesocket(hKMBoxSocket);
                            hKMBoxSocket = INVALID_SOCKET;
                            connected_ = false;
                        }
                    }
                }
                if (result) freeaddrinfo(result);
            }
            break;
    }

    LeaveCriticalSection(&cs_);
    return connected_;
}

void HardwareController::Shutdown() {
    EnterCriticalSection(&cs_);
    
    if (hMakcu != nullptr && hMakcu != INVALID_HANDLE_VALUE) {
        CloseHandle((HANDLE)hMakcu);
        hMakcu = nullptr;
    }
    
    if (hKMBoxSocket != INVALID_SOCKET) {
        shutdown(hKMBoxSocket, SD_BOTH);
        closesocket(hKMBoxSocket);
        hKMBoxSocket = INVALID_SOCKET;
        WSACleanup();
    }

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
        case HardwareMode::GHubBypass:
            MoveGHub(dx, dy);
            break;
        case HardwareMode::RazerBypass:
            MoveRazer(dx, dy);
            break;
        case HardwareMode::MakcuUART:
            MoveMakcu(dx, dy);
            break;
        case HardwareMode::KMBoxNet:
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
    if (hMakcu == nullptr || hMakcu == INVALID_HANDLE_VALUE) return;

    // Протокол Makcu (пример):
    // Команда: 0xAA 0x01 DX DY 0xBB
    unsigned char buffer[5];
    buffer[0] = 0xAA; // Start byte
    buffer[1] = 0x01; // Command: Move
    buffer[2] = (unsigned char)(dx & 0xFF);
    buffer[3] = (unsigned char)(dy & 0xFF);
    buffer[4] = 0xBB; // End byte
    
    DWORD bytesWritten;
    WriteFile((HANDLE)hMakcu, buffer, sizeof(buffer), &bytesWritten, nullptr);
}

void HardwareController::MoveKMBox(int dx, int dy) {
    if (hKMBoxSocket == INVALID_SOCKET) return;

    // Протокол KMbox Net (JSON или бинарный)
    // Пример бинарного пакета:
    struct KMBoxPacket {
        uint8_t header = 0x5A;
        uint8_t cmd = 0x01; // Move
        int16_t dx;
        int16_t dy;
        uint8_t checksum;
    };
    
    KMBoxPacket packet;
    packet.dx = static_cast<int16_t>(dx);
    packet.dy = static_cast<int16_t>(dy);
    packet.checksum = packet.header ^ packet.cmd ^ (dx & 0xFF) ^ (dy & 0xFF);
    
    send(hKMBoxSocket, (const char*)&packet, sizeof(packet), 0);
}

void HardwareController::UpdateConfig(const HardwareConfig& config) {
    // Если режим изменился - переинициализация
    if (config.mode != current_mode_) {
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
