#pragma once
/**
 * @file IMouseInput.h
 * @brief Abstract interface for all mouse input methods
 * 
 * This interface allows easy addition of new input methods
 * (SendInput, Makcu, KMbox, Razer, GHub, Driver, etc.)
 * without changing the main aimbot code.
 */

#ifndef IMOUSE_INPUT_H
#define IMOUSE_INPUT_H

// ============================================================
// Abstract class IMouseInput
// ============================================================
class IMouseInput {
public:
    // Virtual destructor for correct deletion of derived classes
    virtual ~IMouseInput() = default;

    /**
     * @brief Initialize the input method
     * @return true if successful, false if error
     */
    virtual bool Init() = 0;

    /**
     * @brief Move mouse by relative amount
     * @param dx Offset on X axis (in pixels or device units)
     * @param dy Offset on Y axis (in pixels or device units)
     */
    virtual void Move(int dx, int dy) = 0;

    /**
     * @brief Emulate mouse button click (full click: press + release)
     * @param button Button code (0=left, 1=right, 2=middle, etc.)
     */
    virtual void Click(int button) = 0;

    /**
     * @brief Press mouse button (hold)
     * @param button Button code (0=left, 1=right, 2=middle, etc.)
     */
    virtual void Press(int button) = 0;

    /**
     * @brief Release mouse button
     * @param button Button code (0=left, 1=right, 2=middle, etc.)
     */
    virtual void Release(int button) = 0;

    /**
     * @brief Shutdown and release resources
     */
    virtual void Shutdown() = 0;
};

#endif // IMOUSE_INPUT_H
