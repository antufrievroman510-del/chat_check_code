#define WINVER 0x0601
#define _WIN32_WINNT 0x0601
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <iostream>
#include <chrono>
#include <windows.h>
#include <vector>
#include <thread>  
#include <mutex>   
#include <atomic>  
#include <algorithm>
#include <cmath>
#include <string>
#include <intrin.h> 
#include <map>
#include <deque>
#include <set>
#include <locale.h>    
#include <mmsystem.h>  
#include <winsock2.h>
#include <cstring>
#include <fstream>

#pragma comment(lib, "Winmm.lib")
#pragma comment(lib, "ws2_32.lib")

#include "capture.h"
#include "detector.h"
#include "overlay.h"
#include "aimbot.h"
#include "auth.h" 
#include "protect.h"
#include "xorstr.hpp"
#include "VMProtectSDK.h"
#include "head_smoother.h"

#define BUILDING_BYTE_TRACK_EIGEN
#include "BYTETracker.h"

// ==================== ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ====================
std::atomic<bool> g_running(true);
std::mutex g_det_mutex;
std::vector<Detection> g_shared_detections;
bool g_new_detections = false;

std::vector<Detection> g_shared_bodies;
std::mutex g_bodies_mutex;

std::vector<Detection> g_shared_heads;
std::mutex g_heads_mutex;

std::atomic<float> g_last_inference_time(0.0f);
std::atomic<float> g_last_capture_time(0.0f);

std::atomic<bool> g_is_target_locked(false);
std::atomic<float> g_locked_screen_x(0.0f);
std::atomic<float> g_locked_screen_y(0.0f);

std::atomic<float> g_current_zoom(1.0f);

std::mutex g_model_mutex;

int g_capture_w = 1920;
int g_capture_h = 1080;

std::atomic<bool> g_remote_aim_key(false);
SOCKET g_udp_sock = INVALID_SOCKET;

// ========== ГЛОБАЛЬНЫЕ МЕТРИКИ ==========
std::atomic<int> g_byte_active_tracks(0);
std::atomic<float> g_byte_avg_tracklet_len(0.0f);
std::atomic<float> g_byte_avg_speed(0.0f);
std::atomic<float> g_track_loss_rate(0.0f);
std::atomic<float> g_aim_overshoot_ratio(0.0f);
std::atomic<float> g_aim_motion_jerk(0.0f);
std::atomic<float> g_inference_jitter(0.0f);
std::deque<float> g_inference_history;
std::mutex g_metrics_mutex;

HeadSmoother g_head_smoother;

struct SafeConfig {
    float ai_confidence_body, ai_confidence_head;
    float min_box_area_body, min_box_area_head;
    float neural_nms;
    int neural_max_det;
    float fov_scan;
    bool enable_exclusion_zone;
    float excl_x1, excl_y1, excl_x2, excl_y2;
    int memory_enemy_frames;
    int aim_target;
    float aim_offset_x, aim_offset_y;
    bool enable_pose_adaptive;
    float fov_aimbot;
    float aim_min_sens, aim_max_sens;
    float aim_smoother;
    bool sticky_aim;
    float aim_deadzone;
    bool aim_target_lock;
    int aim_target_priority;
    bool aim_dynamic_smooth;
    int aim_switch_delay;
    bool aim_lock_x, aim_lock_y;
    int aim_curve_type;
    bool kalman_enable;
    float kalman_q, kalman_r;
    bool oe_enable;
    float oe_mincutoff, oe_beta;
    bool aim_flicker;
    float flick_speed;
    int aim_flicker_key;
    bool rcs_enable;
    float rcs_pitch, rcs_yaw;
    bool humanizer_enable;
    float hum_reaction_delay, hum_overshoot_chance;
    bool hum_randomize_bone;
    float hum_tremor_scale;
    bool eco_mode;
    bool aim_enable;
    bool elite_tsp_enabled, elite_ballistics_enabled;
    float elite_bullet_speed, elite_bullet_drop;
    bool elite_context_aware, elite_smoke_vision, elite_voice_ctrl, elite_shadow_trainer;
    char shadow_webhook[256];
    float byte_track_thresh;
    int byte_track_buffer;
    float byte_match_thresh;
    int byte_frame_rate;
    int com_port;
    bool use_advanced_sticky_aim;
    float sticky_threshold;
    int sticky_frames_keep;
    int prediction_method;
    float max_move_step;
    float sticky_zone_factor;
    float head_height_ratio;
    bool hum_micro_movements;
    float hum_micro_amplitude;
    float hum_reaction_jitter;

    // НОВЫЕ ПОЛЯ ДЛЯ АИМБОТА (Sunone)
    int detection_resolution;
    float mouse_sensitivity;
    float mouse_yaw;
    float mouse_pitch;
    float fovX;
    float fovY;
    float min_speed_multiplier;
    float max_speed_multiplier;
    float snap_radius;
    float near_radius;
    float speed_curve_exponent;
    float snap_boost_factor;
    bool kalman_compensate_detection_delay;
    float kalman_additional_prediction_ms;
    float kalman_reset_timeout_sec;
    float prediction_interval;
    bool disable_headshot;
};
SafeConfig g_safe_cfg;
std::mutex g_cfg_mutex;

// ==================== ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ====================
bool IsAimKeyPressed(Overlay* overlay) {
    MUTATE_SIGNATURE;
    if (overlay->aim_key_main != 0 && (GetAsyncKeyState(overlay->aim_key_main) & 0x8000)) return true;
    if (overlay->aim_key_sub != 0 && (GetAsyncKeyState(overlay->aim_key_sub) & 0x8000)) return true;
    return false;
}

void GetModelSize(int model_idx, int& out_w, int& out_h) {
    if (model_idx == 0) { out_w = 512; out_h = 288; }
    else if (model_idx == 1) { out_w = 736; out_h = 416; }
    else if (model_idx == 2) { out_w = 512; out_h = 288; }
    else if (model_idx == 3) { out_w = 736; out_h = 416; }
    else { out_w = 736; out_h = 416; }
}

