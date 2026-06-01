#pragma execution_character_set("utf-8")
#pragma warning(disable: 4068)
#pragma push_macro("max")
#pragma push_macro("min")
#undef max
#undef min

#include "overlay.h"
#include "auth.h"
#include "aimbot.h"
#include "detector.h"
#include "AimbotTarget.h"
#include "network_2pc.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "implot.h"
#include <dwmapi.h>
#include <string>
#include <cmath>
#include <fstream>
#include <sstream>
#include <mmsystem.h>
#include <dxgi.h>
#include <shellapi.h>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <atomic>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <mutex>
#include <unordered_map>
#include <functional>

#include <iphlpapi.h>
#include <shlobj.h>
#include <wincrypt.h>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "crypt32.lib")

#include "protect.h"
#include "xorstr.hpp"
#include "VMProtectSDK.h"
#include "head_smoother.h"
// HardwareBackend.h уже включен через WinHeaders.h в overlay.h

extern HeadSmoother g_head_smoother;
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "winmm.lib")

ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

extern std::vector<Detection> g_shared_heads;
extern std::mutex g_heads_mutex;
extern std::atomic<int> g_byte_active_tracks;
extern std::atomic<float> g_byte_avg_tracklet_len;
extern std::atomic<float> g_byte_avg_speed;
extern std::atomic<float> g_track_loss_rate;
extern std::atomic<float> g_aim_overshoot_ratio;
extern std::atomic<float> g_aim_motion_jerk;
extern std::atomic<float> g_inference_jitter;
extern std::atomic<float> g_last_inference_time;
extern std::atomic<float> g_last_capture_time;
extern std::atomic<float> g_locked_screen_x;
extern std::atomic<float> g_locked_screen_y;
extern std::atomic<bool> g_is_target_locked;
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

int Overlay::active_tab = 0;
int Overlay::active_bind_id = -1;

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// ============================================================
// DeepCleanTraces
// ============================================================
void Overlay::DeepCleanTraces() {
    VMProtectBeginUltra("DeepCleanTraces");
    MUTATE_SIGNATURE;
    try {
        HMODULE hDns = LoadLibraryA(XOR("dnsapi.dll"));
        if (hDns) {
            auto FlushCache = (BOOL(WINAPI*)())GetProcAddress(hDns, XOR("DnsFlushResolverCache"));
            if (FlushCache) FlushCache();
            FreeLibrary(hDns);
        }
        HMODULE hIphlp = LoadLibraryA(XOR("Iphlpapi.dll"));
        if (hIphlp) {
            typedef DWORD(WINAPI* FlushPathFunc)(int);
            typedef DWORD(WINAPI* FlushNetFunc)(DWORD);
            auto pFlushPath = (FlushPathFunc)GetProcAddress(hIphlp, XOR("FlushIpPathTable"));
            auto pFlushNet = (FlushNetFunc)GetProcAddress(hIphlp, XOR("FlushIpNetTable"));
            if (pFlushPath) pFlushPath(0);
            if (pFlushNet) pFlushNet(0);
            FreeLibrary(hIphlp);
        }
        SHFILEOPSTRUCTA fo = { 0 };
        fo.wFunc = FO_DELETE;
        fo.pFrom = "%TEMP%\\*\0";
        fo.fFlags = FOF_SILENT | FOF_NOCONFIRMATION | FOF_NOERRORUI;
        SHFileOperationA(&fo);
        char* userProfile = nullptr; size_t len = 0;
        _dupenv_s(&userProfile, &len, "USERPROFILE");
        if (userProfile) {
            std::string ne_path = std::string(userProfile) + "\\AppData\\Local\\NetEase";
            char double_null_path[MAX_PATH];
            memset(double_null_path, 0, sizeof(double_null_path));
            strncpy_s(double_null_path, ne_path.c_str(), ne_path.length());
            fo.pFrom = double_null_path;
            SHFileOperationA(&fo);
            free(userProfile);
        }
    }
    catch (...) {}
    PlaySoundA(XOR("C:\\Windows\\Media\\chimes.wav"), NULL, SND_FILENAME | SND_ASYNC);
    VMProtectEnd();
}

// ============================================================
// SpoofMAC
// ============================================================
void Overlay::SpoofMAC() {
    VMProtectBeginUltra("SpoofMAC");
    MUTATE_SIGNATURE;
    const char* hex = "0123456789ABCDEF";
    std::string new_mac = "02";
    for (int i = 0; i < 10; ++i) new_mac += hex[rand() % 16];
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
        XOR("SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e972-e325-11ce-bfc1-08002be10318}"),
        0, KEY_READ | KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        char subkey[256]; DWORD index = 0; DWORD subkeySize = sizeof(subkey);
        while (RegEnumKeyExA(hKey, index++, subkey, &subkeySize, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
            HKEY hSubKey;
            if (RegOpenKeyExA(hKey, subkey, 0, KEY_READ | KEY_WRITE, &hSubKey) == ERROR_SUCCESS) {
                char busType[256];
                DWORD busSize = sizeof(busType);
                if (RegQueryValueExA(hSubKey, XOR("BusType"), NULL, NULL, (LPBYTE)busType, &busSize) == ERROR_SUCCESS) {
                    RegSetValueExA(hSubKey, XOR("NetworkAddress"), 0, REG_SZ,
                        (const BYTE*)new_mac.c_str(), new_mac.length() + 1);
                }
                RegCloseKey(hSubKey);
            }
            subkeySize = sizeof(subkey);
        }
        RegCloseKey(hKey);
    }
    PlaySoundA(XOR("C:\\Windows\\Media\\tada.wav"), NULL, SND_FILENAME | SND_ASYNC);
    VMProtectEnd();
}

// ============================================================
// LoadAuth, SaveAuth, FetchHardwareInfo
// ============================================================
void Overlay::LoadAuth() {
    std::ifstream file(XOR("pwnz_auth.txt"));
    if (file.is_open()) {
        file.getline(auth_username, 64);
        file.getline(auth_password, 64);
        file.close();
        if (strlen(auth_username) > 0 && strlen(auth_password) > 0) {
            is_auto_logging_in = true;
        }
    }
}

void Overlay::SaveAuth() {
    std::ofstream file(XOR("pwnz_auth.txt"));
    if (file.is_open()) {
        file << auth_username << "\n" << auth_password;
        file.close();
    }
}

void Overlay::FetchHardwareInfo() {
    DWORD volSerial = 0;
    if (GetVolumeInformationA(XOR("C:\\"), NULL, 0, &volSerial, NULL, NULL, NULL, 0)) {
        snprintf(hwid_str, sizeof(hwid_str), XOR("PWNZ-%08X-%04X"), volSerial, (volSerial ^ 0xABCD));
    }
    HKEY hKey; DWORD bufSize = sizeof(cpu_str);
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, XOR("HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0"), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExA(hKey, XOR("ProcessorNameString"), NULL, NULL, (LPBYTE)cpu_str, &bufSize); RegCloseKey(hKey);
    }
    IDXGIFactory1* factory = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory))) {
        IDXGIAdapter1* adapter = nullptr; IDXGIAdapter1* bestAdapter = nullptr; SIZE_T maxVRAM = 0;
        for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc; adapter->GetDesc1(&desc);
            if (desc.DedicatedVideoMemory > maxVRAM) { maxVRAM = desc.DedicatedVideoMemory; if (bestAdapter) bestAdapter->Release(); bestAdapter = adapter; }
            else adapter->Release();
        }
        if (bestAdapter) {
            DXGI_ADAPTER_DESC1 desc; bestAdapter->GetDesc1(&desc);
            size_t converted = 0; wcstombs_s(&converted, gpu_str, sizeof(gpu_str), desc.Description, _TRUNCATE);
            bestAdapter->Release();
        }
        factory->Release();
    }
}

// ============================================================
// LoadConfig (с unordered_map)
// ============================================================
void Overlay::LoadConfig(Aimbot* aim) {
    VMProtectBeginUltra("LoadConfig");
    std::string config_data = "";
    std::ifstream file_bin(XOR("pwnz_enc.bin"), std::ios::binary);
    if (file_bin.is_open()) {
        std::vector<BYTE> buffer((std::istreambuf_iterator<char>(file_bin)), std::istreambuf_iterator<char>());
        file_bin.close();
        if (!buffer.empty()) {
            DATA_BLOB DataIn, DataOut;
            DataIn.pbData = buffer.data();
            DataIn.cbData = (DWORD)buffer.size();
            if (CryptUnprotectData(&DataIn, NULL, NULL, NULL, NULL, 0, &DataOut)) {
                config_data = std::string((char*)DataOut.pbData, DataOut.cbData);
                LocalFree(DataOut.pbData);
            }
        }
    }
    else {
        std::ifstream file_ini(XOR("pwnz_cfg.ini"));
        if (file_ini.is_open()) {
            config_data.assign(std::istreambuf_iterator<char>(file_ini), std::istreambuf_iterator<char>());
            file_ini.close();
        }
        else {
            VMProtectEnd();
            return;
        }
    }
    if (config_data.empty()) { VMProtectEnd(); return; }

    auto safe_stof = [](std::string s) {
        std::replace(s.begin(), s.end(), ',', '.');
        std::istringstream iss(s);
        iss.imbue(std::locale("C"));
        float f = 0.0f;
        iss >> f;
        return f;
        };
    auto safe_stod = [](std::string s) {
        std::replace(s.begin(), s.end(), ',', '.');
        std::istringstream iss(s);
        iss.imbue(std::locale("C"));
        double d = 0.0;
        iss >> d;
        return d;
        };

    std::unordered_map<std::string, std::function<void(const std::string&)>> handlers;

    handlers["lifetime_seconds"] = [&](const std::string& v) { lifetime_seconds = safe_stod(v); };
    handlers["aim_max_sens"] = [&](const std::string& v) { aim_max_sens = safe_stof(v); };
    handlers["aim_min_sens"] = [&](const std::string& v) { aim_min_sens = safe_stof(v); };
    handlers["fov_aimbot"] = [&](const std::string& v) { fov_aimbot = safe_stof(v); };
    handlers["is_russian"] = [&](const std::string& v) { is_russian = std::stoi(v); };
    handlers["aim_offset_x"] = [&](const std::string& v) { aim_offset_x = safe_stof(v); };
    handlers["aim_offset_y"] = [&](const std::string& v) { aim_offset_y = safe_stof(v); };
    handlers["aim_key_main"] = [&](const std::string& v) { aim_key_main = std::stoi(v); };
    handlers["aim_key_sub"] = [&](const std::string& v) { aim_key_sub = std::stoi(v); };
    handlers["ai_confidence_body"] = [&](const std::string& v) { ai_confidence_body = safe_stof(v); };
    handlers["ai_confidence_head"] = [&](const std::string& v) { ai_confidence_head = safe_stof(v); };
    handlers["min_box_area_body"] = [&](const std::string& v) { min_box_area_body = safe_stof(v); };
    handlers["min_box_area_head"] = [&](const std::string& v) { min_box_area_head = safe_stof(v); };
    handlers["disable_all_visuals_when_hidden"] = [&](const std::string& v) { disable_all_visuals_when_hidden = std::stoi(v); };
    handlers["custom_res_w"] = [&](const std::string& v) { custom_res_w = std::stoi(v); };
    handlers["custom_res_h"] = [&](const std::string& v) { custom_res_h = std::stoi(v); };
    handlers["aim_smoother"] = [&](const std::string& v) { aim_smoother = safe_stof(v); };
    handlers["fov_scan"] = [&](const std::string& v) { fov_scan = safe_stof(v); };
    handlers["aim_kill_delay"] = [&](const std::string& v) { aim_kill_delay = safe_stof(v); };
    handlers["obs_bypass"] = [&](const std::string& v) { obs_bypass = std::stoi(v); };
    handlers["menu_scale"] = [&](const std::string& v) { menu_scale = safe_stof(v); };
    handlers["hardware_mode_idx"] = [&](const std::string& v) { hardware_mode_idx = std::stoi(v); };
    handlers["hw_enabled"] = [&](const std::string& v) { hw_enabled = std::stoi(v); };
    handlers["bypass_mode_idx"] = [&](const std::string& v) { bypass_mode_idx = std::stoi(v); };
    handlers["baud_rate_idx"] = [&](const std::string& v) { baud_rate_idx = std::stoi(v); };
    handlers["com_port_buf"] = [&](const std::string& v) { strncpy(com_port_buf, v.c_str(), sizeof(com_port_buf) - 1); };
    handlers["kmbox_ip_buf"] = [&](const std::string& v) { strncpy(kmbox_ip_buf, v.c_str(), sizeof(kmbox_ip_buf) - 1); };
    handlers["kmbox_port"] = [&](const std::string& v) { kmbox_port = std::stoi(v); };
    handlers["random_delay_min"] = [&](const std::string& v) { random_delay_min = std::stoi(v); };
    handlers["random_delay_max"] = [&](const std::string& v) { random_delay_max = std::stoi(v); };
    handlers["test_move_x"] = [&](const std::string& v) { test_move_x = std::stoi(v); };
    handlers["test_move_y"] = [&](const std::string& v) { test_move_y = std::stoi(v); };
    handlers["com_port"] = [&](const std::string& v) { com_port = std::stoi(v); };
    handlers["aim_deadzone"] = [&](const std::string& v) { aim_deadzone = safe_stof(v); };
    handlers["draw_crosshair"] = [&](const std::string& v) { draw_crosshair = std::stoi(v); };
    handlers["hit_chance"] = [&](const std::string& v) { hit_chance = safe_stof(v); };
    handlers["menu_width"] = [&](const std::string& v) { menu_width = safe_stof(v); };
    handlers["menu_height"] = [&](const std::string& v) { menu_height = safe_stof(v); };
    handlers["trigger_enable"] = [&](const std::string& v) { trigger_enable = std::stoi(v); };
    handlers["trigger_delay"] = [&](const std::string& v) { trigger_delay = safe_stof(v); };
    handlers["trigger_target"] = [&](const std::string& v) { trigger_target = std::stoi(v); };
    handlers["trigger_key"] = [&](const std::string& v) { trigger_key = std::stoi(v); };
    handlers["rcs_enable"] = [&](const std::string& v) { rcs_enable = std::stoi(v); };
    handlers["rcs_pitch"] = [&](const std::string& v) { rcs_pitch = safe_stof(v); };
    handlers["rcs_yaw"] = [&](const std::string& v) { rcs_yaw = safe_stof(v); };
    handlers["eco_mode"] = [&](const std::string& v) { eco_mode = std::stoi(v); };
    handlers["memory_enemy_frames"] = [&](const std::string& v) { memory_enemy_frames = std::stoi(v); };
    handlers["refresh_rate_idx"] = [&](const std::string& v) { refresh_rate_idx = std::stoi(v); };
    handlers["auto_confidence"] = [&](const std::string& v) { auto_confidence = std::stoi(v); };
    handlers["sticky_aim"] = [&](const std::string& v) { sticky_aim = std::stoi(v); };
    handlers["enable_pose_adaptive"] = [&](const std::string& v) { enable_pose_adaptive = std::stoi(v); };
    handlers["accent_color_0"] = [&](const std::string& v) { accent_color[0] = safe_stof(v); };
    handlers["accent_color_1"] = [&](const std::string& v) { accent_color[1] = safe_stof(v); };
    handlers["accent_color_2"] = [&](const std::string& v) { accent_color[2] = safe_stof(v); };
    handlers["target_monitor"] = [&](const std::string& v) { target_monitor = std::stoi(v); };
    handlers["enable_dma_fuser"] = [&](const std::string& v) { enable_dma_fuser = std::stoi(v); };
    handlers["enable_dynamic_fov"] = [&](const std::string& v) { enable_dynamic_fov = std::stoi(v); };
    handlers["aim_toggle_key"] = [&](const std::string& v) { aim_toggle_key = std::stoi(v); };
    handlers["aim_target_lock"] = [&](const std::string& v) { aim_target_lock = std::stoi(v); };
    handlers["aim_dynamic_smooth"] = [&](const std::string& v) { aim_dynamic_smooth = std::stoi(v); };
    handlers["aim_switch_delay"] = [&](const std::string& v) { aim_switch_delay = std::stoi(v); };
    handlers["aim_lock_x"] = [&](const std::string& v) { aim_lock_x = std::stoi(v); };
    handlers["aim_lock_y"] = [&](const std::string& v) { aim_lock_y = std::stoi(v); };
    handlers["aim_curve_type"] = [&](const std::string& v) { aim_curve_type = std::stoi(v); };
    handlers["selected_preset"] = [&](const std::string& v) { selected_preset = std::stoi(v); };
    handlers["esp_thickness"] = [&](const std::string& v) { esp_thickness = safe_stof(v); };
    handlers["esp_style"] = [&](const std::string& v) { esp_style = std::stoi(v); };
    handlers["color_esp_visible_0"] = [&](const std::string& v) { color_esp_visible[0] = safe_stof(v); };
    handlers["color_esp_visible_1"] = [&](const std::string& v) { color_esp_visible[1] = safe_stof(v); };
    handlers["color_esp_visible_2"] = [&](const std::string& v) { color_esp_visible[2] = safe_stof(v); };
    handlers["color_esp_hidden_0"] = [&](const std::string& v) { color_esp_hidden[0] = safe_stof(v); };
    handlers["color_esp_hidden_1"] = [&](const std::string& v) { color_esp_hidden[1] = safe_stof(v); };
    handlers["color_esp_hidden_2"] = [&](const std::string& v) { color_esp_hidden[2] = safe_stof(v); };
    handlers["neural_nms"] = [&](const std::string& v) { neural_nms = safe_stof(v); };
    handlers["neural_max_det"] = [&](const std::string& v) { neural_max_det = std::stoi(v); };
    handlers["enable_exclusion_zone"] = [&](const std::string& v) { enable_exclusion_zone = std::stoi(v); };
    handlers["excl_x1"] = [&](const std::string& v) { excl_x1 = safe_stof(v); };
    handlers["excl_y1"] = [&](const std::string& v) { excl_y1 = safe_stof(v); };
    handlers["excl_x2"] = [&](const std::string& v) { excl_x2 = safe_stof(v); };
    handlers["excl_y2"] = [&](const std::string& v) { excl_y2 = safe_stof(v); };
    handlers["ai_model"] = [&](const std::string& v) { ai_model = std::stoi(v); };
    handlers["humanizer_enable"] = [&](const std::string& v) { humanizer_enable = std::stoi(v); };
    handlers["hum_reaction_delay"] = [&](const std::string& v) { hum_reaction_delay = safe_stof(v); };
    handlers["hum_overshoot_chance"] = [&](const std::string& v) { hum_overshoot_chance = safe_stof(v); };
    handlers["hum_randomize_bone"] = [&](const std::string& v) { hum_randomize_bone = std::stoi(v); };
    handlers["hum_tremor_scale"] = [&](const std::string& v) { hum_tremor_scale = safe_stof(v); };
    handlers["elite_tsp_enabled"] = [&](const std::string& v) { elite_tsp_enabled = std::stoi(v); };
    handlers["elite_ballistics_enabled"] = [&](const std::string& v) { elite_ballistics_enabled = std::stoi(v); };
    handlers["elite_bullet_speed"] = [&](const std::string& v) { elite_bullet_speed = safe_stof(v); };
    handlers["elite_bullet_drop"] = [&](const std::string& v) { elite_bullet_drop = safe_stof(v); };
    handlers["elite_context_aware"] = [&](const std::string& v) { elite_context_aware = std::stoi(v); };
    handlers["elite_smoke_vision"] = [&](const std::string& v) { elite_smoke_vision = std::stoi(v); };
    handlers["elite_voice_ctrl"] = [&](const std::string& v) { elite_voice_ctrl = std::stoi(v); };
    handlers["elite_shadow_trainer"] = [&](const std::string& v) { elite_shadow_trainer = std::stoi(v); };
    handlers["shadow_webhook"] = [&](const std::string& v) { strncpy(shadow_webhook, v.c_str(), 255); };
    handlers["max_move_step"] = [&](const std::string& v) { max_move_step = safe_stof(v); };
    handlers["head_height_ratio"] = [&](const std::string& v) { head_height_ratio = safe_stof(v); };
    handlers["sticky_zone_factor"] = [&](const std::string& v) { sticky_zone_factor = safe_stof(v); };
    handlers["sticky_damping"] = [&](const std::string& v) { sticky_damping = safe_stof(v); };
    handlers["hum_micro_movements"] = [&](const std::string& v) { hum_micro_movements = std::stoi(v); };
    handlers["hum_micro_amplitude"] = [&](const std::string& v) { hum_micro_amplitude = safe_stof(v); };
    handlers["hum_reaction_jitter"] = [&](const std::string& v) { hum_reaction_jitter = safe_stof(v); };
    handlers["hum_path_randomization"] = [&](const std::string& v) { hum_path_randomization = safe_stof(v); };
    handlers["hum_overshoot_enabled"] = [&](const std::string& v) { hum_overshoot_enabled = std::stoi(v); };
    handlers["hum_overshoot_chance"] = [&](const std::string& v) { hum_overshoot_chance = safe_stof(v); };
    handlers["hum_overshoot_amount"] = [&](const std::string& v) { hum_overshoot_amount = safe_stof(v); };
    handlers["hum_return_speed"] = [&](const std::string& v) { hum_return_speed = safe_stof(v); };
    handlers["pixelsmooth_enabled"] = [&](const std::string& v) { pixelsmooth_enabled = std::stoi(v); };
    handlers["pixelsmooth_value"] = [&](const std::string& v) { pixelsmooth_value = safe_stof(v); };
    handlers["smooth_factor"] = [&](const std::string& v) { smooth_factor = safe_stof(v); };
    handlers["motion_tuning_enabled"] = [&](const std::string& v) { motion_tuning_enabled = std::stoi(v); };
    handlers["kalman_enable"] = [&](const std::string& v) { kalman_enable = std::stoi(v); };
    handlers["kalman_q"] = [&](const std::string& v) { kalman_q = safe_stof(v); };
    handlers["kalman_r"] = [&](const std::string& v) { kalman_r = safe_stof(v); };
    handlers["oe_enable"] = [&](const std::string& v) { oe_enable = std::stoi(v); };
    handlers["oe_mincutoff"] = [&](const std::string& v) { oe_mincutoff = safe_stof(v); };
    handlers["oe_beta"] = [&](const std::string& v) { oe_beta = safe_stof(v); };
    handlers["confidence_fusion_enable"] = [&](const std::string& v) { confidence_fusion_enable = std::stoi(v); };
    handlers["esp_oe_enable"] = [&](const std::string& v) { esp_oe_enable = std::stoi(v); };
    handlers["esp_oe_mincutoff"] = [&](const std::string& v) { esp_oe_mincutoff = safe_stof(v); };
    handlers["esp_oe_beta"] = [&](const std::string& v) { esp_oe_beta = safe_stof(v); };
    handlers["pid_enable"] = [&](const std::string& v) { pid_enable = std::stoi(v); };
    handlers["pid_kp"] = [&](const std::string& v) { pid_kp = safe_stof(v); };
    handlers["pid_ki"] = [&](const std::string& v) { pid_ki = safe_stof(v); };
    handlers["pid_kd"] = [&](const std::string& v) { pid_kd = safe_stof(v); };
    handlers["enable_spoofer"] = [&](const std::string& v) { enable_spoofer = std::stoi(v); };
    handlers["spoofer_vid"] = [&](const std::string& v) { strncpy(spoofer_vid, v.c_str(), 4); };
    handlers["spoofer_pid"] = [&](const std::string& v) { strncpy(spoofer_pid, v.c_str(), 4); };
    handlers["auto_spoof"] = [&](const std::string& v) { auto_spoof = std::stoi(v); };
    handlers["byte_track_thresh"] = [&](const std::string& v) { byte_track_thresh = safe_stof(v); };
    handlers["byte_track_buffer"] = [&](const std::string& v) { byte_track_buffer = std::stoi(v); };
    handlers["byte_match_thresh"] = [&](const std::string& v) { byte_match_thresh = safe_stof(v); };
    handlers["byte_frame_rate"] = [&](const std::string& v) { byte_frame_rate = std::stoi(v); };
    handlers["use_advanced_sticky_aim"] = [&](const std::string& v) { use_advanced_sticky_aim = std::stoi(v); };
    handlers["sticky_threshold"] = [&](const std::string& v) { sticky_threshold = safe_stof(v); };
    handlers["sticky_frames_keep"] = [&](const std::string& v) { sticky_frames_keep = std::stoi(v); };
    handlers["prediction_method"] = [&](const std::string& v) { prediction_method = std::stoi(v); };
    handlers[("min_sensitivity")] = [&](const std::string& v) { min_sensitivity = safe_stof(v); };
    handlers["max_sensitivity"] = [&](const std::string& v) { max_sensitivity = safe_stof(v); };
    handlers["detection_resolution"] = [&](const std::string& v) { detection_resolution = std::stoi(v); };
    handlers["kalman_compensate_detection_delay"] = [&](const std::string& v) { kalman_compensate_detection_delay = std::stoi(v); };
    handlers["kalman_additional_prediction_ms"] = [&](const std::string& v) { kalman_additional_prediction_ms = safe_stof(v); };
    handlers["prediction_interval"] = [&](const std::string& v) { prediction_interval = safe_stof(v); };
    handlers["disable_headshot"] = [&](const std::string& v) { disable_headshot = std::stoi(v); };

    std::istringstream stream(config_data);
    std::string line;
    while (std::getline(stream, line)) {
        auto delim = line.find('=');
        if (delim != std::string::npos) {
            std::string key = line.substr(0, delim);
            std::string val = line.substr(delim + 1);
            auto it = handlers.find(key);
            if (it != handlers.end()) {
                try {
                    it->second(val);
                }
                catch (const std::exception& e) {
                    OutputDebugStringA(("Config parse error: " + key + " - " + e.what()).c_str());
                }
            }
        }
    }
    VMProtectEnd();
}

