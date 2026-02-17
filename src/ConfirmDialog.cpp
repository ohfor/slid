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

    // Dynamic button positions (computed in Draw)
    static std::vector<double> s_btnXPositions;

    // Layout constants (internal)
    static constexpr double POPUP_H_2BTN = 94.0;
    static constexpr double POPUP_H_MULTI = 114.0;   // taller for 3+ buttons (2-line title)
    static constexpr double BTN_W_2BTN   = 80.0;
    static constexpr double BTN_W_MULTI  = 110.0;    // wider for longer button labels
    static constexpr double BTN_H      = 26.0;
    static constexpr double BTN_Y_OFF_2BTN  = 54.0;  // button Y offset from popup top
    static constexpr double BTN_Y_OFF_MULTI = 72.0;  // more room for 2-line title
    static constexpr double BTN_GAP    = 12.0;
    static constexpr double TITLE_PAD  = 12.0;
    static constexpr double TITLE_H_2BTN  = 28.0;
    static constexpr double TITLE_H_MULTI = 48.0;    // taller for 2-line titles

    // Colors (shared with SLIDMenu palette)
    static constexpr uint32_t COLOR_BG        = 0x0A0A0A;
    static constexpr uint32_t COLOR_BORDER    = 0x666666;
    static constexpr uint32_t COLOR_BTN_NORM  = 0x1A1A1A;
    static constexpr uint32_t COLOR_BTN_SEL   = 0x444444;
    static constexpr uint32_t COLOR_BTN_HOVER = 0x2A2A2A;
    static constexpr int ALPHA_DIM            = 50;
    static constexpr int ALPHA_BTN_NORM       = 70;
    static constexpr int ALPHA_BTN_SEL        = 90;
    static constexpr int ALPHA_BTN_HOVER      = 80;

    // Scaleform clip names
    static constexpr const char* CLIP_DIM    = "_cdDim";
    static constexpr const char* CLIP_BG     = "_cdBg";
    static constexpr const char* CLIP_BORDER = "_cdBorder";
    static constexpr const char* CLIP_TITLE  = "_cdTitle";
    // Buttons use dynamic names: _cdBtn0, _cdBtn1, etc.

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

    static std::string BtnClipName(int a_index) {
        return std::string("_cdBtn") + std::to_string(a_index);
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

        double btnY = BtnY();
        if (a_my < btnY || a_my > btnY + BTN_H) return -1;

        double btnW = GetBtnW();
        int count = ButtonCount();
        for (int i = 0; i < count; i++) {
            if (i < static_cast<int>(s_btnXPositions.size())) {
                double bx = s_btnXPositions[i];
                if (a_mx >= bx && a_mx <= bx + btnW) return i;
            }
        }

        return -1;
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

        RE::GFxValue root;
        s_movie->GetVariable(&root, "_root");
        if (root.IsUndefined()) return;

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

        // Compute button X positions (centered as a group)
        double totalBtnW = count * btnW + (count - 1) * BTN_GAP;
        double startX = s_popupX + (s_config.popupW - totalBtnW) / 2.0;
        s_btnXPositions.clear();
        s_btnXPositions.reserve(count);
        for (int i = 0; i < count; i++) {
            s_btnXPositions.push_back(startX + i * (btnW + BTN_GAP));
        }

        double btnY = BtnY();

        // Create button clips
        for (int i = 0; i < count; i++) {
            std::string clipName = BtnClipName(i);
            int depth = 504 + i;

            RE::GFxValue clip;
            RE::GFxValue args[2];
            args[0].SetString(clipName.c_str());
            args[1].SetNumber(static_cast<double>(depth));
            root.Invoke("createEmptyMovieClip", &clip, args, 2);
            if (clip.IsUndefined()) continue;

            RE::GFxValue px, py;
            px.SetNumber(s_btnXPositions[i]); py.SetNumber(btnY);
            clip.SetMember("_x", px);
            clip.SetMember("_y", py);

            // Background child clip
            RE::GFxValue bg;
            RE::GFxValue bgArgs[2];
            bgArgs[0].SetString("_bg"); bgArgs[1].SetNumber(1.0);
            clip.Invoke("createEmptyMovieClip", &bg, bgArgs, 2);

            // Label text field
            RE::GFxValue tfArgs[6];
            tfArgs[0].SetString("_label"); tfArgs[1].SetNumber(10.0);
            tfArgs[2].SetNumber(0.0); tfArgs[3].SetNumber(3.0);
            tfArgs[4].SetNumber(btnW); tfArgs[5].SetNumber(BTN_H - 3.0);
            clip.Invoke("createTextField", nullptr, tfArgs, 6);

            std::string labelPath = std::string("_root.") + clipName + "._label";
            ScaleformUtil::SetTextFieldFormat(s_movie, labelPath, 14, 0xCCCCCC);

            // Center-align label
            RE::GFxValue labelTF;
            s_movie->GetVariable(&labelTF, labelPath.c_str());
            if (!labelTF.IsUndefined()) {
                RE::GFxValue alignFmt;
                s_movie->CreateObject(&alignFmt, "TextFormat");
                if (!alignFmt.IsUndefined()) {
                    RE::GFxValue alignVal;
                    alignVal.SetString("center");
                    alignFmt.SetMember("align", alignVal);
                    RE::GFxValue fmtArgs[1];
                    fmtArgs[0] = alignFmt;
                    labelTF.Invoke("setTextFormat", nullptr, fmtArgs, 1);
                    labelTF.Invoke("setNewTextFormat", nullptr, fmtArgs, 1);
                }
            }

            RE::GFxValue textVal;
            textVal.SetString(s_config.buttons[i].c_str());
            s_movie->SetVariable((labelPath + ".text").c_str(), textVal);
        }

        Redraw();
    }

    // --- Internal: Redraw button highlights only ---

    static void Redraw() {
        if (!s_movie) return;

        int count = ButtonCount();
        double btnW = GetBtnW();

        for (int i = 0; i < count; i++) {
            std::string clipPath = std::string("_root.") + BtnClipName(i);
            RE::GFxValue clip;
            s_movie->GetVariable(&clip, clipPath.c_str());
            if (clip.IsUndefined()) continue;

            RE::GFxValue bg;
            clip.GetMember("_bg", &bg);
            if (bg.IsUndefined()) continue;

            bg.Invoke("clear", nullptr, nullptr, 0);

            uint32_t color = COLOR_BTN_NORM;
            int alpha = ALPHA_BTN_NORM;
            if (i == s_selectedIndex) {
                color = COLOR_BTN_SEL;
                alpha = ALPHA_BTN_SEL;
            } else if (i == s_hoverIndex) {
                color = COLOR_BTN_HOVER;
                alpha = ALPHA_BTN_HOVER;
            }

            RE::GFxValue fillArgs[2];
            fillArgs[0].SetNumber(static_cast<double>(color));
            fillArgs[1].SetNumber(static_cast<double>(alpha));
            bg.Invoke("beginFill", nullptr, fillArgs, 2);

            RE::GFxValue pt[2];
            pt[0].SetNumber(0.0); pt[1].SetNumber(0.0);
            bg.Invoke("moveTo", nullptr, pt, 2);
            pt[0].SetNumber(btnW);
            bg.Invoke("lineTo", nullptr, pt, 2);
            pt[1].SetNumber(BTN_H);
            bg.Invoke("lineTo", nullptr, pt, 2);
            pt[0].SetNumber(0.0);
            bg.Invoke("lineTo", nullptr, pt, 2);
            pt[1].SetNumber(0.0);
            bg.Invoke("lineTo", nullptr, pt, 2);

            bg.Invoke("endFill", nullptr, nullptr, 0);
        }
    }

    // --- Internal: Destroy all Scaleform clips ---

    static void Destroy() {
        if (!s_movie) return;

        RE::GFxValue root;
        s_movie->GetVariable(&root, "_root");

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

        // Dynamic button clips
        int count = ButtonCount();
        for (int i = 0; i < count; i++) {
            std::string clipPath = std::string("_root.") + BtnClipName(i);
            RE::GFxValue clip;
            s_movie->GetVariable(&clip, clipPath.c_str());
            if (!clip.IsUndefined()) {
                clip.Invoke("removeMovieClip", nullptr, nullptr, 0);
            }
        }

        s_btnXPositions.clear();
    }
}
