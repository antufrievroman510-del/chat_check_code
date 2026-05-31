#pragma once
#include <Windows.h>
#include <cmath>
#include <random>
#include <algorithm>

// Простая структура вектора для математики
struct Vector2 {
    float x, y;
    Vector2() : x(0), y(0) {}
    Vector2(float _x, float _y) : x(_x), y(_y) {}
};

// Структура конфигурации аимбота (связь с меню)
struct AimConfig {
    bool enabled = false;
    int aimbone = 0; // 0 - Head, 1 - Neck, 2 - Body
    float smooth = 1.0f; // 1.0 = мгновенно, >1.0 = плавнее
    int smooth_method = 0; // 0 - Linear, 1 - EaseOut, 2 - Bezier
    float fov = 5.0f; // Радиус круга прицеливания
    bool recoil_control = false;
    float rcs_smooth = 1.0f;
    bool humanize = false;
    float humanize_strength = 0.5f;
    bool prediction = false;
    float bullet_speed = 0.0f; // Для предикта
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
    // Расчет дистанции между двумя точками
    static float CalculateDistance(float x1, float y1, float x2, float y2);

    // Расчет угла в градусах
    static float CalculateAngle(float x1, float y1, float x2, float y2);

    // Продвинутое сглаживание (Linear, Ease-Out, Bezier)
    static Vector2 ApplySmooth(Vector2 currentPos, Vector2 targetPos, float smoothFactor, int smoothMethod);

    // Добавление человеческой погрешности (Humanize)
    static Vector2 AddHumanize(Vector2 targetPos, float humanizeAmount);

    // Предикт движения (Prediction)
    static Vector2 PredictPosition(Vector2 targetPos, Vector2 targetVel, float bulletSpeed, float distance);

    // Ограничение FOV (Circle Check)
    static bool IsWithinFOV(Vector2 startPos, Vector2 targetPos, float fovRadius);

    // Основная функция расчета движения (обертка)
    static AimResult CalculateMove(
        float currentX, float currentY,
        float targetX, float targetY,
        const AimConfig& config,
        Vector2 targetVel = Vector2(0, 0),
        bool is_shooting = false
    );

private:
    static std::random_device rd;
    static std::mt19937 gen;
};