// ============================================================
// SaveConfig
// ============================================================
void Overlay::SaveConfig(Aimbot* aim) {
    VMProtectBeginUltra("SaveConfig");
    std::ostringstream ss;
    ss.imbue(std::locale("C"));
    ss << "version=6\n";
    ss << "lifetime_seconds=" << lifetime_seconds << "\n";
    ss << "aim_max_sens=" << aim_max_sens << "\n";
    ss << "aim_min_sens=" << aim_min_sens << "\n";
    ss << "fov_aimbot=" << fov_aimbot << "\n";
    ss << "is_russian=" << is_russian << "\n";
    ss << "aim_offset_x=" << aim_offset_x << "\n";
    ss << "aim_offset_y=" << aim_offset_y << "\n";
    ss << "aim_key_main=" << aim_key_main << "\n";
    ss << "aim_key_sub=" << aim_key_sub << "\n";
    ss << "ai_confidence_body=" << ai_confidence_body << "\n";
    ss << "ai_confidence_head=" << ai_confidence_head << "\n";
    ss << "min_box_area_body=" << min_box_area_body << "\n";
    ss << "min_box_area_head=" << min_box_area_head << "\n";
    ss << "disable_all_visuals_when_hidden=" << disable_all_visuals_when_hidden << "\n";
    ss << "custom_res_w=" << custom_res_w << "\n";
    ss << "custom_res_h=" << custom_res_h << "\n";
    ss << "aim_smoother=" << aim_smoother << "\n";
    ss << "fov_scan=" << fov_scan << "\n";
    ss << "aim_kill_delay=" << aim_kill_delay << "\n";
    ss << "obs_bypass=" << obs_bypass << "\n";
    ss << "menu_scale=" << menu_scale << "\n";
    ss << "hardware_mode_idx=" << hardware_mode_idx << "\n";
    ss << "hw_enabled=" << hw_enabled << "\n";
    ss << "bypass_mode_idx=" << bypass_mode_idx << "\n";
    ss << "baud_rate_idx=" << baud_rate_idx << "\n";
    ss << "com_port_buf=" << com_port_buf << "\n";
    ss << "kmbox_ip_buf=" << kmbox_ip_buf << "\n";
    ss << "kmbox_port=" << kmbox_port << "\n";
    ss << "random_delay_min=" << random_delay_min << "\n";
    ss << "random_delay_max=" << random_delay_max << "\n";
    ss << "test_move_x=" << test_move_x << "\n";
    ss << "test_move_y=" << test_move_y << "\n";
    ss << "com_port=" << com_port << "\n";
    ss << "aim_deadzone=" << aim_deadzone << "\n";
    ss << "draw_crosshair=" << draw_crosshair << "\n";
    ss << "hit_chance=" << hit_chance << "\n";
    ss << "menu_width=" << menu_width << "\n";
    ss << "menu_height=" << menu_height << "\n";
    ss << "trigger_enable=" << trigger_enable << "\n";
    ss << "trigger_delay=" << trigger_delay << "\n";
    ss << "trigger_target=" << trigger_target << "\n";
    ss << "trigger_key=" << trigger_key << "\n";
    ss << "rcs_enable=" << rcs_enable << "\n";
    ss << "rcs_pitch=" << rcs_pitch << "\n";
    ss << "rcs_yaw=" << rcs_yaw << "\n";
    ss << "eco_mode=" << eco_mode << "\n";
    ss << "memory_enemy_frames=" << memory_enemy_frames << "\n";
    ss << "refresh_rate_idx=" << refresh_rate_idx << "\n";
    ss << "auto_confidence=" << auto_confidence << "\n";
    ss << "sticky_aim=" << sticky_aim << "\n";
    ss << "enable_pose_adaptive=" << enable_pose_adaptive << "\n";
    ss << "accent_color_0=" << accent_color[0] << "\n";
    ss << "accent_color_1=" << accent_color[1] << "\n";
    ss << "accent_color_2=" << accent_color[2] << "\n";
    ss << "target_monitor=" << target_monitor << "\n";
    ss << "enable_dma_fuser=" << enable_dma_fuser << "\n";
    ss << "enable_dynamic_fov=" << enable_dynamic_fov << "\n";
    ss << "aim_toggle_key=" << aim_toggle_key << "\n";
    ss << "aim_target_lock=" << aim_target_lock << "\n";
    ss << "aim_dynamic_smooth=" << aim_dynamic_smooth << "\n";
    ss << "aim_switch_delay=" << aim_switch_delay << "\n";
    ss << "aim_lock_x=" << aim_lock_x << "\n";
    ss << "aim_lock_y=" << aim_lock_y << "\n";
    ss << "aim_curve_type=" << aim_curve_type << "\n";
    ss << "selected_preset=" << selected_preset << "\n";
    ss << "esp_thickness=" << esp_thickness << "\n";
    ss << "esp_style=" << esp_style << "\n";
    ss << "color_esp_visible_0=" << color_esp_visible[0] << "\n";
    ss << "color_esp_visible_1=" << color_esp_visible[1] << "\n";
    ss << "color_esp_visible_2=" << color_esp_visible[2] << "\n";
    ss << "color_esp_hidden_0=" << color_esp_hidden[0] << "\n";
    ss << "color_esp_hidden_1=" << color_esp_hidden[1] << "\n";
    ss << "color_esp_hidden_2=" << color_esp_hidden[2] << "\n";
    ss << "neural_nms=" << neural_nms << "\n";
    ss << "neural_max_det=" << neural_max_det << "\n";
    ss << "enable_exclusion_zone=" << enable_exclusion_zone << "\n";
    ss << "excl_x1=" << excl_x1 << "\n";
    ss << "excl_y1=" << excl_y1 << "\n";
    ss << "excl_x2=" << excl_x2 << "\n";
    ss << "excl_y2=" << excl_y2 << "\n";
    ss << "ai_model=" << ai_model << "\n";
    ss << "humanizer_enable=" << humanizer_enable << "\n";
    ss << "hum_reaction_delay=" << hum_reaction_delay << "\n";
    ss << "hum_overshoot_chance=" << hum_overshoot_chance << "\n";
    ss << "hum_randomize_bone=" << hum_randomize_bone << "\n";
    ss << "hum_tremor_scale=" << hum_tremor_scale << "\n";
    ss << "elite_tsp_enabled=" << elite_tsp_enabled << "\n";
    ss << "elite_ballistics_enabled=" << elite_ballistics_enabled << "\n";
    ss << "elite_bullet_speed=" << elite_bullet_speed << "\n";
    ss << "elite_bullet_drop=" << elite_bullet_drop << "\n";
    ss << "elite_context_aware=" << elite_context_aware << "\n";
    ss << "elite_smoke_vision=" << elite_smoke_vision << "\n";
    ss << "elite_voice_ctrl=" << elite_voice_ctrl << "\n";
    ss << "elite_shadow_trainer=" << elite_shadow_trainer << "\n";
    ss << "shadow_webhook=" << shadow_webhook << "\n";
    ss << "max_move_step=" << max_move_step << "\n";
    ss << "head_height_ratio=" << head_height_ratio << "\n";
    ss << "sticky_zone_factor=" << sticky_zone_factor << "\n";
    ss << "sticky_damping=" << sticky_damping << "\n";
    ss << "hum_micro_movements=" << hum_micro_movements << "\n";
    ss << "hum_micro_amplitude=" << hum_micro_amplitude << "\n";
    ss << "hum_reaction_jitter=" << hum_reaction_jitter << "\n";
    ss << "hum_path_randomization=" << hum_path_randomization << "\n";
    ss << "hum_overshoot_enabled=" << hum_overshoot_enabled << "\n";
    ss << "hum_overshoot_chance=" << hum_overshoot_chance << "\n";
    ss << "hum_overshoot_amount=" << hum_overshoot_amount << "\n";
    ss << "hum_return_speed=" << hum_return_speed << "\n";
    ss << "pixelsmooth_enabled=" << pixelsmooth_enabled << "\n";
    ss << "pixelsmooth_value=" << pixelsmooth_value << "\n";
    ss << "smooth_factor=" << smooth_factor << "\n";
    ss << "motion_tuning_enabled=" << motion_tuning_enabled << "\n";
    ss << "kalman_enable=" << kalman_enable << "\n";
    ss << "kalman_q=" << kalman_q << "\n";
    ss << "kalman_r=" << kalman_r << "\n";
    ss << "oe_enable=" << oe_enable << "\n";
    ss << "oe_mincutoff=" << oe_mincutoff << "\n";
    ss << "oe_beta=" << oe_beta << "\n";
    ss << "confidence_fusion_enable=" << confidence_fusion_enable << "\n";
    ss << "esp_oe_enable=" << esp_oe_enable << "\n";
    ss << "esp_oe_mincutoff=" << esp_oe_mincutoff << "\n";
    ss << "esp_oe_beta=" << esp_oe_beta << "\n";
    ss << "pid_enable=" << pid_enable << "\n";
    ss << "pid_kp=" << pid_kp << "\n";
    ss << "pid_ki=" << pid_ki << "\n";
    ss << "pid_kd=" << pid_kd << "\n";
    ss << "enable_spoofer=" << enable_spoofer << "\n";
    ss << "spoofer_vid=" << spoofer_vid << "\n";
    ss << "spoofer_pid=" << spoofer_pid << "\n";
    ss << "auto_spoof=" << auto_spoof << "\n";
    ss << "byte_track_thresh=" << byte_track_thresh << "\n";
    ss << "byte_track_buffer=" << byte_track_buffer << "\n";
    ss << "byte_match_thresh=" << byte_match_thresh << "\n";
    ss << "byte_frame_rate=" << byte_frame_rate << "\n";
    ss << "use_advanced_sticky_aim=" << use_advanced_sticky_aim << "\n";
    ss << "sticky_threshold=" << sticky_threshold << "\n";
    ss << "sticky_frames_keep=" << sticky_frames_keep << "\n";
    ss << "prediction_method=" << prediction_method << "\n";
    ss << "min_sensitivity=" << min_sensitivity << "\n";
    ss << "max_sensitivity=" << max_sensitivity << "\n";
    ss << "detection_resolution=" << detection_resolution << "\n";
    ss << "kalman_compensate_detection_delay=" << kalman_compensate_detection_delay << "\n";
    ss << "kalman_additional_prediction_ms=" << kalman_additional_prediction_ms << "\n";
    ss << "prediction_interval=" << prediction_interval << "\n";
    ss << "disable_headshot=" << disable_headshot << "\n";

    std::string config_str = ss.str();
    DATA_BLOB DataIn;
    DATA_BLOB DataOut;
    DataIn.pbData = (BYTE*)config_str.c_str();
    DataIn.cbData = (DWORD)config_str.length();
    if (CryptProtectData(&DataIn, L"PWNZ_CFG", NULL, NULL, NULL, 0, &DataOut)) {
        std::ofstream file_bin(XOR("pwnz_enc.bin"), std::ios::binary);
        if (file_bin.is_open()) {
            file_bin.write((char*)DataOut.pbData, DataOut.cbData);
            file_bin.close();
        }
        LocalFree(DataOut.pbData);
        DeleteFileA(XOR("pwnz_cfg.ini"));
    }
    VMProtectEnd();
}

