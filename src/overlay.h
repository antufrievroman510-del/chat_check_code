#pragma once

#include "WinHeaders.h"

#include <d3d11.h>
#include <vector>
#include <string>
#include <mutex>
#include <memory>
#include "detector.h"
#include "aimbot.h"
#include "network_2pc.h"
#include "imgui.h"
#include "implot.h"

class OneEuroFilter {
private:
    double mincutoff, beta, dcutoff;
    double x_prev, dx_prev, t_prev;
    bool first_time;
    double alpha(double cutoff, double dt);
public:
    OneEuroFilter(double mincutoff = 1.0, double beta = 0.0, double dcutoff = 1.0);
    void UpdateParams(double mc, double b);
    void Reset();
    double Filter(double x, double t);
};

extern std::vector<Detection> g_shared_heads;
extern std::mutex g_heads_mutex;

struct MacroStep { int type; int val; std::string display_text; };
struct ChatMessage { std::string text; bool is_user; };

class Overlay {
public:
    // ---- Состояние (все поля остаются как у вас) ----
    bool is_loading = true;
    float load_progress = 0.0f;
    int current_tip_idx = 0;
    bool is_auto_logging_in = false;
    float auto_login_anim_time = 0.0f;
    bool is_authenticated = false;
    bool is_register_mode = false;
    char auth_username[64] = "";
    char auth_password[64] = "";
    std::string auth_status_msg = "";
    float auth_status_col[3] = { 1,1,1 };
    float last_auth_check_time = 0.0f;
    std::string user_expiry_date = "1970-01-01 00:00:00";

    bool draw_esp = true;
    bool draw_fov = true;
    bool draw_fov_neural = true;
    bool draw_watermark = true;
    bool draw_crosshair = false;
    bool disable_all_visuals_when_hidden = true;
    float esp_thickness = 1.5f;
    int esp_style = 0;
    float color_esp_visible[3] = { 1,0.2f,0.2f };
    float color_esp_hidden[3] = { 0.7f,0.2f,1 };
    bool esp_oe_enable = true;
    float esp_oe_mincutoff = 0.5f;
    float esp_oe_beta = 0.01f;
    OneEuroFilter espFilterX, espFilterY, espFilterW, espFilterH;
    bool was_empty_last_frame = true;

