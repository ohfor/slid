#pragma once

#include "ButtonBar.h"
#include "DirectionalInput.h"
#include "RestockCategory.h"
#include "ScaleformUtil.h"

#include <chrono>
#include <functional>
#include <string>
#include <vector>

namespace RestockConfig {
    constexpr std::string_view MENU_NAME = "SLIDRestockConfigMenu";
    constexpr std::string_view FILE_NAME = "SLIDConfig";  // reuses same font-only SWF

    // Layout
    constexpr double POPUP_W       = 720.0;
    constexpr double HEADER_H      = 66.0;
    constexpr double PANEL_H       = 400.0;
    constexpr double FOOTER_H      = 92.0;   // guide(20) + gap(12) + btn(28) + gap(12) + pad(20)
    constexpr double POPUP_H       = HEADER_H + PANEL_H + FOOTER_H;
    constexpr double BROWSER_W     = 340.0;
    constexpr double PAD_W         = 320.0;
    constexpr double PANEL_GAP     = 20.0;
    constexpr double SIDE_PAD      = 20.0;
    constexpr double ROW_H         = 22.0;

    // Pad quantity buttons
    constexpr double QTY_BTN_W     = 20.0;
    constexpr double QTY_NUM_W     = 32.0;
    constexpr double TRASH_W       = 20.0;   // trash icon hit zone width

    // Colors
    constexpr uint32_t COLOR_BG           = 0x0A0A0A;
    constexpr uint32_t COLOR_BORDER       = 0x666666;
    constexpr uint32_t COLOR_TITLE        = 0xFFFFFF;
    constexpr uint32_t COLOR_SUBTITLE     = 0x888888;
    constexpr uint32_t COLOR_GUIDE        = 0x888888;
    constexpr uint32_t COLOR_PANEL_BG     = 0x111111;
    constexpr uint32_t COLOR_PANEL_HEADER = 0xBBBBBB;
    constexpr uint32_t COLOR_HEADER_ROW   = 0xDDDDDD;
    constexpr uint32_t COLOR_ITEM         = 0xCCCCCC;
    constexpr uint32_t COLOR_ITEM_DIM     = 0x555555;
    constexpr uint32_t COLOR_CURSOR       = 0x444444;
    constexpr uint32_t COLOR_HOVER        = 0x333333;
    constexpr uint32_t COLOR_ADJUST       = 0x3A3A20;  // adjust mode row highlight (warm tint)
    constexpr uint32_t COLOR_QTY          = 0xAACC88;
    constexpr uint32_t COLOR_QTY_ADJUST   = 0xDDB866;  // gold qty in adjust mode
    constexpr uint32_t COLOR_QTY_BTN      = 0x888888;
    constexpr uint32_t COLOR_QTY_BTN_HOVER = 0xDDDDDD;
    constexpr uint32_t COLOR_PAD_EMPTY    = 0x555555;
    constexpr uint32_t COLOR_DIVIDER      = 0x444444;
    constexpr uint32_t COLOR_SCROLLTRACK  = 0x333333;
    constexpr uint32_t COLOR_SCROLLTHUMB  = 0x777777;
    constexpr uint32_t COLOR_TRASH        = 0x555555;
    constexpr uint32_t COLOR_TRASH_HOVER  = 0xCC4444;
    constexpr uint32_t COLOR_DELETE_FLASH = 0x882222;
    constexpr uint32_t COLOR_ACCENT      = 0xD4AF37;  // gold accent (matches ContextMenu)

    constexpr int ALPHA_DIM        = 50;
    constexpr int ALPHA_BG         = 95;
    constexpr int ALPHA_PANEL_BG   = 60;
    constexpr int ALPHA_CURSOR     = 60;
    constexpr int ALPHA_ADJUST     = 70;
    constexpr int ALPHA_HOVER      = 40;
    // Delete animation
    constexpr float DELETE_ANIM_SECS = 0.5f;

    // Focus targets
    enum class FocusTarget { kBrowser, kPad, kButtonBar };

    // Browser row (left panel)
    struct BrowserRow {
        std::string id;
        std::string label;
        bool isHeader = false;     // family root with children — non-selectable
        bool expanded = true;
        bool onPad = false;        // dimmed if already added to pad
        std::string parentID;      // for child rows — empty for headers/standalone
    };

    // Pad row (right panel)
    struct PadRow {
        std::string id;
        std::string label;
        uint16_t quantity = 1;
    };

    class Menu : public RE::IMenu {
    public:
        static void Register();
        static RE::IMenu* Create();

        Menu();
        ~Menu() override = default;