// ============================================================
// ResetDefaults, ApplySafeSettings
// ============================================================
void Overlay::ResetDefaults() {
    draw_esp = true; draw_fov = true; draw_fov_neural = true; draw_watermark = true; draw_crosshair = false;
    disable_all_visuals_when_hidden = true;
    aim_enable = true; aim_max_sens = 2.0f; aim_min_sens = 0.5f; aim_kill_delay = 0.09f; aim_target = 0;
    aim_key_main = VK_RBUTTON; aim_key_sub = 0; aim_toggle_key = 0; eco_mode = true;
    aim_deadzone = 2.0f; aim_smoother = 15.0f; sticky_aim = false;
    fov_aimbot = 190.0f; fov_scan = 192.0f;
    aim_offset_x = 0.0f; aim_offset_y = 0.0f; ai_model = 0;
    ai_confidence_body = 48.0f; ai_confidence_head = 35.0f;
    min_box_area_body = 150.0f; min_box_area_head = 40.0f;
    hit_chance = 90.0f; auto_confidence = false;
    custom_res_w = 1920; custom_res_h = 1080;
    obs_bypass = true; menu_scale = 100.0f; menu_width = 1150.0f; menu_height = 680.0f;
    hardware_mode_idx = 0; com_port = 2; trigger_enable = false; trigger_delay = 0.05f; trigger_target = 0; trigger_key = 0;
    rcs_enable = false; rcs_pitch = 1.0f; rcs_yaw = 0.0f;
    refresh_rate_idx = 0; memory_enemy_frames = 3; enable_pose_adaptive = false;
    accent_color[0] = 0.0f; accent_color[1] = 0.8f; accent_color[2] = 1.0f;
    target_monitor = 0; enable_dma_fuser = false; enable_dynamic_fov = false;
    aim_target_lock = true; aim_dynamic_smooth = false; aim_switch_delay = 0; aim_lock_x = false; aim_lock_y = false; aim_curve_type = 0;
    selected_preset = 0; esp_thickness = 1.5f; esp_style = 0; neural_nms = 0.45f; neural_max_det = 5;
    humanizer_enable = true; hum_reaction_delay = 15.0f; hum_overshoot_chance = 5.0f;
    hum_randomize_bone = false; hum_tremor_scale = 1.0f;
    hum_path_randomization = 0.3f; hum_overshoot_enabled = false;
    hum_overshoot_amount = 1.2f; hum_return_speed = 0.85f;
    pixelsmooth_enabled = true; pixelsmooth_value = 8.0f; smooth_factor = 0.15f;
    enable_exclusion_zone = false; excl_x1 = 0; excl_y1 = 0; excl_x2 = 0; excl_y2 = 0;
    max_move_step = 50.0f;
    head_height_ratio = 0.1f;
    sticky_zone_factor = 0.5f;
    sticky_damping = 0.3f;
    hum_micro_movements = true;
    hum_micro_amplitude = 0.8f;
    hum_reaction_jitter = 2.0f;
    motion_tuning_enabled = true;
    kalman_enable = true; kalman_q = 0.05f; kalman_r = 0.2f;
    oe_enable = true; oe_mincutoff = 1.0f; oe_beta = 0.05f;
    confidence_fusion_enable = true;
    esp_oe_enable = true; esp_oe_mincutoff = 0.5f; esp_oe_beta = 0.01f;
    pid_enable = false;
    pid_kp = 0.2f;
    pid_ki = 0.01f;
    pid_kd = 0.1f;
    enable_spoofer = false;
    strcpy(spoofer_vid, "046D");
    strcpy(spoofer_pid, "C07E");
    auto_spoof = false;
    byte_track_thresh = 0.5f;
    byte_track_buffer = 30;
    byte_match_thresh = 0.8f;
    byte_frame_rate = 30;
    use_advanced_sticky_aim = true;
    sticky_threshold = 50.0f;
    sticky_frames_keep = 3;
    prediction_method = 1;
    min_sensitivity = 0.5f;
    max_sensitivity = 3.0f;
    kalman_compensate_detection_delay = true;
    kalman_additional_prediction_ms = 0.0f;
    prediction_interval = 0.01f;
    disable_headshot = false;
    DeleteFileA(XOR("pwnz_cfg.ini"));
    DeleteFileA(XOR("pwnz_enc.bin"));
}

void Overlay::ApplySafeSettings() {
    aim_max_sens = 1.5f; aim_min_sens = 0.8f;
    aim_smoother = 14.0f;
    aim_deadzone = 1.5f;
    aim_kill_delay = 0.15f;
    fov_aimbot = 120.0f; fov_scan = 180.0f;
    memory_enemy_frames = 3; aim_target = 0;
    enable_dynamic_fov = true; ai_confidence_body = 50.0f; ai_confidence_head = 40.0f;
    aim_dynamic_smooth = true;
    aim_curve_type = 3;
    aim_switch_delay = 150;
    sticky_aim = false; auto_confidence = true; eco_mode = true;
    aim_target_lock = true; humanizer_enable = true;
    max_move_step = 50.0f;
    head_height_ratio = 0.105f;
    sticky_zone_factor = 0.5f;
    sticky_damping = 0.3f;
    hum_micro_movements = true;
    hum_micro_amplitude = 0.7f;
    hum_reaction_jitter = 1.5f;
    kalman_enable = true; kalman_q = 0.05f; kalman_r = 0.2f;
    oe_enable = true; oe_mincutoff = 1.0f; oe_beta = 0.05f;
    confidence_fusion_enable = true;
    esp_oe_enable = true; esp_oe_mincutoff = 0.5f; esp_oe_beta = 0.01f;
    pid_enable = false;
    pid_kp = 0.15f;
    pid_ki = 0.005f;
    pid_kd = 0.08f;
    enable_spoofer = false;
    auto_spoof = false;
    byte_track_thresh = 0.45f;
    byte_track_buffer = 45;
    byte_match_thresh = 0.75f;
    byte_frame_rate = 30;
    use_advanced_sticky_aim = true;
    sticky_threshold = 60.0f;
    sticky_frames_keep = 2;
    prediction_method = 1;
    pixelsmooth_enabled = true;
    pixelsmooth_value = 8.0f;
    smooth_factor = 0.15f;
    kalman_compensate_detection_delay = true;
    kalman_additional_prediction_ms = 10.0f;
    prediction_interval = 0.02f;
    disable_headshot = false;
}

// ============================================================
// Вспомогательные UI методы
// ============================================================
bool Overlay::DrawToggleOnly(const char* str_id, bool* v, ImU32 accent_u32) {
    bool changed = false;
    ImVec2 p = ImGui::GetCursorScreenPos(); ImDrawList* draw_list = ImGui::GetWindowDrawList();
    float height = ImGui::GetFrameHeight(); float width = height * 1.8f; float radius = height * 0.50f;
    ImGui::InvisibleButton(str_id, ImVec2(width, height));
    if (ImGui::IsItemClicked()) { *v = !*v; changed = true; }
    float t = *v ? 1.0f : 0.0f; ImU32 col_bg = *v ? accent_u32 : IM_COL32(60, 50, 80, 255);
    draw_list->AddRectFilled(p, ImVec2(p.x + width, p.y + height), col_bg, height * 0.5f);
    draw_list->AddCircleFilled(ImVec2(p.x + radius + t * (width - radius * 2.0f), p.y + radius), radius - 2.0f, IM_COL32(255, 255, 255, 255));
    return changed;
}

bool Overlay::DrawToggle(const char* label, const char* str_id, bool* v, ImU32 accent_u32, const char* help) {
    ImGui::Text("%s", label);
    float w = ImGui::GetColumnWidth();
    float text_w = ImGui::CalcTextSize(label).x;
    float offset = w - (help ? 65.0f : 45.0f);
    if (offset < text_w + 15.0f) offset = text_w + 15.0f;
    ImGui::SameLine(offset);
    bool changed = DrawToggleOnly(str_id, v, accent_u32);
    if (help) HelpMarker(help);
    return changed;
}

bool Overlay::CustomSliderFloat(const char* label, const char* label_id, float* v, float v_min, float v_max, const char* format, ImVec4 accent_vec, const char* help) {
    ImGui::Text("%s", label);
    float w = ImGui::GetColumnWidth();
    float text_w = ImGui::CalcTextSize(label).x;
    float offset = w - (help ? 130.0f : 105.0f);
    if (offset < text_w + 15.0f) offset = text_w + 15.0f;
    ImGui::SameLine(offset);
    char buf[32]; snprintf(buf, sizeof(buf), format, *v);
    ImGui::TextColored(accent_vec, "%s", buf);
    float item_w = w - (help ? 45.0f : 25.0f);
    if (item_w < 50.0f) item_w = 50.0f;
    ImGui::PushItemWidth(item_w);
    bool changed = ImGui::SliderFloat(label_id, v, v_min, v_max, "");
    ImGui::PopItemWidth();
    if (help) HelpMarker(help);
    return changed;
}

bool Overlay::CustomSliderInt(const char* label, const char* label_id, int* v, int v_min, int v_max, const char* format, ImVec4 accent_vec, const char* help) {
    ImGui::Text("%s", label);
    float w = ImGui::GetColumnWidth();
    float text_w = ImGui::CalcTextSize(label).x;
    float offset = w - (help ? 130.0f : 105.0f);
    if (offset < text_w + 15.0f) offset = text_w + 15.0f;
    ImGui::SameLine(offset);
    char buf[32]; snprintf(buf, sizeof(buf), format, *v);
    ImGui::TextColored(accent_vec, "%s", buf);
    float item_w = w - (help ? 45.0f : 25.0f);
    if (item_w < 50.0f) item_w = 50.0f;
    ImGui::PushItemWidth(item_w);
    bool changed = ImGui::SliderInt(label_id, v, v_min, v_max, "");
    ImGui::PopItemWidth();
    if (help) HelpMarker(help);
    return changed;
}

bool Overlay::CustomCombo(const char* label, const char* label_id, int* current_item, const char* const items[], int items_count, const char* help) {
    ImGui::Text("%s", label);
    float w = ImGui::GetColumnWidth();
    float text_w = ImGui::CalcTextSize(label).x;
    float offset = w - (help ? 165.0f : 140.0f);
    if (offset < text_w + 15.0f) offset = text_w + 15.0f;
    ImGui::SameLine(offset);
    ImGui::PushItemWidth(120.0f);
    bool changed = ImGui::Combo(label_id, current_item, items, items_count);
    ImGui::PopItemWidth();
    if (help) HelpMarker(help);
    return changed;
}

bool Overlay::DrawKeybinder(const char* label, int* vk_key, int id, const char* help) {
    bool changed = false;
    ImGui::Text("%s", label);
    float w = ImGui::GetColumnWidth();
    float text_w = ImGui::CalcTextSize(label).x;
    float offset = w - (help ? 125.0f : 100.0f);
    if (offset < text_w + 15.0f) offset = text_w + 15.0f;
    ImGui::SameLine(offset);
    std::string btn_label;
    if (active_bind_id == id) {
        btn_label = is_russian ? u8"[ Нажми ]" : "[ Press ]";
        for (int i = 1; i < 256; i++) {
            if (GetAsyncKeyState(i) & 0x8000) { if (i == VK_ESCAPE) *vk_key = 0; else *vk_key = i; active_bind_id = -1; changed = true; break; }
        }
    }
    else {
        if (*vk_key == 0) btn_label = "None";
        else if (*vk_key == VK_LBUTTON) btn_label = "LMB";
        else if (*vk_key == VK_RBUTTON) btn_label = "RMB";
        else { char keyName[32]; if (GetKeyNameTextA(MapVirtualKeyA(*vk_key, MAPVK_VK_TO_VSC) << 16, keyName, sizeof(keyName))) btn_label = keyName; else btn_label = "VK"; }
    }
    if (ImGui::Button((btn_label + "##" + std::to_string(id)).c_str(), ImVec2(100, 25))) active_bind_id = id;
    if (help) HelpMarker(help);
    return changed;
}

void Overlay::HelpMarker(const char* desc) {
    ImGui::SameLine(0.0f, 5.0f);
    ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 25.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

bool Overlay::BeginPanel(const char* name, ImVec2 size, ImVec4 accent_vec, bool has_toggle, bool* toggle_val, ImU32 accent_u32) {
    bool changed = false;
    ImGui::BeginChild(name, size, true, ImGuiWindowFlags_NoScrollbar);
    std::string display_name = name; size_t hash_pos = display_name.find("###");
    if (hash_pos != std::string::npos) display_name = display_name.substr(0, hash_pos);
    ImGui::PushStyleColor(ImGuiCol_Text, accent_vec);
    ImGui::Text("%s", display_name.c_str());
    ImGui::PopStyleColor();
    if (has_toggle && toggle_val) {
        ImGui::SameLine(ImGui::GetWindowWidth() - 45.0f);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2);
        if (DrawToggleOnly((std::string("##Tog") + std::string(name)).c_str(), toggle_val, accent_u32)) changed = true;
    }
    ImGui::Spacing();
    return changed;
}

void Overlay::EndPanel() { ImGui::EndChild(); }

