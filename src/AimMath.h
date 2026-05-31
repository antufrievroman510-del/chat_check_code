#pragma once
#include <Windows.h>
#include <cmath>
#include <random>
#include <algorithm>

// Структура конфигурации аимбота (связь с меню)
struct AimConfig {
    bool enabled = false;
    int aimbone = 0; // 0 - Head, 1 - Neck, 2 - Body
    float smooth = 1.0f; // 1.0 = мгновенно, >1.0 = плавнее
    float fov = 5.0f; // Радиус круга прицеливания в пикселях/градусах
    bool recoil_control = false;
    float rcs_smooth = 1.0f;
    bool humanize = false;
    float humanize_strength = 0.5f;
    int trigger_bot_key = 0; // VK код
};

// Структура результата расчета
struct AimResult {
    float deltaX = 0.0f;
    float deltaY = 0.0f;
    bool target_locked = false;
    int target_id = -1;
};

class AimMath {
public:
    // Основная функция расчета движения
    static AimResult CalculateMove(
        float currentX, float currentY,
        float targetX, float targetY,
        const AimConfig& config,
        bool is_shooting = false
    );

    // Проверка попадания точки в круг FOV
    static bool IsInFov(float centerX, float centerY, float pointX, float pointY, float fovRadius);

    // Расчет дистанции между точками
    static float GetDistance(float x1, float y1, float x2, float y2);

    // Функция сглаживания (адаптирована из sunone_aimbot_2)
    static float ApplySmooth(float delta, float smoothFactor);

    // Добавление человеческого фактора (шум)
    static float AddHumanize(float value, float strength);

private:
    static std::random_device rd;
    static std::mt19937 gen;
    static std::uniform_real_distribution<> dis;
};
