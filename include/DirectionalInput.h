#pragma once

#include <chrono>

namespace DirectionalInput {

    // --- Thumbstick debounce ---
    // Edge-triggered direction from continuous analog input.

    struct ThumbstickState {
        bool up    = false;
        bool down  = false;
        bool left  = false;
        bool right = false;
    };

    enum class Direction {
        kNone,
        kUp,
        kDown,
        kLeft,
        kRight
    };

    // Returns edges (newly pressed since last call). Caller checks each direction.
    struct ThumbstickEdges {
        bool up    = false;
        bool down  = false;
        bool left  = false;
        bool right = false;
    };

    inline ThumbstickEdges ProcessThumbstick(float a_xValue, float a_yValue,
                                              ThumbstickState& a_state,
                                              float a_deadzone = 0.5f)
    {
        bool nowUp    = a_yValue > a_deadzone;
        bool nowDown  = a_yValue < -a_deadzone;
        bool nowLeft  = a_xValue < -a_deadzone;
        bool nowRight = a_xValue > a_deadzone;

        ThumbstickEdges edges;
        edges.up    = nowUp    && !a_state.up;
        edges.down  = nowDown  && !a_state.down;
        edges.left  = nowLeft  && !a_state.left;
        edges.right = nowRight && !a_state.right;

        a_state.up    = nowUp;
        a_state.down  = nowDown;
        a_state.left  = nowLeft;
        a_state.right = nowRight;

        return edges;
    }

    // --- D-pad / keyboard repeat ---
    // Initial delay before first repeat, then fixed interval.

    struct RepeatState {
        int  direction  = 0;     // -1 = up, +1 = down, 0 = none
        bool active     = false; // true after initial delay
        std::chrono::steady_clock::time_point lastTime;

        float initialDelay = 0.4f;
        float interval     = 0.08f;
    };

    // Process a vertical direction (from thumbstick or D-pad).
    // Returns true if the action should fire (first press or repeat trigger).
    inline bool ProcessRepeat(int a_direction, RepeatState& a_state) {
        if (a_direction == 0) {
            a_state.direction = 0;
            a_state.active = false;
            return false;
        }

        auto now = std::chrono::steady_clock::now();

        if (a_direction != a_state.direction) {
            // New direction — fire immediately
            a_state.direction = a_direction;
            a_state.active = false;
            a_state.lastTime = now;
            return true;
        }

        // Same direction held — check timing
        float elapsed = std::chrono::duration<float>(now - a_state.lastTime).count();
        float delay = a_state.active ? a_state.interval : a_state.initialDelay;

        if (elapsed >= delay) {
            a_state.active = true;
            a_state.lastTime = now;
            return true;
        }

        return false;
    }

    // Process a button press/held/release event for repeat.
    // buttonDown = first press, buttonPressed = held, buttonUp = released.
    // Returns true if the action should fire.
    inline bool ProcessButtonRepeat(int a_direction, bool a_isDown, bool a_isPressed,
                                     bool a_isUp, RepeatState& a_state)
    {
        auto now = std::chrono::steady_clock::now();

        if (a_isDown) {
            a_state.direction = a_direction;
            a_state.active = false;
            a_state.lastTime = now;
            return true;
        }

        if (a_isPressed && a_direction == a_state.direction) {
            float elapsed = std::chrono::duration<float>(now - a_state.lastTime).count();
            float delay = a_state.active ? a_state.interval : a_state.initialDelay;
            if (elapsed >= delay) {
                a_state.active = true;
                a_state.lastTime = now;
                return true;
            }
        }

        if (a_isUp && a_direction == a_state.direction) {
            a_state.direction = 0;
            a_state.active = false;
        }

        return false;
    }

    // Reset all state (call when changing modes / focus)
    inline void Reset(ThumbstickState& a_ts, RepeatState& a_repeat) {
        a_ts = {};
        a_repeat.direction = 0;
        a_repeat.active = false;
    }
}
