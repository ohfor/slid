#include "ActionBar.h"
#include "ScaleformUtil.h"
#include "TranslationService.h"

namespace ActionBar {

    // --- Layout constants ---
    static constexpr double BTN_H    = 28.0;
    static constexpr double BTN_GAP  = 8.0;

    static const char* LABEL_KEYS[BTN_COUNT] = {
        "$SLID_BtnWhoosh", "$SLID_BtnSort", "$SLID_BtnSweep", "$SLID_BtnDefaults", "$SLID_BtnClose"
    };
    static const double WIDTHS[BTN_COUNT]  = {120.0, 100.0, 100.0, 100.0, 100.0};

    // Colors
    static constexpr uint32_t COLOR_NORMAL  = 0x1A1A1A;
    static constexpr uint32_t COLOR_SELECT  = 0x444444;
    static constexpr uint32_t COLOR_HOVER   = 0x2A2A2A;
    static constexpr uint32_t COLOR_FLASH   = 0x666666;
    static constexpr int ALPHA_NORMAL = 70;
    static constexpr int ALPHA_SELECT = 90;
    static constexpr int ALPHA_HOVER  = 80;
    static constexpr int ALPHA_FLASH  = 95;

    // Flash timing
    static constexpr float FLASH_DURATION = 0.12f;

    // Hold timing
    static constexpr float HOLD_DEAD_ZONE    = 0.2f;   // Whoosh: click/hold distinction
    static constexpr float HOLD_ANIM_DURATION = 1.0f;  // shared: 1.0s fill

    // Hold fill colors
    static constexpr uint32_t COLOR_DEFAULTS_FILL = 0x446688;
    static constexpr int ALPHA_DEFAULTS_FILL = 80;
    static constexpr uint32_t COLOR_WHOOSH_FILL = 0x448844;
    static constexpr int ALPHA_WHOOSH_FILL = 80;

    // --- Module state ---
    static RE::GFxMovieView* s_movie = nullptr;
    static RE::GFxValue      s_btns[BTN_COUNT];
    static double             s_btnX[BTN_COUNT] = {};
    static double             s_barY = 0.0;

    // Flash state
    static bool  s_flashActive = false;
    static int   s_flashIndex  = -1;
    static std::chrono::steady_clock::time_point s_flashStart;

    // Defaults hold state
    static bool  s_defaultsHolding = false;
    static std::chrono::steady_clock::time_point s_defaultsHoldStart;

    // Whoosh hold state
    static bool  s_whooshHolding = false;
    static std::chrono::steady_clock::time_point s_whooshHoldStart;
    static int   s_whooshHoldIndex = 0;

    // --- Internal helpers ---

    static void DrawBtnRect(RE::GFxValue& a_bgClip, int a_index,
                            uint32_t a_color, int a_alpha) {
        a_bgClip.Invoke("clear", nullptr, nullptr, 0);

        RE::GFxValue fillArgs[2];
        fillArgs[0].SetNumber(static_cast<double>(a_color));
        fillArgs[1].SetNumber(static_cast<double>(a_alpha));
        a_bgClip.Invoke("beginFill", nullptr, fillArgs, 2);

        double w = WIDTHS[a_index];
        RE::GFxValue pt[2];
        pt[0].SetNumber(0.0); pt[1].SetNumber(0.0);
        a_bgClip.Invoke("moveTo", nullptr, pt, 2);
        pt[0].SetNumber(w);
        a_bgClip.Invoke("lineTo", nullptr, pt, 2);
        pt[1].SetNumber(BTN_H);
        a_bgClip.Invoke("lineTo", nullptr, pt, 2);
        pt[0].SetNumber(0.0);
        a_bgClip.Invoke("lineTo", nullptr, pt, 2);
        pt[1].SetNumber(0.0);
        a_bgClip.Invoke("lineTo", nullptr, pt, 2);

        a_bgClip.Invoke("endFill", nullptr, nullptr, 0);
    }

