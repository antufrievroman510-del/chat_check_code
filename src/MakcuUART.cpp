#include "MakcuUART.h"
// Winsock заголовки уже подключены в MakcuUART.h
#include <iostream>
#include <thread>
#include <chrono>
#include <sstream>
#include "MakcuState.h"  // Подключаем заголовок с объявлением глобальных переменных

// Глобальные переменные определены в MakcuState.cpp

// Протокол Makcu ESP32S3 (прошивка MAKCM) использует текстовые команды формата:
// km.move(x,y)      - движение мыши
// km.left(1/0)      - нажать/отпустить ЛКМ
// km.right(1/0)     - нажать/отпустить ПКМ
// km.middle(1/0)    - нажать/отпустить колесо
// km.side1(1/0)     - нажать/отпустить боковую кнопку 1
// km.side2(1/0)     - нажать/отпустить боковую кнопку 2
// Важно: Для корректного клика нужно отправить пару команд button(1) -> button(0)

MakcuUART::MakcuUART() 
    : hComPort(nullptr), isConnected(false), packetDelayMs(5), m_portName("COM3"), m_baudRate(115200) {}

MakcuUART::~MakcuUART() {
    Shutdown();
}

// Реализация интерфейса IMouseInput
bool MakcuUART::Init() {
    return Connect(m_portName, m_baudRate);
}

void MakcuUART::Move(int dx, int dy) {
    MoveMouse(dx, dy);
}

void MakcuUART::Click(int button) {
    ClickMouse(button);
}

bool MakcuUART::ClickMouse(int button) {
    if (!isConnected || hComPort == nullptr) {
        std::cerr << "[MakcuUART] ClickMouse: Not connected!" << std::endl;
        return false;
    }

    std::lock_guard<std::mutex> lock(mtx);

    // Формируем обозначение кнопки для нового протокола
    // button: 0=ЛКМ (left), 1=ПКМ (right), 2=Колесо (middle), 3=Боковая1 (side1), 4=Боковая2 (side2)
    std::string buttonCmd;
    switch (button) {
        case 0: buttonCmd = "left"; break;   // ЛКМ
        case 1: buttonCmd = "right"; break;  // ПКМ
        case 2: buttonCmd = "middle"; break; // Колесо (нажатие)
        case 3: buttonCmd = "side1"; break;  // Боковая кнопка 1
        case 4: buttonCmd = "side2"; break;  // Боковая кнопка 2
        default: buttonCmd = "left"; break;  // По умолчанию ЛКМ
    }
    
    // Прошивка MAKCM требует раздельные команды нажатия (1) и отпускания (0)
    // Формируем команду нажатия: km.button(1)\r\n
    std::ostringstream pressCmd;
    pressCmd << "km." << buttonCmd << "(1)\r\n";
    std::string pressCommand = pressCmd.str();
    
    // Формируем команду отпускания: km.button(0)\r\n
    std::ostringstream releaseCmd;
    releaseCmd << "km." << buttonCmd << "(0)\r\n";
    std::string releaseCommand = releaseCmd.str();

    std::cout << "[MakcuUART] Sending click: " << buttonCmd << "(1) -> " << buttonCmd << "(0)" << std::endl;
    
    // Отправляем нажатие
    bool pressResult = WriteBytes(reinterpret_cast<const unsigned char*>(pressCommand.c_str()), pressCommand.length());
    if (!pressResult) {
        std::cerr << "[MakcuUART] Failed to send press command" << std::endl;
        return false;
    }
    
    // Небольшая задержка между нажатием и отпусканием (имитация реального клика)
    // 50ms достаточно для регистрации клика в игре
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Отправляем отпускание
    bool releaseResult = WriteBytes(reinterpret_cast<const unsigned char*>(releaseCommand.c_str()), releaseCommand.length());
    if (!releaseResult) {
        std::cerr << "[MakcuUART] Failed to send release command" << std::endl;
        return false;
    }
    
    // Дополнительная задержка после клика
    if (packetDelayMs > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(packetDelayMs));
    }

    return true;
}

/**
 * @brief Нажатие кнопки мыши (удержание)
 * Отправляет только команду нажатия km.button(1) без отпускания
 */
