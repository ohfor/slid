#pragma once

namespace ScaleformUtil {
    // Gamepad button codes (RE::INPUT_DEVICE::kGamepad IDCodes)
    constexpr uint32_t GAMEPAD_DPAD_UP    = 0x0001;
    constexpr uint32_t GAMEPAD_DPAD_DOWN  = 0x0002;
    constexpr uint32_t GAMEPAD_DPAD_LEFT  = 0x0004;
    constexpr uint32_t GAMEPAD_DPAD_RIGHT = 0x0008;
    constexpr uint32_t GAMEPAD_A          = 0x1000;
    constexpr uint32_t GAMEPAD_B          = 0x2000;
    constexpr uint32_t GAMEPAD_X          = 0x4000;
    constexpr uint32_t GAMEPAD_LB         = 0x0100;
    constexpr uint32_t GAMEPAD_RB         = 0x0200;

    // Returns the embedded font name appropriate for the game's current language.
    // CJK languages (Japanese, Korean, Simplified/Traditional Chinese) → "Noto Sans CJK SC Regular"
    // All other languages → "Noto Sans"
    // Result is cached on first call (language doesn't change mid-session).
    const char* GetFont();

    // Y-position correction for Noto Sans vs the previously-used Arial font.
    // Noto Sans has a larger ascent-to-capHeight gap (727/2048 vs 387/2048 em units),
    // which pushes glyphs ~2.3px lower inside TextFields at 14px.
    // Subtract this from Y when creating text fields to restore proper visual centering.
    inline double TextYCorrection(int a_fontSize) {
        return static_cast<double>(a_fontSize) * (340.0 / 2048.0);  // ≈ 0.166 * fontSize
    }

    // Drawing API helpers — stateless free functions, take movie pointer explicitly

    void DrawFilledRect(RE::GFxMovieView* a_movie, const char* a_name, int a_depth,
                        double a_x, double a_y, double a_w, double a_h,
                        uint32_t a_color, int a_alpha);

    void DrawBorderRect(RE::GFxMovieView* a_movie, const char* a_name, int a_depth,
                        double a_x, double a_y, double a_w, double a_h,
                        uint32_t a_color);

    void DrawLine(RE::GFxMovieView* a_movie, const char* a_name, int a_depth,
                  double a_x1, double a_y1, double a_x2, double a_y2,
                  uint32_t a_color);

    void CreateLabel(RE::GFxMovieView* a_movie, const char* a_name, int a_depth,
                     double a_x, double a_y, double a_w, double a_h,
                     const char* a_text, int a_size, uint32_t a_color);

    void SetTextFieldFormat(RE::GFxMovieView* a_movie, const std::string& a_path,
                            int a_size, uint32_t a_color);
}
