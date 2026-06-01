#pragma once
#include "WinHeaders.h"
#include <cmath>
#include <random>
#include <algorithm>

// Простая структура вектора для математики
struct Vector2 {
    float x, y;
    Vector2() : x(0), y(0) {}
    Vector2(float _x, float _y) : x(_x), y(_y) {}
    
    // Операторы для удобства
    Vector2 operator+(const Vector2& other) const { return Vector2(x + other.x, y + other.y); }
    Vector2 operator-(const Vector2& other) const { return Vector2(x - other.x, y - other.y); }
    Vector2 operator*(float scalar) const { return Vector2(x * scalar, y * scalar); }
    Vector2 operator/(float scalar) const { return Vector2(x / scalar, y / scalar); }
    Vector2& operator+=(const Vector2& other) { x += other.x; y += other.y; return *this; }
};

// Структура конфигурации аимбота (связь с меню)
struct AimConfig {
    bool enabled = false;
    int aimbone = 0; // 0 - Head, 1 - Neck, 2 - Body
    float smooth = 1.0f; // 1.0 = мгновенно, >1.0 = плавнее
    int smooth_method = 0; // 0 - Linear, 1 - EaseOut, 2 - Bezier
    float fov = 5.0f; // Радиус круга прицеливания
    int fireKey = 0; // VK код клавиши выстрела
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
    static float GetDistance(float x1, float y1, float x2, float y2);
    
    // Расчет дистанции между векторами
    static float CalculateDistance(const Vector2& v1, const Vector2& v2);

    // Проверка попадания в FOV
    static bool IsInFov(float centerX, float centerY, float pointX, float pointY, float fovRadius);
    
    // Проверка попадания в FOV (векторная версия)
    static bool IsWithinFOV(const Vector2& startPos, const Vector2& targetPos, float fovRadius);

    // Продвинутое сглаживание (возвращает новую дельту)
    static float ApplySmooth(float delta, float smoothFactor);
    
    // Сглаживание вектора (для будущих расширений)
    static Vector2 ApplySmoothVec(const Vector2& currentPos, const Vector2& targetPos, float smoothFactor, int smoothMethod);

    // Добавление человеческой погрешности (Humanize)
    static float AddHumanize(float value, float strength);
    
    // Humanize для вектора
    static Vector2 AddHumanizeVec(const Vector2& targetPos, float humanizeAmount);

    // Предикт движения (Prediction)
    static Vector2 PredictPosition(const Vector2& targetPos, const Vector2& targetVel, float bulletSpeed, float distance);

    // Основная функция расчета движения (обертка)
    // Упрощенная сигнатура под текущую логику
    static AimResult CalculateMove(
        float currentX, float currentY,
        float targetX, float targetY,
        const AimConfig& config,
        bool is_shooting = false
    );

private:
    static std::random_device rd;
    static std::mt19937 gen;
    static std::uniform_real_distribution<float> dis;
};