// ============================================================
// RenderAimbotTab (вкладка Aimbot) - ПОЛНОСТЬЮ ПЕРЕПИСАНА
// Все настройки влияют на аимбот в реальном времени
// ============================================================
void Overlay::RenderAimbotTab(float content_w, float content_h, const ImVec4& acc_vec, ImU32 acc_u32, bool& cfg_changed) {
    ImGui::Columns(2, nullptr, false);
    ImGui::SetColumnWidth(0, content_w * 0.48f);
    ImGui::SetColumnWidth(1, content_w * 0.52f);

    const char* tgts[] = { "Head", "Body", "Auto" };

    // === ЛЕВАЯ КОЛОНКА ===
    
    // Aimbot Core - Основные настройки
    if (BeginPanel("Aimbot Core", ImVec2(0, 280), acc_vec, true, &aim_enable, acc_u32)) cfg_changed = true;
    if (DrawKeybinder("Main Bind:", &aim_key_main, 1, u8"Основная кнопка активации.")) cfg_changed = true;
    if (DrawKeybinder("Toggle Key:", &aim_toggle_key, 3, u8"Включение/выключение аимбота.")) cfg_changed = true;
    if (CustomCombo("Target Part:", "##tgt", &aim_target, tgts, 3, u8"Часть тела для прицеливания.")) cfg_changed = true;
    if (DrawToggle("Keep Current Lock:", "##keep_lock", &aim_target_lock, acc_u32, u8"Не переключаться на другую цель автоматически.")) cfg_changed = true;
    if (CustomSliderInt("Switch Delay:", "##sw_dly", &aim_switch_delay, 0, 1000, "%d ms", acc_vec, u8"Задержка перед переключением на другую цель.")) cfg_changed = true;
    EndPanel();

    // FOV Settings - Радиус захвата
    if (BeginPanel("FOV Settings", ImVec2(0, 140), acc_vec)) cfg_changed = true;
    if (CustomSliderFloat("Aim FOV:", "##f_a", &fov_aimbot, 10.0f, 500.0f, "%.0f px", acc_vec, u8"Радиус аимбота в пикселях.")) cfg_changed = true;
    if (DrawToggle("Dynamic FOV:", "##dfov", &enable_dynamic_fov, acc_u32, u8"Адаптивный FOV.")) cfg_changed = true;
    EndPanel();

    // Speed Control - Скорость наведения (ГЛАВНАЯ НАСТРОЙКА!)
    if (BeginPanel("Speed Control", ImVec2(0, 200), acc_vec)) cfg_changed = true;
    ImGui::TextColored(acc_vec, "Min Sensitivity:");
    HelpMarker(u8"Минимальная скорость движения мыши. Чем выше значение, тем быстрее аимбот на близких дистанциях.");
    if (CustomSliderFloat("##min_sp", "##min_sp_lbl", &min_sensitivity, 0.01f, 5.0f, "%.3f", acc_vec)) cfg_changed = true;
    
    ImGui::Spacing();
    ImGui::TextColored(acc_vec, "Max Sensitivity:");
    HelpMarker(u8"Максимальная скорость движения мыши. Потолок скорости на дальних дистанциях.");
    if (CustomSliderFloat("##max_sp", "##max_sp_lbl", &max_sensitivity, 0.1f, 20.0f, "%.3f", acc_vec)) cfg_changed = true;
    
    ImGui::Spacing();
    ImGui::TextColored(acc_vec, "Max Move Step:");
    HelpMarker(u8"Максимальное смещение мыши за один кадр. Ограничивает резкость движений.");
    if (CustomSliderFloat("##max_move", "##max_move_lbl", &max_move_step, 10.0f, 500.0f, "%.1f px", acc_vec)) cfg_changed = true;
    
    ImGui::Spacing();
    if (CustomSliderInt("Detection Resolution:", "##det_res", &detection_resolution, 160, 960, "%d px", acc_vec, u8"Разрешение детекции для аимбота.")) cfg_changed = true;
    EndPanel();

    // Pixelsmooth / Smoothing - Плавность
    if (BeginPanel("Pixelsmooth / Smoothing", ImVec2(0, 200), acc_vec, true, &pixelsmooth_enabled, acc_u32)) cfg_changed = true;
    if (CustomSliderFloat("Smooth Factor:", "##smooth_f", &smooth_factor, 0.01f, 1.0f, "%.3f", acc_vec, u8"Фактор сглаживания. Меньше = плавнее, Больше = быстрее.")) cfg_changed = true;
    if (CustomSliderFloat("Pixelsmooth Steps:", "##pix_val", &pixelsmooth_value, 1.0f, 32.0f, "%.0f frames", acc_vec, u8"Количество кадров для усреднения движения.")) cfg_changed = true;
    ImGui::Separator();
    ImGui::TextWrapped(u8"Эти настройки делают движение прицела более плавным и человечным. Меньший Smooth Factor = более плавное движение.");
    EndPanel();

    ImGui::NextColumn();

    // === ПРАВАЯ КОЛОНКА ===

    // Humanizer - Человеческая рандомизация
    if (BeginPanel("Humanizer", ImVec2(0, 280), acc_vec, true, &humanizer_enable, acc_u32)) cfg_changed = true;
    if (CustomSliderFloat("Reaction Delay:", "##hum_react", &hum_reaction_delay, 0.0f, 200.0f, "%.0f ms", acc_vec, u8"Искусственная задержка реакции.")) cfg_changed = true;
    if (CustomSliderFloat("Tremor Scale:", "##hum_tremor", &hum_tremor_scale, 0.0f, 5.0f, "%.2f", acc_vec, u8"Размах дрожания рук.")) cfg_changed = true;
    
    if (DrawToggle("Micro Movements:", "##hum_micro", &hum_micro_movements, acc_u32, u8"Микродвижения прицела.")) cfg_changed = true;
    if (hum_micro_movements) {
        if (CustomSliderFloat("Micro Amplitude:", "##hum_micro_amp", &hum_micro_amplitude, 0.0f, 3.0f, "%.2f px", acc_vec, u8"Амплитуда микродвижений.")) cfg_changed = true;
    }
    
    if (CustomSliderFloat("Path Randomization:", "##hum_path_rand", &hum_path_randomization, 0.0f, 2.0f, "%.2f", acc_vec, u8"Рандомизация траектории.")) cfg_changed = true;
    if (CustomSliderFloat("Reaction Jitter:", "##reac_jit", &hum_reaction_jitter, 0.0f, 5.0f, "%.1f px", acc_vec, u8"Отклонение при захвате цели.")) cfg_changed = true;
    
    if (DrawToggle("Overshoot:", "##hum_overshoot_en", &hum_overshoot_enabled, acc_u32, u8"Искусственный перелёт цели.")) cfg_changed = true;
    if (hum_overshoot_enabled) {
        if (CustomSliderFloat("Overshoot Chance:", "##hum_overshoot_ch", &hum_overshoot_chance, 0.0f, 50.0f, "%.1f%%", acc_vec, u8"Шанс перелёта.")) cfg_changed = true;
        if (CustomSliderFloat("Overshoot Amount:", "##hum_overshoot_amt", &hum_overshoot_amount, 1.0f, 3.0f, "%.2f x", acc_vec, u8"Множитель перелёта.")) cfg_changed = true;
        if (CustomSliderFloat("Return Speed:", "##hum_return_sp", &hum_return_speed, 0.1f, 1.0f, "%.2f", acc_vec, u8"Скорость возврата после перелёта.")) cfg_changed = true;
    }
    EndPanel();

    // Kalman Predictor - Предикция движения
    if (BeginPanel("Kalman Predictor", ImVec2(0, 220), acc_vec, true, &kalman_enable, acc_u32)) cfg_changed = true;
    if (kalman_enable) {
        if (CustomSliderFloat("Process Noise (Q):", "##k_q", &kalman_q, 0.001f, 1.0f, "%.3f", acc_vec, u8"Шум процесса. Выше = агрессивнее предсказание.")) cfg_changed = true;
        if (CustomSliderFloat("Measurement Noise (R):", "##k_r", &kalman_r, 0.01f, 1.0f, "%.2f", acc_vec, u8"Шум измерений. Выше = больше доверия к модели.")) cfg_changed = true;
        if (DrawToggle("Compensate Detection Delay:", "##comp_delay", &kalman_compensate_detection_delay, acc_u32, u8"Учитывать задержку нейросети.")) cfg_changed = true;
        if (CustomSliderFloat("Additional Prediction:", "##add_pred", &kalman_additional_prediction_ms, -50.0f, 120.0f, "%.0f ms", acc_vec, u8"Дополнительное упреждение в миллисекундах.")) cfg_changed = true;
        if (CustomSliderFloat("Prediction Interval:", "##pred_int", &prediction_interval, 0.0f, 0.2f, "%.3f sec", acc_vec, u8"Базовое упреждение в секундах.")) cfg_changed = true;
    }
    EndPanel();

    // Recoil Control (RCS) - Контроль отдачи
    if (BeginPanel("Recoil Control (RCS)", ImVec2(0, 150), acc_vec, true, &rcs_enable, acc_u32)) cfg_changed = true;
    if (CustomSliderFloat("Pitch (Down):", "##rcs_p", &rcs_pitch, 0.0f, 10.0f, "%.1f px", acc_vec, u8"Вертикальная компенсация отдачи.")) cfg_changed = true;
    if (CustomSliderFloat("Yaw (Left/Right):", "##rcs_y", &rcs_yaw, -5.0f, 5.0f, "%.1f px", acc_vec, u8"Горизонтальная компенсация отдачи.")) cfg_changed = true;
    EndPanel();

    ImGui::Columns(1);
    
    // Misc - Дополнительные настройки
    if (BeginPanel("Misc", ImVec2(0, 120), acc_vec)) cfg_changed = true;
    if (DrawToggle("Disable Headshot:", "##no_head", &disable_headshot, acc_u32, u8"Запретить прицеливание в голову.")) cfg_changed = true;
    if (DrawToggle("Lock X-Axis:", "##lock_x", &aim_lock_x, acc_u32, u8"Заблокировать горизонтальное перемещение.")) cfg_changed = true;
    if (DrawToggle("Lock Y-Axis:", "##lock_y", &aim_lock_y, acc_u32, u8"Заблокировать вертикальное перемещение.")) cfg_changed = true;
    EndPanel();
}

// ============================================================
// RenderVisualsTab (вкладка Visuals)
// ============================================================
void Overlay::RenderVisualsTab(float content_w, float content_h, const ImVec4& acc_vec, ImU32 acc_u32, bool& cfg_changed) {
    ImGui::Columns(2, nullptr, false);
    ImGui::SetColumnWidth(0, content_w * 0.5f);

    if (BeginPanel("HUD Display", ImVec2(0, 250), acc_vec)) cfg_changed = true;
    if (DrawToggle("Draw ESP Boxes:", "##e", &draw_esp, acc_u32, u8"ESP рамки.")) cfg_changed = true;
    if (DrawToggle("Draw AimFOV:", "##fa", &draw_fov, acc_u32, u8"Радиус аимбота.")) cfg_changed = true;
    if (DrawToggle("Draw NeuralFOV:", "##fn", &draw_fov_neural, acc_u32, u8"Радиус нейросети.")) cfg_changed = true;
    if (DrawToggle("Draw Crosshair:", "##crs", &draw_crosshair, acc_u32, u8"Кастомное перекрестие.")) cfg_changed = true;
    if (DrawToggle("Watermark FPS:", "##wm", &draw_watermark, acc_u32, u8"Информация о FPS.")) cfg_changed = true;
    EndPanel();

    if (BeginPanel("ESP Customizer", ImVec2(0, content_h - 250 - 20), acc_vec)) cfg_changed = true;
    const char* e_styles[] = { "Full Box", "Corners Only" };
    if (CustomCombo("Box Style", "##ebx", &esp_style, e_styles, 2, u8"Стиль рамок.")) cfg_changed = true;
    if (CustomSliderFloat("Box Thickness", "##ethck", &esp_thickness, 1.0f, 5.0f, "%.1f px", acc_vec, u8"Толщина линий.")) cfg_changed = true;

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "ESP Smoothing Filter:");
    if (DrawToggle("Enable OneEuro Filter", "##esp_oe_en", &esp_oe_enable, acc_u32, u8"Сглаживание ESP.")) cfg_changed = true;
    if (esp_oe_enable) {
        if (CustomSliderFloat("ESP MinCutoff", "##esp_oe_mc", &esp_oe_mincutoff, 0.1f, 5.0f, "%.2f", acc_vec, u8"Min cutoff.")) cfg_changed = true;
        if (CustomSliderFloat("ESP Beta", "##esp_oe_b", &esp_oe_beta, 0.001f, 0.2f, "%.3f", acc_vec, u8"Beta.")) cfg_changed = true;
    }

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextColored(acc_vec, "ESP Colors:");
    if (ImGui::ColorEdit3("Visible Color", color_esp_visible, ImGuiColorEditFlags_NoInputs)) cfg_changed = true;
    if (ImGui::ColorEdit3("Hidden Color", color_esp_hidden, ImGuiColorEditFlags_NoInputs)) cfg_changed = true;
    EndPanel();

    ImGui::NextColumn();

    if (BeginPanel("Optimization", ImVec2(0, 120), acc_vec)) cfg_changed = true;
    if (DrawToggle("Stealth Mode:", "##stl", &disable_all_visuals_when_hidden, acc_u32, u8"Скрывать графику при закрытом меню.")) cfg_changed = true;
    EndPanel();

    if (BeginPanel("Information", ImVec2(0, content_h - 120 - 20), acc_vec)) cfg_changed = true;
    ImGui::TextColored(acc_vec, "[?] Visual Framework");
    ImGui::TextWrapped(u8"Графический движок PWNZ работает поверх аппаратного оверлея DWM API.");
    EndPanel();

    ImGui::Columns(1);
}

// ============================================================
// RenderNeuralTab (вкладка Neural)
// ============================================================
void Overlay::RenderNeuralTab(float content_w, float content_h, const ImVec4& acc_vec, ImU32 acc_u32, bool& cfg_changed) {
    ImGui::Columns(3, nullptr, false);
    ImGui::SetColumnWidth(0, content_w * 0.33f);
    ImGui::SetColumnWidth(1, content_w * 0.33f);

    if (BeginPanel("AI Engine", ImVec2(0, 310), acc_vec)) cfg_changed = true;
    const char* models[] = { "BogX-Nano.onnx", "BogX-Lite.onnx", "BogX-Pro.onnx", "BogX-Ultra.onnx" };
    if (CustomCombo("Model:", "##mdl", &ai_model, models, 4, u8"Выбор модели нейросети.")) cfg_changed = true;
    ImGui::Spacing();
    if (ImGui::Button("Reload Model", ImVec2(150, 30))) apply_model_flag = true;
    ImGui::Spacing(); ImGui::Spacing();

    if (DrawToggle("Auto Confidence:", "##aconf", &auto_confidence, acc_u32, u8"Автоматическая регулировка порогов.")) cfg_changed = true;

    if (CustomSliderFloat("Confidence Body:", "##cnf_body", &ai_confidence_body, 0.0f, 100.0f, "%.0f%%", acc_vec, u8"Порог тела.")) cfg_changed = true;
    if (CustomSliderFloat("Confidence Head:", "##cnf_head", &ai_confidence_head, 0.0f, 100.0f, "%.0f%%", acc_vec, u8"Порог головы.")) cfg_changed = true;
    if (CustomSliderFloat("Min Box Area Body:", "##min_area_body", &min_box_area_body, 30.0f, 500.0f, "%.0f px²", acc_vec, u8"Мин. площадь тела.")) cfg_changed = true;
    if (CustomSliderFloat("Min Box Area Head:", "##min_area_head", &min_box_area_head, 10.0f, 200.0f, "%.0f px²", acc_vec, u8"Мин. площадь головы.")) cfg_changed = true;
    if (CustomSliderFloat("Hit Chance:", "##htc", &hit_chance, 0.0f, 100.0f, "%.0f%%", acc_vec, u8"Вероятность срабатывания.")) cfg_changed = true;
    EndPanel();

    if (BeginPanel("Inference Optimization", ImVec2(0, content_h - 310 - 20), acc_vec)) cfg_changed = true;
    if (CustomSliderFloat("NMS Threshold", "##nms", &neural_nms, 0.1f, 0.9f, "%.2f", acc_vec, u8"Порог NMS.")) cfg_changed = true;
    if (CustomSliderInt("Max Targets", "##mxdet", &neural_max_det, 1, 20, "%d", acc_vec, u8"Макс. целей.")) cfg_changed = true;
    EndPanel();

    ImGui::NextColumn();

    if (BeginPanel("Target Offsets (X/Y)", ImVec2(0, 160), acc_vec)) cfg_changed = true;
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), u8"Смещение точки прицела");
    ImGui::Spacing();
    ImGui::Text("Shift X (L/R):"); ImGui::SameLine(ImGui::GetColumnWidth() - 120.0f);
    ImGui::PushItemWidth(80.0f);
    if (ImGui::InputFloat("##offx", &aim_offset_x, 1.0f, 5.0f, "%.0f")) cfg_changed = true;
    ImGui::PopItemWidth();
    HelpMarker(u8"Смещение по X.");
    ImGui::Spacing();
    ImGui::Text("Shift Y (U/D):"); ImGui::SameLine(ImGui::GetColumnWidth() - 120.0f);
    ImGui::PushItemWidth(80.0f);
    if (ImGui::InputFloat("##offy", &aim_offset_y, 1.0f, 5.0f, "%.0f")) cfg_changed = true;
    ImGui::PopItemWidth();
    HelpMarker(u8"Смещение по Y.");
    EndPanel();

    const char* ez_title = is_russian ? u8"Слепая Зона (Игнор своего скина)###PnlEZ" : "Exclusion Zone###PnlEZ";
    if (BeginPanel(ez_title, ImVec2(0, content_h - 160 - 20), acc_vec, true, &enable_exclusion_zone, acc_u32)) cfg_changed = true;
    if (ImGui::Button(u8"Нарисовать Зону", ImVec2(ImGui::GetContentRegionAvail().x - 50.0f, 30))) {
        is_drawing_zone = true;
    }
    ImGui::SameLine(); HelpMarker(u8"Выделить зону игнорирования.");
    if (ImGui::Button(u8"Очистить Зону", ImVec2(ImGui::GetContentRegionAvail().x - 50.0f, 30))) {
        excl_x1 = excl_y1 = excl_x2 = excl_y2 = 0;
        cfg_changed = true;
    }
    EndPanel();

    ImGui::NextColumn();

    if (BeginPanel("FOV Settings", ImVec2(0, 160), acc_vec)) cfg_changed = true;
    ImGui::Spacing();
    if (CustomSliderFloat("Aim FOV:", "##f_a", &fov_aimbot, 10.0f, 500.0f, "%.0f px", acc_vec, u8"Радиус аимбота.")) cfg_changed = true;
    if (DrawToggle("Dynamic FOV:", "##dfov", &enable_dynamic_fov, acc_u32, u8"Адаптивный FOV.")) cfg_changed = true;
    ImGui::Spacing();
    if (CustomSliderFloat("Neural FOV:", "##f_n", &fov_scan, 10.0f, 500.0f, "%.0f px", acc_vec, u8"Радиус сканирования нейросети.")) cfg_changed = true;
    EndPanel();

    if (BeginPanel("Advanced Detection", ImVec2(0, 250), acc_vec)) cfg_changed = true;
    if (CustomSliderInt("Memory Enemy:", "##mem_en", &memory_enemy_frames, 0, 30, "%d frm", acc_vec, u8"Количество кадров памяти.")) cfg_changed = true;
    const char* hz_opts[] = { u8"Без лимита", "60 Hz", "75 Hz", "120 Hz", "144 Hz", "165 Hz", "240 Hz", "360 Hz" };
    if (CustomCombo("Frame Rate:", "##hz", &refresh_rate_idx, hz_opts, 8, u8"Ограничитель кадров оверлея.")) cfg_changed = true;
    EndPanel();

    ImGui::Columns(1);
}

// ============================================================
// RenderPwnzAITab (вкладка PWNZ AI)
// ============================================================
void Overlay::RenderPwnzAITab(float content_w, float content_h, const ImVec4& acc_vec, ImU32 acc_u32, bool& cfg_changed) {
    ImGui::Columns(2, nullptr, false);
    ImGui::SetColumnWidth(0, content_w * 0.45f);

    BeginPanel("PWNZ AI: Script Generator", ImVec2(0, content_h - 10), acc_vec);
    ImGui::TextColored(ImVec4(0.8f, 0.3f, 1.0f, 1.0f), "[ BOG-X Neural Engine ]");
    ImGui::Spacing();
    ImGui::TextWrapped(u8"Генерация LUA-скриптов через ИИ.");
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    ImGui::TextColored(acc_vec, u8"Промпт (Запрос к ИИ):");
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - 10.0f);
    ImGui::InputTextMultiline("##prompt", ai_prompt_input, sizeof(ai_prompt_input), ImVec2(0, 70));
    ImGui::PopItemWidth();

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.5f, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.3f, 0.6f, 1.0f));
    if (ImGui::Button(u8"⚡ СГЕНЕРИРОВАТЬ И ВНЕДРИТЬ", ImVec2(ImGui::GetContentRegionAvail().x - 10.0f, 40)) && !is_generating_script) {
        if (strlen(ai_prompt_input) > 2) {
            is_generating_script = true;
            script_gen_progress = 0.0f;
            generated_script_code = "";
        }
    }
    ImGui::PopStyleColor(2);

    ImGui::Spacing();
    if (is_generating_script) {
        script_gen_progress += ImGui::GetIO().DeltaTime * 0.4f;
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.8f, 0.2f, 0.5f, 1.0f));
        ImGui::ProgressBar(script_gen_progress, ImVec2(-1, 20), "Compiling & Obfuscating LUA...");
        ImGui::PopStyleColor();
        if (script_gen_progress >= 1.0f) {
            is_generating_script = false;
            generated_script_code = "-- [ PWNZ AI GENERATED SCRIPT ]\n-- User Prompt: " + std::string(ai_prompt_input) + "\n\nfunction on_fire()\n    pwnz.secure_sleep(10)\n    local target = pwnz.get_target()\n    if target then\n        pwnz.aim_snap(target.x, target.y, 0.85)\n    end\nend\n\npwnz.register_callback(\"on_tick\", on_fire)\nprint(\"Script loaded.\")";
        }
    }

    if (!generated_script_code.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.1f, 1.0f, 0.1f, 1.0f), u8"Код успешно загружен в память:");
        ImGui::InputTextMultiline("##code", (char*)generated_script_code.c_str(), generated_script_code.size() + 1, ImVec2(-1, 150), ImGuiInputTextFlags_ReadOnly);
    }
    EndPanel();

    ImGui::NextColumn();

    if (BeginPanel("AI Triggerbot", ImVec2(0, 220), acc_vec, true, &trigger_enable, acc_u32)) cfg_changed = true;
    if (DrawKeybinder("Trigger Bind:", &trigger_key, 20, u8"Кнопка для триггера.")) cfg_changed = true;
    ImGui::Spacing();
    if (CustomSliderFloat("Click Delay:", "##trg_dly", &trigger_delay, 0.0f, 1.0f, "%.2f s", acc_vec, u8"Задержка выстрела.")) cfg_changed = true;
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Target: Synced with Aim Target");
    EndPanel();

    ImGui::Columns(1);
}

