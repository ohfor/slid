#pragma once

#include "ScaleformUtil.h"

#include <functional>

namespace TagInputMenu {
    constexpr std::string_view MENU_NAME = "SLIDTagInputMenu";
    constexpr std::string_view FILE_NAME = "SLIDConfig";  // reuses same font-only SWF

    // Layout (compact popup)
    constexpr double POPUP_W = 400.0;
    constexpr double POPUP_H = 130.0;
    constexpr double INPUT_H = 28.0;
    constexpr int MAX_CHARS = 40;

    // Colors
    constexpr uint32_t COLOR_BG           = 0x0A0A0A;
    constexpr uint32_t COLOR_BORDER       = 0x666666;
    constexpr uint32_t COLOR_TITLE        = 0xFFFFFF;
    constexpr uint32_t COLOR_HINT         = 0x777777;
    constexpr uint32_t COLOR_INPUT_BG     = 0x1A1A1A;
    constexpr uint32_t COLOR_INPUT_BORDER = 0x888888;
    constexpr uint32_t COLOR_INPUT_TEXT   = 0xFFFFFF;
    constexpr uint32_t COLOR_CURSOR       = 0xFFFFFF;
    constexpr uint32_t COLOR_SELECT_BG    = 0x264F78;
    constexpr uint32_t COLOR_BTN_NORMAL   = 0x1A1A1A;
    constexpr uint32_t COLOR_BTN_SELECT   = 0x444444;
    constexpr uint32_t COLOR_BTN_HOVER    = 0x2A2A2A;
    constexpr uint32_t COLOR_BTN_LABEL    = 0xCCCCCC;
    constexpr int ALPHA_DIM        = 50;
    constexpr int ALPHA_BG         = 95;
    constexpr int ALPHA_INPUT      = 90;
    constexpr int ALPHA_SELECT     = 80;
    constexpr int ALPHA_BTN_NORMAL = 70;
    constexpr int ALPHA_BTN_SELECT = 90;
    constexpr int ALPHA_BTN_HOVER  = 80;

    // Button layout
    constexpr double BTN_ACCEPT_W = 120.0;
    constexpr double BTN_CANCEL_W = 100.0;
    constexpr double BTN_H        = 28.0;
    constexpr double BTN_GAP      = 12.0;

    // Cursor blink
    constexpr float CURSOR_BLINK_INTERVAL = 0.53f;  // seconds
    constexpr double CURSOR_WIDTH = 1.5;

    // Double-click detection
    constexpr float DOUBLE_CLICK_TIME = 0.4f;  // seconds

    // Fallback char width if textWidth measurement fails
    constexpr double FALLBACK_CHAR_WIDTH = 8.5;

    class Menu : public RE::IMenu {
    public:
        static void Register();
        static RE::IMenu* Create();

        Menu();
        ~Menu() override;

        void PostCreate() override;
        RE::UI_MESSAGE_RESULTS ProcessMessage(RE::UIMessage& a_message) override;

        // Open the tag-input popup for a container
        static void Show(RE::FormID a_formID, const std::string& a_defaultName, bool a_isRename);

        // Open with a custom commit callback (bypasses TagContainer)
        using CommitCallback = std::function<void(const std::string&)>;
        static void ShowWithCallback(const std::string& a_title, const std::string& a_defaultName,
                                     CommitCallback a_callback);

        static void Hide();
        static bool IsOpen();

        // Input actions (called from InputHandler)
        static void Confirm();
        static void Cancel();
        static void HandleChar(std::uint32_t a_charCode);
        static void HandleBackspace();
        static void HandleDelete();
        static void HandleArrowLeft(bool a_shift);
        static void HandleArrowRight(bool a_shift);
        static void HandleHome(bool a_shift);
        static void HandleEnd(bool a_shift);
        static void HandleSelectAll();

        // Mouse actions
        static void OnMouseDown();
        static void OnMouseUp();
        static void OnMouseMove();
        static void OnDoubleClick();

    private:
        // Pending tag state (statics survive across menu instances)
        static RE::FormID  s_pendingFormID;
        static std::string s_defaultName;
        static bool        s_isRename;
        static std::string    s_titleOverride;   // custom title (empty = default)
        static CommitCallback s_commitCallback;   // custom commit (null = TagContainer)

        // Text buffer and selection (statics survive across instances)
        static std::string s_currentText;
        static int         s_selStart;
        static int         s_selEnd;
        static bool        s_allSelected;

        // Selection helpers
        static int  SelMin();
        static int  SelMax();
        static bool HasSelection();
        static void DeleteSelection();
        static void ClearSelection();  // collapse selection to s_selEnd

        // Cursor + selection visuals
        void UpdateCursorPosition();
        void UpdateSelectionHighlight();
        static void ResetCursorBlink();
        int  XToCharPos(double a_screenX);
        double MeasureTextWidth(int a_charCount);

        // Instance state (reset each open)
        bool m_cursorVisible = true;
        std::chrono::steady_clock::time_point m_cursorBlinkTime;
        bool m_mouseDown = false;
        double m_inputFieldX = 0.0;
        double m_inputFieldY = 0.0;
        double m_inputFieldW = 0.0;

        // Button hover state
        int m_hoverButton = -1;  // -1=none, 0=accept, 1=cancel
        double m_btnAcceptX = 0.0;
        double m_btnCancelX = 0.0;
        double m_btnY = 0.0;

        // Double-click detection
        std::chrono::steady_clock::time_point m_lastClickTime;

        // AllowTextInput ownership — only decrement if we successfully incremented
        bool m_ownedTextInput = false;

        // textWidth measurement availability
        bool m_textWidthWorks = true;
        bool m_textWidthTested = false;

        // Drawing
        void DrawPopup();
        void CreateInputField(const char* a_name, int a_depth, double a_x, double a_y,
                             double a_w, double a_h, const char* a_defaultText,
                             int a_size, uint32_t a_color);
        void DrawButtons();
        static void UpdateButtonVisual(int a_btnIndex, uint32_t a_color, int a_alpha);
        static void UpdateButtonHover();
        std::pair<float, float> GetMousePos() const;
        bool HitTestButton(float a_mx, float a_my, int a_btnIndex) const;
        bool HitTestInputField(float a_mx, float a_my) const;
        static void UpdateTextField();
        void CommitTag(const std::string& a_name);
    };

    // Dedicated input handler — only active when TagInputMenu is open
    class InputHandler : public RE::BSTEventSink<RE::InputEvent*> {
    public:
        static InputHandler* GetSingleton();
        static void Register();

        RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event,
                                               RE::BSTEventSource<RE::InputEvent*>* a_source) override;

    private:
        InputHandler() = default;
        InputHandler(const InputHandler&) = delete;
        InputHandler& operator=(const InputHandler&) = delete;

        static bool s_shiftHeld;
    };
}