inline void DownscaleImage(const unsigned char* src, int src_w, int src_h, unsigned char* dst, int dst_w, int dst_h) {
    float scale_x = (float)src_w / dst_w;
    float scale_y = (float)src_h / dst_h;
    for (int y = 0; y < dst_h; ++y) {
        int py = (std::min)(static_cast<int>(y * scale_y), src_h - 1);
        for (int x = 0; x < dst_w; ++x) {
            int px = (std::min)(static_cast<int>(x * scale_x), src_w - 1);
            int src_idx = (py * src_w + px) * 4;
            int dst_idx = (y * dst_w + x) * 4;
            dst[dst_idx] = src[src_idx];
            dst[dst_idx + 1] = src[src_idx + 1];
            dst[dst_idx + 2] = src[src_idx + 2];
            dst[dst_idx + 3] = src[src_idx + 3];
        }
    }
}

bool EnsureFP16Model(const std::string& model_path, std::string& out_fp16_path) {
    if (model_path.find("_fp16.onnx") != std::string::npos) {
        out_fp16_path = model_path;
        return true;
    }
    std::string fp16_path = model_path;
    size_t dot_pos = fp16_path.rfind('.');
    if (dot_pos != std::string::npos) fp16_path.insert(dot_pos, "_fp16");
    else fp16_path += "_fp16.onnx";
    out_fp16_path = fp16_path;
    if (GetFileAttributesA(fp16_path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        std::cout << "[FP16] Found existing FP16 model: " << fp16_path << std::endl;
        return true;
    }
    std::cout << "[FP16] Converting model to FP16 (this may take a few seconds)..." << std::endl;
    std::string cmd = "python -c \"import onnx; from onnxconverter_common import float16; "
        "model = onnx.load('" + model_path + "'); "
        "model_fp16 = float16.convert_float_to_float16(model); "
        "onnx.save(model_fp16, '" + fp16_path + "')\"";
    int ret = system(cmd.c_str());
    if (ret != 0) {
        std::cerr << "[FP16] Conversion failed. Using original FP32 model." << std::endl;
        out_fp16_path = model_path;
        return false;
    }
    std::cout << "[FP16] Conversion completed: " << fp16_path << std::endl;
    return true;
}

// ==================== ФУНКЦИЯ ДЛЯ СПУФЕРА ====================
bool FindAndSpoofArduino(const char* target_vid, const char* target_pid) {
    HKEY hKeyUsb;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
        "SYSTEM\\CurrentControlSet\\Enum\\USB", 0, KEY_READ, &hKeyUsb) != ERROR_SUCCESS) {
        return false;
    }
    bool found = false;
    DWORD index = 0;
    char subkeyName[256];
    DWORD size = sizeof(subkeyName);
    while (RegEnumKeyExA(hKeyUsb, index++, subkeyName, &size, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
        if (strstr(subkeyName, "VID_2341") && (strstr(subkeyName, "PID_8036") || strstr(subkeyName, "PID_0036"))) {
            found = true;
            break;
        }
        size = sizeof(subkeyName);
    }
    RegCloseKey(hKeyUsb);
    if (found) {
        printf("[Spoofer] Found Arduino Leonardo. Spoofing to %s:%s\n", target_vid, target_pid);
        std::string cmd = "python spoofer.py " + std::string(target_vid) + " " + std::string(target_pid);
        system(cmd.c_str());
        return true;
    }
    return false;
}

