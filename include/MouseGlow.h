#pragma once

namespace MouseGlow {
    // Gold radial gradient that follows the mouse cursor, masked to panel bounds
    constexpr uint32_t COLOR  = 0xD4AF37;
    constexpr double   ALPHA  = 12.0;
    constexpr double   RADIUS = 300.0;

    // Create a masked radial glow clip at the given depth.
    // The glow is centered at (0,0) in its own coordinate space; call SetPosition to place it.
    // The mask clips the glow to the rectangle (maskX, maskY, maskW, maskH).
    void Create(RE::GFxMovieView* a_movie, const char* a_name, int a_depth,
                double a_maskX, double a_maskY, double a_maskW, double a_maskH);

    // Reposition the glow clip to follow the mouse.
    void SetPosition(RE::GFxMovieView* a_movie, const char* a_name, double a_x, double a_y);

    // Remove the glow clip and its mask clip.
    void Destroy(RE::GFxMovieView* a_movie, const char* a_name);
}