bool MakcuUART::PressButton(int button) {
    if (!isConnected || hComPort == nullptr) {
        std::cerr << "[MakcuUART] PressButton: Not connected!" << std::endl;
        return false;
    }

    std::lock_guard<std::mutex> lock(mtx);

    // Формируем обозначение кнопки
    std::string buttonCmd;
    switch (button) {
        case 0: buttonCmd = "left"; break;   // ЛКМ
        case 1: buttonCmd = "right"; break;  // ПКМ
        case 2: buttonCmd = "middle"; break; // Колесо
        case 3: buttonCmd = "side1"; break;  // Боковая кнопка 1
        case 4: buttonCmd = "side2"; break;  // Боковая кнопка 2
        default: buttonCmd = "left"; break;
    }
    
    // Формируем команду нажатия: km.button(1)\r\n
    std::ostringstream pressCmd;
    pressCmd << "km." << buttonCmd << "(1)\r\n";
    std::string pressCommand = pressCmd.str();

    std::cout << "[MakcuUART] Press button: " << buttonCmd << "(1)" << std::endl;
    
    // Отправляем нажатие
    bool result = WriteBytes(reinterpret_cast<const unsigned char*>(pressCommand.c_str()), pressCommand.length());
    if (!result) {
        std::cerr << "[MakcuUART] Failed to send press command" << std::endl;
        return false;
    }
    
    // === КРИТИЧНО: Обновляем локальное состояние кнопки ===
    // Это позволяет отслеживать состояние кнопок в аппаратном режиме
    switch (button) {
        case 0: 
            m_lmb_pressed.store(true); 
            pwnz_ai::g_makcu_shooting.store(true); // LMB = shooting
            break;
        case 1: 
            m_rmb_pressed.store(true); 
            pwnz_ai::g_makcu_aiming.store(true);   // RMB = aiming
            break;
        case 2: 
            m_mmb_pressed.store(true); 
            pwnz_ai::g_makcu_zooming.store(true);  // MMB = zooming
            break;
    }

    return true;
}

/**
 * @brief Отпускание кнопки мыши
 * Отправляет только команду отпускания km.button(0)
 */
bool MakcuUART::ReleaseButton(int button) {
    if (!isConnected || hComPort == nullptr) {
        std::cerr << "[MakcuUART] ReleaseButton: Not connected!" << std::endl;
        return false;
    }

    std::lock_guard<std::mutex> lock(mtx);

    // Формируем обозначение кнопки
    std::string buttonCmd;
    switch (button) {
        case 0: buttonCmd = "left"; break;   // ЛКМ
        case 1: buttonCmd = "right"; break;  // ПКМ
        case 2: buttonCmd = "middle"; break; // Колесо
        case 3: buttonCmd = "side1"; break;  // Боковая кнопка 1
        case 4: buttonCmd = "side2"; break;  // Боковая кнопка 2
        default: buttonCmd = "left"; break;
    }
    
    // Формируем команду отпускания: km.button(0)\r\n
    std::ostringstream releaseCmd;
    releaseCmd << "km." << buttonCmd << "(0)\r\n";
    std::string releaseCommand = releaseCmd.str();

    std::cout << "[MakcuUART] Release button: " << buttonCmd << "(0)" << std::endl;
    
    // Отправляем отпускание
    bool result = WriteBytes(reinterpret_cast<const unsigned char*>(releaseCommand.c_str()), releaseCommand.length());
    if (!result) {
        std::cerr << "[MakcuUART] Failed to send release command" << std::endl;
        return false;
    }
    
    // === КРИТИЧНО: Обновляем локальное состояние кнопки ===
    // Это позволяет отслеживать состояние кнопок в аппаратном режиме
    switch (button) {
        case 0: 
            m_lmb_pressed.store(false); 
            pwnz_ai::g_makcu_shooting.store(false); // LMB = shooting
            break;
        case 1: 
            m_rmb_pressed.store(false); 
            pwnz_ai::g_makcu_aiming.store(false);   // RMB = aiming
            break;
        case 2: 
            m_mmb_pressed.store(false); 
            pwnz_ai::g_makcu_zooming.store(false);  // MMB = zooming
            break;
    }

    return true;
}

void MakcuUART::Shutdown() {
    Disconnect();
}