    static void DrawHoldProgress(int a_btnIndex, float a_ratio,
                                 uint32_t a_color, int a_alpha) {
        if (a_btnIndex < 0 || a_btnIndex >= BTN_COUNT) return;
        if (s_btns[a_btnIndex].IsUndefined()) return;

        auto& btn = s_btns[a_btnIndex];

        RE::GFxValue fillClip;
        btn.GetMember("_fill", &fillClip);
        if (fillClip.IsUndefined()) {
            RE::GFxValue args[2];
            args[0].SetString("_fill");
            args[1].SetNumber(5.0);  // between bg(1) and label(10)
            btn.Invoke("createEmptyMovieClip", &fillClip, args, 2);
        }
        if (fillClip.IsUndefined()) return;

        fillClip.Invoke("clear", nullptr, nullptr, 0);

        double fillW = WIDTHS[a_btnIndex] * static_cast<double>(a_ratio);
        if (fillW < 1.0) return;

        RE::GFxValue fillArgs[2];
        fillArgs[0].SetNumber(static_cast<double>(a_color));
        fillArgs[1].SetNumber(static_cast<double>(a_alpha));
        fillClip.Invoke("beginFill", nullptr, fillArgs, 2);

        RE::GFxValue pt[2];
        pt[0].SetNumber(0.0); pt[1].SetNumber(0.0);
        fillClip.Invoke("moveTo", nullptr, pt, 2);
        pt[0].SetNumber(fillW);
        fillClip.Invoke("lineTo", nullptr, pt, 2);
        pt[1].SetNumber(BTN_H);
        fillClip.Invoke("lineTo", nullptr, pt, 2);
        pt[0].SetNumber(0.0);
        fillClip.Invoke("lineTo", nullptr, pt, 2);
        pt[1].SetNumber(0.0);
        fillClip.Invoke("lineTo", nullptr, pt, 2);

        fillClip.Invoke("endFill", nullptr, nullptr, 0);
    }

    static void ClearHoldProgress(int a_btnIndex) {
        if (a_btnIndex < 0 || a_btnIndex >= BTN_COUNT) return;
        if (s_btns[a_btnIndex].IsUndefined()) return;

        RE::GFxValue fillClip;
        s_btns[a_btnIndex].GetMember("_fill", &fillClip);
        if (!fillClip.IsUndefined()) {
            fillClip.Invoke("clear", nullptr, nullptr, 0);
        }
    }

    // --- Lifecycle ---

    void Draw(RE::GFxMovieView* a_movie, double a_panelX, double a_panelW, double a_barY,
              bool a_focused, int a_selectedIndex) {
        s_movie = a_movie;
        s_barY = a_barY;

        RE::GFxValue root;
        s_movie->GetVariable(&root, "_root");
        if (root.IsUndefined()) return;

        double totalW = 0.0;
        for (int i = 0; i < BTN_COUNT; i++) totalW += WIDTHS[i];
        totalW += BTN_GAP * (BTN_COUNT - 1);

        double x = a_panelX + (a_panelW - totalW) / 2.0;

        for (int i = 0; i < BTN_COUNT; i++) {
            std::string btnName = "_actionBtn" + std::to_string(i);
            double btnW = WIDTHS[i];
            s_btnX[i] = x;

            RE::GFxValue args[2];
            args[0].SetString(btnName.c_str());
            args[1].SetNumber(static_cast<double>(300 + i));
            root.Invoke("createEmptyMovieClip", &s_btns[i], args, 2);
            if (s_btns[i].IsUndefined()) continue;

            RE::GFxValue posX, posY;
            posX.SetNumber(x);
            posY.SetNumber(a_barY);
            s_btns[i].SetMember("_x", posX);
            s_btns[i].SetMember("_y", posY);

            // Background child clip
            RE::GFxValue bgClip;
            RE::GFxValue bgArgs[2];
            bgArgs[0].SetString("_bg"); bgArgs[1].SetNumber(1.0);
            s_btns[i].Invoke("createEmptyMovieClip", &bgClip, bgArgs, 2);
            if (!bgClip.IsUndefined()) {
                uint32_t color = (a_focused && a_selectedIndex == i) ? COLOR_SELECT : COLOR_NORMAL;
                int alpha = (a_focused && a_selectedIndex == i) ? ALPHA_SELECT : ALPHA_NORMAL;
                DrawBtnRect(bgClip, i, color, alpha);
            }

            // Label text field
            RE::GFxValue tfArgs[6];
            tfArgs[0].SetString("_label"); tfArgs[1].SetNumber(10.0);
            tfArgs[2].SetNumber(0.0); tfArgs[3].SetNumber(4.0);
            tfArgs[4].SetNumber(btnW); tfArgs[5].SetNumber(BTN_H - 4.0);
            s_btns[i].Invoke("createTextField", nullptr, tfArgs, 6);

            std::string labelPath = "_root." + btnName + "._label";
            ScaleformUtil::SetTextFieldFormat(s_movie, labelPath, 13, 0xCCCCCC);

            // Center-align
            RE::GFxValue tf;
            s_movie->GetVariable(&tf, labelPath.c_str());
            if (!tf.IsUndefined()) {
                RE::GFxValue alignFmt;
                s_movie->CreateObject(&alignFmt, "TextFormat");
                if (!alignFmt.IsUndefined()) {
                    RE::GFxValue alignVal;
                    alignVal.SetString("center");
                    alignFmt.SetMember("align", alignVal);
                    RE::GFxValue fmtArgs[1];
                    fmtArgs[0] = alignFmt;
                    tf.Invoke("setTextFormat", nullptr, fmtArgs, 1);
                    tf.Invoke("setNewTextFormat", nullptr, fmtArgs, 1);
                }
            }

            RE::GFxValue textVal;
            std::string label = T(LABEL_KEYS[i]);
            textVal.SetString(label.c_str());
            s_movie->SetVariable((labelPath + ".text").c_str(), textVal);

            x += btnW + BTN_GAP;
        }
    }

