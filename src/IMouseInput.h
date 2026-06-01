#pragma once
/**
 * @file IMouseInput.h
 * @brief Абстрактный интерфейс для всех методов ввода мыши
 * 
 * Этот интерфейс позволяет легко добавлять новые методы ввода
 * (SendInput, Makcu, KMbox, Razer, GHub, Driver и т.д.)
 * без изменения основного кода аимбота.
 */

#ifndef IMOUSE_INPUT_H
#define IMOUSE_INPUT_H

// ============================================================
// Абстрактный класс IMouseInput
// ============================================================
class IMouseInput {
public:
    // Виртуальный деструктор для корректного удаления наследников
    virtual ~IMouseInput() = default;

    /**
     * @brief Инициализация метода ввода
     * @return true если успешно, false если ошибка
     */
    virtual bool Init() = 0;

    /**
     * @brief Перемещение мыши на относительное значение
     * @param dx Смещение по оси X (в пикселях или единицах устройства)
     * @param dy Смещение по оси Y (в пикселях или единицах устройства)
     */
    virtual void Move(int dx, int dy) = 0;

    /**
     * @brief Эмуляция нажатия кнопки мыши
     * @param button Код кнопки (0=левая, 1=правая, 2=средняя и т.д.)
     */
    virtual void Click(int button) = 0;

    /**
     * @brief Завершение работы и освобождение ресурсов
     */
    virtual void Shutdown() = 0;
};

#endif // IMOUSE_INPUT_H
