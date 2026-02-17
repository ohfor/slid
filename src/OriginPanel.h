#pragma once

namespace OriginPanel {

    // Create the origin row MovieClip and text fields.
    // Call once during InitRows.
    void Draw(RE::GFxMovieView* a_movie, RE::FormID a_masterFormID,
              double a_x, double a_y, double a_w, double a_rowH);

    // Per-frame update (flash timer).
    void Update(RE::GFxMovieView* a_movie);

    // Update the count display. If a_predictedCount != a_currentCount and a_predictedCount >= 0,
    // shows "current > predicted" with a colored delta.
    void UpdateCount(RE::GFxMovieView* a_movie, int a_currentCount, int a_predictedCount);

    // Set the count text to flash color (after Sort).
    void SetCountFlash(RE::GFxMovieView* a_movie, bool a_flash);

    // Update the count text after Sort (count value + optional flash color).
    void SetCount(RE::GFxMovieView* a_movie, int a_count, bool a_flash);

    // Clean up.
    void Destroy();
}
