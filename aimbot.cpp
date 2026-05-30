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

#pragma comment(lib, "ws2_32.lib")

extern std::atomic<float> g_last_inference_time;
extern std::atomic<bool> g_is_target_locked;
extern std::atomic<float> g_locked_screen_x;
extern std::atomic<float> g_locked_screen_y;

static long long g_first_seen_time = 0;
static long long g_last_update_time = 0;
static long long g_last_target_drop_time = 0;

// Состояние для pixelsmooth / сглаживания
static std::vector<std::pair<int, int>> g_move_history;
static const int MAX_MOVE_HISTORY = 16;
static float g_overshoot_x = 0.0f, g_overshoot_y = 0.0f;
static bool g_in_overshoot = false;
static long long g_overshoot_start_time = 0;

thread_local std::random_device Aimbot::rd;
thread_local std::mt19937 Aimbot::gen(rd());
thread_local std::normal_distribution<float> Aimbot::gauss_dist(0.0f, 1.0f);

typedef UINT(WINAPI* SendInputPtr)(UINT, LPINPUT, int);
static SendInputPtr DynamicSendInput = nullptr;

// ============================================================
// Аппаратный вывод (ваш исходный код)
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

    m_tracker.reset();
    m_kalman.reset();
    m_kalmanInitialized = false;
    m_lastPredictionTime = std::chrono::steady_clock::now();

    windCarryX = windCarryY = 0.0;
    windVelX = windVelY = 0.0;
    windNoiseX = windNoiseY = 0.0;
    windFracX = windFracY = 0.0;
    windPatternX = windPatternY = 0.0;
    windPatternPhaseA = windPatternPhaseB = 0.0;
    windPatternRateA = windPatternRateB = 0.0;

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
    if (hSerial == INVALID_HANDLE_VALUE) return false;

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
    if (hSerial != INVALID_HANDLE_VALUE) { CloseHandle(hSerial); hSerial = INVALID_HANDLE_VALUE; }
    if (udp_socket != INVALID_SOCKET) { closesocket(udp_socket); udp_socket = INVALID_SOCKET; WSACleanup(); }
}