// ==================== АСИНХРОННЫЙ ПОТОК ИНФЕРЕНСА ====================
void InferenceThread(DXGICapture* cap, Detector* det, Overlay* overlay) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    unsigned char* capture_buffers[2] = {
        new unsigned char[3840 * 2160 * 4],
        new unsigned char[3840 * 2160 * 4]
    };
    unsigned char* zoom_buffers[2] = {
        new unsigned char[3840 * 2160 * 4],
        new unsigned char[3840 * 2160 * 4]
    };

    std::vector<Detection> detections_buffers[2];
    int current_write = 0;
    int current_read = 0;
    bool buffer_ready[2] = { false, false };

    static std::unique_ptr<BYTETracker> body_tracker = nullptr;
    static std::unique_ptr<BYTETracker> head_tracker = nullptr;
    static int last_track_thresh_int = -1, last_track_buffer_int = -1, last_match_thresh_int = -1, last_frame_rate_int = -1;
    static int last_head_thresh_int = -1, last_head_buffer_int = -1;

    static std::map<int, float> smooth_rel_y;
    static std::map<int, float> smooth_rel_x;

    static long long last_track_loss_time = 0;
    static int total_track_losses = 0;
    static int prev_active_track_count = 0;
    static std::set<int> prev_track_ids;

    while (g_running) {
        if (overlay->unload_flag) break;

        SafeConfig local_cfg;
        {
            std::lock_guard<std::mutex> lock(g_cfg_mutex);
            memcpy(&local_cfg, &g_safe_cfg, sizeof(SafeConfig));
        }

        bool is_aiming = IsAimKeyPressed(overlay);
        bool stealth_active = !overlay->is_menu_open && overlay->disable_all_visuals_when_hidden && !overlay->is_drawing_zone;
        bool actually_drawing_visuals = (overlay->draw_esp || overlay->draw_fov || overlay->draw_fov_neural) && !stealth_active;
        bool should_infer = overlay->is_menu_open || actually_drawing_visuals || is_aiming || overlay->is_drawing_zone;

        if (!should_infer) {
            std::lock_guard<std::mutex> lock(g_det_mutex);
            g_shared_detections.clear();
            g_new_detections = false;
            std::lock_guard<std::mutex> lock_b(g_bodies_mutex);
            g_shared_bodies.clear();
            std::lock_guard<std::mutex> lock_h(g_heads_mutex);
            g_shared_heads.clear();
            Sleep(10);
            continue;
        }

        int cur_thresh = (int)(local_cfg.byte_track_thresh * 100);
        int cur_buffer = local_cfg.byte_track_buffer;
        int cur_match = (int)(local_cfg.byte_match_thresh * 100);
        int cur_fps = local_cfg.byte_frame_rate;
        if (!body_tracker || last_track_thresh_int != cur_thresh || last_track_buffer_int != cur_buffer ||
            last_match_thresh_int != cur_match || last_frame_rate_int != cur_fps) {
            body_tracker = std::make_unique<BYTETracker>(
                local_cfg.byte_track_thresh, local_cfg.byte_track_buffer,
                local_cfg.byte_match_thresh, local_cfg.byte_frame_rate);
            last_track_thresh_int = cur_thresh;
            last_track_buffer_int = cur_buffer;
            last_match_thresh_int = cur_match;
            last_frame_rate_int = cur_fps;
            smooth_rel_y.clear();
            smooth_rel_x.clear();
        }

        int head_thresh = std::max(15, cur_thresh / 2);
        int head_buffer = cur_buffer + 15;
        if (!head_tracker || last_head_thresh_int != head_thresh || last_head_buffer_int != head_buffer) {
            head_tracker = std::make_unique<BYTETracker>(
                head_thresh / 100.0f, head_buffer,
                local_cfg.byte_match_thresh, local_cfg.byte_frame_rate);
            last_head_thresh_int = head_thresh;
            last_head_buffer_int = head_buffer;
        }

        int current_yolo_w = 960, current_yolo_h = 544;
        {
            std::lock_guard<std::mutex> mod_lock(g_model_mutex);
            current_yolo_w = det->get_width();
            current_yolo_h = det->get_height();
        }
        if (current_yolo_w < 100 || current_yolo_h < 100) { Sleep(5); continue; }

        float current_zoom = 1.0f;
        g_current_zoom.store(current_zoom);
        int capture_w = (std::min)(static_cast<int>(current_yolo_w * current_zoom), g_capture_w);
        int capture_h = (std::min)(static_cast<int>(current_yolo_h * current_zoom), g_capture_h);
        int roi_screen_x = (g_capture_w / 2) - (capture_w / 2);
        int roi_screen_y = (g_capture_h / 2) - (capture_h / 2);

        auto cap_start = std::chrono::high_resolution_clock::now();
        bool frame_captured = false;
        if (current_zoom > 1.0f) {
            frame_captured = cap->GetHardwareROIFrame(zoom_buffers[current_write], roi_screen_x, roi_screen_y, capture_w, capture_h);
            if (frame_captured) DownscaleImage(zoom_buffers[current_write], capture_w, capture_h, capture_buffers[current_write], current_yolo_w, current_yolo_h);
        }
        else {
            frame_captured = cap->GetHardwareROIFrame(capture_buffers[current_write], roi_screen_x, roi_screen_y, capture_w, capture_h);
        }
        auto cap_end = std::chrono::high_resolution_clock::now();
        g_last_capture_time = std::chrono::duration<float, std::milli>(cap_end - cap_start).count();

        if (frame_captured) {
            auto infer_start = std::chrono::high_resolution_clock::now();
            std::vector<Detection> current_frame_raw;
            {
                std::lock_guard<std::mutex> mod_lock(g_model_mutex);
                float body_conf = local_cfg.ai_confidence_body / 100.0f;
                float head_conf = local_cfg.ai_confidence_head / 100.0f;
                if (overlay->auto_confidence && g_is_target_locked.load()) {
                    body_conf = (std::max)(0.35f, body_conf - 0.05f);
                    head_conf = (std::max)(0.25f, head_conf - 0.05f);
                }
                current_frame_raw = det->run_inference(capture_buffers[current_write], current_yolo_w, current_yolo_h,
                    body_conf, head_conf, local_cfg.neural_nms, local_cfg.neural_max_det,
                    local_cfg.elite_smoke_vision);
            }
            auto infer_end = std::chrono::high_resolution_clock::now();
            float infer_ms = std::chrono::duration<float, std::milli>(infer_end - infer_start).count();
            g_last_inference_time = infer_ms;

            {
                std::lock_guard<std::mutex> lock(g_metrics_mutex);
                g_inference_history.push_back(infer_ms);
                if (g_inference_history.size() > 100) g_inference_history.pop_front();
                if (g_inference_history.size() >= 10) {
                    float sum = 0.0f, sum_sq = 0.0f;
                    for (float v : g_inference_history) { sum += v; sum_sq += v * v; }
                    float mean = sum / g_inference_history.size();
                    float variance = (sum_sq / g_inference_history.size()) - (mean * mean);
                    g_inference_jitter = std::sqrt(std::max(0.0f, variance));
                }
            }

            for (auto& d : current_frame_raw) {
                d.box.x = d.box.x * current_zoom + roi_screen_x;
                d.box.y = d.box.y * current_zoom + roi_screen_y;
                d.box.w *= current_zoom;
                d.box.h *= current_zoom;
            }
            detections_buffers[current_write] = std::move(current_frame_raw);
            buffer_ready[current_write] = true;
            current_write = 1 - current_write;
        }

        if (buffer_ready[current_read]) {
            std::vector<Detection> filtered_raw = std::move(detections_buffers[current_read]);
            buffer_ready[current_read] = false;

            float scan_radius_sq = local_cfg.fov_scan * local_cfg.fov_scan;
            float center_x = g_capture_w / 2.0f, center_y = g_capture_h / 2.0f;
            std::vector<Detection> filtered;
            for (auto& d : filtered_raw) {
                float cx = d.box.x + d.box.w / 2, cy = d.box.y + d.box.h / 2;
                if (local_cfg.enable_exclusion_zone && (local_cfg.excl_x2 - local_cfg.excl_x1 > 0)) {
                    if (cx >= local_cfg.excl_x1 && cx <= local_cfg.excl_x2 && cy >= local_cfg.excl_y1 && cy <= local_cfg.excl_y2) continue;
                }
                float dx = cx - center_x, dy = cy - center_y;
                if (dx * dx + dy * dy <= scan_radius_sq) filtered.push_back(d);
            }

            std::vector<Detection> body_dets, head_dets;
            for (const auto& d : filtered) {
                if (d.class_id == 0) body_dets.push_back(d);
                else if (d.class_id == 1) head_dets.push_back(d);
            }

            Eigen::MatrixXf body_detection_matrix(body_dets.size(), 5);
            int row = 0;
            for (const auto& d : body_dets) {
                body_detection_matrix(row, 0) = d.box.x;
                body_detection_matrix(row, 1) = d.box.y;
                body_detection_matrix(row, 2) = d.box.w;
                body_detection_matrix(row, 3) = d.box.h;
                body_detection_matrix(row, 4) = d.confidence;
                row++;
            }
            body_detection_matrix.conservativeResize(row, 5);
            std::vector<KalmanBBoxTrack> body_tracks;
            if (row > 0) body_tracks = body_tracker->process_frame_detections(body_detection_matrix);

            Eigen::MatrixXf head_detection_matrix(head_dets.size(), 5);
            row = 0;
            for (const auto& d : head_dets) {
                head_detection_matrix(row, 0) = d.box.x;
                head_detection_matrix(row, 1) = d.box.y;
                head_detection_matrix(row, 2) = d.box.w;
                head_detection_matrix(row, 3) = d.box.h;
                head_detection_matrix(row, 4) = d.confidence;
                row++;
            }
            head_detection_matrix.conservativeResize(row, 5);
            std::vector<KalmanBBoxTrack> head_tracks;
            if (row > 0) head_tracks = head_tracker->process_frame_detections(head_detection_matrix);

            std::vector<Detection> final_bodies;
            std::set<int> current_track_ids;
            for (const auto& track : body_tracks) {
                Detection body;
                body.class_id = 0;
                body.confidence = track.get_score();
                auto tlwh = track.tlwh();
                body.box.x = (float)tlwh(0);
                body.box.y = (float)tlwh(1);
                body.box.w = (float)tlwh(2);
                body.box.h = (float)tlwh(3);
                body.track_id = track.get_track_id();
                final_bodies.push_back(body);
                if (body.track_id != -1) current_track_ids.insert(body.track_id);
            }

            {
                long long now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                if (last_track_loss_time == 0) last_track_loss_time = now_ms;
                for (int old_id : prev_track_ids) {
                    if (current_track_ids.find(old_id) == current_track_ids.end()) {
                        total_track_losses++;
                    }
                }
                float elapsed_sec = (now_ms - last_track_loss_time) / 1000.0f;
                if (elapsed_sec >= 1.0f) {
                    g_track_loss_rate = total_track_losses / elapsed_sec;
                    total_track_losses = 0;
                    last_track_loss_time = now_ms;
                }
                prev_track_ids = current_track_ids;
                g_byte_active_tracks = (int)current_track_ids.size();
            }

            std::vector<bool> head_track_matched(head_tracks.size(), false);
            std::vector<Detection> final_heads;
            const float IOU_MATCH_THRESH = 0.3f;
            for (const auto& body_track : body_tracks) {
                Eigen::Vector4d body_tlwh = body_track.tlwh();
                int best_idx = -1;
                float best_iou = IOU_MATCH_THRESH;
                for (size_t i = 0; i < head_tracks.size(); ++i) {
                    if (head_track_matched[i]) continue;
                    Eigen::Vector4d head_tlwh = head_tracks[i].tlwh();
                    float inter_x1 = std::max(body_tlwh(0), head_tlwh(0));
                    float inter_y1 = std::max(body_tlwh(1), head_tlwh(1));
                    float inter_x2 = std::min(body_tlwh(0) + body_tlwh(2), head_tlwh(0) + head_tlwh(2));
                    float inter_y2 = std::min(body_tlwh(1) + body_tlwh(3), head_tlwh(1) + head_tlwh(3));
                    if (inter_x2 > inter_x1 && inter_y2 > inter_y1) {
                        float inter_area = (inter_x2 - inter_x1) * (inter_y2 - inter_y1);
                        float body_area = body_tlwh(2) * body_tlwh(3);
                        float head_area = head_tlwh(2) * head_tlwh(3);
                        float iou = inter_area / (body_area + head_area - inter_area);
                        if (iou > best_iou) {
                            best_iou = iou;
                            best_idx = (int)i;
                        }
                    }
                }
                if (best_idx != -1) {
                    Detection head;
                    head.class_id = 1;
                    head.confidence = head_tracks[best_idx].get_score();
                    auto tlwh = head_tracks[best_idx].tlwh();
                    head.box.x = (float)tlwh(0);
                    head.box.y = (float)tlwh(1);
                    head.box.w = (float)tlwh(2);
                    head.box.h = (float)tlwh(3);
                    head.track_id = body_track.get_track_id();
                    final_heads.push_back(head);
                    head_track_matched[best_idx] = true;
                }
            }
            for (size_t i = 0; i < head_tracks.size(); ++i) {
                if (!head_track_matched[i]) {
                    Detection head;
                    head.class_id = 1;
                    head.confidence = head_tracks[i].get_score();
                    auto tlwh = head_tracks[i].tlwh();
                    head.box.x = (float)tlwh(0);
                    head.box.y = (float)tlwh(1);
                    head.box.w = (float)tlwh(2);
                    head.box.h = (float)tlwh(3);
                    head.track_id = -1;
                    final_heads.push_back(head);
                }
            }

            std::map<int, Detection> body_map;
            for (const auto& body : final_bodies) {
                if (body.track_id != -1) {
                    body_map[body.track_id] = body;
                }
            }

            for (auto& head : final_heads) {
                if (head.track_id == -1) continue;
                auto it = body_map.find(head.track_id);
                if (it == body_map.end()) continue;

                const Detection& body = it->second;
                float shoulder_y = body.box.y + body.box.h * 0.25f;
                float head_center_x = head.box.x + head.box.w / 2.0f;
                float head_center_y = head.box.y + head.box.h / 2.0f;
                float body_center_x = body.box.x + body.box.w / 2.0f;

                float rel_y = (head_center_y - shoulder_y) / body.box.h;
                float rel_x = (head_center_x - body_center_x) / body.box.w;

                int tid = head.track_id;
                if (smooth_rel_y.find(tid) == smooth_rel_y.end()) {
                    smooth_rel_y[tid] = rel_y;
                    smooth_rel_x[tid] = rel_x;
                }
                else {
                    smooth_rel_y[tid] = smooth_rel_y[tid] * 0.8f + rel_y * 0.2f;
                    smooth_rel_x[tid] = smooth_rel_x[tid] * 0.8f + rel_x * 0.2f;
                }

                float stable_head_y = shoulder_y + smooth_rel_y[tid] * body.box.h;
                float stable_head_x = body_center_x + smooth_rel_x[tid] * body.box.w;

                head.box.x = stable_head_x - head.box.w / 2.0f;
                head.box.y = stable_head_y - head.box.h / 2.0f;
            }

            std::set<int> active_body_ids;
            for (const auto& body : final_bodies) {
                if (body.track_id != -1) active_body_ids.insert(body.track_id);
            }
            for (auto it = smooth_rel_y.begin(); it != smooth_rel_y.end(); ) {
                if (active_body_ids.find(it->first) == active_body_ids.end()) it = smooth_rel_y.erase(it);
                else ++it;
            }
            for (auto it = smooth_rel_x.begin(); it != smooth_rel_x.end(); ) {
                if (active_body_ids.find(it->first) == active_body_ids.end()) it = smooth_rel_x.erase(it);
                else ++it;
            }

            {
                std::lock_guard<std::mutex> lock(g_bodies_mutex);
                g_shared_bodies = final_bodies;
            }
            {
                std::lock_guard<std::mutex> lock(g_heads_mutex);
                g_shared_heads = final_heads;
            }
            {
                std::lock_guard<std::mutex> lock(g_det_mutex);
                g_shared_detections = g_shared_bodies;
                g_new_detections = true;
            }
            current_read = 1 - current_read;
        }

        Sleep(local_cfg.eco_mode ? 2 : 1);
    }

    delete[] capture_buffers[0];
    delete[] capture_buffers[1];
    delete[] zoom_buffers[0];
    delete[] zoom_buffers[1];
}