    bool aim_enable = true;
    float aim_max_sens = 2.0f;
    float aim_min_sens = 1.0f;
    float aim_smoother = 10.0f;
    bool sticky_aim = false;
    float aim_kill_delay = 0.09f;
    int aim_target = 0;
    int aim_key_main = VK_RBUTTON;
    int aim_key_sub = 0;
    int aim_toggle_key = 0;
    float aim_deadzone = 1.0f;
    bool enable_pose_adaptive = false;
    bool enable_dynamic_fov = false;
    bool aim_target_lock = true;
    int aim_target_priority = 0;
    bool aim_dynamic_smooth = false;
    int aim_switch_delay = 0;
    bool aim_lock_x = false;
    bool aim_lock_y = false;
    int aim_curve_type = 0;
    bool kalman_enable = true;
    float kalman_q = 0.05f;
    float kalman_r = 0.2f;
    bool oe_enable = true;
    float oe_mincutoff = 1.0f;
    float oe_beta = 0.05f;
    bool confidence_fusion_enable = true;
    bool pid_enable = false;
    float pid_kp = 0.2f;
    float pid_ki = 0.01f;
    float pid_kd = 0.1f;
    bool enable_spoofer = false;
    char spoofer_vid[5] = "046D";
    char spoofer_pid[5] = "C07E";
    bool auto_spoof = false;
    bool aim_flicker = false;
    float flick_speed = 2.0f;
    int aim_flicker_key = 0;
    bool humanizer_enable = true;
    float hum_reaction_delay = 15.0f;
    bool hum_randomize_bone = false;
    float hum_tremor_scale = 1.0f;
    bool enable_exclusion_zone = false;
    bool motion_tuning_enabled = false;
    bool is_drawing_zone = false;
    float excl_x1 = 0, excl_y1 = 0, excl_x2 = 0, excl_y2 = 0;
    float fov_aimbot = 190.0f, fov_scan = 192.0f;
    float aim_offset_x = 0, aim_offset_y = 0;
    int ai_model = 0;
    float ai_confidence_body = 48.0f;
    float ai_confidence_head = 35.0f;
    bool auto_confidence = false;
    float hit_chance = 90.0f;
    bool apply_model_flag = false;
    float min_box_area_body = 150.0f;
    float min_box_area_head = 40.0f;
    float neural_nms = 0.45f;
    int neural_max_det = 5;
    int refresh_rate_idx = 0;
    bool is_first_frame_init = true;
    int memory_enemy_frames = 3;
    bool eco_mode = true;
    bool trigger_enable = false;
    float trigger_delay = 0.05f;
    int trigger_target = 0;
    int trigger_key = 0;
    bool rcs_enable = false;
    float rcs_pitch = 1.0f;
    float rcs_yaw = 0.0f;
    int macro_key = 0;
    int current_macro_action = 0;
    int macro_input_val = 50;
    std::vector<MacroStep> macro_sequence;
    int selected_script = 0;
    char ai_prompt_input[256] = "";
    bool is_generating_script = false;
    float script_gen_progress = 0.0f;
    std::string generated_script_code = "";
    int custom_res_w = 1920;
    int custom_res_h = 1080;
    int active_res_w = 1920, active_res_h = 1080;
    bool apply_res_flag = false;
    bool obs_bypass = true;
    float menu_scale = 100.0f;
    float menu_width = 1150.0f;
    float menu_height = 720.0f;
    bool unload_flag = false;
    int hardware_type = 0;
    int com_port = 2;
    bool apply_hw_flag = false;
    float accent_color[3] = { 0,0.8f,1 };
    bool is_benchmarking = false;
    float bench_prog = 0.0f;
    bool bench_done = false;
    int target_monitor = 0;
    bool enable_dma_fuser = false;
    float last_ping_ms = -1.0f;
    int selected_preset = 0;
    bool show_hw_tutorial = false;
    int tutorial_hw_selected = 0;
    bool is_menu_open = false;
    bool is_russian = true;
    double lifetime_seconds = 0.0;
    char hwid_str[64] = "UNKNOWN", cpu_str[128] = "CPU", gpu_str[128] = "GPU", os_str[64] = "Windows 10/11";
    bool show_ai_chat = false;
    char chat_input[512] = "";
    std::vector<ChatMessage> chat_history;
    bool elite_tsp_enabled = false;
    bool elite_ballistics_enabled = false;
    float elite_bullet_speed = 800.0f;
    float elite_bullet_drop = 9.8f;
    bool elite_context_aware = false;
    bool elite_smoke_vision = false;
    bool elite_voice_ctrl = false;
    bool elite_shadow_trainer = false;
    char shadow_webhook[256] = "";
    float max_move_step = 50.0f;
    float lock_radius_base = 200.0f;
    float lock_radius_scale = 3.0f;
    float head_height_ratio = 0.1f;
    float sticky_zone_factor = 0.5f;
    float sticky_damping = 0.3f;
    bool hum_micro_movements = true;
    float hum_micro_amplitude = 0.8f;
    float hum_reaction_jitter = 2.0f;
    float hum_path_randomization = 0.3f;
    bool hum_overshoot_enabled = false;
    float hum_overshoot_chance = 5.0f;
    float hum_overshoot_amount = 1.2f;
    float hum_return_speed = 0.85f;
    bool pixelsmooth_enabled = true;
    float pixelsmooth_value = 8.0f;
    float smooth_factor = 0.15f;
    float byte_track_thresh = 0.5f;
    int byte_track_buffer = 30;
    float byte_match_thresh = 0.8f;
    int byte_frame_rate = 30;
    int byte_active_tracks = 0;
    float byte_avg_tracklet_len = 0.0f;
    float byte_avg_speed = 0.0f;
    bool use_advanced_sticky_aim = true;
    float sticky_threshold = 50.0f;
    int sticky_frames_keep = 3;
    int prediction_method = 1;

    float min_sensitivity = 0.5f;
    float max_sensitivity = 3.0f;
    bool kalman_compensate_detection_delay = true;
    float kalman_additional_prediction_ms = 0.0f;
    float prediction_interval = 0.01f;
    bool disable_headshot = false;
    int detection_resolution = 960;

    // Hardware 2PC Settings
    char com_port_buf[32] = "COM3";
    int baud_rate_idx = 4; // 115200
    char kmbox_ip_buf[32] = "192.168.1.100";
    int kmbox_port = 8888;
    int hardware_mode_idx = 0; // 0=Local, 1=Macku, 2=KMbox
    int bypass_mode_idx = 0;   // 0=None, 1=GHub, 2=Razer, 3=Random
    int random_delay_min = 10;
    int random_delay_max = 30;
    bool hw_enabled = false;
    
    // Test values
    int test_move_x = 50;
    int test_move_y = 50;