    void Update(bool a_focused, int a_selectedIndex,
                bool a_hoverActive, int a_hoverIndex) {
        for (int i = 0; i < BTN_COUNT; i++) {
            if (s_btns[i].IsUndefined()) continue;
            if (s_flashActive && s_flashIndex == i) continue;

            RE::GFxValue bgClip;
            s_btns[i].GetMember("_bg", &bgClip);
            if (bgClip.IsUndefined()) continue;

            uint32_t color = COLOR_NORMAL;
            int alpha = ALPHA_NORMAL;
            if (a_focused && a_selectedIndex == i) {
                color = COLOR_SELECT;
                alpha = ALPHA_SELECT;
            } else if (a_hoverActive && a_hoverIndex == i) {
                color = COLOR_HOVER;
                alpha = ALPHA_HOVER;
            }

            DrawBtnRect(bgClip, i, color, alpha);
        }
    }

    void Destroy() {
        s_flashActive = false;
        s_defaultsHolding = false;
        s_whooshHolding = false;
        s_movie = nullptr;
        for (auto& btn : s_btns) btn = RE::GFxValue();
    }

    // --- Defaults hold ---

    void StartDefaultsHold() {
        s_defaultsHolding = true;
        s_defaultsHoldStart = std::chrono::steady_clock::now();
        DrawHoldProgress(BTN_DEFAULTS, 0.0f, COLOR_DEFAULTS_FILL, ALPHA_DEFAULTS_FILL);
        logger::debug("Defaults: hold started");
    }

    void UpdateDefaultsHold() {
        if (!s_defaultsHolding) return;

        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - s_defaultsHoldStart).count();
        float ratio = std::clamp(elapsed / HOLD_ANIM_DURATION, 0.0f, 1.0f);

        DrawHoldProgress(BTN_DEFAULTS, ratio, COLOR_DEFAULTS_FILL, ALPHA_DEFAULTS_FILL);

