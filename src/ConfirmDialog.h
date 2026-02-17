#pragma once

#include "ScaleformUtil.h"

#include <functional>
#include <string>
#include <vector>

namespace ConfirmDialog {

    struct Config {
        std::string title = "Are you sure?";
        std::vector<std::string> buttons = {"Yes", "No"};
        double popupW     = 240.0;
        int defaultIndex  = 1;      // index into buttons (default last for safety)
    };

    using Callback = std::function<void(int selectedIndex)>;

    // Show a modal confirmation popup with N buttons.
    // The movie pointer must remain valid until Close() is called.
    void Show(RE::GFxMovieView* a_movie, const Config& a_config, Callback a_callback);

    // Close the popup, invoking the callback with the selected button index.
    void Close(int a_selectedIndex);

    // True if the dialog is currently displayed.
    bool IsOpen();

    // Navigation (keyboard/gamepad)
    void NavigateLeft();
    void NavigateRight();
    void Confirm();   // select current button
    void Cancel();    // dismiss as last button (by convention)

    // Mouse support
    // Returns -1 (miss) or button index [0..N-1].
    int HitTest(float a_mx, float a_my);

    // Update hover highlight. Pass -1 to clear.
    void UpdateHover(int a_btnIndex);

    // Cached popup geometry for external mouse dispatch
    double GetPopupX();
    double GetPopupY();
    double GetPopupW();
    double GetPopupH();
}