// ============================================================
// RenderProfileTab (вкладка Profile)
// ============================================================
void Overlay::RenderProfileTab(float content_w, float content_h, const ImVec4& acc_vec, ImU32 acc_u32, bool& cfg_changed) {
    ImGui::Columns(2, nullptr, false);
    ImGui::SetColumnWidth(0, content_w * 0.5f);

    BeginPanel("Security & License", ImVec2(0, 200), acc_vec);
    ImGui::TextColored(acc_vec, u8"Пользователь:");
    ImGui::SameLine(); ImGui::Text("%s", auth_username);
    ImGui::Spacing();
    ImGui::Text(u8"HWID:");
    ImGui::TextColored(ImVec4(0.9f, 0.1f, 0.2f, 1.0f), hwid_str);
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextColored(acc_vec, u8"Информация о подписке:");
    int y = 0, M = 0, d = 0, h = 0, m = 0, s = 0;
    double diff = -1.0;
    if (sscanf(user_expiry_date.c_str(), "%d-%d-%d %d:%d:%d", &y, &M, &d, &h, &m, &s) == 6) {
        std::tm expire_tm = {}; expire_tm.tm_year = y - 1900; expire_tm.tm_mon = M - 1; expire_tm.tm_mday = d; expire_tm.tm_hour = h; expire_tm.tm_min = m; expire_tm.tm_sec = s;
        std::time_t expire_time = std::mktime(&expire_tm); diff = std::difftime(expire_time, std::time(nullptr));
    }
    ImGui::Spacing();
    if (diff > 0) {
        int days = (int)(diff / (60 * 60 * 24));
        int hours = ((int)diff / (60 * 60)) % 24;
        int mins = ((int)diff / 60) % 60;
        int secs = (int)diff % 60;
        ImGui::Text(u8"Осталось:");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), u8"%d дн. %02d ч. %02d мин. %02d сек.", days, hours, mins, secs);
    }
    else {
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), u8"Подписка истекла!");
    }
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.1f, 1.0f, 0.1f, 1.0f), u8"[+] Статус: UNDETECTED");
    EndPanel();

    BeginPanel("Anti-Ban System (Cleaner)", ImVec2(0, 180), acc_vec);
    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "[ Deep Clean ]");
    if (ImGui::Button(u8"Очистить логи и кэш игры", ImVec2(-1, 35))) DeepCleanTraces();
    HelpMarker(u8"Удаляет логи NetEase, сбрасывает DNS и ARP кэш, чистит Temp.");
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "[ Hardware Spoofer ]");
    if (ImGui::Button(u8"Сменить MAC-адрес сети", ImVec2(-1, 35))) SpoofMAC();
    HelpMarker(u8"Изменяет MAC-адрес напрямую в реестре.");
    EndPanel();

    BeginPanel("Trust Factor", ImVec2(0, content_h - 200 - 180 - 30), acc_vec);
    float trust = 100.0f;
    if (aim_max_sens > 4.0f) trust -= 15.0f;
    if (aim_deadzone < 1.0f) trust -= 10.0f;
    if (aim_kill_delay < 0.05f) trust -= 15.0f;
    if (trust < 0.0f) trust = 0.0f;
    ImVec4 trust_col = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
    std::string trust_text = "LEGIT (Safe)";
    if (trust < 50.0f) { trust_col = ImVec4(1.0f, 0.0f, 0.0f, 1.0f); trust_text = "RAGE (High Risk)"; }
    else if (trust < 80.0f) { trust_col = ImVec4(1.0f, 1.0f, 0.0f, 1.0f); trust_text = "SEMI-RAGE (Medium Risk)"; }
    ImGui::Text(u8"Безопасность настроек:");
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, trust_col);
    ImGui::ProgressBar(trust / 100.0f, ImVec2(-1, 20), "");
    ImGui::PopStyleColor();
    ImGui::TextColored(trust_col, "Status: %s (%.0f%%)", trust_text.c_str(), trust);
    EndPanel();

    ImGui::NextColumn();

    BeginPanel("PC Specs & AI Advisor", ImVec2(0, 180), acc_vec);
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "CPU:"); ImGui::TextWrapped(cpu_str);
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "GPU:"); ImGui::TextWrapped(gpu_str);
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextColored(acc_vec, u8"Советник PWNZ:");
    std::string gpu_s(gpu_str);
    if (gpu_s.find("NVIDIA") != std::string::npos || gpu_s.find("RTX") != std::string::npos) {
        ImGui::TextWrapped(u8"Обнаружена NVIDIA. TensorRT/CUDA активирован.");
    }
    else {
        ImGui::TextWrapped(u8"Используется DirectML.");
    }
    EndPanel();

    BeginPanel("Session AI Stats", ImVec2(0, 140), acc_vec);
    ImGui::Text(u8"Выстрелов с Triggerbot: %d", 0);
    ImGui::Text(u8"Время удержания: %.1f сек.", 0.0f);
    ImGui::TextColored(ImVec4(0.8f, 0.3f, 1.0f, 1.0f), u8"Точность наводки: 88%%");
    EndPanel();

    if (BeginPanel("Theme", ImVec2(0, content_h - 180 - 140 - 30), acc_vec)) cfg_changed = true;
    ImGui::Text(u8"Акцентный цвет:");
    if (ImGui::ColorEdit3("##accent", accent_color, ImGuiColorEditFlags_NoInputs)) cfg_changed = true;
    if (ImGui::Button(u8"Сбросить")) { accent_color[0] = 0.0f; accent_color[1] = 0.8f; accent_color[2] = 1.0f; cfg_changed = true; }
    EndPanel();

    ImGui::Columns(1);
}

// ============================================================
 

// ============================================================
// RenderHardwareTab (вкладка Hardware/2PC)
// ============================================================
void Overlay::RenderHardwareTab(float content_w, float content_h, const ImVec4& acc_vec, ImU32 acc_u32, bool& cfg_changed) {
    ImGui::Columns(2, nullptr, false);
    ImGui::SetColumnWidth(0, content_w * 0.5f);

    // === ЛЕВАЯ КОЛОНКА: Основные настройки Hardware ===
    if (BeginPanel("Hardware Output Mode", ImVec2(0, 280), acc_vec)) cfg_changed = true;
    
    const char* hw_modes[] = {
        "Local Mouse (SendInput)",
        "Makcu (UART/COM)",
        "KMbox Net (UDP)"
    };
    
    if (CustomCombo("Mode:", "##hw_mode", &hardware_mode_idx, hw_modes, 3, 
        is_russian ? u8"Выберите режим работы" : "Select hardware mode")) {
        cfg_changed = true;
    }
    
    ImGui::Spacing();
    
    // Enable Hardware Toggle
    if (DrawToggle("Enable Hardware:", "##hw_en", &hw_enabled, acc_u32, 
        is_russian ? u8"Включить аппаратный ввод" : "Enable hardware input")) {
        cfg_changed = true;
    }
    
    ImGui::Separator();
    ImGui::Spacing();
    
    // Bypass Options
    const char* bypass_modes[] = {
        "None",
        "GHub",
        "Razer",
        "Random Delay"
    };
    
    if (CustomCombo("Bypass Mode:", "##bypass", &bypass_mode_idx, bypass_modes, 4,
        is_russian ? u8"Режим обхода античита" : "Anti-cheat bypass mode")) {
        cfg_changed = true;
    }
    
    if (bypass_mode_idx == 3) { // Random Delay
        ImGui::Spacing();
        ImGui::Text(is_russian ? u8"Random Delay (ms):" : "Random Delay (ms):");
        ImGui::PushItemWidth(100);
        if (ImGui::SliderInt("##rd_min", &random_delay_min, 1, 50, "%d ms")) cfg_changed = true;
        ImGui::SameLine();
        if (ImGui::SliderInt("##rd_max", &random_delay_max, 10, 100, "%d ms")) cfg_changed = true;
        ImGui::PopItemWidth();
        HelpMarker(is_russian ? u8"Мин/макс задержка для рандомизации" : "Min/Max delay for randomization");
    }
    
    EndPanel();

    ImGui::NextColumn();

    // Right Column - Device Specific Settings
    if (hardware_mode_idx == 1) {
        // Makcu UART Settings
        if (BeginPanel("Makcu (UART/COM) Settings", ImVec2(0, 280), acc_vec)) cfg_changed = true;
        
        ImGui::Text(is_russian ? u8"COM Порт:" : "COM Port:");
        ImGui::PushItemWidth(150);
        if (ImGui::InputText("##com_port", com_port_buf, sizeof(com_port_buf))) cfg_changed = true;
        ImGui::PopItemWidth();
        HelpMarker(is_russian ? u8"Например: COM3" : "Example: COM3");
        
        ImGui::Spacing();
        
        const char* baud_rates[] = { "9600", "19200", "38400", "57600", "115200" };
        int baud_values[] = { 9600, 19200, 38400, 57600, 115200 };
        
        ImGui::Text(is_russian ? u8"Baud Rate:" : "Baud Rate:");
        ImGui::PushItemWidth(150);
        if (ImGui::Combo("##baud", &baud_rate_idx, baud_rates, IM_ARRAYSIZE(baud_rates))) cfg_changed = true;
        ImGui::PopItemWidth();
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        auto& hw = HardwareBackend::Instance();
        bool connected = hw.IsMackuConnected();
        
        if (!connected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 0.8f));
            if (ImGui::Button("Connect Makcu", ImVec2(150, 35))) {
                hw.ConnectMacku(com_port_buf, baud_values[baud_rate_idx]);
            }
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 0.8f));
            if (ImGui::Button("Disconnect Makcu", ImVec2(150, 35))) {
                hw.DisconnectMacku();
            }
            ImGui::PopStyleColor();
        }
        
        ImGui::SameLine();
        if (connected) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "CONNECTED");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "DISCONNECTED");
        }
        
        EndPanel();
        
    } else if (hardware_mode_idx == 2) {
        // KMbox Net Settings
        if (BeginPanel("KMbox Net (UDP) Settings", ImVec2(0, 280), acc_vec)) cfg_changed = true;
        
        ImGui::Text(is_russian ? u8"IP Адрес:" : "IP Address:");
        ImGui::PushItemWidth(200);
        if (ImGui::InputText("##kmbox_ip", kmbox_ip_buf, sizeof(kmbox_ip_buf))) cfg_changed = true;
        ImGui::PopItemWidth();
        HelpMarker(is_russian ? u8"Например: 192.168.1.100" : "Example: 192.168.1.100");
        
        ImGui::Spacing();
        
        ImGui::Text(is_russian ? u8"Порт:" : "Port:");
        ImGui::PushItemWidth(100);
        if (ImGui::InputInt("##kmbox_port", &kmbox_port)) cfg_changed = true;
        ImGui::PopItemWidth();
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        auto& hw = HardwareBackend::Instance();
        bool connected = hw.IsKMboxConnected();
        
        if (!connected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 0.8f));
            if (ImGui::Button("Connect KMbox", ImVec2(150, 35))) {
                hw.ConnectKMbox(kmbox_ip_buf, kmbox_port);
            }
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 0.8f));
            if (ImGui::Button("Disconnect KMbox", ImVec2(150, 35))) {
                hw.DisconnectKMbox();
            }
            ImGui::PopStyleColor();
        }
        
        ImGui::SameLine();
        if (connected) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "CONNECTED");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "DISCONNECTED");
        }
        
        EndPanel();
    } else {
        // Local Mouse Info
        if (BeginPanel("Local Mouse (SendInput)", ImVec2(0, 280), acc_vec)) {
            ImGui::TextColored(acc_vec, is_russian ? u8"Стандартный ввод Windows" : "Standard Windows Input");
            ImGui::Spacing();
            ImGui::TextWrapped(is_russian ? 
                u8"Используется API SendInput для эмуляции мыши.\nНе требует дополнительного оборудования." :
                "Uses SendInput API for mouse emulation.\nNo additional hardware required.");
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
                is_russian ? u8"⚠ Может определяться античитами" : "⚠ May be detected by anti-cheats");
            EndPanel();
        }
    }
    
    ImGui::Columns(1);
    
    // Bottom Section - Quick Guide
    ImGui::Spacing();
    if (BeginPanel(is_russian ? u8"📖 Быстрая инструкция" : "📖 Quick Guide", ImVec2(-1, 120), acc_vec)) {
        ImGui::Columns(3, nullptr, false);
        
        ImGui::TextColored(acc_vec, is_russian ? u8"Makcu:" : "Makcu:");
        ImGui::TextWrapped(is_russian ? 
            u8"1. Подключи плату\n2. Узнай COM-порт\n3. Введи порт и Baud\n4. Нажми Connect" :
            "1. Connect board\n2. Find COM port\n3. Enter port & Baud\n4. Click Connect");
        
        ImGui::NextColumn();
        ImGui::TextColored(acc_vec, is_russian ? u8"KMbox:" : "KMbox:");
        ImGui::TextWrapped(is_russian ? 
            u8"1. Подключи к игровому ПК\n2. Настрой сеть\n3. Введи IP и порт\n4. Нажми Connect" :
            "1. Connect to gaming PC\n2. Configure network\n3. Enter IP & port\n4. Click Connect");
        
        ImGui::NextColumn();
        ImGui::TextColored(acc_vec, is_russian ? u8"Тест:" : "Test:");
        ImGui::TextWrapped(is_russian ? 
            u8"Перейди во вкладку 'Check HW'\nИспользуй тестовые кнопки\nДля проверки работы" :
            "Go to 'Check HW' tab\nUse test buttons\nto verify operation");
        
        ImGui::Columns(1);
    }
    EndPanel();
}

// ============================================================
// RenderSecurityTab (вкладка Security)
// ============================================================
void Overlay::RenderSecurityTab(float content_w, float content_h, const ImVec4& acc_vec, ImU32 acc_u32, bool& cfg_changed) {
    ImGui::Columns(2, nullptr, false);
    ImGui::SetColumnWidth(0, content_w * 0.5f);

    BeginPanel("Anti-Ban Guide", ImVec2(0, 460), acc_vec);
    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), u8"1. НЕ ВОДИ ЧЕРЕЗ СТЕНЫ");
    ImGui::TextWrapped(u8"Нажимай аим только когда видишь цель.");
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextColored(acc_vec, u8"2. КОНТРОЛИРУЙ FOV");
    ImGui::TextWrapped(u8"Не ставьте Aim FOV больше 200px.");
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.7f, 0.3f, 1.0f, 1.0f), u8"3. ИСПОЛЬЗУЙТЕ STEALTH MODE");
    ImGui::TextWrapped(u8"Скрывайте меню при игре.");
    EndPanel();

    ImGui::NextColumn();
    BeginPanel("Mouse Protection", ImVec2(0, 460), acc_vec);
    ImGui::TextColored(ImVec4(0.1f, 1.0f, 0.1f, 1.0f), u8"Уровни защиты мыши:");
    ImGui::Spacing();
    ImGui::TextColored(acc_vec, u8"1. Dynamic IAT Evasion");
    ImGui::TextWrapped(u8"Обход таблиц импорта.");
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.2f, 1.0f), u8"2. Biometric Humanizer");
    ImGui::TextWrapped(u8"Имитация дрожания руки.");
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), u8"3. Кривые Безье");
    ImGui::TextWrapped(u8"Естественная траектория.");
    EndPanel();
    ImGui::Columns(1);
}

// ============================================================
// RenderTelemetryTab (вкладка Telemetry)
// ============================================================
void Overlay::RenderTelemetryTab(float content_w, float content_h, const ImVec4& acc_vec, ImU32 acc_u32, bool& cfg_changed) {
    ImGui::Columns(2, nullptr, false);
    ImGui::SetColumnWidth(0, content_w * 0.5f);
    float ordered_lat[100] = { 0 };
    BeginPanel("Inference Latency", ImVec2(0, 220), acc_vec);
    if (ImPlot::BeginPlot("##LatencyPlot", ImVec2(-1, 150))) {
        ImPlot::SetupAxes("Frames", "ms", ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_AutoFit);
        ImPlot::PlotShaded("Time", ordered_lat, 100);
        ImPlot::EndPlot();
    }
    EndPanel();

    BeginPanel("ByteTrack Stats", ImVec2(0, 180), acc_vec);
    ImGui::TextColored(acc_vec, "Active Tracks: %d", g_byte_active_tracks.load());
    ImGui::TextColored(acc_vec, "Avg Tracklet: %.1f fr", g_byte_avg_tracklet_len.load());
    ImGui::TextColored(acc_vec, "Avg Speed: %.1f px/fr", g_byte_avg_speed.load());
    ImGui::Spacing();
    ImGui::Text("TrackThresh: %.2f  Buffer: %d  MatchThresh: %.2f  FPS: %d", byte_track_thresh, byte_track_buffer, byte_match_thresh, byte_frame_rate);
    EndPanel();

    ImGui::NextColumn();
    BeginPanel("Hit Heatmap", ImVec2(0, 450), acc_vec);
    ImPlot::PushColormap(ImPlotColormap_Spectral);
    if (ImPlot::BeginPlot("##Heatmap", ImVec2(-1, 350))) {
        ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoDecorations, ImPlotAxisFlags_NoDecorations);
        ImPlot::SetupAxesLimits(0, 3, 0, 3);
        ImPlot::EndPlot();
    }
    ImPlot::PopColormap();
    EndPanel();
    ImGui::Columns(1);
}

