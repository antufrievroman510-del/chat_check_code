#pragma once

#define NOMINMAX
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 
#endif

#include <winsock2.h>
#include <windows.h>
#include <vector>
#include <string>
#include <random>
#include <cmath>
#include <algorithm>
#include <chrono>

#include "detector.h"
#include "aim_kalman.h"
#include "AimbotTarget.h"
#include "AimMath.h"
#include "MouseController.h"

// ============================================================
// Класс Aimbot
// ============================================================
class Aimbot {
public:
    Aimbot();
    ~Aimbot();
    
    void SetConfig(const AimConfig& cfg);
    void Update(const std::vector<Detection>& detections, int screen_w, int screen_h,
        bool is_new_frame, long long current_time_ms, float zoom_scale = 1.0f);
    void ResetTarget();
    bool InitHardware();
    void CloseHardware();
    void SendHardwareMove(int x, int y);
    void SendHardwareClick();
    
    // Настройки (публичные для совместимости с меню и другими файлами)
    bool aim_enable = true;
    bool aim_target_lock = true;
    int aim_target = 0;  // 0=Auto, 1=Head, 2=Body
    int aim_key_main = VK_RBUTTON;
    float fov = 190.0f;
    float smooth_factor = 0.15f;
    int detection_resolution = 320;
    bool disable_headshot = false;
    bool humanizer_enable = true;
    float hum_reaction_delay = 25.0f;
    float hum_tremor_scale = 0.8f;
    bool hum_micro_movements = true;
    float hum_micro_amplitude = 0.5f;
    float hum_reaction_jitter = 1.5f;
    float hum_path_randomization = 0.3f;
    bool hum_overshoot_enabled = false;
    float hum_overshoot_chance = 3.0f;
    float hum_overshoot_amount = 1.2f;
    float hum_return_speed = 0.85f;
    float kalman_additional_prediction_ms = 0.0f;
    float prediction_interval = 0.01f;
    bool kalman_enabled = true;
    bool kalman_compensate_detection_delay = true;
    int kalman_warmup_frames = 2;
    float kalman_process_noise_position = 40.0f;
    float kalman_process_noise_velocity = 1800.0f;
    float kalman_measurement_noise = 35.0f;
    float kalman_velocity_damping = 0.08f;
    float kalman_max_velocity = 20000.0f;
    int hardware_type = 0;
    int com_port = 3;
    std::string net_ip = "192.168.1.100";
    int net_port = 3333;
    
    // Дополнительные настройки
    float min_sensitivity = 0.1f;
    float max_sensitivity = 20.0f;
    float max_move_step = 150.0f;
    bool enable_dynamic_fov = false;
    float target_offset_x = 0.0f;
    float target_offset_y = 0.0f;
    bool aim_lock_x = false;
    bool aim_lock_y = false;
    bool rcs_enable = false;
    float rcs_pitch = 1.0f;
    float rcs_yaw = 0.0f;
    bool elite_ballistics_enabled = false;
    float elite_bullet_speed = 800.0f;
    float elite_bullet_drop = 9.8f;
    bool pixelsmooth_enabled = true;
    float pixelsmooth_value = 8.0f;
    
    // Переменные состояния (публичные для доступа из main.cpp и overlay.cpp)
    float current_fov = 190.0f;
    int stat_shots_fired = 0;
    long long stat_tracking_time_ms = 0;
    
    // Elite функции (заглушки для совместимости)
    bool elite_context_aware = false;
    bool elite_smoke_vision = false;
    bool elite_voice_ctrl = false;
    bool elite_tsp_enabled = false;
    bool elite_shadow_trainer = false;
    std::string shadow_webhook = "";
    bool wind_mouse_enabled = false;

private:
    // Внутренняя конфигурация (использует структуру из AimMath.h)
    AimConfig m_config;
    
    // Методы класса
    std::pair<double, double> degToCounts(double degX, double degY) const;
    std::pair<double, double> calcMovement(double targetX, double targetY);
    double calculateSpeedMultiplier(double distance) const;
    double currentDetectionDelaySec() const;
    double currentPredictionLookaheadSec(double detectionDelaySec) const;
    std::pair<double, double> predictTargetPosition(double targetX, double targetY,
        std::chrono::steady_clock::time_point observationTime);
    void applyWindMouse(int& dx, int& dy);
    float AddJitter(float value, float amplitude);

    // Внутренние переменные
    MultiTargetTracker m_tracker;
    aim::AimKalman2D m_kalman;
    aim::AimKalmanTelemetry m_lastKalmanTelemetry;
    std::chrono::steady_clock::time_point m_lastPredictionTime;
    double m_lastDetectionDelaySec = 0.0;
    double m_lastPredictionLookaheadSec = 0.0;
    bool m_kalmanInitialized = false;
    
    double prev_target_x = 0.0, prev_target_y = 0.0;
    long long last_target_time = 0;
    float g_frac_x = 0.0f, g_frac_y = 0.0f;
    
    HANDLE hSerial = INVALID_HANDLE_VALUE;
    SOCKET udp_socket = INVALID_SOCKET;
    sockaddr_in udp_addr;
    
    float latency_hist[100] = { 0 };
    int hist_offset = 0;
    long long g_last_update_time = 0;
    
    // Состояние для pixelsmooth / overshoot
    std::vector<std::pair<int, int>> g_move_history;
    static const int MAX_MOVE_HISTORY = 16;
    float g_overshoot_x = 0.0f, g_overshoot_y = 0.0f;
    bool g_in_overshoot = false;
    long long g_overshoot_start_time = 0;
    
    thread_local static std::random_device rd;
    thread_local static std::mt19937 gen;
    thread_local static std::normal_distribution<float> gauss_dist;
};