        if (ratio >= 1.0f) {
            s_defaultsHolding = false;
            ClearHoldProgress(BTN_DEFAULTS);
            logger::info("Defaults: hold completed");
        }
    }

    void CancelDefaultsHold() {
        if (s_defaultsHolding) {
            s_defaultsHolding = false;
            ClearHoldProgress(BTN_DEFAULTS);
            logger::debug("Defaults: hold cancelled");
        }
    }

    bool IsDefaultsHolding() {
        return s_defaultsHolding;
    }

    // --- Whoosh hold ---

    void StartWhooshHold(int a_btnIndex) {
        s_whooshHolding = true;
        s_whooshHoldStart = std::chrono::steady_clock::now();
        s_whooshHoldIndex = a_btnIndex;
        DrawHoldProgress(a_btnIndex, 0.0f, COLOR_WHOOSH_FILL, ALPHA_WHOOSH_FILL);
        logger::debug("Whoosh: hold started (btn {})", a_btnIndex);
    }

    void UpdateWhooshHold() {
        if (!s_whooshHolding) return;

        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - s_whooshHoldStart).count();

        if (elapsed < HOLD_DEAD_ZONE) return;  // still in dead zone

        float ratio = std::clamp((elapsed - HOLD_DEAD_ZONE) / HOLD_ANIM_DURATION, 0.0f, 1.0f);
        DrawHoldProgress(s_whooshHoldIndex, ratio, COLOR_WHOOSH_FILL, ALPHA_WHOOSH_FILL);

        if (ratio >= 1.0f) {
            s_whooshHolding = false;
            ClearHoldProgress(s_whooshHoldIndex);
            logger::info("Whoosh: hold completed — reconfig");
        }
    }

    void ReleaseWhooshHold() {
        if (!s_whooshHolding) return;
        s_whooshHolding = false;
        ClearHoldProgress(s_whooshHoldIndex);
    }

    void CancelWhooshHold() {
        if (s_whooshHolding) {
            s_whooshHolding = false;
            ClearHoldProgress(s_whooshHoldIndex);
            logger::debug("Whoosh: hold cancelled");
        }
    }

    bool IsWhooshHolding() {
        return s_whooshHolding;
    }

    bool IsWhooshPastDeadZone() {
        if (!s_whooshHolding) return false;
        float elapsed = std::chrono::duration<float>(
            std::chrono::steady_clock::now() - s_whooshHoldStart).count();
        return elapsed >= HOLD_DEAD_ZONE;
    }

    // --- Flash ---

    void FlashButton(int a_index) {
        s_flashActive = true;
        s_flashIndex = a_index;
        s_flashStart = std::chrono::steady_clock::now();

        if (a_index >= 0 && a_index < BTN_COUNT && !s_btns[a_index].IsUndefined()) {
            RE::GFxValue bgClip;
            s_btns[a_index].GetMember("_bg", &bgClip);
            if (!bgClip.IsUndefined()) {
                DrawBtnRect(bgClip, a_index, COLOR_FLASH, ALPHA_FLASH);
            }
        }
    }

    void UpdateFlash() {
        if (!s_flashActive) return;
        float elapsed = std::chrono::duration<float>(
            std::chrono::steady_clock::now() - s_flashStart).count();
        if (elapsed >= FLASH_DURATION) {
            s_flashActive = false;
            s_flashIndex = -1;
            // Caller must call Update() to restore normal highlight
        }
    }

    // --- Guide text ---

    std::string GetGuideText(int a_selectedIndex) {
        switch (a_selectedIndex) {
            case BTN_WHOOSH:   return T("$SLID_GuideWhoosh");
            case BTN_SORT:     return T("$SLID_GuideSort");
            case BTN_SWEEP:    return T("$SLID_GuideSweep");
            case BTN_DEFAULTS: return T("$SLID_GuideDefaults");
            case BTN_CLOSE:    return T("$SLID_GuideClose");
            default:           return "";
        }
    }

    // --- Mouse hit-testing ---

    int HitTest(float a_mx, float a_my) {
        if (a_my < s_barY || a_my > s_barY + BTN_H) return -1;
        for (int i = 0; i < BTN_COUNT; i++) {
            if (a_mx >= s_btnX[i] && a_mx <= s_btnX[i] + WIDTHS[i]) return i;
        }
        return -1;
    }

    double GetButtonX(int a_index) {
        if (a_index < 0 || a_index >= BTN_COUNT) return 0.0;
        return s_btnX[a_index];
    }

    double GetButtonW(int a_index) {
        if (a_index < 0 || a_index >= BTN_COUNT) return 0.0;
        return WIDTHS[a_index];
    }

    double GetBarY() {
        return s_barY;
    }
}
