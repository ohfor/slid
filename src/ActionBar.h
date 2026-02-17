#pragma once

#include <chrono>
#include <string>

namespace ActionBar {

    // Action button indices
    constexpr int BTN_WHOOSH    = 0;
    constexpr int BTN_SORT      = 1;
    constexpr int BTN_SWEEP     = 2;
    constexpr int BTN_DEFAULTS  = 3;
    constexpr int BTN_CLOSE     = 4;
    constexpr int BTN_COUNT     = 5;

    // Signal emitted by Activate()
    enum class Signal {
        kNone,
        kWhooshTap,       // Whoosh released before hold threshold
        kWhooshReconfig,  // Whoosh hold completed — open reconfig popup
        kSort,
        kSweep,
        kDefaults,        // Defaults hold completed — show confirm
        kClose
    };

    // --- Lifecycle ---

    // Draw the action bar into the movie. Call once after menu opens.
    void Draw(RE::GFxMovieView* a_movie, double a_panelX, double a_panelW, double a_barY,
              bool a_focused, int a_selectedIndex);

    // Redraw button highlights (call when focus/selection changes).
    void Update(bool a_focused, int a_selectedIndex,
                bool a_hoverActive, int a_hoverIndex);

    // Clean up (call on menu close).
    void Destroy();

    // --- Hold mechanics ---

    void StartDefaultsHold();
    void UpdateDefaultsHold();    // call per-frame while held
    void CancelDefaultsHold();
    bool IsDefaultsHolding();

    void StartWhooshHold(int a_btnIndex);
    void UpdateWhooshHold();      // call per-frame while held
    void ReleaseWhooshHold();     // released before threshold → returns true if tap
    void CancelWhooshHold();
    bool IsWhooshHolding();
    bool IsWhooshPastDeadZone();  // true if past dead zone (hold started visually)

    // --- Flash ---

    void FlashButton(int a_index);
    void UpdateFlash();           // call per-frame

    // --- Guide text ---

    // Returns guide text for the currently selected action button.
    std::string GetGuideText(int a_selectedIndex);

    // --- Mouse hit-testing ---

    // Returns button index [0..BTN_COUNT-1] or -1 if not over any button.
    int HitTest(float a_mx, float a_my);

    // Returns cached X position for button (for external use).
    double GetButtonX(int a_index);
    double GetButtonW(int a_index);
    double GetBarY();
}