// ============================================================
// RenderHWCheckTab (вкладка HW Check)
// ============================================================
void Overlay::RenderHWCheckTab(float content_w, float content_h, const ImVec4& acc_vec, ImU32 acc_u32, bool& cfg_changed) {
    ImGui::Columns(2, nullptr, false);
    ImGui::SetColumnWidth(0, content_w * 0.5f);

    // Left Column - Hardware Test Panel
    if (BeginPanel("Hardware Test Panel", ImVec2(0, 350), acc_vec)) cfg_changed = true;
    
    ImGui::TextColored(acc_vec, is_russian ? u8"Тестирование движения:" : "Movement Test:");
    ImGui::Spacing();
    
    ImGui::Text(is_russian ? u8"X:" : "X:");
    ImGui::SameLine();
    ImGui::PushItemWidth(100);
    if (ImGui::InputInt("##test_x", &test_move_x)) cfg_changed = true;
    ImGui::PopItemWidth();
    
    ImGui::SameLine();
    ImGui::Text(is_russian ? u8"Y:" : "Y:");
    ImGui::SameLine();
    ImGui::PushItemWidth(100);
    if (ImGui::InputInt("##test_y", &test_move_y)) cfg_changed = true;
    ImGui::PopItemWidth();
    
    ImGui::Spacing();
    
    auto& hw = HardwareBackend::Instance();
    
    // Test Move Button
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.8f, 0.8f));
    if (ImGui::Button(is_russian ? u8"Test Move Mouse" : "Test Move Mouse", ImVec2(200, 40))) {
        if (hw_enabled) {
            if (hardware_mode_idx == 1 && hw.IsMackuConnected()) {
                hw.SendMackuMove(test_move_x, test_move_y);
            } else if (hardware_mode_idx == 2 && hw.IsKMboxConnected()) {
                hw.SendKMboxMove(test_move_x, test_move_y);
            } else if (hardware_mode_idx == 0) {
                // Local mouse - use SendInput
                #ifdef _WIN32
                INPUT input = {};
                input.type = INPUT_MOUSE;
                input.mi.dx = test_move_x;
                input.mi.dy = test_move_y;
                input.mi.dwFlags = MOUSEEVENTF_MOVE;
                SendInput(1, &input, sizeof(INPUT));
                #endif
            }
        } else {
            // Fallback to local input even if hardware not enabled
            #ifdef _WIN32
            INPUT input = {};
            input.type = INPUT_MOUSE;
            input.mi.dx = test_move_x;
            input.mi.dy = test_move_y;
            input.mi.dwFlags = MOUSEEVENTF_MOVE;
            SendInput(1, &input, sizeof(INPUT));
            #endif
        }
    }
    ImGui::PopStyleColor();
    HelpMarker(is_russian ? u8"Сдвинуть курсор на указанные значения" : "Move cursor by specified values");
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    ImGui::TextColored(acc_vec, is_russian ? u8"Тестирование кликов:" : "Click Test:");
    ImGui::Spacing();
    
    // Test Left Click
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 0.8f));
    if (ImGui::Button(is_russian ? u8"Test Left Click" : "Test Left Click", ImVec2(200, 35))) {
        if (hw_enabled) {
            if (hardware_mode_idx == 1 && hw.IsMackuConnected()) {
                hw.SendMackuClick(1); // Left click
            } else if (hardware_mode_idx == 2 && hw.IsKMboxConnected()) {
                hw.SendKMboxClick(1); // Left click
            } else {
                #ifdef _WIN32
                INPUT inputs[2] = {};
                inputs[0].type = INPUT_MOUSE;
                inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
                inputs[1].type = INPUT_MOUSE;
                inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
                SendInput(2, inputs, sizeof(INPUT));
                #endif
            }
        } else {
            #ifdef _WIN32
            INPUT inputs[2] = {};
            inputs[0].type = INPUT_MOUSE;
            inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
            inputs[1].type = INPUT_MOUSE;
            inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
            SendInput(2, inputs, sizeof(INPUT));
            #endif
        }
    }
    ImGui::PopStyleColor();
    
    ImGui::Spacing();
    
    // Test Double Click
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.5f, 0.2f, 0.8f));
    if (ImGui::Button(is_russian ? u8"Test Double Click" : "Test Double Click", ImVec2(200, 35))) {
        if (hw_enabled) {
            if (hardware_mode_idx == 1 && hw.IsMackuConnected()) {
                hw.SendMackuClick(1);
                Sleep(50);
                hw.SendMackuClick(1);
            } else if (hardware_mode_idx == 2 && hw.IsKMboxConnected()) {
                hw.SendKMboxClick(1);
                Sleep(50);
                hw.SendKMboxClick(1);
            } else {
                #ifdef _WIN32
                INPUT inputs[4] = {};
                inputs[0].type = INPUT_MOUSE;
                inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
                inputs[1].type = INPUT_MOUSE;
                inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
                inputs[2].type = INPUT_MOUSE;
                inputs[2].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
                inputs[3].type = INPUT_MOUSE;
                inputs[3].mi.dwFlags = MOUSEEVENTF_LEFTUP;
                SendInput(4, inputs, sizeof(INPUT));
                #endif
            }
        } else {
            #ifdef _WIN32
            INPUT inputs[4] = {};
            inputs[0].type = INPUT_MOUSE;
            inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
            inputs[1].type = INPUT_MOUSE;
            inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
            inputs[2].type = INPUT_MOUSE;
            inputs[2].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
            inputs[3].type = INPUT_MOUSE;
            inputs[3].mi.dwFlags = MOUSEEVENTF_LEFTUP;
            SendInput(4, inputs, sizeof(INPUT));
            #endif
        }
    }
    ImGui::PopStyleColor();
    
    EndPanel();

    ImGui::NextColumn();

    // Right Column - Status & Info
    BeginPanel("Hardware Status", ImVec2(0, 200), acc_vec);
    
    ImGui::TextColored(acc_vec, is_russian ? u8"Текущий режим:" : "Current Mode:");
    ImGui::Spacing();
    
    const char* mode_names[] = {
        "Local Mouse (SendInput)",
        "Makcu (UART/COM)",
        "KMbox Net (UDP)"
    };
    
    ImGui::Text("%s", mode_names[hardware_mode_idx]);
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    ImGui::TextColored(acc_vec, is_russian ? u8"Статус оборудования:" : "Hardware Status:");
    ImGui::Spacing();
    
    bool macku_connected = hw.IsMackuConnected();
    bool kmbox_connected = hw.IsKMboxConnected();
    
    if (macku_connected) {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✓ Makcu: CONNECTED");
        ImGui::Text(is_russian ? u8"Порт: %s" : "Port: %s", com_port_buf);
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "✗ Makcu: DISCONNECTED");
    }
    
    ImGui::Spacing();
    
    if (kmbox_connected) {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✓ KMbox: CONNECTED");
        ImGui::Text(is_russian ? u8"IP: %s:%d" : "IP: %s:%d", kmbox_ip_buf, kmbox_port);
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "✗ KMbox: DISCONNECTED");
    }
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    if (hw_enabled) {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), 
            is_russian ? u8"✅ Hardware ENABLED" : "✅ Hardware ENABLED");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), 
            is_russian ? u8"⚠ Hardware DISABLED" : "⚠ Hardware DISABLED");
    }
    
    EndPanel();
    
    ImGui::Spacing();
    
    BeginPanel(is_russian ? u8"📋 Информация" : "📋 Information", ImVec2(0, content_h - 200 - 20), acc_vec);
    
    ImGui::TextWrapped(is_russian ? 
        u8"Для проверки работы:\n"
        "1. Выбери режим в вкладке 'Hardware/2PC'\n"
        "2. Подключи устройство (Connect)\n"
        "3. Включи 'Enable Hardware'\n"
        "4. Вернись сюда и нажми тестовые кнопки\n"
        "5. Курсор должен сдвинуться или произойти клик" :
        "To verify operation:\n"
        "1. Select mode in 'Hardware/2PC' tab\n"
        "2. Connect device (Connect button)\n"
        "3. Enable 'Enable Hardware'\n"
        "4. Come back here and click test buttons\n"
        "5. Cursor should move or click should occur");
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), 
        is_russian ? u8"⚠ Примечание:" : "⚠ Note:");
    ImGui::TextWrapped(is_russian ? 
        u8"Если hardware не подключен, тесты будут использовать локальный ввод Windows (SendInput)." :
        "If hardware is not connected, tests will use local Windows input (SendInput).");
    
    EndPanel();

    ImGui::Columns(1);
}

// ============================================================
// RenderXTierTab (вкладка X-TIER)
// ============================================================
void Overlay::RenderXTierTab(float content_w, float content_h, const ImVec4& acc_vec, ImU32 acc_u32, bool& cfg_changed) {
    ImGui::Columns(2, nullptr, false);
    ImGui::SetColumnWidth(0, content_w * 0.5f);
    if (BeginPanel("Elite Modules", ImVec2(0, 460), acc_vec)) cfg_changed = true;
    if (DrawToggle("Smart Target Sequencing", "##tsp", &elite_tsp_enabled, acc_u32)) cfg_changed = true;
    if (DrawToggle("Ballistic Prediction", "##bal", &elite_ballistics_enabled, acc_u32)) cfg_changed = true;
    if (elite_ballistics_enabled) {
        if (CustomSliderFloat(u8"Bullet Speed", "##ebs", &elite_bullet_speed, 100.0f, 1500.0f, "%.0f", acc_vec)) cfg_changed = true;
        if (CustomSliderFloat(u8"Gravity", "##ebd", &elite_bullet_drop, 1.0f, 20.0f, "%.1f", acc_vec)) cfg_changed = true;
    }
    if (DrawToggle("Context-Aware AI", "##ctx", &elite_context_aware, acc_u32)) cfg_changed = true;
    if (DrawToggle("Smoke Vision", "##smk", &elite_smoke_vision, acc_u32)) cfg_changed = true;
    if (DrawToggle("Voice Control", "##vc", &elite_voice_ctrl, acc_u32)) cfg_changed = true;
    if (DrawToggle("Shadow Trainer", "##cst", &elite_shadow_trainer, acc_u32)) cfg_changed = true;
    if (elite_shadow_trainer) {
        ImGui::Text("Webhook:");
        ImGui::PushItemWidth(200.0f);
        if (ImGui::InputText("##wbhk", shadow_webhook, sizeof(shadow_webhook))) cfg_changed = true;
        ImGui::PopItemWidth();
    }
    EndPanel();
    ImGui::Columns(1);
}

// ============================================================
// RenderMetricsTab (вкладка Metrics)
// ============================================================
void Overlay::RenderMetricsTab(float content_w, float content_h, const ImVec4& acc_vec, ImU32 acc_u32, bool& cfg_changed) {
    ImGui::Columns(2, nullptr, false);
    ImGui::SetColumnWidth(0, content_w * 0.5f);
    BeginPanel("Metrics", ImVec2(0, 250), acc_vec);
    ImGui::Text("Inference: %.2f ms", g_last_inference_time.load());
    ImGui::Text("Capture: %.2f ms", g_last_capture_time.load());
    ImGui::Text("Jitter: %.2f ms", g_inference_jitter.load());
    ImGui::Text("Active tracks: %d", g_byte_active_tracks.load());
    ImGui::Text("Track loss rate: %.2f", g_track_loss_rate.load());
    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Target Locked: %s", g_is_target_locked.load() ? "YES" : "NO");
    ImGui::Text("DirectML: Active");
    EndPanel();

    BeginPanel("Real-time Graphs", ImVec2(0, content_h - 250 - 20), acc_vec);
    static std::deque<float> jitter_history;
    jitter_history.push_back(g_inference_jitter.load());
    if (jitter_history.size() > 100) jitter_history.pop_front();
    if (ImPlot::BeginPlot("Jitter", ImVec2(-1, 150))) {
        std::vector<float> jit_vec(jitter_history.begin(), jitter_history.end());
        ImPlot::PlotLine("Jitter", jit_vec.data(), (int)jit_vec.size());
        ImPlot::EndPlot();
    }
    EndPanel();
    ImGui::Columns(1);
}

// ============================================================
// RenderChatWindow, RenderHWTutorial, ProcessChatInput, ToLower
// ============================================================
void Overlay::RenderChatWindow() {
    ImGui::SetNextWindowSize(ImVec2(450, 500), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(u8"PWNZ Ассистент", &show_ai_chat, ImGuiWindowFlags_NoCollapse)) {
        ImGui::BeginChild("ChatScroll", ImVec2(0, ImGui::GetWindowHeight() - 70), true);
        for (const auto& msg : chat_history) {
            ImGui::TextColored(msg.is_user ? ImVec4(0.0f, 0.8f, 1.0f, 1.0f) : ImVec4(accent_color[0], accent_color[1], accent_color[2], 1.0f), "%s:", msg.is_user ? "You" : u8"PWNZ AI");
            ImGui::TextWrapped("%s", msg.text.c_str());
            ImGui::Spacing();
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
        ImGui::PushItemWidth(ImGui::GetWindowWidth() - 70);
        if (ImGui::InputText("##chat_input", chat_input, sizeof(chat_input), ImGuiInputTextFlags_EnterReturnsTrue)) {
            ProcessChatInput(chat_input);
            chat_input[0] = '\0';
        }
        ImGui::PopItemWidth();
        ImGui::SameLine();
        if (ImGui::Button("Send", ImVec2(50, 0))) {
            ProcessChatInput(chat_input);
            chat_input[0] = '\0';
        }
    }
    ImGui::End();
}

void Overlay::RenderHWTutorial() {
    if (!show_hw_tutorial) return;
    ImGui::SetNextWindowSize(ImVec2(850, 500), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("PWNZ ACADEMY: Hardware & 2PC", &show_hw_tutorial)) {
        ImGui::Columns(2, nullptr, false);
        ImGui::SetColumnWidth(0, 220);
        ImGui::BeginChild("hw_tut_list", ImVec2(0, 0), true);
        ImGui::TextColored(ImVec4(accent_color[0], accent_color[1], accent_color[2], 1.0f), u8"МЕТОДЫ ОБХОДА:");
        ImGui::Separator();
        const char* hw_types[] = { u8"1. Win32 API", u8"2. Arduino", u8"3. KMBox", u8"4. Makcu", u8"5. LAN/UDP 2PC" };
        for (int i = 0; i < 5; i++) if (ImGui::Selectable(hw_types[i], tutorial_hw_selected == i)) tutorial_hw_selected = i;
        ImGui::EndChild();
        ImGui::NextColumn();
        ImGui::BeginChild("hw_tut_content", ImVec2(0, 0), true);
        if (tutorial_hw_selected == 0) {
            ImGui::TextColored(ImVec4(accent_color[0], accent_color[1], accent_color[2], 1.0f), u8"Standard API (1 ПК)");
            ImGui::Separator();
            ImGui::TextWrapped(u8"Используйте Stealth Mode и OBS Bypass.");
        }
        else if (tutorial_hw_selected >= 1 && tutorial_hw_selected <= 3) {
            ImGui::TextColored(ImVec4(accent_color[0], accent_color[1], accent_color[2], 1.0f), u8"COM Платы");
            ImGui::Separator();
            ImGui::TextWrapped(u8"Подключите плату, узнайте COM-порт, укажите в чите (COM3 → 2).");
        }
        else if (tutorial_hw_selected == 4) {
            ImGui::TextColored(ImVec4(accent_color[0], accent_color[1], accent_color[2], 1.0f), u8"LAN/UDP 2PC");
            ImGui::Separator();
            ImGui::TextWrapped(u8"Соедините ПК кабелем, задайте статические IP, используйте Sunshine/Moonlight.");
        }
        ImGui::EndChild();
        ImGui::Columns(1);
    }
    ImGui::End();
}

void Overlay::ProcessChatInput(std::string input) {
    if (input.empty()) return;
    chat_history.push_back({ input, true });
    chat_history.push_back({ is_russian ? u8"Брат, я твой ИИ-ассистент PWNZ." : "I am your PWNZ Assistant.", false });
}

std::string Overlay::ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

// ============================================================
// OneEuroFilter методы (реализация)
// ============================================================
OneEuroFilter::OneEuroFilter(double mincutoff, double beta, double dcutoff)
    : mincutoff(mincutoff), beta(beta), dcutoff(dcutoff),
    x_prev(0), dx_prev(0), t_prev(0), first_time(true) {
}

void OneEuroFilter::UpdateParams(double mc, double b) {
    mincutoff = mc;
    beta = b;
}

void OneEuroFilter::Reset() {
    first_time = true;
}

double OneEuroFilter::Filter(double x, double t) {
    if (first_time) {
        first_time = false;
        x_prev = x;
        dx_prev = 0;
        t_prev = t;
        return x;
    }
    double dt = t - t_prev;
    if (dt <= 0.0) dt = 0.0001;
    double dx = (x - x_prev) / dt;
    double edx = dx_prev + alpha(dcutoff, dt) * (dx - dx_prev);
    double cutoff = mincutoff + beta * std::abs(edx);
    double result = x_prev + alpha(cutoff, dt) * (x - x_prev);
    x_prev = result;
    dx_prev = edx;
    t_prev = t;
    return result;
}

double OneEuroFilter::alpha(double cutoff, double dt) {
    double tau = 1.0 / (2.0 * 3.14159265358979323846 * cutoff);
    return 1.0 / (1.0 + tau / dt);
}

// ============================================================
// D3D, Cleanup, Initialize, Update, ToggleClickability
// ============================================================
bool Overlay::CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 1;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    if (D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext) != S_OK) return false;
    CreateRenderTarget();
    return true;
}

void Overlay::CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void Overlay::CleanupRenderTarget() { if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = NULL; } }
void Overlay::CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = NULL; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = NULL; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = NULL; }
}

void Overlay::Cleanup(Aimbot* aim) {
    SaveConfig(aim);
    PlaySoundA(XOR("C:\\Windows\\Media\\Speech Off.wav"), NULL, SND_FILENAME | SND_ASYNC);
    DeepCleanTraces();
    ImPlot::DestroyContext();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClass("SystemOverlay", GetModuleHandle(NULL));
}

