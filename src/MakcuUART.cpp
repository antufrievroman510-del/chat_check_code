#include "MakcuUART.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <vector>

// Определение глобальных переменных состояния кнопок
namespace pwnz_ai {
    std::atomic<bool> g_makcu_aiming{false};    // Состояние ПКМ (прицеливание) - для 2PC
    std::atomic<bool> g_makcu_shooting{false};  // Состояние ЛКМ (стрельба) - для 2PC
    std::atomic<bool> g_makcu_zooming{false};   // Состояние СКМ (зум) - для 2PC
}

// Глобальная переменная для 2PC-связки (объявлена в main.cpp)
extern std::atomic<bool> g_remote_aim_key;

// ============================================================
// ПРОТОКОЛ MAKCU NATIVE API (согласно https://makcu.k4tech.net/native)
// ============================================================
// Команды (передаются как ASCII текст):
//   km.version()\r\n          - запрос версии прошивки
//   km.move(dx,dy)\r\n        - движение мыши (dx, dy - int16)
//   km.left(1)\r\n            - нажать ЛКМ
//   km.left(0)\r\n            - отпустить ЛКМ
//   km.right(1)\r\n           - нажать ПКМ
//   km.right(0)\r\n           - отпустить ПКМ
//   km.middle(1)\r\n          - нажать СКМ
//   km.middle(0)\r\n          - отпустить СКМ
//   km.buttons(1)\r\n         - включить стрим событий кнопок
//   km.buttons(0)\r\n         - выключить стрим событий кнопок
//
// Ответы устройства:
//   - Все ответы заканчиваются промптом ">>>" [reference:1]
//   - Пример: "km.version()\r\nMAKCU v3.7\r\n>>>" 
//   - События кнопок: префикс "km." (6B 6D 2E) + 1 байт маска кнопок [reference:5]
//
// Маска кнопок (биты):
//   Бит 0 - левая кнопка (ЛКМ)
//   Бит 1 - правая кнопка (ПКМ)  
//   Бит 2 - средняя кнопка (СКМ)
//   Бит 3 - боковая 1
//   Бит 4 - боковая 2
//
// Последовательность подключения [reference:3]:
//   1. Открыть порт на 4 Мбит/с (4000000)
//   2. Отправить km.version()\r\n
//   3. Если в ответе есть "km.MAKCU" - связь установлена
//   4. Если нет - выполнить смену скорости:
//      a. Открыть порт на 115200
//      b. Отправить бинарный фрейм смены скорости: DE AD 05 00 A5 00 09 3D 00
//      c. Ждать 100 мс
//      d. Закрыть порт
//      e. Открыть порт на 4 Мбит/с
//      f. Ждать 50 мс, сбросить входной буфер
//      g. Отправить km.version()\r\n
//      h. Если ответа нет - подключение не удалось

MakcuUART::MakcuUART() 
    : hComPort(nullptr), isConnected(false), packetDelayMs(1), m_portName("COM3"), m_baudRate(115200) {}

MakcuUART::~MakcuUART() {
    Shutdown();
}

bool MakcuUART::Init() {
    return Connect(m_portName, m_baudRate);
}

void MakcuUART::Move(int dx, int dy) {
    MoveMouse(dx, dy);
}

void MakcuUART::Click(int button) {
    // Для текстового протокола клик = press + release с небольшой задержкой
    PressButton(button);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ReleaseButton(button);
}

void MakcuUART::Shutdown() {
    Disconnect();
}