// ==================== ПОТОК АИМБОТА ====================
void AimbotLoop(Aimbot* aim, Overlay* overlay) {
    static bool spoofed = false;
    static DWORD last_spoof_check = 0;
    static int last_com_port = -1;

    while (g_running) {
        if (overlay->unload_flag) break;
        if (!overlay->is_authenticated) { Sleep(10); continue; }

        SafeConfig local_cfg;
        {
            std::lock_guard<std::mutex> lock(g_cfg_mutex);
            memcpy(&local_cfg, &g_safe_cfg, sizeof(SafeConfig));
        }
        bool currently_aiming = (IsAimKeyPressed(overlay) || g_remote_aim_key.load()) && local_cfg.aim_enable;

        if (local_cfg.com_port != last_com_port) {
            aim->com_port = local_cfg.com_port;
            aim->InitHardware();
            last_com_port = local_cfg.com_port;
        }

        if (overlay->auto_spoof && !spoofed && (GetTickCount() - last_spoof_check > 5000)) {
            last_spoof_check = GetTickCount();
            if (FindAndSpoofArduino(overlay->spoofer_vid, overlay->spoofer_pid)) {
                spoofed = true;
                Sleep(2000);
            }
        }

        if (currently_aiming) {
            std::vector<Detection> current_det;
            bool is_new_frame = false;
            {
                std::lock_guard<std::mutex> lock(g_det_mutex);
                if (local_cfg.aim_target == 0) {
                    std::lock_guard<std::mutex> lock_head(g_heads_mutex);
                    current_det = g_shared_heads;
                }
                else if (local_cfg.aim_target == 1) {
                    std::lock_guard<std::mutex> lock_body(g_bodies_mutex);
                    current_det = g_shared_bodies;
                }
                else {
                    std::lock_guard<std::mutex> lock_head(g_heads_mutex);
                    std::lock_guard<std::mutex> lock_body(g_bodies_mutex);
                    current_det = g_shared_heads;
                    current_det.insert(current_det.end(), g_shared_bodies.begin(), g_shared_bodies.end());
                }
                is_new_frame = g_new_detections;
                g_new_detections = false;
            }
            if (!is_new_frame) { Sleep(1); continue; }

            if (local_cfg.aim_target_lock && current_det.size() > 1) {
                float center_x = g_capture_w / 2.0f;
                float center_y = g_capture_h / 2.0f;
                int best_idx = 0;
                float best_dist_sq = (current_det[0].box.x + current_det[0].box.w / 2 - center_x) * (current_det[0].box.x + current_det[0].box.w / 2 - center_x) +
                    (current_det[0].box.y + current_det[0].box.h / 2 - center_y) * (current_det[0].box.y + current_det[0].box.h / 2 - center_y);
                for (size_t i = 1; i < current_det.size(); ++i) {
                    float dx = current_det[i].box.x + current_det[i].box.w / 2 - center_x;
                    float dy = current_det[i].box.y + current_det[i].box.h / 2 - center_y;
                    float dist_sq = dx * dx + dy * dy;
                    if (dist_sq < best_dist_sq) {
                        best_dist_sq = dist_sq;
                        best_idx = (int)i;
                    }
                }
                std::vector<Detection> single_det;
                single_det.push_back(current_det[best_idx]);
                current_det = single_det;
            }

            auto now = std::chrono::steady_clock::now();
            long long current_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

            // ========== ПЕРЕДАЧА ПАРАМЕТРОВ В АИМБОТ (только существующие поля) ==========
            aim->aim_target = local_cfg.aim_target;
            aim->target_offset_x = local_cfg.aim_offset_x;
            aim->target_offset_y = local_cfg.aim_offset_y;
            aim->fov = local_cfg.fov_aimbot;
            aim->rcs_enable = local_cfg.rcs_enable;
            aim->rcs_pitch = local_cfg.rcs_pitch;
            aim->rcs_yaw = local_cfg.rcs_yaw;
            aim->humanizer_enable = local_cfg.humanizer_enable;
            aim->hum_reaction_delay = local_cfg.hum_reaction_delay;
            aim->hum_tremor_scale = local_cfg.hum_tremor_scale;
            aim->hum_micro_movements = local_cfg.hum_micro_movements;
            aim->hum_micro_amplitude = local_cfg.hum_micro_amplitude;
            aim->hum_reaction_jitter = local_cfg.hum_reaction_jitter;
            aim->elite_tsp_enabled = local_cfg.elite_tsp_enabled;
            aim->elite_ballistics_enabled = local_cfg.elite_ballistics_enabled;
            aim->elite_bullet_speed = local_cfg.elite_bullet_speed;
            aim->elite_bullet_drop = local_cfg.elite_bullet_drop;
            aim->elite_context_aware = local_cfg.elite_context_aware;
            aim->elite_smoke_vision = local_cfg.elite_smoke_vision;
            aim->elite_voice_ctrl = local_cfg.elite_voice_ctrl;
            aim->elite_shadow_trainer = local_cfg.elite_shadow_trainer;
            strncpy(aim->shadow_webhook, local_cfg.shadow_webhook, 255);
            aim->max_move_step = local_cfg.max_move_step;
            aim->aim_target_lock = local_cfg.aim_target_lock;
            aim->aim_lock_x = local_cfg.aim_lock_x;
            aim->aim_lock_y = local_cfg.aim_lock_y;

            // НОВЫЕ ПАРАМЕТРЫ (Sunone)
            aim->detection_resolution = local_cfg.detection_resolution;
            aim->mouse_sensitivity = local_cfg.mouse_sensitivity;
            aim->mouse_yaw = local_cfg.mouse_yaw;
            aim->mouse_pitch = local_cfg.mouse_pitch;
            aim->fovX = local_cfg.fovX;
            aim->fovY = local_cfg.fovY;
            aim->min_speed_multiplier = local_cfg.min_speed_multiplier;
            aim->max_speed_multiplier = local_cfg.max_speed_multiplier;
            aim->snap_radius = local_cfg.snap_radius;
            aim->near_radius = local_cfg.near_radius;
            aim->speed_curve_exponent = local_cfg.speed_curve_exponent;
            aim->snap_boost_factor = local_cfg.snap_boost_factor;
            aim->kalman_enabled = local_cfg.kalman_enable;
            aim->kalman_process_noise_position = local_cfg.kalman_q;
            aim->kalman_measurement_noise = local_cfg.kalman_r;
            aim->kalman_compensate_detection_delay = local_cfg.kalman_compensate_detection_delay;
            aim->kalman_additional_prediction_ms = local_cfg.kalman_additional_prediction_ms;
            aim->prediction_interval = local_cfg.prediction_interval;
            aim->disable_headshot = local_cfg.disable_headshot;
            aim->wind_mouse_enabled = false;  // пока отключено

            aim->Update(current_det, g_capture_w, g_capture_h, is_new_frame, current_time_ms, g_current_zoom.load());
            Sleep(1);
        }
        else {
            aim->ResetTarget();
            Sleep(5);
        }
    }
}

