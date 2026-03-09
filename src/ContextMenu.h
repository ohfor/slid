#pragma once

#include "ContextResolver.h"
#include "ScaleformUtil.h"
#include "DirectionalInput.h"

#include <chrono>
#include <functional>

namespace ContextMenu {

    using Callback = std::function<void(ContextResolver::Action, const std::string& networkName, RE::FormID containerFormID)>;

    // Layout (centered popup, 1280x720 canvas)
    constexpr double POPUP_W = 260.0;
    constexpr double ROW_H   = 28.0;
    constexpr double PAD     = 16.0;
    constexpr double TITLE_H = 26.0;
    constexpr double NETWORK_H = 22.0;
    constexpr double SEP_H    = 1.0;     // separator line
    constexpr double DESC_H   = 52.0;    // description area (3 lines at size 12)
    constexpr double DESC_PAD = 8.0;     // top padding inside description

    // Cursor indicator
    constexpr double CURSOR_W  = 4.0;
    constexpr double CURSOR_PAD = 6.0;

    // Colors
    constexpr uint32_t COLOR_BG           = 0x0A0A0A;
    constexpr uint32_t COLOR_BORDER       = 0x666666;
    constexpr uint32_t COLOR_TITLE        = 0xFFFFFF;
    constexpr uint32_t COLOR_NETWORK      = 0xD4AF37;
    constexpr uint32_t COLOR_CHEVRON      = 0x888888;
    constexpr uint32_t COLOR_ROW_NORMAL   = 0xCCCCCC;
    constexpr uint32_t COLOR_ROW_SELECTED = 0xFFFFFF;
    constexpr uint32_t COLOR_CURSOR       = 0xD4AF37;
    constexpr uint32_t COLOR_CURSOR_BG    = 0x1A1A1A;
    constexpr uint32_t COLOR_SEP          = 0x333333;
    constexpr uint32_t COLOR_DESC         = 0x999999;

    constexpr int ALPHA_BG       = 95;
    constexpr int ALPHA_CURSOR   = 50;
    constexpr int ALPHA_DIM      = 30;

    // Hold fill colors
    constexpr uint32_t COLOR_HOLD_BLUE   = 0x446688;
    constexpr uint32_t COLOR_HOLD_GREEN  = 0x448866;
    constexpr uint32_t COLOR_HOLD_RED    = 0x884444;
    constexpr uint32_t COLOR_HOLD_YELLOW = 0x886644;
    constexpr int      ALPHA_HOLD        = 80;
    constexpr float    HOLD_DEAD_ZONE    = 0.2f;
    constexpr float    HOLD_DURATION     = 1.0f;

    // Submenu layout
    constexpr double SUBMENU_W           = 220.0;
    constexpr double SUBMENU_GAP         = 4.0;
    constexpr double SUBMENU_ROW_H       = 28.0;
    constexpr double SUBMENU_PAD         = 12.0;
    constexpr int    SUBMENU_MAX_VISIBLE = 8;

    // Hold mechanic types
    enum class HoldType { kNone, kHoldConfirm, kHoldReconfigure };

    // Focus state for main vs submenu
    enum class FocusState { kMain, kSubMenu };

    struct SubMenuEntry {
        RE::FormID  formID;
        std::string name;
        bool        isMaster;
    };

    struct HoldConfig {
        uint32_t color;
        int      alpha;
        float    deadZone;    // seconds before fill starts (0 = immediate)
        float    duration;    // fill duration in seconds
    };

    class Menu : public RE::IMenu {
    public:
        static constexpr std::string_view MENU_NAME = "SLID_ContextMenu"sv;
        static constexpr std::string_view FILE_NAME = "SLIDConfig"sv;

        Menu();
        ~Menu() override = default;

        static void Register();
        static RE::IMenu* Create();

        static void Show(const ContextResolver::ResolvedContext& a_context, Callback a_callback);
        static void Hide();
        static bool IsOpen();

        RE::UI_MESSAGE_RESULTS ProcessMessage(RE::UIMessage& a_message) override;

        // Input actions
        static void CursorUp();
        static void CursorDown();
        static void CycleLeft();
        static void CycleRight();
        static void Confirm();
        static void Cancel();
        static void OnMouseMove();
        static void OnMouseClick();
        static void OnMouseDown();
        static void OnMouseUp();

        // Hold mechanic
        static HoldType   GetHoldType(ContextResolver::Action a_action);
        static HoldConfig GetHoldConfig(ContextResolver::Action a_action);
        static void BeginHold();
        static void UpdateHold();
        static void ReleaseHold();
        static void CancelHold();

    private:
        void BuildUI();
        void DrawBackground();
        void DrawTitle();
        void DrawNetworkSubtitle();
        void RedrawNetworkSubtitle();
        void DrawActionRows();
        void RedrawActionRows();
        void DrawDescription();
        void RedrawDescription();
        void DrawCursorHighlight();
        void UpdateCursorHighlight();
        void DrawHoldProgress(float a_ratio, uint32_t a_color, int a_alpha);
        void ClearHoldProgress();

        int  HitTestRow(float a_mx, float a_my) const;

        double PopupH() const;

        // Submenu
        void BuildSubMenuEntries();
        void DrawSubMenu();
        void HideSubMenu();
        void RedrawSubMenu();
        void SubMenuCursorUp();
        void SubMenuCursorDown();
        void EnterSubMenu();
        void ExitSubMenu();
        void ConfirmSubMenu();
        int  HitTestSubMenu(float a_mx, float a_my) const;
        bool IsOnOpenRow() const;
        void DrawChevron();
        void UpdateSubMenuVisibility();

        ContextResolver::ResolvedContext m_context;
        Callback m_callback;
        int  m_cursor = 0;
        int  m_hoverRow = -1;

        // Hold state
        bool   m_holding = false;
        int    m_holdRow = -1;
        std::chrono::steady_clock::time_point m_holdStart;

        // Cached geometry
        double m_popupX  = 0.0;
        double m_popupY  = 0.0;
        double m_rowsY   = 0.0;  // Y offset of first action row (relative to popup)

        // Submenu state
        FocusState m_focus = FocusState::kMain;
        std::vector<SubMenuEntry> m_subMenuEntries;
        int  m_subMenuCursor = 0;
        int  m_subMenuScroll = 0;
        bool m_subMenuVisible = false;
        double m_subMenuX = 0.0;
        double m_subMenuY = 0.0;

        RE::GFxValue m_root;

        friend class InputHandler;
    };

    class InputHandler : public RE::BSTEventSink<RE::InputEvent*> {
    public:
        static InputHandler* GetSingleton();
        static void Register();

        RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event,
                                               RE::BSTEventSource<RE::InputEvent*>* a_source) override;

    private:
        InputHandler() = default;

        DirectionalInput::ThumbstickState m_thumbState;
        DirectionalInput::RepeatState     m_repeatV;
        DirectionalInput::RepeatState     m_repeatH;
    };
}
