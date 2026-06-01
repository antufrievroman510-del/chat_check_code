#include "aimbot.h"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <thread>
#include <atomic>
#include <random>
#include <vector>
#include <chrono>

#include "protect.h"
#include "xorstr.hpp"
#include "VMProtectSDK.h"
#include "AimMath.h"
#include "MouseController.h"
#include "overlay.h"  // [ДОБАВЛЕНО] Для типа Overlay в SyncFromOverlay()

#pragma comment(lib, "ws2_32.lib")

extern std::atomic<float> g_last_inference_time;
extern std::atomic<bool> g_is_target_locked;
extern std::atomic<float> g_locked_screen_x;
extern std::atomic<float> g_locked_screen_y;
extern std::atomic<bool> g_remote_aim_key;  // Глобальная переменная из main.cpp

// Глобальные переменные для совместимости
static long long g_first_seen_time = 0;
static long long g_last_target_drop_time = 0;

thread_local std::random_device Aimbot::rd;
thread_local std::mt19937 Aimbot::gen(rd());
thread_local std::normal_distribution<float> Aimbot::gauss_dist(0.0f, 1.0f);

typedef UINT(WINAPI* SendInputPtr)(UINT, LPINPUT, int);
static SendInputPtr DynamicSendInput = nullptr;

// ============================================================
// Конструктор / Деструктор
// ============================================================
Aimbot::Aimbot() {
    MUTATE_SIGNATURE;
    ResetTarget();
}

Aimbot::~Aimbot() {
    CloseHardware();
}

void Aimbot::SetConfig(const AimConfig& cfg) {
    m_config = cfg;
    // Синхронизация старого и нового конфига для совместимости
    aim_enable = cfg.enabled;
    // Прямое использование smooth из конфига (без двойной конвертации)
    smooth_factor = cfg.smooth;
    fov = cfg.fov;
    aim_key_main = cfg.fireKey;
}

// ============================================================
// Синхронизация с Overlay в реальном времени
// ============================================================
void Aimbot::SyncFromOverlay(Overlay& overlay) {
    // Копируем все настройки из Overlay напрямую в Aimbot
    // Это обеспечивает мгновенное применение изменений из меню
    
    // Основные настройки
    aim_enable = overlay.aim_enable;
    aim_target = overlay.aim_target;
    aim_key_main = overlay.aim_key_main;
    aim_key_sub = overlay.aim_key_sub;          // Синхронизируем дополнительную клавишу
    aim_toggle_key = overlay.aim_toggle_key;    // Синхронизируем клавишу переключения
    aim_target_lock = overlay.aim_target_lock;
    
    // FOV и чувствительность
    fov = overlay.fov_aimbot;
    min_sensitivity = overlay.min_sensitivity;
    max_sensitivity = overlay.max_sensitivity;
    detection_resolution = overlay.detection_resolution;
    
    // Humanizer
    humanizer_enable = overlay.humanizer_enable;
    hum_reaction_delay = overlay.hum_reaction_delay;
    hum_tremor_scale = overlay.hum_tremor_scale;
    hum_micro_movements = overlay.hum_micro_movements;
    hum_micro_amplitude = overlay.hum_micro_amplitude;
    hum_reaction_jitter = overlay.hum_reaction_jitter;
    hum_path_randomization = overlay.hum_path_randomization;
    hum_overshoot_enabled = overlay.hum_overshoot_enabled;
    hum_overshoot_chance = overlay.hum_overshoot_chance;
    hum_overshoot_amount = overlay.hum_overshoot_amount;
    hum_return_speed = overlay.hum_return_speed;
    
    // Pixelsmooth / Smoothing
    pixelsmooth_enabled = overlay.pixelsmooth_enabled;
    pixelsmooth_value = overlay.pixelsmooth_value;
    smooth_factor = overlay.smooth_factor;
    
    // Kalman Predictor
    kalman_enabled = overlay.kalman_enable;
    kalman_process_noise_position = overlay.kalman_q * 1000.0f;  // Масштабирование
    kalman_measurement_noise = overlay.kalman_r * 100.0f;
    kalman_compensate_detection_delay = overlay.kalman_compensate_detection_delay;
    kalman_additional_prediction_ms = overlay.kalman_additional_prediction_ms;
    prediction_interval = overlay.prediction_interval;
    
    // Recoil Control
    rcs_enable = overlay.rcs_enable;
    rcs_pitch = overlay.rcs_pitch;
    rcs_yaw = overlay.rcs_yaw;
    
    // Misc
    disable_headshot = overlay.disable_headshot;
    aim_lock_x = overlay.aim_lock_x;
    aim_lock_y = overlay.aim_lock_y;
    target_offset_x = overlay.aim_offset_x;
    target_offset_y = overlay.aim_offset_y;
    
    // Hardware
    hardware_type = overlay.hardware_mode_idx;  // Связываем hardware_mode_idx из UI с hardware_type в aimbot
    com_port = overlay.com_port;
    bypass_mode = overlay.bypass_mode_idx;      // Добавляем синхронизацию bypass_mode
    
    // Elite / Ballistics
    elite_ballistics_enabled = overlay.elite_ballistics_enabled;
    elite_bullet_speed = overlay.elite_bullet_speed;
    elite_bullet_drop = overlay.elite_bullet_drop;
    
    // Max move step (критичная настройка скорости)
    max_move_step = overlay.max_move_step;
    
    // === КРИТИЧНО: Синхронизация m_config для AimMath::CalculateMove ===
    // Обновляем ВСЕ поля m_config ПЕРЕД каждым вызовом Update()
    m_config.enabled = aim_enable;              // КРИТИЧНО: должно быть актуальным
    m_config.fov = fov;
    m_config.smooth = smooth_factor;
    m_config.fireKey = aim_key_main;
    m_config.aimbone = aim_target;
    m_config.recoil_control = rcs_enable;
    m_config.rcs_smooth = 1.0f;
    m_config.humanize = humanizer_enable;
    m_config.humanize_strength = hum_tremor_scale;
    m_config.prediction = kalman_enabled;
    m_config.bullet_speed = elite_bullet_speed;
}