bool MakcuUART::Connect(const std::string& portName, int baudRate) {
    std::lock_guard<std::mutex> lock(mtx);
    
    if (isConnected) {
        Disconnect();
    }

    // Формируем имя порта для Windows
    std::string fullPortName = "\\\\.\\" + portName;
    
    std::cout << "[MakcuUART] Starting connection sequence for: " << fullPortName << std::endl;
    
    // ============================================================
    // ШАГ 1: Пробуем подключиться сразу на 4 Мбит/с [reference:3]
    // ============================================================
    const int HIGH_SPEED_BAUD = 4000000;  // 4 Мбит/с - рабочая скорость MAKCU
    const int LOW_SPEED_BAUD = 115200;     // Скорость по умолчанию
    
    hComPort = CreateFileA(
        fullPortName.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,          // No sharing
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr
    );

    if (hComPort == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        hComPort = nullptr;
        std::cerr << "[MakcuUART] Failed to open COM port: " << portName 
                  << " (Error: " << err << ")" << std::endl;
        return false;
    }

    // Настраиваем порт на 4 Мбит/с
    DCB dcbSerialParams = { 0 };
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

    if (!GetCommState(hComPort, &dcbSerialParams)) {
        Disconnect();
        return false;
    }

    dcbSerialParams.BaudRate = HIGH_SPEED_BAUD;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;

    if (!SetCommState(hComPort, &dcbSerialParams)) {
        Disconnect();
        return false;
    }

    // Таймауты для чтения/записи
    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 100;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 100;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    SetCommTimeouts(hComPort, &timeouts);

    // Очистка буферов
    PurgeComm(hComPort, PURGE_TXCLEAR | PURGE_RXCLEAR);
    
    // Включаем DTR и RTS
    dcbSerialParams.fDtrControl = DTR_CONTROL_ENABLE;
    dcbSerialParams.fRtsControl = RTS_CONTROL_ENABLE;
    SetCommState(hComPort, &dcbSerialParams);
    
    Sleep(50); // Задержка для стабилизации

    // ============================================================
    // ШАГ 2: Отправляем km.version() и проверяем ответ [reference:3]
    // ============================================================
    std::cout << "[MakcuUART] Sending km.version() at 4 Mbit/s..." << std::endl;
    
    // Очищаем буфер перед отправкой
    PurgeComm(hComPort, PURGE_RXCLEAR);
    Sleep(10);
    
    // Отправляем команду версии
    const char* versionCmd = "km.version()\r\n";
    DWORD bytesWritten;
    WriteFile(hComPort, versionCmd, static_cast<DWORD>(strlen(versionCmd)), &bytesWritten, nullptr);
    
    // Ждём ответ
    Sleep(100);
    
    // Читаем ответ
    char responseBuffer[256] = { 0 };
    DWORD bytesRead = 0;
    ReadFile(hComPort, responseBuffer, sizeof(responseBuffer) - 1, &bytesRead, nullptr);
    responseBuffer[bytesRead] = '\0';
    
    std::cout << "[MakcuUART] Response: " << responseBuffer << std::endl;
    
    // Проверяем наличие "km.MAKCU" в ответе
    bool isConnectedAtHighSpeed = (strstr(responseBuffer, "km.MAKCU") != nullptr || 
                                   strstr(responseBuffer, "MAKCU") != nullptr);
    
    if (isConnectedAtHighSpeed) {
        std::cout << "[MakcuUART] Successfully connected at 4 Mbit/s!" << std::endl;
        isConnected = true;
        m_portName = portName;
        m_baudRate = HIGH_SPEED_BAUD;
        
        // Запускаем поток мониторинга кнопок
        StartMonitoring();
        
        // Включаем стрим событий кнопок [reference:4]
        Sleep(50);
        WriteCommand("km.buttons(1)\r\n");
        std::cout << "[MakcuUART] Enabled button event stream (km.buttons(1))" << std::endl;
        
        return true;
    }
    
    // ============================================================
    // ШАГ 3: Если не удалось - выполняем смену скорости [reference:3]
    // ============================================================
    std::cout << "[MakcuUART] High speed connection failed. Trying baud rate switch sequence..." << std::endl;
    
    // Закрываем порт
    CloseHandle(hComPort);
    hComPort = nullptr;
    Sleep(50);
    
    // Открываем порт на 115200
    hComPort = CreateFileA(
        fullPortName.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr
    );
    
    if (hComPort == INVALID_HANDLE_VALUE) {
        std::cerr << "[MakcuUART] Failed to open port at 115200 for speed switch" << std::endl;
        return false;
    }
    
    // Настраиваем на 115200
    dcbSerialParams = { 0 };
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
    GetCommState(hComPort, &dcbSerialParams);
    dcbSerialParams.BaudRate = LOW_SPEED_BAUD;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;
    SetCommState(hComPort, &dcbSerialParams);
    
    // Отправляем бинарный фрейм смены скорости [reference:3]
    // DE AD 05 00 A5 00 09 3D 00
    const uint8_t speedSwitchFrame[] = { 0xDE, 0xAD, 0x05, 0x00, 0xA5, 0x00, 0x09, 0x3D, 0x00 };
    WriteFile(hComPort, speedSwitchFrame, sizeof(speedSwitchFrame), &bytesWritten, nullptr);
    std::cout << "[MakcuUART] Sent speed switch binary frame" << std::endl;
    
    // Ждём 100 мс [reference:3]
    Sleep(100);
    
    // Закрываем порт
    CloseHandle(hComPort);
    hComPort = nullptr;
    Sleep(50);
    
    // Открываем порт на 4 Мбит/с
    hComPort = CreateFileA(
        fullPortName.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr
    );
    
    if (hComPort == INVALID_HANDLE_VALUE) {
        std::cerr << "[MakcuUART] Failed to reopen port at 4 Mbit/s after speed switch" << std::endl;
        return false;
    }
    
    // Настраиваем на 4 Мбит/с
    dcbSerialParams = { 0 };
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
    GetCommState(hComPort, &dcbSerialParams);
    dcbSerialParams.BaudRate = HIGH_SPEED_BAUD;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;
    SetCommState(hComPort, &dcbSerialParams);
    
    // Ждём 50 мс, сбрасываем входной буфер [reference:3]
    Sleep(50);
    PurgeComm(hComPort, PURGE_RXCLEAR);
    
    // Включаем DTR и RTS
    dcbSerialParams.fDtrControl = DTR_CONTROL_ENABLE;
    dcbSerialParams.fRtsControl = RTS_CONTROL_ENABLE;
    SetCommState(hComPort, &dcbSerialParams);
    
    // Отправляем km.version() ещё раз
    std::cout << "[MakcuUART] Sending km.version() after speed switch..." << std::endl;
    PurgeComm(hComPort, PURGE_RXCLEAR);
    Sleep(10);
    
    WriteFile(hComPort, versionCmd, static_cast<DWORD>(strlen(versionCmd)), &bytesWritten, nullptr);
    
    // Ждём ответ
    Sleep(100);
    
    // Читаем ответ
    bytesRead = 0;
    memset(responseBuffer, 0, sizeof(responseBuffer));
    ReadFile(hComPort, responseBuffer, sizeof(responseBuffer) - 1, &bytesRead, nullptr);
    responseBuffer[bytesRead] = '\0';
    
    std::cout << "[MakcuUART] Response after speed switch: " << responseBuffer << std::endl;
    
    // Проверяем ответ
    bool isConnectedAfterSwitch = (strstr(responseBuffer, "km.MAKCU") != nullptr || 
                                   strstr(responseBuffer, "MAKCU") != nullptr);
    
    if (!isConnectedAfterSwitch) {
        std::cerr << "[MakcuUART] Connection failed after speed switch sequence" << std::endl;
        Disconnect();
        return false;
    }
    
    // Успешное подключение
    std::cout << "[MakcuUART] Successfully connected after speed switch!" << std::endl;
    isConnected = true;
    m_portName = portName;
    m_baudRate = HIGH_SPEED_BAUD;
    
    // Запускаем поток мониторинга кнопок
    StartMonitoring();
    
    // Включаем стрим событий кнопок [reference:4]
    Sleep(50);
    WriteCommand("km.buttons(1)\r\n");
    std::cout << "[MakcuUART] Enabled button event stream (km.buttons(1))" << std::endl;
    
    return true;
}