        void PostCreate() override;
        RE::UI_MESSAGE_RESULTS ProcessMessage(RE::UIMessage& a_message) override;

        // Show with initial config and callback
        using Callback = std::function<void(bool, RestockCategory::RestockConfig)>;
        static void Show(const RestockCategory::RestockConfig& a_initial, Callback a_callback);
        static void Hide();
        static bool IsOpen();

        // Input actions
        static void NavigateUp();
        static void NavigateDown();
        static void NavigateLeft();
        static void NavigateRight();
        static void Activate();       // Add to pad / toggle adjust mode / confirm button
        static void RemoveItem();     // Start delete animation on pad
        static void SwitchPanel();    // Tab / LB
        static void Confirm();
        static void Cancel();
        static void SetDefault();
        static void ClearAll();
        static void AdjustQty(int a_delta);  // direct qty adjust (for +/- keys)
        static bool IsAdjusting();           // true if in adjust mode on pad

        // Mouse
        static void OnMouseMove();
        static void OnMouseDown();
        static void OnMouseUp();
        static void OnMouseHeld();
        static void OnMouseRightClick();
        static void OnMouseScroll(int a_direction);

        // Input device tracking
        static void SetGamepadMode(bool a_gamepad);
        static void CancelButtonHold();

    private:
        static Callback s_callback;
        static RestockCategory::RestockConfig s_initialConfig;

        // Focus
        FocusTarget m_focus = FocusTarget::kBrowser;

        // Browser state
        std::vector<BrowserRow> m_browserRows;
        int m_browserCursor = 0;
        int m_browserScroll = 0;
        int m_browserHover = -1;

        // Pad state
        std::vector<PadRow> m_padRows;
        int m_padCursor = 0;
        int m_padScroll = 0;
        int m_padHover = -1;

        // Adjust mode — row is "lifted" for qty editing
        bool m_adjustMode = false;

        // Delete animation
        int  m_deleteRow = -1;  // row index being deleted, -1 = none
        std::chrono::steady_clock::time_point m_deleteStart;

        // Pad qty button hover: row index, and which button (false=minus, true=plus)
        int  m_hoverQtyRow = -1;
        bool m_hoverQtyIsPlus = false;

        // Trash icon hover
        int m_hoverTrashRow = -1;

        // Input device mode
        bool m_gamepad = false;

        // Button bar
        ButtonBar m_buttonBar;
        int m_btnIndex = 0;
        int m_hoverBtnIndex = -1;

        // Build browser rows from category taxonomy
        void BuildBrowserRows();
        void SyncBrowserOnPadState();
        void LoadFromConfig(const RestockCategory::RestockConfig& a_config);
        RestockCategory::RestockConfig AssembleConfig() const;

        // Browser navigation helpers
        int NextSelectableRow(int a_from, int a_dir) const;
        bool IsBrowserRowVisible(int a_index) const;
        std::string QualifiedLabel(const BrowserRow& a_row) const;

        // Pad helpers
        void AddToPad(const std::string& a_id, const std::string& a_label);
        void RemoveFromPad(int a_index);
        void StartDelete(int a_index);
        void FinishDelete();

        // Panel-specific navigation
        void BrowserUp();
        void BrowserDown();
        void BrowserActivate();
        void PadUp();
        void PadDown();
        void PadAdjustQty(int a_delta);

        // Visible row counts
        int BrowserVisibleRows() const;
        int PadVisibleRows() const;
        int BrowserMaxScroll() const;
        int PadMaxScroll() const;

        // Drawing
        void DrawAll();
        void DrawChrome();
        void DrawBrowser();
        void DrawPad();
        void DrawButtons();
        void DrawGuideText();
        void DrawScrollbar(const char* a_prefix, int a_depth, double a_x, double a_y,
                           double a_h, int a_totalRows, int a_visibleRows, int a_scrollOffset);
        void UpdateAll();
        void RemoveClip(const std::string& a_name);
        std::pair<float, float> GetMousePos() const;

        // Cached geometry
        double m_popupX = 0.0;
        double m_popupY = 0.0;
        double m_browserX = 0.0;
        double m_browserY = 0.0;
        double m_padX = 0.0;
        double m_padY = 0.0;
        double m_btnY = 0.0;
        double m_guideY = 0.0;
        int    m_baseDepth = 120;
    };

    // Input handler
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

        DirectionalInput::ThumbstickState m_thumbState;
        DirectionalInput::RepeatState m_repeatState;
        DirectionalInput::RepeatState m_adjustRepeat;     // L/R repeat in adjust mode
        DirectionalInput::RepeatState m_mouseQtyRepeat;   // mouse hold on +/-
    };
}