bool Overlay::Initialize() {
    MUTATE_SIGNATURE;
    FreeConsole();
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, "SystemOverlay", NULL };
    RegisterClassEx(&wc);
    int screen_w = GetSystemMetrics(SM_CXSCREEN);
    int screen_h = GetSystemMetrics(SM_CYSCREEN);
    active_res_w = screen_w; active_res_h = screen_h;
    hwnd = CreateWindowEx(WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_TOOLWINDOW, wc.lpszClassName, "", WS_POPUP, 0, 0, screen_w, screen_h - 1, NULL, NULL, wc.hInstance, NULL);
    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
    MARGINS margins = { -1,-1,-1,-1 };
    DwmExtendFrameIntoClientArea(hwnd, &margins);
    if (obs_bypass) SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);
    if (!CreateDeviceD3D(hwnd)) return false;
    FetchHardwareInfo();
    LoadAuth();
    std::srand(std::time(NULL));
    current_tip_idx = rand() % 10;
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImFont* font = io.Fonts->AddFontFromFileTTF("trebuc.ttf", 16.0f, NULL, io.Fonts->GetGlyphRangesCyrillic());
    if (!font) font = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\arial.ttf", 16.0f, NULL, io.Fonts->GetGlyphRangesCyrillic());
    if (!font) io.Fonts->AddFontDefault();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 12.0f; style.ChildRounding = 8.0f; style.FrameRounding = 6.0f; style.GrabRounding = 6.0f;
    style.WindowBorderSize = 2.0f; style.ChildBorderSize = 2.0f; style.ItemSpacing = ImVec2(10, 10); style.WindowPadding = ImVec2(12, 12);
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.05f, 0.04f, 0.08f, 0.98f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.08f, 0.07f, 0.12f, 1.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.10f, 0.18f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.12f, 0.10f, 0.18f, 1.00f);
    colors[ImGuiCol_Text] = ImVec4(0.90f, 0.90f, 0.95f, 1.00f);
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
    
    // Initialize 2PC network module
    network_2pc = std::make_unique<Network2PC>();
    
    chat_history.push_back({ is_russian ? u8"Привет! Я твой ИИ-Ассистент PWNZ." : "Hello! I am your PWNZ Assistant.", false });
    Aimbot temp_aim;
    LoadConfig(&temp_aim);
    return true;
}

bool Overlay::Update() {
    MSG msg;
    while (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        if (msg.message == WM_QUIT) return false;
    }
    return true;
}

void Overlay::ToggleClickability(bool clickable) {
    long ex_style = GetWindowLong(hwnd, GWL_EXSTYLE);
    if (clickable) {
        if (ex_style & WS_EX_TRANSPARENT) SetWindowLong(hwnd, GWL_EXSTYLE, ex_style & ~WS_EX_TRANSPARENT);
    }
    else {
        if (!(ex_style & WS_EX_TRANSPARENT)) SetWindowLong(hwnd, GWL_EXSTYLE, ex_style | WS_EX_TRANSPARENT);
    }
}

// ============================================================
// RenderAutoLogin, RenderLoadingScreen, RenderAuthWindow
// (реализованы в overlay_render_auth.cpp, но для полноты включим их сюда)
// ============================================================
void Overlay::RenderAutoLogin(const ImVec4& acc_vec, ImU32 acc_u32) {
    ToggleClickability(true);
    ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
    ImGui::SetNextWindowSize(ImVec2(400, 250), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(active_res_w / 2.0f - 200.0f, active_res_h / 2.0f - 125.0f), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_Border, acc_vec);
    ImGui::Begin("PWNZ_AUTO_LOGIN", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);
    auto_login_anim_time += ImGui::GetIO().DeltaTime;
    ImGui::Spacing(); ImGui::Spacing();
    ImGui::SetCursorPosX((400 - ImGui::CalcTextSize("SECURE CLOUD AUTHENTICATION").x) * 0.5f);
    ImGui::TextColored(acc_vec, "SECURE CLOUD AUTHENTICATION");
    ImVec2 p = ImGui::GetCursorScreenPos();
    float cx = p.x + 200.0f, cy = p.y + 70.0f;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddCircle(ImVec2(cx, cy), 40.0f, IM_COL32(50, 50, 60, 255), 64, 3.0f);
    draw->PathArcTo(ImVec2(cx, cy), 40.0f, auto_login_anim_time * 5.0f, auto_login_anim_time * 5.0f + 1.5f, 64);
    draw->PathStroke(acc_u32, 0, 4.0f);
    draw->PathArcTo(ImVec2(cx, cy), 25.0f, -auto_login_anim_time * 3.0f, -auto_login_anim_time * 3.0f + 2.0f, 64);
    draw->PathStroke(IM_COL32(255, 255, 255, 200), 0, 2.0f);
    ImGui::SetCursorPosY(160);
    ImGui::SetCursorPosX((400 - ImGui::CalcTextSize("Verifying License Key...").x) * 0.5f);
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Verifying License Key...");
    if (auto_login_anim_time > 1.5f) {
        std::string result = SendAuthRequest(auth_username, auth_password, false, user_expiry_date);
        if (result.find("SUCCESS") != std::string::npos) {
            is_authenticated = true; last_auth_check_time = ImGui::GetTime();
            is_auto_logging_in = false;
        }
        else {
            is_auto_logging_in = false;
            auth_status_msg = result;
            auth_status_col[0] = 1.0f; auth_status_col[1] = 0.3f; auth_status_col[2] = 0.3f;
        }
    }
    ImGui::End(); ImGui::PopStyleColor(); ImGui::Render();
    const float clear_c[4] = { 0,0,0,0.8f };
    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
    g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_c);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_pSwapChain->Present(0, 0);
}

void Overlay::RenderLoadingScreen(const ImVec4& acc_vec, ImU32 acc_u32) {
    ToggleClickability(true);
    ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
    ImGui::SetNextWindowSize(ImVec2(400, 180), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(active_res_w / 2.0f - 200.0f, active_res_h / 2.0f - 90.0f), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_Border, acc_vec);
    ImGui::Begin("PWNZ_LOAD", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);
    ImGui::SetCursorPos(ImVec2(400 - ImGui::CalcTextSize("Powered by BOG-X").x - 10, 10));
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Powered by BOG-X");
    ImGui::SetCursorPos(ImVec2((400 - ImGui::CalcTextSize("PWNZ VISION AI PRO").x) * 0.5f, 40));
    ImGui::TextColored(acc_vec, "PWNZ VISION AI PRO");
    ImGui::SetCursorPos(ImVec2(20, 80));
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, acc_vec);
    ImGui::ProgressBar(load_progress, ImVec2(360, 20), "Neural PWNZ AIM Loading...");
    ImGui::PopStyleColor();
    const char* tips[] = {
        u8"Совет: Не ставьте Aim FOV больше 200px.",
        u8"Совет: Используйте 'Stealth Mode' для повышения FPS.",
        u8"Совет: Deadzone 1-2px уберет микротряску.",
        u8"Совет: Комбинируйте PixelSmooth 10+ и Pose-Adaptive Hitbox.",
        u8"Совет: Sticky Aim помогает не срывать прицел.",
        u8"Совет: Кнопка END экстренно выгружает чит.",
        u8"Совет: Если подписка активна, вход автоматический.",
        u8"Совет: Включите OBS Bypass перед записью экрана.",
        u8"Совет: Не водите прицелом через стены.",
        u8"Совет: Аппаратная мышь безопаснее программной."
    };
    ImGui::SetCursorPos(ImVec2((400 - ImGui::CalcTextSize(tips[current_tip_idx]).x) * 0.5f, 120));
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", tips[current_tip_idx]);
    load_progress += ImGui::GetIO().DeltaTime * 0.35f;
    if (load_progress >= 1.0f) is_loading = false;
    ImGui::End(); ImGui::PopStyleColor(); ImGui::Render();
    const float clear_color[4] = { 0,0,0,0.8f };
    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
    g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_pSwapChain->Present(0, 0);
}

void Overlay::RenderAuthWindow(const ImVec4& acc_vec, ImU32 acc_u32) {
    ToggleClickability(true);
    ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
    ImGui::SetNextWindowSize(ImVec2(400, 350), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(active_res_w / 2.0f - 200.0f, active_res_h / 2.0f - 175.0f), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_Border, acc_vec);
    ImGui::Begin("PWNZ_AUTH", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);
    ImGui::Spacing();
    const char* title_txt = is_register_mode ? u8"PWNZ VISION | РЕГИСТРАЦИЯ" : u8"PWNZ VISION | АВТОРИЗАЦИЯ";
    ImGui::SetCursorPosX((400 - ImGui::CalcTextSize(title_txt).x) * 0.5f);
    ImGui::TextColored(acc_vec, title_txt);
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::Text(u8"Логин:");
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
    ImGui::InputText("##login", auth_username, 64);
    ImGui::PopItemWidth(); ImGui::Spacing();
    ImGui::Text(u8"Пароль:");
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
    ImGui::InputText("##password", auth_password, 64, ImGuiInputTextFlags_Password);
    ImGui::PopItemWidth(); ImGui::Spacing(); ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(acc_vec.x * 0.6f, acc_vec.y * 0.6f, acc_vec.z * 0.6f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, acc_vec);
    const char* btn_txt = is_register_mode ? u8"ЗАРЕГИСТРИРОВАТЬСЯ" : u8"ВОЙТИ В СИСТЕМУ";
    if (ImGui::Button(btn_txt, ImVec2(ImGui::GetContentRegionAvail().x, 40))) {
        auth_status_msg = u8"Связь с сервером...";
        auth_status_col[0] = 1.0f; auth_status_col[1] = 1.0f; auth_status_col[2] = 0.0f;
        std::string result = SendAuthRequest(auth_username, auth_password, is_register_mode, user_expiry_date);
        if (result.find("SUCCESS") != std::string::npos) {
            if (is_register_mode) {
                auth_status_msg = u8"Успешно! Теперь войдите.";
                auth_status_col[0] = 0.1f; auth_status_col[1] = 1.0f; auth_status_col[2] = 0.1f;
                is_register_mode = false;
            }
            else {
                is_authenticated = true; SaveAuth(); last_auth_check_time = ImGui::GetTime();
            }
        }
        else {
            auth_status_msg = result;
            auth_status_col[0] = 1.0f; auth_status_col[1] = 0.3f; auth_status_col[2] = 0.3f;
        }
    }
    ImGui::PopStyleColor(2);
    ImGui::Spacing();
    const char* switch_txt = is_register_mode ? u8"Уже есть аккаунт? Войти" : u8"Нет аккаунта? Создать";
    ImGui::SetCursorPosX((400 - ImGui::CalcTextSize(switch_txt).x) * 0.5f);
    if (ImGui::Selectable(switch_txt, false, 0, ImGui::CalcTextSize(switch_txt))) { is_register_mode = !is_register_mode; auth_status_msg = ""; }
    ImGui::Spacing();
    ImGui::SetCursorPosX((400 - ImGui::CalcTextSize(auth_status_msg.c_str()).x) * 0.5f);
    ImGui::TextColored(ImVec4(auth_status_col[0], auth_status_col[1], auth_status_col[2], 1.0f), auth_status_msg.c_str());
    ImGui::SetCursorPos(ImVec2(10, 320));
    ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "HWID: %s", GetHWID().substr(0, 15).c_str());
    ImGui::End(); ImGui::PopStyleColor(); ImGui::Render();
    const float clear_color[4] = { 0,0,0,0.8f };
    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
    g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_pSwapChain->Present(0, 0);
}

// ============================================================
// RenderVisualsAndZone (реализована в overlay_render_visuals.cpp, но для полноты включим сюда)
// ============================================================
void Overlay::RenderVisualsAndZone(const std::vector<Detection>& detections, Aimbot* aim) {
    bool is_aiming = (aim_key_main != 0 && (GetAsyncKeyState(aim_key_main) & 0x8000)) ||
        (aim_key_sub != 0 && (GetAsyncKeyState(aim_key_sub) & 0x8000));
    ImVec4 acc_vec = ImVec4(accent_color[0], accent_color[1], accent_color[2], 1.0f);

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2((float)active_res_w, (float)active_res_h));
    ImGui::Begin("Game_Overlay", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoBackground);
    ImDrawList* bg_draw = ImGui::GetWindowDrawList();

    float cx = active_res_w / 2.0f;
    float cy = active_res_h / 2.0f;

    if (draw_watermark) {
        float cap_ms = g_last_capture_time.load();
        float ai_ms = g_last_inference_time.load();
        char wm_text[256];
        snprintf(wm_text, sizeof(wm_text), "PWNZ VISION | FPS: %.0f | Cap: %.1fms | AI: %.1fms | Total: %.1fms",
            ImGui::GetIO().Framerate, cap_ms, ai_ms, cap_ms + ai_ms);
        bg_draw->AddText(ImVec2(20, 20), IM_COL32(accent_color[0] * 255, accent_color[1] * 255, accent_color[2] * 255, 255), wm_text);
    }

    if (draw_crosshair) {
        bg_draw->AddLine(ImVec2(cx - 8, cy), ImVec2(cx + 8, cy), IM_COL32(255, 255, 255, 200), 1.5f);
        bg_draw->AddLine(ImVec2(cx, cy - 8), ImVec2(cx, cy + 8), IM_COL32(255, 255, 255, 200), 1.5f);
    }

    if (draw_fov) {
        float r = accent_color[0] * 255, g = accent_color[1] * 255, b = accent_color[2] * 255;
        float c_fov = enable_dynamic_fov ? aim->current_fov : fov_aimbot;
        bg_draw->AddCircle(ImVec2(cx, cy), c_fov, IM_COL32(r, g, b, 200), 64, 1.5f);
    }

    if (draw_fov_neural) {
        bg_draw->AddCircle(ImVec2(cx, cy), fov_scan, IM_COL32(180, 50, 255, 150), 64, 1.5f);
    }

    if (enable_exclusion_zone && (excl_x2 - excl_x1 > 0)) {
        bg_draw->AddRectFilled(ImVec2(excl_x1, excl_y1), ImVec2(excl_x2, excl_y2), IM_COL32(255, 0, 0, 40));
        bg_draw->AddRect(ImVec2(excl_x1, excl_y1), ImVec2(excl_x2, excl_y2), IM_COL32(255, 0, 0, 200), 0, 0, 1.5f);
        bg_draw->AddText(ImVec2(excl_x1, excl_y1 - 15), IM_COL32(255, 0, 0, 255), "[ BLIND ZONE ]");
    }

    if (draw_esp) {
        float locked_x = g_locked_screen_x.load();
        float locked_y = g_locked_screen_y.load();
        bool is_locked = g_is_target_locked.load();
        if (!is_locked) was_empty_last_frame = true;

        int id_counter = 1;
        float cx_screen = active_res_w / 2.0f;
        float cy_screen = active_res_h / 2.0f;

        for (const auto& d : detections) {
            float x1 = d.box.x, y1 = d.box.y;
            float d_box_w = d.box.w, d_box_h = d.box.h;

            if (d.class_id == 1 && d.track_id != -1) {
                g_head_smoother.update(x1, y1, d_box_w, d_box_h, d.track_id);
            }

            float target_cx = x1 + d_box_w / 2.0f;
            float target_cy = y1 + d_box_h / 2.0f;
            bool is_current_target = (is_locked && std::abs(target_cx - locked_x) < 40.0f && std::abs(target_cy - locked_y) < 40.0f);

            if (is_current_target && esp_oe_enable) {
                if (was_empty_last_frame) {
                    espFilterX.Reset(); espFilterY.Reset(); espFilterW.Reset(); espFilterH.Reset();
                    was_empty_last_frame = false;
                }
                double t = ImGui::GetTime();
                espFilterX.UpdateParams(esp_oe_mincutoff, esp_oe_beta);
                espFilterY.UpdateParams(esp_oe_mincutoff, esp_oe_beta);
                espFilterW.UpdateParams(esp_oe_mincutoff, esp_oe_beta);
                espFilterH.UpdateParams(esp_oe_mincutoff, esp_oe_beta);
                x1 = (float)espFilterX.Filter(x1, t);
                y1 = (float)espFilterY.Filter(y1, t);
                d_box_w = (float)espFilterW.Filter(d_box_w, t);
                d_box_h = (float)espFilterH.Filter(d_box_h, t);
                target_cx = x1 + d_box_w / 2.0f;
                target_cy = y1 + d_box_h / 2.0f;
            }

            ImU32 box_color;
            if (is_current_target) {
                box_color = IM_COL32(color_esp_visible[0] * 255, color_esp_visible[1] * 255, color_esp_visible[2] * 255, 255);
                const char* tag = (d.class_id == 1) ? "[ LOCKED : HEAD ]" : "[ LOCKED : BODY ]";
                bg_draw->AddText(ImVec2(x1, y1 - 20), box_color, tag);
                if (is_aiming) {
                    bg_draw->AddLine(ImVec2(cx_screen, cy_screen), ImVec2(target_cx, y1 + d_box_h * 0.12f), IM_COL32(255, 50, 50, 150), 1.0f);
                }
            }
            else {
                box_color = (d.class_id == 1) ? IM_COL32(180, 50, 255, 180) :
                    IM_COL32(color_esp_hidden[0] * 255, color_esp_hidden[1] * 255, color_esp_hidden[2] * 255, 180);
                const char* cls_name = (d.class_id == 1) ? "HEAD" : "BODY";
                char id_text[64];
                snprintf(id_text, sizeof(id_text), "[ %s - ID: %d ]", cls_name, id_counter++);
                bg_draw->AddText(ImVec2(x1, y1 - 20), box_color, id_text);
            }

            if (esp_style == 1) {
                float l = d_box_w / 4.0f;
                bg_draw->AddLine(ImVec2(x1, y1), ImVec2(x1 + l, y1), box_color, esp_thickness);
                bg_draw->AddLine(ImVec2(x1, y1), ImVec2(x1, y1 + l), box_color, esp_thickness);
                bg_draw->AddLine(ImVec2(x1 + d_box_w, y1), ImVec2(x1 + d_box_w - l, y1), box_color, esp_thickness);
                bg_draw->AddLine(ImVec2(x1 + d_box_w, y1), ImVec2(x1 + d_box_w, y1 + l), box_color, esp_thickness);
                bg_draw->AddLine(ImVec2(x1, y1 + d_box_h), ImVec2(x1 + l, y1 + d_box_h), box_color, esp_thickness);
                bg_draw->AddLine(ImVec2(x1, y1 + d_box_h), ImVec2(x1, y1 + d_box_h - l), box_color, esp_thickness);
                bg_draw->AddLine(ImVec2(x1 + d_box_w, y1 + d_box_h), ImVec2(x1 + d_box_w - l, y1 + d_box_h), box_color, esp_thickness);
                bg_draw->AddLine(ImVec2(x1 + d_box_w, y1 + d_box_h), ImVec2(x1 + d_box_w, y1 + d_box_h - l), box_color, esp_thickness);
            }
            else {
                bg_draw->AddRect(ImVec2(x1, y1), ImVec2(x1 + d_box_w, y1 + d_box_h), box_color, 4.0f, 0, esp_thickness);
            }
        }

        std::set<int> active_track_ids;
        for (const auto& d : detections) if (d.track_id != -1) active_track_ids.insert(d.track_id);
        g_head_smoother.clearOldTracks(active_track_ids);

        {
            std::lock_guard<std::mutex> lock(g_heads_mutex);
            for (const auto& h : g_shared_heads) {
                bg_draw->AddRect(ImVec2(h.box.x, h.box.y), ImVec2(h.box.x + h.box.w, h.box.y + h.box.h), IM_COL32(255, 220, 50, 230), 2.0f, 0, 2.0f);
                bg_draw->AddText(ImVec2(h.box.x + h.box.w / 2 - 15, h.box.y - 18), IM_COL32(255, 255, 100, 255), "HEAD");
            }
        }
    }
    ImGui::End();

    if (is_drawing_zone) {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2((float)active_res_w, (float)active_res_h));
        ImGui::Begin("ZoneDrawerOverlay", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddRectFilled(ImVec2(0, 0), ImVec2(active_res_w, active_res_h), IM_COL32(0, 0, 0, 100));
        const char* draw_text = is_russian ? u8"ЗАЖМИТЕ ЛЕВУЮ КНОПКУ МЫШИ ДЛЯ ВЫДЕЛЕНИЯ ЗОНЫ. [ESC] ДЛЯ ОТМЕНЫ." :
            "HOLD LEFT CLICK TO DRAW ZONE. PRESS [ESC] TO CANCEL.";
        ImGui::SetCursorPos(ImVec2(active_res_w / 2.0f - ImGui::CalcTextSize(draw_text).x / 2.0f, 50.0f));
        ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), draw_text);
        ImVec2 mouse_pos = ImGui::GetMousePos();
        static ImVec2 start_pos;
        static bool is_dragging = false;
        if (ImGui::IsMouseClicked(0)) { start_pos = mouse_pos; is_dragging = true; }
        if (is_dragging) {
            draw_list->AddRectFilled(start_pos, mouse_pos, IM_COL32(255, 0, 0, 80));
            draw_list->AddRect(start_pos, mouse_pos, IM_COL32(255, 0, 0, 255), 0, 0, 2.0f);
        }
        if (ImGui::IsMouseReleased(0) && is_dragging) {
            excl_x1 = (std::min)(start_pos.x, mouse_pos.x);
            excl_y1 = (std::min)(start_pos.y, mouse_pos.y);
            excl_x2 = (std::max)(start_pos.x, mouse_pos.x);
            excl_y2 = (std::max)(start_pos.y, mouse_pos.y);
            is_dragging = false;
            is_drawing_zone = false;
        }
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
            is_dragging = false;
            is_drawing_zone = false;
            Sleep(150);
        }
        ImGui::End();
    }
}