void MakcuUART::Disconnect() {
    std::lock_guard<std::mutex> lock(mtx);
    
    StopMonitoring();
    
    if (hComPort != nullptr) {
        // Отключаем стрим кнопок перед закрытием
        WriteCommand("km.buttons(0)\r\n");
        Sleep(50);  // Даём время на отправку команды
        
        CloseHandle(hComPort);
        hComPort = nullptr;
    }
    isConnected = false;
    std::cout << "[MakcuUART] Disconnected" << std::endl;
}

bool MakcuUART::IsConnected() const {
    return isConnected;
}

bool MakcuUART::MoveMouse(int dx, int dy) {
    if (!isConnected || hComPort == nullptr) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mtx);

    // Формируем текстовую команду движения
    std::ostringstream cmd;
    cmd << "km.move(" << dx << "," << dy << ")\r\n";
    
    bool result = WriteCommand(cmd.str().c_str());
    
    // DEBUG логирование для отладки 2PC-связки
#ifdef _DEBUG
    if (result) {
        std::cout << "[MakcuUART] Sent move: (" << dx << ", " << dy << ")" << std::endl;
    }
#endif
    
    if (result && packetDelayMs > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(packetDelayMs));
    }

    return result;
}

bool MakcuUART::PressButton(int button) {
    if (!isConnected || hComPort == nullptr) {
        std::cerr << "[MakcuUART] PressButton: Not connected!" << std::endl;
        return false;
    }

    std::lock_guard<std::mutex> lock(mtx);

    // Определяем команду кнопки
    const char* cmd = nullptr;
    switch (button) {
        case 0: cmd = "km.left(1)\r\n"; break;   // ЛКМ
        case 1: cmd = "km.right(1)\r\n"; break;  // ПКМ
        case 2: cmd = "km.middle(1)\r\n"; break; // СКМ
        default: cmd = "km.left(1)\r\n"; break;
    }
    
    bool result = WriteCommand(cmd);
    
    if (result) {
        // Обновляем локальное состояние
        switch (button) {
            case 0: m_lmb_pressed.store(true); break;
            case 1: m_rmb_pressed.store(true); break;
            case 2: m_mmb_pressed.store(true); break;
        }
        std::cout << "[MakcuUART] Press button " << button << std::endl;
    }
    
    return result;
}