bool MakcuUART::Connect(const std::string& portName, int baudRate) {
    std::lock_guard<std::mutex> lock(mtx);
    
    if (isConnected) {
        Disconnect();
    }

    // Формируем имя порта для Windows (\\.\COM3)
    // Для портов выше COM9 обязательно использование префикса \\\.
    std::string fullPortName = "\\\\.\\" + portName;
    
    std::cout << "[MakcuUART] Attempting to connect to: " << fullPortName
              << " at " << baudRate << " baud..." << std::endl;
    
    hComPort = CreateFileA(
        fullPortName.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,          // No sharing
        nullptr,    // Default security attributes
        OPEN_EXISTING,
        0,          // No flags or attributes
        nullptr     // No template file
    );

    // Проверка на INVALID_HANDLE_VALUE, а не на nullptr
    if (hComPort == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        hComPort = nullptr;
        std::cerr << "[MakcuUART] Failed to open COM port: " << portName 
                  << " (Error: " << err << ")" << std::endl;
        if (err == ERROR_ACCESS_DENIED) {
            std::cerr << "ERROR: Port is busy or access denied. Close Arduino IDE, terminal apps, etc." << std::endl;
        } else if (err == ERROR_FILE_NOT_FOUND) {
            std::cerr << "ERROR: Port does not exist. Check Device Manager for correct COM number." << std::endl;
        }
        return false;
    }

    // Настройка параметров COM-порта
    DCB dcbSerialParams = { 0 };
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

    if (!GetCommState(hComPort, &dcbSerialParams)) {
        Disconnect();
        return false;
    }

    dcbSerialParams.BaudRate = baudRate;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;

    if (!SetCommState(hComPort, &dcbSerialParams)) {
        Disconnect();
        return false;
    }

    // Установка таймаутов (опционально, для надежности)
    COMMTIMEOUTS timeouts = { 0 };
    timeouts.WriteTotalTimeoutConstant = 500;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    SetCommTimeouts(hComPort, &timeouts);

    // Очистка буферов перед началом работы
    PurgeComm(hComPort, PURGE_TXCLEAR | PURGE_RXCLEAR);
    
    // Включаем DTR и RTS для питания ESP32
    dcbSerialParams.fDtrControl = DTR_CONTROL_ENABLE;
    dcbSerialParams.fRtsControl = RTS_CONTROL_ENABLE;
    SetCommState(hComPort, &dcbSerialParams);
    
    // Небольшая задержка для стабилизации соединения
    Sleep(100);

    isConnected = true;
    return true;
}

void MakcuUART::Disconnect() {
    std::lock_guard<std::mutex> lock(mtx);
    
    if (hComPort != nullptr) {
        CloseHandle(hComPort);
        hComPort = nullptr;
    }
    isConnected = false;
}

bool MakcuUART::IsConnected() const {
    return isConnected;
}

bool MakcuUART::MoveMouse(int dx, int dy) {
    if (!isConnected || hComPort == nullptr) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mtx);

    // Формируем текстовую команду: km.move(x,y)\n
    // ESP32S3 прошивка ожидает именно такой формат
    std::ostringstream cmd;
    cmd << "km.move(" << dx << "," << dy << ")\r\n";
    std::string command = cmd.str();

    bool result = WriteBytes(reinterpret_cast<const unsigned char*>(command.c_str()), command.length());
    
    if (result && packetDelayMs > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(packetDelayMs));
    }

    return result;
}

bool MakcuUART::MoveMouseAbsolute(int x, int y, int /*width*/, int /*height*/) {
    if (!isConnected || hComPort == nullptr) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mtx);

    // Для абсолютного позиционирования используем ту же команду
    // Прошивка сама масштабирует координаты если нужно
    std::ostringstream cmd;
    cmd << "km.move(" << x << "," << y << ")\r\n";
    std::string command = cmd.str();

    bool result = WriteBytes(reinterpret_cast<const unsigned char*>(command.c_str()), command.length());
    
    if (result && packetDelayMs > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(packetDelayMs));
    }

    return result;
}

void MakcuUART::SetPacketDelayMs(int ms) {
    packetDelayMs = (ms < 0) ? 0 : ms;
}

bool MakcuUART::WriteBytes(const unsigned char* data, size_t length) {
    DWORD bytesWritten;
    BOOL result = WriteFile(hComPort, data, static_cast<DWORD>(length), &bytesWritten, nullptr);
    
    if (!result || bytesWritten != length) {
        // Ошибка записи или таймаут
        Disconnect();
        return false;
    }
    
    // Принудительно сбрасываем буфер вывода, чтобы данные сразу ушли в ESP32
    FlushFileBuffers(hComPort);
    
    return true;
}

// ============================================================
// Реализация потока мониторинга состояния кнопок
// ============================================================

void MakcuUART::StartMonitoring() {
    if (m_monitoring.load()) {
        return; // Уже запущен
    }
    
    m_stopMonitoring.store(false);
    m_monitorThread = std::thread(&MakcuUART::monitoringLoop, this);
    m_monitoring.store(true);
    
    std::cout << "[MakcuUART] Button monitoring started" << std::endl;
}