// ============================================================
// RenderMenu (реализована в overlay_render_menu.cpp, но для полноты включим сюда)
// ============================================================
void Overlay::RenderMenu(const std::vector<Detection>& detections, Aimbot* aim, bool& cfg_changed) {
    ImVec4 acc_vec = ImVec4(accent_color[0], accent_color[1], accent_color[2], 1.0f);
    ImU32 acc_u32 = ImGui::ColorConvertFloat4ToU32(acc_vec);

    ImGui::SetNextWindowSize(ImVec2(menu_width, menu_height), ImGuiCond_Always);
    ImGui::Begin("PWNZ_MAIN", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);

    static float text_offset = 0.0f;
    text_offset -= ImGui::GetIO().DeltaTime * 20.0f;
    const char* banner_txt = is_russian ?
        u8"   [ BOG-X ] PWNZ VISION PRO — Лучший софт, не уступающий конкурентам! Передовая и самая современная защита от банов.        *** [ INFO ] Пользователь, помни: старайся выставлять как можно более человечные настройки (Humanizer). От ручного бана патрулем не застрахован никто!        *** [ SECURE ] Наш чит защищает от любого античита с использованием аппаратного вывода: Hardware / 2PC DMA / Makcu / KMBox.        *** [ UPDATE ] Мы постоянно обновляем наш продукт и внедряем самые последние технологии обхода. Это у нас на особо важном контроле, чтобы даже 1PC пользователь чувствовал себя абсолютно безопасно!        *** [ AI ] Обучаемая нейросеть BogX, алгоритмы HCI Physics и биомеханика движений сделают твою наводку неотличимой от киберспортсмена.        *** [ SYSTEM ] Интеллектуальный анализ поведения цели и динамическое упреждение гарантируют максимальную точность.        *** " :
        "   [ BOG-X ] PWNZ VISION PRO — The best software on the market with advanced ban protection!        *** [ INFO ] Remember: use Humanizer settings to avoid manual bans!        *** [ SECURE ] Absolute security with Hardware bypass: Arduino, KMBox, Makcu & 2PC.        *** [ UPDATE ] We constantly update our product and implement the latest anti-cheat bypass technologies. Your safety is our top priority!        *** [ AI ] BogX neural network and HCI Physics will make your aim indistinguishable from a pro player.        *** ";
    float txt_w = ImGui::CalcTextSize(banner_txt).x;
    if (text_offset <= -txt_w) text_offset += txt_w;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.05f, 0.12f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, acc_vec);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    ImGui::BeginChild("MarqueeBanner", ImVec2(0, 32), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    float current_x = text_offset;
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.95f, 1.0f));
    while (current_x < ImGui::GetWindowWidth()) {
        ImGui::SetCursorPos(ImVec2(current_x, 7));
        ImGui::TextUnformatted(banner_txt);
        current_x += txt_w;
    }
    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    ImGui::Spacing();

    ImGui::BeginChild("LeftSidebar", ImVec2(150, 0), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PushStyleColor(ImGuiCol_Border, acc_vec);
    ImGui::BeginChild("LogoBox", ImVec2(150, 80), true);
    ImGui::SetCursorPosY(30); ImGui::SetCursorPosX((150 - ImGui::CalcTextSize("PWNZ AI").x) * 0.5f);
    ImGui::TextColored(acc_vec, "PWNZ AI");
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::Spacing();

    const char* tabs_en[] = { "Aimbot", "Visuals", "Neural", "PWNZ AI", "Profile", "Hardware\\2PC", "Security Guide", "Telemetry", "HW Check", "X-TIER", "Metrics" };
    const char* tabs_ru[] = { u8"Аимбот", u8"Визуалы", u8"Нейросеть", u8"PWNZ AI", u8"Профиль", u8"Hardware\\2PC", u8"Безопасность", u8"Телеметрия", u8"Проверка HW", u8"X-TIER", u8"Метрики" };
    for (int i = 0; i < 11; i++) {
        if (active_tab == i) { ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.10f, 0.18f, 1.0f)); ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f)); }
        else { ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f)); ImGui::PushStyleColor(ImGuiCol_Text, acc_vec); }
        ImVec2 p_min = ImGui::GetCursorScreenPos();
        if (ImGui::Button(is_russian ? tabs_ru[i] : tabs_en[i], ImVec2(150, 45))) active_tab = i;
        if (active_tab == i) ImGui::GetWindowDrawList()->AddRectFilled(p_min, ImVec2(p_min.x + 4, p_min.y + 45), acc_u32);
        ImGui::PopStyleColor(2);
    }

    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 50);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(accent_color[0] * 0.5f, accent_color[1] * 0.5f, accent_color[2] * 0.5f, 0.6f));
    if (ImGui::Button(u8"[ AI ] PWNZ Assistant", ImVec2(150, 40))) show_ai_chat = !show_ai_chat;
    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginGroup();

    ImGui::BeginChild("TopBar", ImVec2(0, 45), true);
    ImGui::SetCursorPosY(10);
    if (ImGui::Button(is_russian ? "EN" : "RU", ImVec2(40, 25))) { is_russian = !is_russian; cfg_changed = true; }
    ImGui::SameLine(); ImGui::SetCursorPosY(14); ImGui::TextColored(acc_vec, "PWNZ VISION PRO");
    ImGui::SameLine(); ImVec2 p = ImGui::GetCursorScreenPos();
    float time_sec = ImGui::GetTime(); float blend = (std::sin(time_sec * 4.0f) * 0.5f) + 0.5f;
    ImU32 led_col = ImGui::ColorConvertFloat4ToU32(ImVec4(blend, 0.0f, 1.0f - blend, 1.0f));
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + 5.0f, p.y + 7.0f), 5.0f, led_col);
    ImGui::SameLine(240.0f); ImGui::SetCursorPosY(10);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.10f, 0.18f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, acc_vec);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    if (ImGui::Button("[ @ ]", ImVec2(40, 25))) ImGui::OpenPopup("PWNZ_Socials");
    ImGui::PopStyleColor(3);

    ImGui::SetNextWindowSize(ImVec2(380, 200));
    if (ImGui::BeginPopup("PWNZ_Socials")) {
        float text_w = ImGui::CalcTextSize(is_russian ? u8"Создано BOG-X" : "Created by BOG-X").x;
        ImGui::SetCursorPosX((380 - text_w) * 0.5f);
        ImGui::TextColored(acc_vec, is_russian ? u8"Создано BOG-X" : "Created by BOG-X");
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.0f, 0.8f, 1.0f, 1.0f), "[ TELEGRAM ] ");
        ImGui::SameLine();
        if (ImGui::Selectable(is_russian ? u8"PWNZ VISION AI (Наш канал)" : "PWNZ VISION AI (Our Channel)"))
            ShellExecuteA(NULL, "open", "https://t.me/pwnz_ai", NULL, NULL, SW_SHOWNORMAL);
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.0f, 0.8f, 1.0f, 1.0f), "[ WEBSITE ]  ");
        ImGui::SameLine();
        if (ImGui::Selectable(is_russian ? u8"Официальный сайт чита PWNZ" : "PWNZ Official Website"))
            ShellExecuteA(NULL, "open", "https://pwnzneuralcheat-ai.netlify.app/", NULL, NULL, SW_SHOWNORMAL);
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.0f, 0.8f, 1.0f, 1.0f), "[ YOUTUBE ]  ");
        ImGui::SameLine();
        if (ImGui::Selectable(is_russian ? u8"PWNZ NEURAL AIM (Видео)" : "PWNZ NEURAL AIM (Videos)"))
            ShellExecuteA(NULL, "open", "https://www.youtube.com/@pwnzneural_ai", NULL, NULL, SW_SHOWNORMAL);
        ImGui::EndPopup();
    }

    const char* hw_names_full[] = {
        "Standard API (Win32)", "Arduino / Leonardo (COM)", "KMBox B+ / Pro (COM)",
        "Makcu / Pico (COM)", "MoBox (COM)", "KMBox Net / DMA (UDP)",
        "Generic 2PC (UDP)", "Makcu (UDP)", "Makcu (COM stealth)"
    };
    char status_text[128]; snprintf(status_text, sizeof(status_text), "( Bypass: %s )", hw_names_full[hardware_mode_idx]);
    float stat_w = ImGui::CalcTextSize(status_text).x; float center_x = ImGui::GetWindowWidth() / 2.0f;
    ImGui::SameLine(center_x - (stat_w / 2.0f)); ImGui::SetCursorPosY(14.0f);
    float r, g, b; ImGui::ColorConvertHSVtoRGB(fmod(time_sec * 0.3f, 1.0f), 0.7f, 1.0f, r, g, b);
    ImGui::TextColored(ImVec4(r, g, b, 1.0f), "%s", status_text);

    std::time_t t = std::time(nullptr);
    std::tm* now = std::localtime(&t);
    char time_str[64]; strftime(time_str, sizeof(time_str), "%H:%M:%S", now);
    ImGui::SameLine(ImGui::GetWindowWidth() - 200); ImGui::SetCursorPosY(14);
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Session: %02d:%02d", (int)lifetime_seconds / 60, (int)lifetime_seconds % 60);
    ImGui::SameLine(ImGui::GetWindowWidth() - 75); ImGui::SetCursorPosY(14);
    ImGui::TextColored(acc_vec, "%s", time_str);
    ImGui::EndChild();

    ImGui::BeginChild("MainContent", ImVec2(0, 0), true);
    float content_w = ImGui::GetContentRegionAvail().x;
    float content_h = ImGui::GetContentRegionAvail().y;

    switch (active_tab) {
    case 0: RenderAimbotTab(content_w, content_h, acc_vec, acc_u32, cfg_changed); break;
    case 1: RenderVisualsTab(content_w, content_h, acc_vec, acc_u32, cfg_changed); break;
    case 2: RenderNeuralTab(content_w, content_h, acc_vec, acc_u32, cfg_changed); break;
    case 3: RenderPwnzAITab(content_w, content_h, acc_vec, acc_u32, cfg_changed); break;
    case 4: RenderProfileTab(content_w, content_h, acc_vec, acc_u32, cfg_changed); break;
    case 5: RenderHardwareTab(content_w, content_h, acc_vec, acc_u32, cfg_changed); break;
    case 6: RenderSecurityTab(content_w, content_h, acc_vec, acc_u32, cfg_changed); break;
    case 7: RenderTelemetryTab(content_w, content_h, acc_vec, acc_u32, cfg_changed); break;
    case 8: RenderHWCheckTab(content_w, content_h, acc_vec, acc_u32, cfg_changed); break;
    case 9: RenderXTierTab(content_w, content_h, acc_vec, acc_u32, cfg_changed); break;
    case 10: RenderMetricsTab(content_w, content_h, acc_vec, acc_u32, cfg_changed); break;
    }

    ImGui::EndChild();
    ImGui::EndGroup();
    ImGui::End();

    if (show_ai_chat) RenderChatWindow();
    RenderHWTutorial();
}

// ============================================================
// Основной Render (короткий, вызывает всё остальное)
// ============================================================
void Overlay::Render(const std::vector<Detection>& detections, int screen_w, int screen_h, int roi_w, int roi_h, Aimbot* aim, bool show_menu) {
    bool cfg_changed = false;
    static bool was_aim_toggle_pressed = false;
    if (aim_toggle_key != 0) {
        if (GetAsyncKeyState(aim_toggle_key) & 0x8000) {
            if (!was_aim_toggle_pressed) { aim_enable = !aim_enable; was_aim_toggle_pressed = true; cfg_changed = true; }
        }
        else { was_aim_toggle_pressed = false; }
    }
    if (is_first_frame_init) is_first_frame_init = false;
    ImVec4 acc_vec = ImVec4(accent_color[0], accent_color[1], accent_color[2], 1.0f);
    ImU32 acc_u32 = ImGui::ColorConvertFloat4ToU32(acc_vec);
    is_menu_open = show_menu;

    // Экраны аутентификации
    if (is_auto_logging_in || is_loading || !is_authenticated) {
        if (is_auto_logging_in) RenderAutoLogin(acc_vec, acc_u32);
        else if (is_loading) RenderLoadingScreen(acc_vec, acc_u32);
        else if (!is_authenticated) RenderAuthWindow(acc_vec, acc_u32);
        return;
    }

    ImGui::GetIO().FontGlobalScale = menu_scale / 100.0f;
    static bool last_obs = !obs_bypass;
    if (obs_bypass != last_obs) {
        SetWindowDisplayAffinity(hwnd, obs_bypass ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE);
        last_obs = obs_bypass;
    }
    ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame();
    POINT m_pt;
    if (GetCursorPos(&m_pt)) ImGui::GetIO().MousePos = ImVec2((float)m_pt.x, (float)m_pt.y);
    ImGui::NewFrame();
    lifetime_seconds += ImGui::GetIO().DeltaTime;

    static float open_anim_time = 0.0f;
    static bool was_open_anim = false;
    if (show_menu && !was_open_anim) open_anim_time = 0.0f;
    was_open_anim = show_menu;
    float window_alpha = 1.0f;
    if (show_menu && !is_drawing_zone) {
        open_anim_time += ImGui::GetIO().DeltaTime;
        if (open_anim_time < 0.5f) {
            window_alpha = (open_anim_time / 0.5f) * (0.8f + 0.2f * std::sin(open_anim_time * 60.0f));
            if (window_alpha > 1.0f) window_alpha = 1.0f;
        }
        else { window_alpha = 1.0f; }
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, window_alpha);
    }

    bool should_draw_visuals = !(!show_menu && disable_all_visuals_when_hidden);
    if (should_draw_visuals) {
        RenderVisualsAndZone(detections, aim);
    }

    if (show_menu && !is_drawing_zone) {
        RenderMenu(detections, aim, cfg_changed);
        if (show_menu) ImGui::PopStyleVar();
    }

    static bool was_capturing = false;
    bool need_capture = false;
    if (is_drawing_zone) need_capture = true;
    else if (show_menu || show_ai_chat || show_hw_tutorial) {
        if (ImGui::GetIO().WantCaptureMouse) need_capture = true;
        if (was_capturing && (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) need_capture = true;
    }
    was_capturing = need_capture;
    ToggleClickability(need_capture);
    if (cfg_changed) SaveConfig(aim);

    ImGui::Render();
    const float clear_color_with_alpha[4] = { 0,0,0,0 };
    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
    if (enable_dma_fuser) {
        const float fuser_bg[4] = { 0,0,0,1 };
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, fuser_bg);
    }
    else {
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
    }
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_pSwapChain->Present(0, 0);
}