bool MakcuUART::ReleaseButton(int button) {
    if (!isConnected || hComPort == nullptr) {
        std::cerr << "[MakcuUART] ReleaseButton: Not connected!" << std::endl;
        return false;
    }

    std::lock_guard<std::mutex> lock(mtx);

    // Определяем команду отпускания кнопки
    const char* cmd = nullptr;
    switch (button) {
        case 0: cmd = "km.left(0)\r\n"; break;   // ЛКМ
        case 1: cmd = "km.right(0)\r\n"; break;  // ПКМ
        case 2: cmd = "km.middle(0)\r\n"; break; // СКМ
        default: cmd = "km.left(0)\r\n"; break;
    }
    
    bool result = WriteCommand(cmd);
    
    if (result) {
        // Обновляем локальное состояние
        switch (button) {
            case 0: m_lmb_pressed.store(false); break;
            case 1: m_rmb_pressed.store(false); break;
            case 2: m_mmb_pressed.store(false); break;
        }
        std::cout << "[MakcuUART] Release button " << button << std::endl;
    }
    
    return result;
}

void MakcuUART::SetPacketDelayMs(int ms) {
    packetDelayMs = (ms < 0) ? 0 : ms;
}

bool MakcuUART::WriteCommand(const char* command) {
    size_t len = strlen(command);
    DWORD bytesWritten;
    
    // Записываем команду в COM-порт
    BOOL result = WriteFile(hComPort, command, static_cast<DWORD>(len), &bytesWritten, nullptr);
    
    if (!result || bytesWritten != len) {
        DWORD err = GetLastError();
        std::cerr << "[MakcuUART] Write failed: " << err << " (written=" << bytesWritten 
                  << ", expected=" << len << ")" << std::endl;
        Disconnect();
        return false;
    }
    
    // Логирование для отладки 2PC-связки
#ifdef _DEBUG
    std::cout << "[MakcuUART] Sent command: " << command << std::endl;
#endif
    // ВАЖНО: НЕ используем FlushFileBuffers здесь!
    // Это блокирует асинхронную отправку и нарушает синхронизацию с устройством.
    // Устройство должно само обработать команду и отправить ответ >>>
    
    return true;
}

void MakcuUART::StartMonitoring() {
    if (m_monitoring.load()) {
        return;
    }
    
    m_stopMonitoring.store(false);
    m_monitorThread = std::thread(&MakcuUART::monitoringLoop, this);
    m_monitoring.store(true);
    
    std::cout << "[MakcuUART] Button monitoring started" << std::endl;
}

void MakcuUART::StopMonitoring() {
    if (!m_monitoring.load()) {
        return;
    }
    
    m_stopMonitoring.store(true);
    
    if (m_monitorThread.joinable()) {
        m_monitorThread.join();
    }
    
    m_monitoring.store(false);
    
    std::cout << "[MakcuUART] Button monitoring stopped" << std::endl;
}

