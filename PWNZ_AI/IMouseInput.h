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

#include <string>

namespace pwnz_ai {

// ============================================================
// Mouse button enumeration
// ============================================================
enum class MouseButton {
    NONE = 0,
    LEFT = 1,
    RIGHT = 2,
    MIDDLE = 3,
    SIDE1 = 4,
    SIDE2 = 5
};

// ============================================================
// Abstract class IMouseInput
// ============================================================
class IMouseInput {
public:
    // Virtual destructor for correct deletion of derived classes
    virtual ~IMouseInput() noexcept = default;

    /**
     * @brief Initialize the input method
     * @param port Port or connection string (e.g., "COM3" for Makcu)
     * @return true if successful, false if error
     */
    virtual bool Init(const std::string& port = "") = 0;

    /**
     * @brief Move mouse by relative amount
     * @param dx Offset on X axis (in pixels or device units)
     * @param dy Offset on Y axis (in pixels or device units)
     */
    virtual void Move(int dx, int dy) = 0;

    /**
     * @brief Emulate mouse button click (full click: press + release)
     * @param button Button code (use MouseButton enum)
     */
    virtual void Click(MouseButton button) = 0;

    /**
     * @brief Press mouse button (hold)
     * @param button Button code (use MouseButton enum)
     */
    virtual void Press(MouseButton button) = 0;

    /**
     * @brief Release mouse button
     * @param button Button code (use MouseButton enum)
     */
    virtual void Release(MouseButton button) = 0;

    /**
     * @brief Shutdown and release resources
     */
    virtual void Shutdown() = 0;

    /**
     * @brief Check if device is connected
     * @return true if connected, false otherwise
     */
    virtual bool IsConnected() const = 0;
};

} // namespace pwnz_ai

#endif // IMOUSE_INPUT_H
