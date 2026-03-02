#include "ConfirmDialog.h"
#include "ScaleformUtil.h"

namespace ConfirmDialog {

    // --- Module state ---
    static RE::GFxMovieView* s_movie   = nullptr;
    static Config            s_config;
    static Callback          s_callback;
    static bool              s_open     = false;
    static int               s_selectedIndex = 1;
    static int               s_hoverIndex    = -1;

    // Cached popup geometry
    static double s_popupX = 0.0;
    static double s_popupY = 0.0;
    static double s_popupH = 0.0;

    // ButtonBar instance
    static ButtonBar s_bar;

    // Layout constants (internal)
    static constexpr double POPUP_H_2BTN = 94.0;
    static constexpr double POPUP_H_MULTI = 114.0;   // taller for 3+ buttons (2-line title)
    static constexpr double BTN_W_2BTN   = 80.0;
    static constexpr double BTN_W_MULTI  = 110.0;    // wider for longer button labels
    static constexpr double BTN_H      = 26.0;
    static constexpr double BTN_Y_OFF_2BTN  = 54.0;  // button Y offset from popup top
    static constexpr double BTN_Y_OFF_MULTI = 72.0;  // more room for 2-line title
    static constexpr double TITLE_PAD  = 12.0;
    static constexpr double TITLE_H_2BTN  = 28.0;
    static constexpr double TITLE_H_MULTI = 48.0;    // taller for 2-line titles

    // Colors (shared with SLIDMenu palette)
    static constexpr uint32_t COLOR_BG        = 0x0A0A0A;
    static constexpr uint32_t COLOR_BORDER    = 0x666666;
    static constexpr int ALPHA_DIM            = 50;

    // Scaleform clip names
    static constexpr const char* CLIP_DIM    = "_cdDim";
    static constexpr const char* CLIP_BG     = "_cdBg";
    static constexpr const char* CLIP_BORDER = "_cdBorder";
    static constexpr const char* CLIP_TITLE  = "_cdTitle";

    // --- Forward declarations ---
    static void Draw();
    static void Destroy();
    static void Redraw();

    // --- Helpers ---

    static int ButtonCount() {
        return static_cast<int>(s_config.buttons.size());
    }

    static bool IsMultiButton() {
        return ButtonCount() > 2;
    }

    static double GetBtnW() {
        return IsMultiButton() ? BTN_W_MULTI : BTN_W_2BTN;
    }

    static double GetBtnYOff() {
        return IsMultiButton() ? BTN_Y_OFF_MULTI : BTN_Y_OFF_2BTN;
    }

    static double BtnY() {
        return s_popupY + GetBtnYOff();
    }

    // --- Public API ---

    void Show(RE::GFxMovieView* a_movie, const Config& a_config, Callback a_callback) {
        if (s_open) {
            Close(ButtonCount() - 1);
        }

        s_movie    = a_movie;
        s_config   = a_config;
        s_callback = std::move(a_callback);
        s_open     = true;
        s_selectedIndex = a_config.defaultIndex;
        s_hoverIndex    = -1;

        s_popupH = IsMultiButton() ? POPUP_H_MULTI : POPUP_H_2BTN;

        // Center in the SLIDMenu panel area (240,60 — 800x600)
        s_popupX = 240.0 + (800.0 - s_config.popupW) / 2.0;
        s_popupY = 60.0 + (600.0 - s_popupH) / 2.0;

        Draw();
    }

    void Close(int a_selectedIndex) {
        if (!s_open) return;

        s_open = false;
        Destroy();

        // Move callback to local before invoking (callback may re-enter Show)
        auto cb = std::move(s_callback);
        s_callback = nullptr;
        s_movie    = nullptr;

        if (cb) {
            cb(a_selectedIndex);
        }
    }

    bool IsOpen() {
        return s_open;
    }

    void NavigateLeft() {
        if (!s_open) return;
        if (s_selectedIndex > 0) {
            s_selectedIndex--;
            Redraw();
        }
    }