    // ---- Основные методы ----
    bool Initialize();
    bool Update();
    void Render(const std::vector<Detection>& detections, int screen_w, int screen_h, int roi_w, int roi_h, Aimbot* aim, bool show_menu);
    void ToggleClickability(bool clickable);
    void Cleanup(Aimbot* aim);
    void SaveConfig(Aimbot* aim);
    void ResetDefaults();
    void ApplySafeSettings();
    void DeepCleanTraces();
    void SpoofMAC();
    void LoadAuth();
    void SaveAuth();
    void ProcessChatInput(std::string input);
    void RenderChatWindow();
    void RenderHWTutorial();
    std::string ToLower(std::string s);

    // ---- Методы вкладок (объявлены, реализованы в overlay.cpp из старого) ----
    void RenderAimbotTab(float content_w, float content_h, const ImVec4& acc_vec, ImU32 acc_u32, bool& cfg_changed);
    void RenderVisualsTab(float content_w, float content_h, const ImVec4& acc_vec, ImU32 acc_u32, bool& cfg_changed);
    void RenderNeuralTab(float content_w, float content_h, const ImVec4& acc_vec, ImU32 acc_u32, bool& cfg_changed);
    void RenderPwnzAITab(float content_w, float content_h, const ImVec4& acc_vec, ImU32 acc_u32, bool& cfg_changed);
    void RenderProfileTab(float content_w, float content_h, const ImVec4& acc_vec, ImU32 acc_u32, bool& cfg_changed);
    void RenderHardwareTab(float content_w, float content_h, const ImVec4& acc_vec, ImU32 acc_u32, bool& cfg_changed);
    void RenderSecurityTab(float content_w, float content_h, const ImVec4& acc_vec, ImU32 acc_u32, bool& cfg_changed);
    void RenderTelemetryTab(float content_w, float content_h, const ImVec4& acc_vec, ImU32 acc_u32, bool& cfg_changed);
    void RenderHWCheckTab(float content_w, float content_h, const ImVec4& acc_vec, ImU32 acc_u32, bool& cfg_changed);
    void RenderXTierTab(float content_w, float content_h, const ImVec4& acc_vec, ImU32 acc_u32, bool& cfg_changed);
    void RenderMetricsTab(float content_w, float content_h, const ImVec4& acc_vec, ImU32 acc_u32, bool& cfg_changed);

    // ---- Новые методы для разбивки Render ----
    void RenderAutoLogin(const ImVec4& acc_vec, ImU32 acc_u32);
    void RenderLoadingScreen(const ImVec4& acc_vec, ImU32 acc_u32);
    void RenderAuthWindow(const ImVec4& acc_vec, ImU32 acc_u32);
    void RenderVisualsAndZone(const std::vector<Detection>& detections, Aimbot* aim);
    void RenderMenu(const std::vector<Detection>& detections, Aimbot* aim, bool& cfg_changed);

private:
    bool DrawToggleOnly(const char* str_id, bool* v, ImU32 accent_u32);
    bool DrawToggle(const char* label, const char* str_id, bool* v, ImU32 accent_u32, const char* help = nullptr);
    bool CustomSliderFloat(const char* label, const char* label_id, float* v, float v_min, float v_max, const char* format, ImVec4 accent_vec, const char* help = nullptr);
    bool CustomSliderInt(const char* label, const char* label_id, int* v, int v_min, int v_max, const char* format, ImVec4 accent_vec, const char* help = nullptr);
    bool CustomCombo(const char* label, const char* label_id, int* current_item, const char* const items[], int items_count, const char* help = nullptr);
    bool DrawKeybinder(const char* label, int* vk_key, int id, const char* help = nullptr);
    void HelpMarker(const char* desc);
    bool BeginPanel(const char* name, ImVec2 size, ImVec4 accent_vec, bool has_toggle = false, bool* toggle_val = nullptr, ImU32 accent_u32 = 0);
    void EndPanel();

    static int active_tab;
    static int active_bind_id;

    HWND hwnd;
    ID3D11Device* g_pd3dDevice = nullptr;
    ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
    IDXGISwapChain* g_pSwapChain = nullptr;
    ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
    UINT sync_interval = 0;

    bool CreateDeviceD3D(HWND hWnd);
    void CleanupDeviceD3D();
    void CreateRenderTarget();
    void CleanupRenderTarget();
    void LoadConfig(Aimbot* aim);
    void FetchHardwareInfo();

    // 2PC Network module
    std::unique_ptr<Network2PC> network_2pc;
};