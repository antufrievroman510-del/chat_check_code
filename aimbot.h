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

class Aimbot {
public:
    // === Настройки ===
    bool aim_enable = true;
    bool aim_target_lock = true;
    int aim_target = 0;
    int aim_key_main = VK_RBUTTON;
    int aim_key_sub = 0;
    int aim_toggle_key = 0;

    float fovX = 106.0f;
    float fovY = 74.0f;
    int detection_resolution = 320;

    float mouse_sensitivity = 1.0f;
    float mouse_yaw = 0.022f;
    float mouse_pitch = 0.022f;

    float min_speed_multiplier = 0.1f;
    float max_speed_multiplier = 0.1f;
    float snap_radius = 1.5f;
    float near_radius = 25.0f;
    float speed_curve_exponent = 3.0f;
    float snap_boost_factor = 1.15f;

    bool kalman_enabled = true;
    float kalman_process_noise_position = 40.0f;
    float kalman_process_noise_velocity = 1800.0f;
    float kalman_measurement_noise = 35.0f;
    float kalman_velocity_damping = 0.08f;
    float kalman_max_velocity = 20000.0f;
    int kalman_warmup_frames = 2;
    bool kalman_compensate_detection_delay = true;
    float kalman_additional_prediction_ms = 0.0f;
    float kalman_reset_timeout_sec = 0.5f;
    float prediction_interval = 0.01f;

    bool wind_mouse_enabled = false;
    float wind_G = 18.0f;
    float wind_W = 15.0f;
    float wind_M = 10.0f;
    float wind_D = 8.0f;

    float fov = 190.0f;
    float current_fov = 190.0f;
    bool enable_dynamic_fov = false;
    bool disable_headshot = false;
    float target_offset_x = 0.0f;
    float target_offset_y = 0.0f;
    bool aim_lock_x = false;
    bool aim_lock_y = false;
    bool rcs_enable = false;
    float rcs_pitch = 1.0f;
    float rcs_yaw = 0.0f;
    bool humanizer_enable = true;
    float hum_reaction_delay = 15.0f;
    float hum_tremor_scale = 1.2f;
    bool hum_micro_movements = true;
    float hum_micro_amplitude = 0.8f;
    float hum_reaction_jitter = 2.0f;

    float max_move_step = 50.0f;   // ДОБАВЛЕНО

    int hardware_type = 0;
    int com_port = 3;
    HANDLE hSerial = INVALID_HANDLE_VALUE;
    SOCKET udp_socket = INVALID_SOCKET;
    sockaddr_in udp_addr;
    std::string net_ip = "192.168.1.100";
    int net_port = 3333;

    bool elite_tsp_enabled = false;
    bool elite_ballistics_enabled = false;
    float elite_bullet_speed = 800.0f;
    float elite_bullet_drop = 9.8f;
    bool elite_context_aware = false;
    bool elite_smoke_vision = false;
    bool elite_voice_ctrl = false;
    bool elite_shadow_trainer = false;
    char shadow_webhook[256] = "";

    int stat_shots_fired = 0;
    long long stat_tracking_time_ms = 0;
    float latency_hist[100] = { 0 };
    int hist_offset = 0;

    bool InitHardware();
    void CloseHardware();
    void SendHardwareMove(int x, int y);
    void SendHardwareClick();
    void ResetTarget();
    void Update(const std::vector<Detection>& detections, int screen_w, int screen_h,
        bool is_new_frame, long long current_time_ms, float zoom_scale = 1.0f);

private:
    std::pair<double, double> degToCounts(double degX, double degY) const;
    std::pair<double, double> calcMovement(double targetX, double targetY);
    double calculateSpeedMultiplier(double distance) const;
    double currentDetectionDelaySec() const;
    double currentPredictionLookaheadSec(double detectionDelaySec) const;
    std::pair<double, double> predictTargetPosition(double targetX, double targetY,
        std::chrono::steady_clock::time_point observationTime);
    void applyWindMouse(int& dx, int& dy);
    float AddJitter(float value, float amplitude);

    MultiTargetTracker m_tracker;
    aim::AimKalman2D m_kalman;
    aim::AimKalmanTelemetry m_lastKalmanTelemetry;
    std::chrono::steady_clock::time_point m_lastPredictionTime;
    double m_lastDetectionDelaySec = 0.0;
    double m_lastPredictionLookaheadSec = 0.0;
    bool m_kalmanInitialized = false;

    double windCarryX = 0.0, windCarryY = 0.0;
    double windVelX = 0.0, windVelY = 0.0;
    double windNoiseX = 0.0, windNoiseY = 0.0;
    double windFracX = 0.0, windFracY = 0.0;
    double windPatternX = 0.0, windPatternY = 0.0;
    double windPatternPhaseA = 0.0, windPatternPhaseB = 0.0;
    double windPatternRateA = 0.0, windPatternRateB = 0.0;
    std::mt19937 windRng{ std::random_device{}() };

    float g_frac_x = 0.0f, g_frac_y = 0.0f;

    thread_local static std::random_device rd;
    thread_local static std::mt19937 gen;
    thread_local static std::normal_distribution<float> gauss_dist;

    double prev_target_x = 0.0, prev_target_y = 0.0;
    long long last_target_time = 0;
};