// ==================== REMOTE ACTIVATION SERVER ====================
void RemoteActivationServer() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return;
    g_udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_udp_sock == INVALID_SOCKET) {
        WSACleanup();
        return;
    }
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(3334);
    addr.sin_addr.s_addr = INADDR_ANY;
    int timeout = 1000;
    setsockopt(g_udp_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    if (bind(g_udp_sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(g_udp_sock);
        g_udp_sock = INVALID_SOCKET;
        WSACleanup();
        return;
    }
    char buffer[64];
    while (g_running) {
        int len = recv(g_udp_sock, buffer, sizeof(buffer) - 1, 0);
        if (len > 0) {
            buffer[len] = '\0';
            if (strcmp(buffer, "aim_on") == 0) g_remote_aim_key.store(true);
            else if (strcmp(buffer, "aim_off") == 0) g_remote_aim_key.store(false);
        }
    }
    if (g_udp_sock != INVALID_SOCKET) closesocket(g_udp_sock);
    WSACleanup();
}

// ==================== WINMAIN ====================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    std::ofstream logfile("C:\\pwnz_log.txt", std::ios::out | std::ios::trunc);
    if (logfile.is_open()) {
        logfile << "Program started" << std::endl;
        logfile.flush();
    }

    VMProtectBeginUltra("MainEntry");
    timeBeginPeriod(1);
    MUTATE_SIGNATURE;
    SetProcessDPIAware();
    setlocale(LC_ALL, XOR("ru_RU.UTF-8"));

    if (logfile.is_open()) { logfile << "Step 1: after locale" << std::endl; logfile.flush(); }

    g_capture_w = GetSystemMetrics(SM_CXSCREEN);
    g_capture_h = GetSystemMetrics(SM_CYSCREEN);
    DXGICapture cap; Detector det; Overlay overlay; Aimbot aim;
    const char* model_files[] = { "models\\BogX-Nano.onnx", "models\\BogX-Lite.onnx", "models\\BogX-Pro.onnx", "models\\BogX-Ultra.onnx" };

    if (logfile.is_open()) { logfile << "Step 2: before try block" << std::endl; logfile.flush(); }

    try {
        if (logfile.is_open()) { logfile << "Step 3: initializing capture" << std::endl; logfile.flush(); }
        if (!cap.Initialize()) {
            if (logfile.is_open()) { logfile << "ERROR: cap.Initialize failed" << std::endl; logfile.flush(); }
            MessageBoxA(0, "Ошибка захвата экрана!", "FATAL ERROR", MB_ICONERROR);
            VMProtectEnd();
            return -1;
        }
        if (logfile.is_open()) { logfile << "Step 4: capture OK" << std::endl; logfile.flush(); }

        if (!overlay.Initialize()) {
            if (logfile.is_open()) { logfile << "ERROR: overlay.Initialize failed" << std::endl; logfile.flush(); }
            MessageBoxA(0, "Ошибка оверлея!", "FATAL ERROR", MB_ICONERROR);
            VMProtectEnd();
            return -1;
        }
        if (logfile.is_open()) { logfile << "Step 5: overlay OK" << std::endl; logfile.flush(); }

        if (overlay.custom_res_w < 800 || overlay.custom_res_h < 600) {
            overlay.custom_res_w = g_capture_w; overlay.custom_res_h = g_capture_h;
            overlay.SaveConfig(&aim);
        }
        overlay.is_first_frame_init = true;
        aim.hardware_type = overlay.hardware_type;
        aim.com_port = overlay.com_port;
        aim.InitHardware();

        int start_w, start_h; GetModelSize(overlay.ai_model, start_w, start_h);
        std::string model_to_load = model_files[overlay.ai_model];
        std::string fp16_model;
        EnsureFP16Model(model_to_load, fp16_model);
        if (logfile.is_open()) { logfile << "Step 6: loading model: " << fp16_model << std::endl; logfile.flush(); }

        if (!det.initialize(fp16_model, start_w, start_h)) {
            if (logfile.is_open()) { logfile << "ERROR: det.initialize failed" << std::endl; logfile.flush(); }
            MessageBoxA(0, "Нейросеть не загрузилась!", "FATAL ERROR", MB_ICONERROR);
            VMProtectEnd(); return -1;
        }
        if (logfile.is_open()) { logfile << "Step 7: model loaded OK" << std::endl; logfile.flush(); }
    }
    catch (...) {
        if (logfile.is_open()) { logfile << "EXCEPTION in try block" << std::endl; logfile.flush(); }
        MessageBoxA(0, "ФАТАЛЬНЫЙ КРАШ", "Ошибка", MB_ICONERROR);
        VMProtectEnd();
        return -1;
    }

    if (logfile.is_open()) { logfile << "Step 8: creating threads" << std::endl; logfile.flush(); }

    std::atomic<bool> show_menu(true);
    std::thread t_inference(InferenceThread, &cap, &det, &overlay);
    std::thread t_aimbot(AimbotLoop, &aim, &overlay);
    std::thread t_remote(RemoteActivationServer);
    std::thread t_hotkeys([&]() {
        bool insert_was_pressed = false; auto last_toggle_time = std::chrono::steady_clock::now();
        while (g_running) {
            if (GetAsyncKeyState(VK_END) & 0x8000) {
                overlay.unload_flag = true;
                g_running = false;
                if (g_udp_sock != INVALID_SOCKET) {
                    shutdown(g_udp_sock, SD_BOTH);
                    closesocket(g_udp_sock);
                    g_udp_sock = INVALID_SOCKET;
                }
                break;
            }
            bool insert_is_pressed = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
            if (insert_is_pressed && !insert_was_pressed) {
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_toggle_time).count() > 300) {
                    if (!overlay.is_drawing_zone) show_menu.store(!show_menu.load());
                    last_toggle_time = now;
                }
            }
            insert_was_pressed = insert_is_pressed;
            Sleep(1);
        }
        });

    if (logfile.is_open()) { logfile << "Step 9: entering main loop" << std::endl; logfile.flush(); }

    // Инициализация g_safe_cfg значениями по умолчанию
    g_safe_cfg.aim_curve_type = 3;
    g_safe_cfg.max_move_step = 50.0f;
    g_safe_cfg.sticky_zone_factor = 0.5f;
    g_safe_cfg.head_height_ratio = 0.1f;
    g_safe_cfg.hum_micro_movements = true;
    g_safe_cfg.hum_micro_amplitude = 0.8f;
    g_safe_cfg.hum_reaction_jitter = 2.0f;

    // Инициализация новых полей
    g_safe_cfg.detection_resolution = 960;
    g_safe_cfg.mouse_sensitivity = 1.0f;
    g_safe_cfg.mouse_yaw = 0.022f;
    g_safe_cfg.mouse_pitch = 0.022f;
    g_safe_cfg.fovX = 106.0f;
    g_safe_cfg.fovY = 74.0f;
    g_safe_cfg.min_speed_multiplier = 0.1f;
    g_safe_cfg.max_speed_multiplier = 0.1f;
    g_safe_cfg.snap_radius = 1.5f;
    g_safe_cfg.near_radius = 25.0f;
    g_safe_cfg.speed_curve_exponent = 3.0f;
    g_safe_cfg.snap_boost_factor = 1.15f;
    g_safe_cfg.kalman_compensate_detection_delay = true;
    g_safe_cfg.kalman_additional_prediction_ms = 0.0f;
    g_safe_cfg.prediction_interval = 0.01f;
    g_safe_cfg.disable_headshot = false;

    bool was_menu_open = true; auto auth_check_timer = std::chrono::steady_clock::now(); bool screen_cleared_for_stealth = false;
    int render_yolo_w = 960, render_yolo_h = 544;
    while (overlay.Update()) {
        if (overlay.unload_flag) break;
        {
            std::lock_guard<std::mutex> lock(g_cfg_mutex);
            g_safe_cfg.ai_confidence_body = overlay.ai_confidence_body;
            g_safe_cfg.ai_confidence_head = overlay.ai_confidence_head;
            g_safe_cfg.min_box_area_body = overlay.min_box_area_body;
            g_safe_cfg.min_box_area_head = overlay.min_box_area_head;
            g_safe_cfg.neural_nms = overlay.neural_nms;
            g_safe_cfg.neural_max_det = overlay.neural_max_det;
            g_safe_cfg.fov_scan = overlay.fov_scan;
            g_safe_cfg.enable_exclusion_zone = overlay.enable_exclusion_zone;
            g_safe_cfg.excl_x1 = overlay.excl_x1; g_safe_cfg.excl_y1 = overlay.excl_y1;
            g_safe_cfg.excl_x2 = overlay.excl_x2; g_safe_cfg.excl_y2 = overlay.excl_y2;
            g_safe_cfg.memory_enemy_frames = overlay.memory_enemy_frames;
            g_safe_cfg.aim_target = overlay.aim_target;
            g_safe_cfg.aim_offset_x = overlay.aim_offset_x; g_safe_cfg.aim_offset_y = overlay.aim_offset_y;
            g_safe_cfg.enable_pose_adaptive = overlay.enable_pose_adaptive;
            g_safe_cfg.fov_aimbot = overlay.fov_aimbot;
            g_safe_cfg.aim_min_sens = overlay.aim_min_sens; g_safe_cfg.aim_max_sens = overlay.aim_max_sens;
            g_safe_cfg.aim_smoother = overlay.aim_smoother;
            g_safe_cfg.sticky_aim = overlay.sticky_aim;
            g_safe_cfg.aim_deadzone = overlay.aim_deadzone;
            g_safe_cfg.aim_target_lock = overlay.aim_target_lock;
            g_safe_cfg.aim_target_priority = overlay.aim_target_priority;
            g_safe_cfg.aim_dynamic_smooth = overlay.aim_dynamic_smooth;
            g_safe_cfg.aim_switch_delay = overlay.aim_switch_delay;
            g_safe_cfg.aim_lock_x = overlay.aim_lock_x; g_safe_cfg.aim_lock_y = overlay.aim_lock_y;
            g_safe_cfg.aim_curve_type = overlay.aim_curve_type;
            g_safe_cfg.aim_flicker = overlay.aim_flicker;
            g_safe_cfg.flick_speed = overlay.flick_speed;
            g_safe_cfg.aim_flicker_key = overlay.aim_flicker_key;
            g_safe_cfg.rcs_enable = overlay.rcs_enable;
            g_safe_cfg.rcs_pitch = overlay.rcs_pitch; g_safe_cfg.rcs_yaw = overlay.rcs_yaw;
            g_safe_cfg.humanizer_enable = overlay.humanizer_enable;
            g_safe_cfg.hum_reaction_delay = overlay.hum_reaction_delay;
            g_safe_cfg.hum_overshoot_chance = overlay.hum_overshoot_chance;
            g_safe_cfg.hum_randomize_bone = overlay.hum_randomize_bone;
            g_safe_cfg.hum_tremor_scale = overlay.hum_tremor_scale;
            g_safe_cfg.eco_mode = overlay.eco_mode;
            g_safe_cfg.aim_enable = overlay.aim_enable;
            g_safe_cfg.elite_tsp_enabled = overlay.elite_tsp_enabled;
            g_safe_cfg.elite_ballistics_enabled = overlay.elite_ballistics_enabled;
            g_safe_cfg.elite_bullet_speed = overlay.elite_bullet_speed;
            g_safe_cfg.elite_bullet_drop = overlay.elite_bullet_drop;
            g_safe_cfg.elite_context_aware = overlay.elite_context_aware;
            g_safe_cfg.elite_smoke_vision = overlay.elite_smoke_vision;
            g_safe_cfg.elite_voice_ctrl = overlay.elite_voice_ctrl;
            g_safe_cfg.elite_shadow_trainer = overlay.elite_shadow_trainer;
            strncpy(g_safe_cfg.shadow_webhook, overlay.shadow_webhook, 255);
            g_safe_cfg.byte_track_thresh = overlay.byte_track_thresh;
            g_safe_cfg.byte_track_buffer = overlay.byte_track_buffer;
            g_safe_cfg.byte_match_thresh = overlay.byte_match_thresh;
            g_safe_cfg.byte_frame_rate = overlay.byte_frame_rate;
            g_safe_cfg.com_port = overlay.com_port;
            g_safe_cfg.use_advanced_sticky_aim = overlay.use_advanced_sticky_aim;
            g_safe_cfg.sticky_threshold = overlay.sticky_threshold;
            g_safe_cfg.sticky_frames_keep = overlay.sticky_frames_keep;
            g_safe_cfg.prediction_method = overlay.prediction_method;
            g_safe_cfg.max_move_step = overlay.max_move_step;
            g_safe_cfg.sticky_zone_factor = overlay.sticky_zone_factor;
            g_safe_cfg.head_height_ratio = overlay.head_height_ratio;
            g_safe_cfg.hum_micro_movements = overlay.hum_micro_movements;
            g_safe_cfg.hum_micro_amplitude = overlay.hum_micro_amplitude;
            g_safe_cfg.hum_reaction_jitter = overlay.hum_reaction_jitter;
        }
        if (overlay.is_authenticated) {
            auto now_time = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::minutes>(now_time - auth_check_timer).count() >= 60) {
                auth_check_timer = now_time;
                std::string tmp_exp;
                std::string res = SendAuthRequest(overlay.auth_username, overlay.auth_password, false, tmp_exp);
                if (res.find("SUCCESS") == std::string::npos) {
                    MessageBoxA(0, "Подписка кончилась!", "PWNZ VISION", MB_ICONERROR);
                    break;
                }
            }
        }
        bool current_menu_state = show_menu.load();
        overlay.is_menu_open = current_menu_state;
        if (was_menu_open && !current_menu_state && !overlay.is_drawing_zone) overlay.SaveConfig(&aim);
        was_menu_open = current_menu_state;
        if (overlay.apply_hw_flag) {
            overlay.apply_hw_flag = false;
            aim.com_port = overlay.com_port;
            aim.hardware_type = overlay.hardware_type;
            aim.InitHardware();
        }
        if (overlay.apply_res_flag) {
            overlay.apply_res_flag = false;
            g_capture_w = overlay.custom_res_w; g_capture_h = overlay.custom_res_h;
            overlay.active_res_w = g_capture_w; overlay.active_res_h = g_capture_h;
        }
        if (overlay.apply_model_flag) {
            overlay.apply_model_flag = false;
            std::lock_guard<std::mutex> lock(g_model_mutex);
            int new_w, new_h; GetModelSize(overlay.ai_model, new_w, new_h);
            std::string model_to_load = model_files[overlay.ai_model];
            std::string fp16_model;
            EnsureFP16Model(model_to_load, fp16_model);
            det.initialize(fp16_model, new_w, new_h);
            render_yolo_w = det.get_width(); render_yolo_h = det.get_height();
        }
        std::vector<Detection> render_det;
        { std::lock_guard<std::mutex> lock(g_bodies_mutex); render_det = g_shared_bodies; }
        bool stealth_active = !current_menu_state && overlay.disable_all_visuals_when_hidden && !overlay.is_drawing_zone;
        if (stealth_active) {
            if (!screen_cleared_for_stealth) {
                overlay.Render(render_det, g_capture_w, g_capture_h, render_yolo_w, render_yolo_h, &aim, false);
                screen_cleared_for_stealth = true;
            }
            else Sleep(2);
        }
        else {
            screen_cleared_for_stealth = false;
            overlay.Render(render_det, g_capture_w, g_capture_h, render_yolo_w, render_yolo_h, &aim, current_menu_state);
            if (overlay.refresh_rate_idx > 0) {
                int target_fps = 60;
                switch (overlay.refresh_rate_idx) {
                case 1: target_fps = 60; break;
                case 2: target_fps = 75; break;
                case 3: target_fps = 120; break;
                case 4: target_fps = 144; break;
                case 5: target_fps = 165; break;
                case 6: target_fps = 240; break;
                case 7: target_fps = 360; break;
                }
                static auto next_frame = std::chrono::steady_clock::now();
                next_frame += std::chrono::nanoseconds(1000000000 / target_fps);
                auto now = std::chrono::steady_clock::now();
                if (now < next_frame) {
                    auto ms_to_sleep = std::chrono::duration_cast<std::chrono::milliseconds>(next_frame - now).count();
                    if (ms_to_sleep > 1) Sleep((DWORD)(ms_to_sleep - 1));
                    while (std::chrono::steady_clock::now() < next_frame) {}
                }
                else next_frame = std::chrono::steady_clock::now();
            }
            else Sleep(1);
        }
    }
    g_running = false;
    t_inference.join(); t_aimbot.join(); t_hotkeys.join();
    if (t_remote.joinable()) t_remote.join();
    aim.CloseHardware(); timeEndPeriod(1); overlay.Cleanup(&aim);
    VMProtectEnd();

    if (logfile.is_open()) {
        logfile << "Program finished successfully" << std::endl;
        logfile.close();
    }
    return 0;
}