// ============================================================
// Аппаратный вывод (интеграция с MouseController)
// ============================================================
void Aimbot::ResetTarget() {
    if (g_is_target_locked.load()) {
        g_last_target_drop_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    g_is_target_locked.store(false);
    g_locked_screen_x.store(0.0f);
    g_locked_screen_y.store(0.0f);
    g_first_seen_time = 0;
    last_target_time = 0;
    g_frac_x = 0.0f; g_frac_y = 0.0f;
    g_move_history.clear();
    g_overshoot_x = 0.0f; g_overshoot_y = 0.0f;
    g_in_overshoot = false;
    g_overshoot_start_time = 0;

    m_tracker.reset();
    m_kalman.reset();
    m_kalmanInitialized = false;
    m_lastPredictionTime = std::chrono::steady_clock::now();

    stat_shots_fired = 0;
    stat_tracking_time_ms = 0;
}

bool Aimbot::InitHardware() {
    MUTATE_SIGNATURE;
    CloseHardware();

    if (hardware_type == 0) {
        HMODULE hUser32 = GetModuleHandleA(XOR("user32.dll"));
        if (hUser32) {
            DynamicSendInput = (SendInputPtr)GetProcAddress(hUser32, XOR("SendInput"));
        }
        return (DynamicSendInput != nullptr);
    }

    if (hardware_type == 5 || hardware_type == 6) {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return false;
        udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (udp_socket == INVALID_SOCKET) return false;
        udp_addr.sin_family = AF_INET;
        udp_addr.sin_port = htons(net_port);
        udp_addr.sin_addr.s_addr = inet_addr(net_ip.c_str());
        return true;
    }

    std::string port = XOR("\\\\.\\COM") + std::to_string(com_port);
    hSerial = CreateFileA(port.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hSerial == nullptr) return false;

    DCB dcbSerialParams = { 0 };
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
    if (!GetCommState(hSerial, &dcbSerialParams)) return false;
    dcbSerialParams.BaudRate = CBR_115200;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;
    if (!SetCommState(hSerial, &dcbSerialParams)) return false;

    const char* initCmd = "km.buttons(1)\r";
    DWORD bytesWritten;
    WriteFile(hSerial, initCmd, (DWORD)strlen(initCmd), &bytesWritten, NULL);
    Sleep(100);
    return true;
}

void Aimbot::CloseHardware() {
    if (hSerial != nullptr) { CloseHandle(hSerial); hSerial = nullptr; }
    if (udp_socket != INVALID_SOCKET) { closesocket(udp_socket); udp_socket = INVALID_SOCKET; WSACleanup(); }
}

void Aimbot::SendHardwareMove(int x, int y) {
    if (x == 0 && y == 0) return;
    
    // [DEBUG] Логирование отправки движения
    static int move_count = 0;
    move_count++;
    if (move_count % 10 == 0) {  // Логируем каждый 10-й вызов чтобы не спамить
        std::cout << "[AIM DEBUG] SendHardwareMove: dx=" << x << " dy=" << y << " hw=" << hardware_type << std::endl;
    }
    
    if ((hardware_type == 5 || hardware_type == 6) && udp_socket != INVALID_SOCKET) {
        char buffer[64];
        if (hardware_type == 5) snprintf(buffer, sizeof(buffer), XOR("kmnet_move:%d:%d\n"), x, y);
        else snprintf(buffer, sizeof(buffer), XOR("move:%d:%d\n"), x, y);
        sendto(udp_socket, buffer, strlen(buffer), 0, (SOCKADDR*)&udp_addr, sizeof(udp_addr));
        return;
    }

    if (hSerial != INVALID_HANDLE_VALUE) {
        std::string data;
        if (hardware_type == 1) data = std::to_string(x) + XOR(":") + std::to_string(y) + XOR("\n");
        else if (hardware_type == 2) data = XOR("km.move(") + std::to_string(x) + XOR(",") + std::to_string(y) + XOR(")\r\n");
        else if (hardware_type == 3) data = XOR("m,") + std::to_string(x) + XOR(",") + std::to_string(y) + XOR("\r\n");
        else if (hardware_type == 4) data = XOR("move,") + std::to_string(x) + XOR(",") + std::to_string(y) + XOR("\r\n");
        DWORD bytesWritten; WriteFile(hSerial, data.c_str(), data.length(), &bytesWritten, NULL);
        return;
    }

    // КРИТИЧНО: Отправка через SendInput для hardware_type=0 (стандартная мышь Windows)
    // Реализация с учётом bypass_mode (GHub, Razer, Random Delay)
    if (hardware_type == 0 && DynamicSendInput) {
        INPUT input = { 0 };
        input.type = INPUT_MOUSE;
        input.mi.dx = x;
        input.mi.dy = y;
        input.mi.dwFlags = MOUSEEVENTF_MOVE;
        
        // Применяем режим обхода если выбран
        if (bypass_mode == 3) { // Random Delay
            static std::random_device rd;
            static std::mt19937 gen(rd());
            int delay = std::uniform_int_distribution<>(5, 15)(gen);
            Sleep(delay);
        }
        
        UINT result = DynamicSendInput(1, &input, sizeof(INPUT));
        if (move_count % 10 == 0) {
            std::cout << "[AIM DEBUG] SendInput OK: sent=" << result << " dx=" << x << " dy=" << y << std::endl;
        }
    } else if (hardware_type == 0 && !DynamicSendInput) {
        std::cerr << "[AIM ERROR] SendInput is NULL! Hardware not initialized?" << std::endl;
    }
}

void Aimbot::SendHardwareClick() {
    if ((hardware_type == 5 || hardware_type == 6) && udp_socket != INVALID_SOCKET) {
        const char* cmd = (hardware_type == 5) ? XOR("kmnet_click\n") : XOR("click\n");
        sendto(udp_socket, cmd, strlen(cmd), 0, (SOCKADDR*)&udp_addr, sizeof(udp_addr));
        return;
    }

    if (hSerial != INVALID_HANDLE_VALUE) {
        std::string data;
        if (hardware_type == 1) data = XOR("c\n");
        else if (hardware_type == 2) data = XOR("km.click(0)\r\n");
        else if (hardware_type == 3) data = XOR("c\n");
        else if (hardware_type == 4) data = XOR("click\r\n");
        DWORD bytesWritten; WriteFile(hSerial, data.c_str(), data.length(), &bytesWritten, NULL);
        return;
    }

    if (hardware_type == 0 && DynamicSendInput) {
        INPUT input[2] = { 0 };
        input[0].type = INPUT_MOUSE;
        input[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        input[1].type = INPUT_MOUSE;
        input[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
        DynamicSendInput(2, input, sizeof(INPUT));
    }
}

// ============================================================
// Вспомогательные методы
// ============================================================
std::pair<double, double> Aimbot::degToCounts(double degX, double degY) const {
    // Прямое преобразование градусов в пиксели движения
    // Используем fov для масштаба
    float scale = static_cast<float>(detection_resolution) / fov;
    double cx = degX * scale;
    double cy = degY * scale;
    return { cx, cy };
}

double Aimbot::calculateSpeedMultiplier(double distance, int screen_h) const {
    // Расчет скорости наводки на основе min_sensitivity и max_sensitivity
    // min_sensitivity - минимальная скорость (ползунок 0.1-20)
    // max_sensitivity - максимальный потолок скорости (чтобы выше не улетала)
    
    float max_distance = static_cast<float>(screen_h) / 2.0f;
    float dist_ratio = static_cast<float>(distance) / max_distance;
    float norm = (dist_ratio < 0.0f) ? 0.0f : ((dist_ratio > 1.0f) ? 1.0f : dist_ratio);
    
    // Плавная кривая скорости между min_sensitivity и max_sensitivity
    // На близких дистанциях - min_sensitivity, на дальних - приближается к max_sensitivity
    float speed = min_sensitivity + (max_sensitivity - min_sensitivity) * norm;
    
    // Ограничиваем скорость значением max_sensitivity (потолок)
    if (speed > max_sensitivity) speed = max_sensitivity;
    if (speed < min_sensitivity) speed = min_sensitivity;
    
    return static_cast<double>(speed);
}

double Aimbot::currentDetectionDelaySec() const {
    float infMs = g_last_inference_time.load();
    if (!std::isfinite(infMs) || infMs < 0.0f) infMs = 12.0f;
    return static_cast<double>(infMs) / 1000.0;
}

double Aimbot::currentPredictionLookaheadSec(double detectionDelaySec) const {
    double lookahead = std::max(0.0, static_cast<double>(prediction_interval));
    if (kalman_compensate_detection_delay)
        lookahead += std::max(0.0, detectionDelaySec);
    lookahead += static_cast<double>(kalman_additional_prediction_ms) / 1000.0;
    return std::clamp(lookahead, 0.0, 1.5);
}

std::pair<double, double> Aimbot::predictTargetPosition(double targetX, double targetY,
    std::chrono::steady_clock::time_point observationTime) {
    auto current_time = std::chrono::steady_clock::now();
    if (observationTime.time_since_epoch().count() == 0)
        observationTime = current_time;

    double observationAgeSec = std::chrono::duration<double>(current_time - observationTime).count();
    if (!std::isfinite(observationAgeSec) || observationAgeSec < 0.0)
        observationAgeSec = 0.0;

    aim::AimKalmanSettings kalmanSettings;
    kalmanSettings.enabled = kalman_enabled;
    kalmanSettings.process_noise_position = static_cast<double>(kalman_process_noise_position);
    kalmanSettings.process_noise_velocity = static_cast<double>(kalman_process_noise_velocity);
    kalmanSettings.measurement_noise = static_cast<double>(kalman_measurement_noise);
    kalmanSettings.velocity_damping = static_cast<double>(kalman_velocity_damping);
    kalmanSettings.max_velocity = static_cast<double>(kalman_max_velocity);
    kalmanSettings.warmup_frames = kalman_warmup_frames;
    m_kalman.setSettings(kalmanSettings);

    if (m_lastPredictionTime.time_since_epoch().count() == 0 || !m_kalmanInitialized) {
        m_lastPredictionTime = observationTime;
        prev_target_x = targetX;
        prev_target_y = targetY;
        m_kalman.reset();
        const double detectionDelaySec = currentDetectionDelaySec();
        const double lookaheadSec = currentPredictionLookaheadSec(detectionDelaySec);
        m_lastKalmanTelemetry = m_kalman.update(targetX, targetY, 1.0 / 120.0, lookaheadSec);
        m_lastDetectionDelaySec = detectionDelaySec;
        m_lastPredictionLookaheadSec = lookaheadSec;
        m_kalmanInitialized = true;
        return { targetX, targetY };
    }

    double dt = std::chrono::duration<double>(observationTime - m_lastPredictionTime).count();
    if (dt < 1e-8) dt = 1e-8;
    m_lastPredictionTime = observationTime;
    prev_target_x = targetX;
    prev_target_y = targetY;

    const double detectionDelaySec = currentDetectionDelaySec();
    const double lookaheadSec = currentPredictionLookaheadSec(detectionDelaySec);
    m_lastDetectionDelaySec = detectionDelaySec;
    m_lastPredictionLookaheadSec = lookaheadSec;

    m_lastKalmanTelemetry = m_kalman.update(targetX, targetY, dt, lookaheadSec);
    double predictedX = m_lastKalmanTelemetry.predicted_x;
    double predictedY = m_lastKalmanTelemetry.predicted_y;
    if (!std::isfinite(predictedX)) predictedX = targetX;
    if (!std::isfinite(predictedY)) predictedY = targetY;
    return { predictedX, predictedY };
}

// ФУНКЦИЯ calcMovement УДАЛЕНА - ТЕПЕРЬ ИСПОЛЬЗУЕТСЯ AimMath::CalculateMove()

float Aimbot::AddJitter(float value, float amplitude) {
    if (amplitude <= 0.0f) return value;
    return value + gauss_dist(gen) * amplitude;
}

void Aimbot::applyWindMouse(int& dx, int& dy) {
    // Опционально, можно реализовать позже
}

// ============================================================
// Основная логика аимбота
// ============================================================
// ============================================================
// Основная логика аимбота (НОВАЯ ИНТЕГРАЦИЯ)
// ============================================================
void Aimbot::Update(const std::vector<Detection>& detections, int screen_w, int screen_h,
    bool is_new_frame, long long current_time_ms, float zoom_scale) {
    VMProtectBeginMutation("AimbotUpdate");
    MUTATE_SIGNATURE;

    // ИСПРАВЛЕНИЕ: Сначала проверяем клавишу, потом aim_enable
    // Это позволяет аимботу работать даже если в конфиге aim_enable=false
    // но пользователь нажал клавишу активации
    // ИСПРАВЛЕНИЕ: Проверяем все клавиши активации (main, sub, toggle)
    bool key_pressed = false;
    
    // Основная клавиша
    if (aim_key_main != 0 && (GetAsyncKeyState(aim_key_main) & 0x8000)) {
        key_pressed = true;
    }
    // Дополнительная клавиша
    if (aim_key_sub != 0 && (GetAsyncKeyState(aim_key_sub) & 0x8000)) {
        key_pressed = true;
    }
    // Глобальная удалённая клавиша (для 2PC)
    if (g_remote_aim_key.load()) {
        key_pressed = true;
    }
    
    if (!aim_enable && !key_pressed) {
        ResetTarget();
        VMProtectEnd();
        return;
    }
    
    // Если клавиша нажата, но aim_enable=false - включаем принудительно
    if (!aim_enable && key_pressed) {
        aim_enable = true;
    }

    if (g_last_update_time == 0) g_last_update_time = current_time_ms;
    long long delta_t = current_time_ms - g_last_update_time;
    g_last_update_time = current_time_ms;
    latency_hist[hist_offset] = g_last_inference_time.load();

    float center_x = static_cast<float>(screen_w) / 2.0f;
    float center_y = static_cast<float>(screen_h) / 2.0f;
    current_fov = enable_dynamic_fov && g_is_target_locked.load() ? fov * 0.5f : fov;

    // === Шаг 1: Конвертация детектов в формат для трекера ===
    std::vector<RectF> boxes;
    std::vector<int> classes;
    for (const auto& d : detections) {
        float tx = d.box.x + d.box.w / 2.0f;
        float ty = d.box.y + d.box.h / 2.0f;
        float dx = tx - center_x;
        float dy = ty - center_y;
        // Фильтр по FOV
        if (dx * dx + dy * dy > current_fov * current_fov)
            continue;
        boxes.push_back(RectF(d.box.x, d.box.y, d.box.w, d.box.h));
        classes.push_back(d.class_id);
    }

    auto observationTime = std::chrono::steady_clock::time_point(std::chrono::milliseconds(current_time_ms));
    
    // === Шаг 2: Обновление трекера целей ===
    // ИСПРАВЛЕНИЕ: Передаем screen_w/screen_h вместо detection_resolution!
    std::cout << "[AIM DEBUG] Valid targets in FOV: " << boxes.size() << std::endl;
    m_tracker.update(boxes, classes, screen_w, screen_h,
        disable_headshot, aim_target_lock, observationTime);

    // === Шаг 3: Получение захваченной цели ===
    LockedTargetInfo lockInfo;
    if (!m_tracker.getLockedTarget(lockInfo)) {
        ResetTarget();
        hist_offset = (hist_offset + 1) % 100;
        VMProtectEnd();
        return;
    }

    g_is_target_locked.store(true);
    g_locked_screen_x.store(static_cast<float>(lockInfo.target.pivotX));
    g_locked_screen_y.store(static_cast<float>(lockInfo.target.pivotY));

    // === Шаг 4: Задержка реакции (Humanizer) ===
    if (humanizer_enable && hum_reaction_delay > 0.0f) {
        if (g_first_seen_time == 0) g_first_seen_time = current_time_ms;
        if (current_time_ms - g_first_seen_time < hum_reaction_delay) {
            VMProtectEnd();
            return;
        }
    }

    // === Шаг 5: Предикт движения (Kalman) ===
    auto predicted = predictTargetPosition(lockInfo.target.pivotX, lockInfo.target.pivotY, observationTime);
    double targetX = predicted.first;
    double targetY = predicted.second;

    // === Шаг 6: Выбор зоны прицеливания (Head/Body/Auto) ===
    if (aim_target == 1 || aim_target == 2) {
        int requiredClassId = (aim_target == 1) ? 1 : 0;  // 1=Head, 2=Body->0
        bool foundMatchingClass = false;
        for (const auto& d : detections) {
            float tx = d.box.x + d.box.w / 2.0f;
            float ty = d.box.y + d.box.h / 2.0f;
            float dx = tx - center_x;
            float dy = ty - center_y;
            if (dx * dx + dy * dy > current_fov * current_fov)
                continue;
            
            if (d.class_id == requiredClassId) {
                targetX = tx;
                targetY = ty;
                foundMatchingClass = true;
                break;
            }
        }
    }

    // === Шаг 7: Дополнительные оффсеты ===
    targetX += static_cast<double>(target_offset_x);
    targetY += static_cast<double>(target_offset_y);

    // === Шаг 8: Баллистика (Elite) ===
    if (elite_ballistics_enabled && lockInfo.target.w > 0) {
        float estimated_distance = 1000.0f / (lockInfo.target.w + 1.0f);
        float time_to_target = estimated_distance / elite_bullet_speed;
        float bullet_drop_px = 0.5f * elite_bullet_drop * (time_to_target * time_to_target) * 15.0f;
        double velX = m_lastKalmanTelemetry.velocity_x;
        double velY = m_lastKalmanTelemetry.velocity_y;
        targetX += velX * time_to_target;
        targetY -= bullet_drop_px;
    }

    // === Шаг 9: Humanizer (тремор, микро-движения) ===
    if (humanizer_enable && hum_tremor_scale > 0.0f) {
        targetX = AddJitter(static_cast<float>(targetX), hum_tremor_scale * 0.5f);
        targetY = AddJitter(static_cast<float>(targetY), hum_tremor_scale * 0.5f);
        if (hum_micro_movements && hum_micro_amplitude > 0.0f) {
            static float micro_x = 0.0f, micro_y = 0.0f;
            micro_x = micro_x * 0.95f + gauss_dist(gen) * hum_micro_amplitude * 0.1f;
            micro_y = micro_y * 0.95f + gauss_dist(gen) * hum_micro_amplitude * 0.1f;
            targetX += micro_x;
            targetY += micro_y;
        }
        if (hum_reaction_jitter > 0.0f && g_first_seen_time == current_time_ms) {
            targetX += gauss_dist(gen) * hum_reaction_jitter;
            targetY += gauss_dist(gen) * hum_reaction_jitter;
        }
    }

    // === Шаг 10: НОВАЯ МАТЕМАТИКА (AimMath::CalculateMove) ===
    // Удаляем весь старый спагетти-код (Lerp, Smooth, Steps)
    // Вызываем единую функцию расчета
    AimResult result = AimMath::CalculateMove(
        center_x, 
        center_y, 
        static_cast<float>(targetX), 
        static_cast<float>(targetY), 
        m_config, 
        key_pressed
    );

    // Лог результатов расчета
    std::cout << "[AIM DEBUG] Result DX: " << result.deltaX << " DY: " << result.deltaY << std::endl;

    // Получаем дельты из результата
    int mx = static_cast<int>(result.deltaX);
    int my = static_cast<int>(result.deltaY);

    // Ограничение шага (осталось от старой логики, но теперь применяется после AimMath)
    if (std::abs(mx) > max_move_step) mx = (mx > 0) ? static_cast<int>(max_move_step) : -static_cast<int>(max_move_step);
    if (std::abs(my) > max_move_step) my = (my > 0) ? static_cast<int>(max_move_step) : -static_cast<int>(max_move_step);

    // RCS (компенсация отдачи)
    if (rcs_enable && (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
        mx += static_cast<int>(rcs_yaw);
        my += static_cast<int>(rcs_pitch);
    }

    // Lock axes (блокировка осей)
    if (aim_lock_x) mx = 0;
    if (aim_lock_y) my = 0;

    // === Шаг 11: Overshoot (перелёт с возвратом) ===
    if (humanizer_enable && hum_overshoot_enabled && !g_in_overshoot) {
        float rand_val = static_cast<float>(rand()) / RAND_MAX * 100.0f;
        if (rand_val < hum_overshoot_chance && std::abs(mx) > 2 && std::abs(my) > 2) {
            g_in_overshoot = true;
            g_overshoot_start_time = current_time_ms;
            g_overshoot_x = static_cast<float>(mx) * hum_overshoot_amount;
            g_overshoot_y = static_cast<float>(my) * hum_overshoot_amount;
        }
    }

    if (g_in_overshoot) {
        long long elapsed = current_time_ms - g_overshoot_start_time;
        float t = static_cast<float>(elapsed) / 150.0f;
        if (t >= 1.0f) {
            g_in_overshoot = false;
            mx = static_cast<int>(g_overshoot_x * (1.0f - hum_return_speed));
            my = static_cast<int>(g_overshoot_y * (1.0f - hum_return_speed));
        } else {
            mx = static_cast<int>(g_overshoot_x * (1.0f - t * hum_return_speed));
            my = static_cast<int>(g_overshoot_y * (1.0f - t * hum_return_speed));
        }
    }

    // === Шаг 12: Pixelsmooth (сглаживание по истории) ===
    if (pixelsmooth_enabled && pixelsmooth_value > 1.0f) {
        g_move_history.push_back({mx, my});
        if (g_move_history.size() > static_cast<size_t>(pixelsmooth_value)) {
            g_move_history.erase(g_move_history.begin());
        }
        int sum_x = 0, sum_y = 0;
        for (const auto& m : g_move_history) {
            sum_x += m.first;
            sum_y += m.second;
        }
        mx = sum_x / static_cast<int>(g_move_history.size());
        my = sum_y / static_cast<int>(g_move_history.size());
    }

    // === Шаг 13: Lerp сглаживание ===
    if (smooth_factor > 0.0f && smooth_factor < 1.0f) {
        mx = static_cast<int>(g_frac_x * (1.0f - smooth_factor) + mx * smooth_factor);
        my = static_cast<int>(g_frac_y * (1.0f - smooth_factor) + my * smooth_factor);
        g_frac_x = static_cast<float>(mx);
        g_frac_y = static_cast<float>(my);
    } else {
        g_frac_x = static_cast<float>(mx);
        g_frac_y = static_cast<float>(my);
    }

    // === Шаг 14: Path Randomization ===
    if (humanizer_enable && hum_path_randomization > 0.0f) {
        float jitter_x = gauss_dist(gen) * hum_path_randomization;
        float jitter_y = gauss_dist(gen) * hum_path_randomization;
        mx += static_cast<int>(jitter_x);
        my += static_cast<int>(jitter_y);
    }

    // === Шаг 15: Отправка движения через Hardware (напрямую из AimResult) ===
    // УДАЛЕНЫ: Pixelsmooth, Lerp, Path Randomization, Overshoot - теперь это делает AimMath
    if (mx == 0 && my == 0) {
        // Цель в FOV, но движение 0 - возможно FOV мал или Smooth огромный
        if (key_pressed) {
            std::cout << "[AIM DEBUG] Movement is 0. Check FOV/Smooth settings." << std::endl;
        }
        hist_offset = (hist_offset + 1) % 100;
        VMProtectEnd();
        return;
    }

    // === Шаг 16: Отправка движения через Hardware ===
    SendHardwareMove(mx, my);
    hist_offset = (hist_offset + 1) % 100;
    stat_tracking_time_ms += delta_t;

    VMProtectEnd();
}