void MakcuUART::StopMonitoring() {
    if (!m_monitoring.load()) {
        return; // Не запущен
    }
    
    m_stopMonitoring.store(true);
    
    if (m_monitorThread.joinable()) {
        m_monitorThread.join();
    }
    
    m_monitoring.store(false);
    
    std::cout << "[MakcuUART] Button monitoring stopped" << std::endl;
}

void MakcuUART::monitoringLoop() {
    std::cout << "[MakcuUART] Monitoring loop started (Idle mode, no polling)." << std::endl;
    
    // Прошивка Macku (MAKCM) НЕ поддерживает чтение состояния (get_state).
    // Она работает только на прием команд (Write-only).
    // Попытки чтения (ReadFile) без наличия входящих данных могут вызывать блокировки или возврат пустоты,
    // что бесполезно тратит ресурсы и может мешать работе основного потока.
    // 
    // Состояние кнопок (g_makcu_*) обновляется локально в методах PressButton/ReleaseButton.
    // Этот поток просто держит соединение активным и может быть расширен для обработки логов от ESP32 в будущем.

    while (!m_stopMonitoring.load() && isConnected) {
        // Просто спим, чтобы не грузить CPU. 
        // Проверка isConnected нужна для выхода при отключении устройства в другом потоке.
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    std::cout << "[MakcuUART] Monitoring loop ended" << std::endl;
}

void MakcuUART::ParseResponse(const std::string& response) {
    // Парсим ответы от прошивки MAKCM
    // Формат может быть: "btn:1 0 0" или "state:L,R,M"
    
    std::cout << "[MakcuUART] ParseResponse called with: \"" << response << "\"" << std::endl;
    
    // Пример парсинга для формата "btn:L R M" где L/R/M = 0 или 1
    if (response.find("btn:") != std::string::npos) {
        size_t pos = response.find("btn:") + 4;
        std::string btnState = response.substr(pos);
        
        // Удаляем пробелы и переводы строк
        btnState.erase(std::remove_if(btnState.begin(), btnState.end(), ::isspace), btnState.end());
        
        std::cout << "[MakcuUART] Button state string: \"" << btnState << "\" (len=" << btnState.length() << ")" << std::endl;
        
        if (btnState.length() >= 3) {
            bool lmb = (btnState[0] == '1');
            bool rmb = (btnState[1] == '1');
            bool mmb = (btnState[2] == '1');
            
            // Обновляем локальное состояние
            m_lmb_pressed.store(lmb);
            m_rmb_pressed.store(rmb);
            m_mmb_pressed.store(mmb);
            
            // Обновляем глобальные переменные для использования в aimbot.cpp
            pwnz_ai::g_makcu_shooting.store(lmb);   // LMB = стрельба
            pwnz_ai::g_makcu_aiming.store(rmb);     // RMB = прицеливание
            pwnz_ai::g_makcu_zooming.store(mmb);    // MMB = зум
            
            std::cout << "[MakcuUART] Parsed buttons: L=" << lmb << " R=" << rmb << " M=" << mmb << std::endl;
        } else {
            std::cerr << "[MakcuUART] Button state too short: \"" << btnState << "\"" << std::endl;
        }
    }
    // Альтернативный формат: "left:1 right:0 middle:0"
    else {
        if (response.find("left:1") != std::string::npos) {
            m_lmb_pressed.store(true);
            pwnz_ai::g_makcu_shooting.store(true);
            std::cout << "[MakcuUART] LMB pressed (alt format)" << std::endl;
        } else if (response.find("left:0") != std::string::npos) {
            m_lmb_pressed.store(false);
            pwnz_ai::g_makcu_shooting.store(false);
            std::cout << "[MakcuUART] LMB released (alt format)" << std::endl;
        }
        
        if (response.find("right:1") != std::string::npos) {
            m_rmb_pressed.store(true);
            pwnz_ai::g_makcu_aiming.store(true);
            std::cout << "[MakcuUART] RMB pressed (alt format)" << std::endl;
        } else if (response.find("right:0") != std::string::npos) {
            m_rmb_pressed.store(false);
            pwnz_ai::g_makcu_aiming.store(false);
            std::cout << "[MakcuUART] RMB released (alt format)" << std::endl;
        }
        
        if (response.find("middle:1") != std::string::npos) {
            m_mmb_pressed.store(true);
            pwnz_ai::g_makcu_zooming.store(true);
            std::cout << "[MakcuUART] MMB pressed (alt format)" << std::endl;
        } else if (response.find("middle:0") != std::string::npos) {
            m_mmb_pressed.store(false);
            pwnz_ai::g_makcu_zooming.store(false);
            std::cout << "[MakcuUART] MMB released (alt format)" << std::endl;
        }
    }
}
