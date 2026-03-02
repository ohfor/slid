#include "ActionBar.h"
#include "TranslationService.h"

namespace ActionBar {

    // --- Module state ---
    static ButtonBar s_bar;

    // Whoosh hold uses ButtonBar's hold mechanics, but we need to track
    // which button it was started on (always BTN_WHOOSH but stored for safety).
    // Defaults hold also uses ButtonBar.
    // We track which type is active to route completion correctly.
    enum class HoldType { kNone, kDefaults, kWhoosh };
    static HoldType s_holdType = HoldType::kNone;

    // --- Lifecycle ---

    void Draw(RE::GFxMovieView* a_movie, double a_panelX, double a_panelW, double a_barY,
              bool a_focused, int a_selectedIndex) {
        s_holdType = HoldType::kNone;

        s_bar.Init(a_movie, "_actionBtn", 300,
            {
                {T("$SLID_BtnWhoosh"),    120.0, T("$SLID_GuideWhoosh"),
                 true, ButtonColors::HOLD_GREEN, 80, 1.0f, 0.2f},
                {T("$SLID_BtnSort"),      100.0, T("$SLID_GuideSort")},
                {T("$SLID_BtnSweep"),     100.0, T("$SLID_GuideSweep")},
                {T("$SLID_BtnDefaults"),  100.0, T("$SLID_GuideDefaults"),
                 true, ButtonColors::HOLD_BLUE, 80, 1.0f},
                {T("$SLID_BtnClose"),     100.0, T("$SLID_GuideClose")}
            },
            a_panelX + a_panelW / 2.0, a_barY);

        s_bar.Draw(a_focused ? a_selectedIndex : -1, -1);
    }

    void Update(bool a_focused, int a_selectedIndex,
                bool a_hoverActive, int a_hoverIndex) {
        s_bar.Draw(a_focused ? a_selectedIndex : -1,
                   a_hoverActive ? a_hoverIndex : -1);
    }

    void Destroy() {
        s_bar.Destroy();
        s_holdType = HoldType::kNone;
    }

    // --- Defaults hold ---

    void StartDefaultsHold() {
        s_holdType = HoldType::kDefaults;
        s_bar.StartHold(BTN_DEFAULTS);
        logger::debug("Defaults: hold started");
    }

    void UpdateDefaultsHold() {
        if (s_holdType != HoldType::kDefaults) return;
        s_bar.UpdateHold();
        int completed = s_bar.CompletedHold();
        if (completed >= 0) {
            s_holdType = HoldType::kNone;
            logger::info("Defaults: hold completed");
        }
    }

    void CancelDefaultsHold() {
        if (s_holdType != HoldType::kDefaults) return;
        s_bar.CancelHold();
        s_holdType = HoldType::kNone;
        logger::debug("Defaults: hold cancelled");
    }

    bool IsDefaultsHolding() {
        return s_holdType == HoldType::kDefaults && s_bar.IsHolding();
    }

    // --- Whoosh hold ---

    void StartWhooshHold(int a_btnIndex) {
        s_holdType = HoldType::kWhoosh;
        s_bar.StartHold(a_btnIndex);
        logger::debug("Whoosh: hold started (btn {})", a_btnIndex);
    }

    void UpdateWhooshHold() {
        if (s_holdType != HoldType::kWhoosh) return;
        s_bar.UpdateHold();
        int completed = s_bar.CompletedHold();
        if (completed >= 0) {
            s_holdType = HoldType::kNone;
            logger::info("Whoosh: hold completed — reconfig");
        }
    }

    void ReleaseWhooshHold() {
        if (s_holdType != HoldType::kWhoosh) return;
        s_bar.CancelHold();
        s_holdType = HoldType::kNone;
    }

    void CancelWhooshHold() {
        if (s_holdType != HoldType::kWhoosh) return;
        s_bar.CancelHold();
        s_holdType = HoldType::kNone;
        logger::debug("Whoosh: hold cancelled");
    }

    bool IsWhooshHolding() {
        return s_holdType == HoldType::kWhoosh && s_bar.IsHolding();
    }

    bool IsWhooshPastDeadZone() {
        if (s_holdType != HoldType::kWhoosh) return false;
        return s_bar.IsHoldPastDeadZone();
    }

    // --- Flash ---

    void FlashButton(int a_index) {
        s_bar.Flash(a_index);
    }

    void UpdateFlash() {
        s_bar.UpdateFlash();
    }

    // --- Guide text ---

    std::string GetGuideText(int a_selectedIndex) {
        return std::string(s_bar.GetGuideText(a_selectedIndex));
    }

    // --- Mouse hit-testing ---

    int HitTest(float a_mx, float a_my) {
        return s_bar.HitTest(a_mx, a_my);
    }

    double GetButtonX(int a_index) {
        return s_bar.GetButtonX(a_index);
    }

    double GetButtonW(int a_index) {
        return s_bar.GetButtonW(a_index);
    }

    double GetBarY() {
        return s_bar.GetBarY();
    }
}