void Aimbot::SendHardwareMove(int x, int y) {
    if (x == 0 && y == 0) return;
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

    if (hardware_type == 0 && DynamicSendInput) {
        INPUT input = { 0 };
        input.type = INPUT_MOUSE;
        input.mi.dx = x;
        input.mi.dy = y;
        input.mi.dwFlags = MOUSEEVENTF_MOVE;
        DynamicSendInput(1, &input, sizeof(INPUT));
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

double Aimbot::calculateSpeedMultiplier(double distance) const {
    // Простая линейная интерполяция между min и max скоростью
    // Вблизи цели - медленнее, вдали - быстрее
    if (distance < 5.0f)
        return min_aim_speed;
    
    float max_distance = static_cast<float>(detection_resolution) / 2.0f;
    float dist_ratio = distance / max_distance;
    float norm = (dist_ratio < 0.0f) ? 0.0f : ((dist_ratio > 1.0f) ? 1.0f : dist_ratio);
    
    // Плавная кривая скорости
    return min_aim_speed + (max_aim_speed - min_aim_speed) * norm;
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

std::pair<double, double> Aimbot::calcMovement(double targetX, double targetY) {
    double centerX = static_cast<double>(detection_resolution) / 2.0;
    double centerY = static_cast<double>(detection_resolution) / 2.0;
    double offX = targetX - centerX;
    double offY = targetY - centerY;
    double distance = std::hypot(offX, offY);
    
    // Получаем множитель скорости на основе расстояния
    double speed = calculateSpeedMultiplier(distance);
    
    // Преобразуем смещение в пиксели движения напрямую
    // speed уже содержит нужную нам скорость перемещения
    double moveX = offX * speed;
    double moveY = offY * speed;
    
    return { moveX, moveY };
}

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
void Aimbot::Update(const std::vector<Detection>& detections, int screen_w, int screen_h,
    bool is_new_frame, long long current_time_ms, float zoom_scale) {
    VMProtectBeginMutation("AimbotUpdate");
    MUTATE_SIGNATURE;

    if (g_last_update_time == 0) g_last_update_time = current_time_ms;
    long long delta_t = current_time_ms - g_last_update_time;
    g_last_update_time = current_time_ms;
    latency_hist[hist_offset] = g_last_inference_time.load();

    float center_x = screen_w / 2.0f;
    float center_y = screen_h / 2.0f;
    current_fov = enable_dynamic_fov && g_is_target_locked.load() ? fov * 0.5f : fov;

    // Конвертация в RectF и классы
    std::vector<RectF> boxes;
    std::vector<int> classes;
    for (const auto& d : detections) {
        float tx = d.box.x + d.box.w / 2.0f;
        float ty = d.box.y + d.box.h / 2.0f;
        float dx = tx - center_x;
        float dy = ty - center_y;
        if (dx * dx + dy * dy > current_fov * current_fov)
            continue;
        boxes.push_back(RectF(d.box.x, d.box.y, d.box.w, d.box.h));
        classes.push_back(d.class_id);
    }

    auto observationTime = std::chrono::steady_clock::time_point(std::chrono::milliseconds(current_time_ms));
    m_tracker.update(boxes, classes, detection_resolution, detection_resolution,
        disable_headshot, aim_target_lock, observationTime);

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

    if (humanizer_enable && hum_reaction_delay > 0.0f) {
        if (g_first_seen_time == 0) g_first_seen_time = current_time_ms;
        if (current_time_ms - g_first_seen_time < hum_reaction_delay) {
            VMProtectEnd();
            return;
        }
    }

    auto predicted = predictTargetPosition(lockInfo.target.pivotX, lockInfo.target.pivotY, observationTime);
    double targetX = predicted.first;
    double targetY = predicted.second;

    if (elite_ballistics_enabled && lockInfo.target.w > 0) {
        float estimated_distance = 1000.0f / (lockInfo.target.w + 1.0f);
        float time_to_target = estimated_distance / elite_bullet_speed;
        float bullet_drop_px = 0.5f * elite_bullet_drop * (time_to_target * time_to_target) * 15.0f;
        double velX = m_lastKalmanTelemetry.velocity_x;
        double velY = m_lastKalmanTelemetry.velocity_y;
        targetX += velX * time_to_target;
        targetY -= bullet_drop_px;
    }

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

    // === Pixelsmooth / Bezier сглаживание ===
    auto mv = calcMovement(targetX, targetY);
    int mx = static_cast<int>(mv.first);
    int my = static_cast<int>(mv.second);

    if (std::abs(mx) > max_move_step) mx = (mx > 0) ? static_cast<int>(max_move_step) : -static_cast<int>(max_move_step);
    if (std::abs(my) > max_move_step) my = (my > 0) ? static_cast<int>(max_move_step) : -static_cast<int>(max_move_step);

    if (rcs_enable && (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
        mx += static_cast<int>(rcs_yaw);
        my += static_cast<int>(rcs_pitch);
    }

    if (aim_lock_x) mx = 0;
    if (aim_lock_y) my = 0;

    // Применяем overshoot если включен
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
        float t = static_cast<float>(elapsed) / 150.0f;  // 150ms на возврат
        if (t >= 1.0f) {
            g_in_overshoot = false;
            mx = static_cast<int>(g_overshoot_x * (1.0f - hum_return_speed));
            my = static_cast<int>(g_overshoot_y * (1.0f - hum_return_speed));
        } else {
            mx = static_cast<int>(g_overshoot_x * (1.0f - t * hum_return_speed));
            my = static_cast<int>(g_overshoot_y * (1.0f - t * hum_return_speed));
        }
    }

    // Добавляем в историю для pixelsmooth
    if (pixelsmooth_enabled && pixelsmooth_value > 1.0f) {
        g_move_history.push_back({mx, my});
        if (g_move_history.size() > static_cast<size_t>(pixelsmooth_value)) {
            g_move_history.erase(g_move_history.begin());
        }
        
        // Усредняем последние движения
        int sum_x = 0, sum_y = 0;
        for (const auto& m : g_move_history) {
            sum_x += m.first;
            sum_y += m.second;
        }
        mx = sum_x / static_cast<int>(g_move_history.size());
        my = sum_y / static_cast<int>(g_move_history.size());
    }

    // Применяем сглаживание через lerp
    if (smooth_factor > 0.0f && smooth_factor < 1.0f) {
        static int prev_mx = 0, prev_my = 0;
        mx = static_cast<int>(prev_mx * (1.0f - smooth_factor) + mx * smooth_factor);
        my = static_cast<int>(prev_my * (1.0f - smooth_factor) + my * smooth_factor);
        prev_mx = mx;
        prev_my = my;
    }

    // Рандомизация пути (path randomization)
    if (humanizer_enable && hum_path_randomization > 0.0f) {
        float jitter_x = gauss_dist(gen) * hum_path_randomization;
        float jitter_y = gauss_dist(gen) * hum_path_randomization;
        mx += static_cast<int>(jitter_x);
        my += static_cast<int>(jitter_y);
    }

    g_frac_x += static_cast<float>(mx);
    g_frac_y += static_cast<float>(my);
    int final_dx = static_cast<int>(g_frac_x);
    int final_dy = static_cast<int>(g_frac_y);
    g_frac_x -= static_cast<float>(final_dx);
    g_frac_y -= static_cast<float>(final_dy);

    if (final_dx == 0 && final_dy == 0) {
        hist_offset = (hist_offset + 1) % 100;
        VMProtectEnd();
        return;
    }

    if (humanizer_enable) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1 + (rand() % 3)));
    }

    SendHardwareMove(final_dx, final_dy);
    hist_offset = (hist_offset + 1) % 100;
    stat_tracking_time_ms += delta_t;

    VMProtectEnd();
}