    void NavigateRight() {
        if (!s_open) return;
        int maxIdx = ButtonCount() - 1;
        if (s_selectedIndex < maxIdx) {
            s_selectedIndex++;
            Redraw();
        }
    }

    void Confirm() {
        if (!s_open) return;
        Close(s_selectedIndex);
    }

    void Cancel() {
        if (!s_open) return;
        Close(ButtonCount() - 1);  // last button = cancel by convention
    }

    int HitTest(float a_mx, float a_my) {
        if (!s_open) return -1;
        return s_bar.HitTest(a_mx, a_my);
    }

    void UpdateHover(int a_btnIndex) {
        if (!s_open) return;
        if (a_btnIndex == s_hoverIndex) return;
        s_hoverIndex = a_btnIndex;
        Redraw();
    }

    double GetPopupX() { return s_popupX; }
    double GetPopupY() { return s_popupY; }
    double GetPopupW() { return s_config.popupW; }
    double GetPopupH() { return s_popupH; }

    // --- Internal: Draw the full popup ---

    static void Draw() {
        if (!s_movie) return;

        int count = ButtonCount();
        double btnW = GetBtnW();

        // Dim overlay
        ScaleformUtil::DrawFilledRect(s_movie, CLIP_DIM, 500,
            0.0, 0.0, 1280.0, 720.0, 0x000000, ALPHA_DIM);

        // Popup background and border
        ScaleformUtil::DrawFilledRect(s_movie, CLIP_BG, 501,
            s_popupX, s_popupY, s_config.popupW, s_popupH, COLOR_BG, 95);
        ScaleformUtil::DrawBorderRect(s_movie, CLIP_BORDER, 502,
            s_popupX, s_popupY, s_config.popupW, s_popupH, COLOR_BORDER);

        // Title (center-aligned, height depends on button count)
        double titleH = IsMultiButton() ? TITLE_H_MULTI : TITLE_H_2BTN;
        ScaleformUtil::CreateLabel(s_movie, CLIP_TITLE, 503,
            s_popupX + TITLE_PAD, s_popupY + 10.0,
            s_config.popupW - 2 * TITLE_PAD, titleH,
            s_config.title.c_str(), 16, 0xFFFFFF);

        {
            std::string titlePath = std::string("_root.") + CLIP_TITLE;
            RE::GFxValue tf;
            s_movie->GetVariable(&tf, titlePath.c_str());
            if (!tf.IsUndefined()) {
                // Enable word wrap for multi-line titles
                RE::GFxValue wrapVal;
                wrapVal.SetBoolean(true);
                tf.SetMember("wordWrap", wrapVal);
                tf.SetMember("multiline", wrapVal);

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
        }

        // Build ButtonBar from dynamic button list
        std::vector<ButtonDef> defs;
        defs.reserve(count);
        for (int i = 0; i < count; ++i) {
            defs.push_back({s_config.buttons[i], btnW});
        }

        double btnY = BtnY();
        s_bar.Init(s_movie, "_cdBtn", 504, defs,
                   s_popupX + s_config.popupW / 2.0, btnY);

        Redraw();
    }

    // --- Internal: Redraw button highlights only ---

    static void Redraw() {
        s_bar.Draw(s_selectedIndex, s_hoverIndex);
    }

    // --- Internal: Destroy all Scaleform clips ---

    static void Destroy() {
        if (!s_movie) return;

        // Fixed clips
        const char* fixedClips[] = {CLIP_DIM, CLIP_BG, CLIP_BORDER};
        for (const char* name : fixedClips) {
            RE::GFxValue clip;
            s_movie->GetVariable(&clip, (std::string("_root.") + name).c_str());
            if (!clip.IsUndefined()) {
                clip.Invoke("removeMovieClip", nullptr, nullptr, 0);
            }
        }

        // Title TextField
        RE::GFxValue titleTF;
        s_movie->GetVariable(&titleTF, (std::string("_root.") + CLIP_TITLE).c_str());
        if (!titleTF.IsUndefined()) {
            titleTF.Invoke("removeTextField", nullptr, nullptr, 0);
        }

        // Button bar cleanup
        s_bar.Destroy();
    }
}
