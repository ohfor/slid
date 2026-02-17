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
