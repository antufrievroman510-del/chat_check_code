#include "AimMath.h"

// Инициализация генератора случайных чисел
std::random_device AimMath::rd;
std::mt19937 AimMath::gen(AimMath::rd());
std::uniform_real_distribution<float> AimMath::dis(-1.0f, 1.0f);

/**
 * @brief Основная функция расчета движения мыши
 * Реализует логику: Проверка FOV -> Расчет дельты -> Сглаживание -> Humanize
 */
AimResult AimMath::CalculateMove(
    float currentX, float currentY,
    float targetX, float targetY,
    const AimConfig& config,
    bool is_shooting)
{
    AimResult result;

    // 1. Проверка: включен ли аимбот
    if (!config.enabled) {
        return result;
    }

    // 2. Расчет дистанции до цели
    float dist = GetDistance(currentX, currentY, targetX, targetY);

    // 3. Проверка попадания в FOV
    if (!IsInFov(currentX, currentY, targetX, targetY, config.fov)) {
        return result; // Цель вне радиуса действия
    }

    // 4. Расчет сырой дельты (насколько нужно сдвинуть)
    float rawDeltaX = targetX - currentX;
    float rawDeltaY = targetY - currentY;

    // 5. Применение сглаживания (Smooth)
    // Если smooth == 1.0f, движение мгновенное. Чем больше smooth, тем медленнее.
    float smoothedDeltaX = ApplySmooth(rawDeltaX, config.smooth);
    float smoothedDeltaY = ApplySmooth(rawDeltaY, config.smooth);

    // 6. Применение человеческого фактора (Humanize) - добавление микро-шума
    if (config.humanize) {
        smoothedDeltaX += AddHumanize(smoothedDeltaX, config.humanize_strength);
        smoothedDeltaY += AddHumanize(smoothedDeltaY, config.humanize_strength);
    }

    // 7. Заполнение результата
    result.deltaX = smoothedDeltaX;
    result.deltaY = smoothedDeltaY;
    result.target_locked = true;

    return result;
}

/**
 * @brief Проверка, находится ли точка внутри круга заданного радиуса
 */
bool AimMath::IsInFov(float centerX, float centerY, float pointX, float pointY, float fovRadius) {
    float dist = GetDistance(centerX, centerY, pointX, pointY);
    return dist <= fovRadius;
}

/**
 * @brief Вычисление евклидова расстояния между двумя точками
 */
float AimMath::GetDistance(float x1, float y1, float x2, float y2) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    return std::sqrt(dx * dx + dy * dy);
}

/**
 * @brief Функция плавного приближения (Linear Interpolation / Smoothing)
 * Логика из sunone_aimbot: delta = delta / smooth
 * Адаптировано для более предсказуемого поведения:
 * Если smooth = 1, возвращаем полную дельту.
 * Если smooth > 1, делим дельту на коэффициент.
 */
float AimMath::ApplySmooth(float delta, float smoothFactor) {
    if (smoothFactor <= 1.0f) {
        return delta; // Мгновенное перемещение
    }
    
    // Формула плавности: двигаемся только на часть пути
    // Чем выше smoothFactor, тем меньше шаг
    return delta / smoothFactor;
}

/**
 * @brief Добавление случайного шума для имитации человеческой руки
 * @param value Исходное значение
 * @param strength Сила шума (0.0 - нет шума, 1.0 - максимальный)
 */
float AimMath::AddHumanize(float value, float strength) {
    if (strength <= 0.0f) {
        return 0.0f;
    }

    // Генерируем случайное значение в диапазоне [-strength, +strength]
    // Масштабируем его относительно текущей дельты, чтобы шум был пропорционален движению
    float noise = static_cast<float>(dis(gen)) * strength;
    
    // Для малых движений шум должен быть меньше, для больших - больше
    return noise * (std::abs(value) * 0.1f + 1.0f);
}