void MakcuUART::monitoringLoop() {
    std::cout << "[MakcuUART] Monitoring loop started" << std::endl;
    
    // Настраиваем таймауты для неблокирующего чтения
    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    SetCommTimeouts(hComPort, &timeouts);
    
    // Буфер для чтения сырых байтов
    uint8_t buffer[512];
    
    // Буфер для накопления данных между итерациями
    std::vector<uint8_t> leftover;
    
    // Префикс событий кнопок: "km." = 0x6B 0x6D 0x2E [reference:5]
    const uint8_t KM_PREFIX[3] = {0x6B, 0x6D, 0x2E};
    
    while (!m_stopMonitoring.load() && isConnected && hComPort != nullptr) {
        DWORD bytesRead = 0;
        BOOL readResult = ReadFile(hComPort, buffer, sizeof(buffer), &bytesRead, nullptr);
        
        if (readResult && bytesRead > 0) {
            // Добавляем новые данные к остатку
            leftover.insert(leftover.end(), buffer, buffer + bytesRead);
            
            // Ищем префикс "km." в потоке данных
            while (leftover.size() >= 4) {  // Минимум 4 байта: префикс (3) + маска (1)
                // Ищем позицию префикса
                size_t prefixPos = 0;
                bool found = false;
                
                for (size_t i = 0; i <= leftover.size() - 3; i++) {
                    if (leftover[i] == KM_PREFIX[0] && 
                        leftover[i+1] == KM_PREFIX[1] && 
                        leftover[i+2] == KM_PREFIX[2]) {
                        prefixPos = i;
                        found = true;
                        break;
                    }
                }
                
                if (!found) {
                    // Префикс не найден, очищаем буфер от старых данных
                    leftover.clear();
                    break;
                }
                
                // Удаляем всё до префикса
                if (prefixPos > 0) {
                    leftover.erase(leftover.begin(), leftover.begin() + prefixPos);
                }
                
                // Проверяем, есть ли у нас маска кнопки после префикса
                if (leftover.size() < 4) {
                    break;  // Ждём ещё данных
                }
                
                // Байт маски кнопки идёт сразу после префикса "km." [reference:5]
                uint8_t buttonMask = leftover[3];
                
                // Извлекаем биты кнопок из маски
                bool lmb = (buttonMask & 0x01) != 0;  // Бит 0 - левая кнопка
                bool rmb = (buttonMask & 0x02) != 0;  // Бит 1 - правая кнопка
                bool mmb = (buttonMask & 0x04) != 0;  // Бит 2 - средняя кнопка
                bool side1 = (buttonMask & 0x08) != 0;  // Бит 3 - боковая 1
                bool side2 = (buttonMask & 0x10) != 0;  // Бит 4 - боковая 2
                
                // Обновляем состояние кнопок
                UpdateButtonState(lmb, rmb, mmb);
                
                #ifdef _DEBUG
                std::cout << "[MakcuUART] Button event: mask=0x" << std::hex << (int)buttonMask << std::dec
                          << " LMB=" << lmb << " RMB=" << rmb << " MMB=" << mmb << std::endl;
                #endif
                
                // Удаляем обработанные 4 байта
                leftover.erase(leftover.begin(), leftover.begin() + 4);
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(1));  // Минимальная задержка для быстрой реакции
    }
    
    std::cout << "[MakcuUART] Monitoring loop ended" << std::endl;
}

// Обновление состояния кнопок с синхронизацией глобальных переменных
void MakcuUART::UpdateButtonState(bool lmb, bool rmb, bool mmb) {
    // Обновляем локальное состояние и глобальные переменные
    if (m_lmb_pressed.load() != lmb) {
        m_lmb_pressed.store(lmb);
        pwnz_ai::g_makcu_shooting.store(lmb);  // Синхронизация
        std::cout << "[MakcuUART] LMB state changed: " << (lmb ? "pressed" : "released") << std::endl;
    }
    if (m_rmb_pressed.load() != rmb) {
        m_rmb_pressed.store(rmb);
        pwnz_ai::g_makcu_aiming.store(rmb);  // Синхронизация
        std::cout << "[MakcuUART] RMB state changed: " << (rmb ? "pressed" : "released") << std::endl;
    }
    if (m_mmb_pressed.load() != mmb) {
        m_mmb_pressed.store(mmb);
        pwnz_ai::g_makcu_zooming.store(mmb);  // Синхронизация
        std::cout << "[MakcuUART] MMB state changed: " << (mmb ? "pressed" : "released") << std::endl;
    }
    
    // === КРИТИЧНО ДЛЯ 2PC: также обновляем g_remote_aim_key для совместимости ===
    // Это позволяет аимботу работать как через g_makcu_aiming, так и через g_remote_aim_key
    if (rmb || lmb) {
        g_remote_aim_key.store(true);
        #ifdef _DEBUG
        std::cout << "[MakcuUART] 2PC: Aim key ACTIVE" << std::endl;
        #endif
    } else {
        // Только если обе кнопки отпущены, сбрасываем g_remote_aim_key
        g_remote_aim_key.store(false);
        #ifdef _DEBUG
        std::cout << "[MakcuUART] 2PC: Aim key INACTIVE" << std::endl;
        #endif
    }
}

void MakcuUART::ParseResponse(const char* buffer, size_t length) {
    // Парсим ответы вида "btn:X" где X - битовая маска кнопок
    // btn:1 = ЛКМ, btn:2 = ПКМ, btn:4 = СКМ, btn:0 = все отпущены
    
    std::string data(buffer, length);
    
    // Проверяем наличие префикса "btn:"
    size_t pos = data.find("btn:");
    if (pos != std::string::npos) {
        // Извлекаем значение после "btn:"
        size_t endPos = data.find("\r\n", pos);
        if (endPos == std::string::npos) {
            endPos = data.length();  // Если нет \r\n, берём до конца строки
        }
        
        std::string valueStr = data.substr(pos + 4, endPos - pos - 4);
        try {
            int btnValue = std::stoi(valueStr);
            
            bool lmb = (btnValue & 0x01) != 0;
            bool rmb = (btnValue & 0x02) != 0;
            bool mmb = (btnValue & 0x04) != 0;
            
            // Обновляем локальное состояние и глобальные переменные
            if (m_lmb_pressed.load() != lmb) {
                m_lmb_pressed.store(lmb);
                pwnz_ai::g_makcu_shooting.store(lmb);  // Синхронизация
                std::cout << "[MakcuUART] LMB state changed: " << (lmb ? "pressed" : "released") 
                          << " (btnValue=" << btnValue << ")" << std::endl;
            }
            if (m_rmb_pressed.load() != rmb) {
                m_rmb_pressed.store(rmb);
                pwnz_ai::g_makcu_aiming.store(rmb);  // Синхронизация
                std::cout << "[MakcuUART] RMB state changed: " << (rmb ? "pressed" : "released") 
                          << " (btnValue=" << btnValue << ")" << std::endl;
            }
            if (m_mmb_pressed.load() != mmb) {
                m_mmb_pressed.store(mmb);
                pwnz_ai::g_makcu_zooming.store(mmb);  // Синхронизация
                std::cout << "[MakcuUART] MMB state changed: " << (mmb ? "pressed" : "released") 
                          << " (btnValue=" << btnValue << ")" << std::endl;
            }
            
            // === КРИТИЧНО ДЛЯ 2PC: также обновляем g_remote_aim_key для совместимости ===
            // Это позволяет аимботу работать как через g_makcu_aiming, так и через g_remote_aim_key
            if (rmb || lmb) {
                g_remote_aim_key.store(true);
                #ifdef _DEBUG
                std::cout << "[MakcuUART] 2PC: Aim key ACTIVE (btnValue=" << btnValue << ")" << std::endl;
                #endif
            } else {
                // Только если обе кнопки отпущены, сбрасываем g_remote_aim_key
                // Это нужно чтобы не сбросить если другая кнопка ещё нажата
                if (!rmb && !lmb) {
                    g_remote_aim_key.store(false);
                    #ifdef _DEBUG
                    std::cout << "[MakcuUART] 2PC: Aim key INACTIVE (btnValue=" << btnValue << ")" << std::endl;
                    #endif
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[MakcuUART] Parse error: " << e.what() << ", data: " << valueStr << std::endl;
        }
    }
    // Игнорируем все остальные данные (промпты >>>, версии и т.д.)
}
