#pragma once

#include "ChecklistGrid.h"
#include "DirectionalInput.h"
#include "ScaleformUtil.h"

#include <functional>
#include <unordered_set>

namespace WhooshConfig {
    constexpr std::string_view MENU_NAME = "SLIDWhooshConfigMenu";
    constexpr std::string_view FILE_NAME = "SLIDConfig";  // reuses same font-only SWF

    // Grid auto-expansion — grows columns before resorting to scroll
    constexpr int    MIN_COLS    = 3;
    constexpr int    MAX_COLS    = 6;
    constexpr double MAX_GRID_H  = 462.0;   // max grid px height before adding a column
    constexpr double COL_W       = 176.0;   // per-column width (matches ChecklistGrid default)
    constexpr double GRID_PAD    = 60.0;    // horizontal: left(24) + right(24) + scrollbar margin(12)

    // Colors — popup chrome and buttons (grid colors live in ChecklistGrid::Config)
    constexpr uint32_t COLOR_BG           = 0x0A0A0A;
    constexpr uint32_t COLOR_BORDER       = 0x666666;
    constexpr uint32_t COLOR_TITLE        = 0xFFFFFF;
    constexpr uint32_t COLOR_SUBTITLE     = 0x888888;
    constexpr uint32_t COLOR_GUIDE        = 0x888888;
    constexpr uint32_t COLOR_BTN_NORMAL   = 0x1A1A1A;
    constexpr uint32_t COLOR_BTN_SELECT   = 0x444444;
    constexpr uint32_t COLOR_BTN_HOVER    = 0x2A2A2A;
    constexpr uint32_t COLOR_BTN_LABEL    = 0xCCCCCC;
    constexpr int ALPHA_DIM        = 50;
    constexpr int ALPHA_BG         = 95;
    constexpr int ALPHA_BTN_NORMAL = 70;
    constexpr int ALPHA_BTN_SELECT = 90;
    constexpr int ALPHA_BTN_HOVER  = 80;

    // Button layout
    constexpr int BTN_COUNT = 4;  // OK, Default, Clear, Cancel
    constexpr double BTN_W = 100.0;
    constexpr double BTN_H = 28.0;
    constexpr double BTN_GAP = 10.0;

    class Menu : public RE::IMenu {
    public:
        static void Register();
        static RE::IMenu* Create();

        Menu();
        ~Menu() override = default;

        void PostCreate() override;
        RE::UI_MESSAGE_RESULTS ProcessMessage(RE::UIMessage& a_message) override;

        // Show with initial filter set and callback
        using Callback = std::function<void(bool, std::unordered_set<std::string>)>;  // (confirmed, filterIDs)
        static void Show(const std::unordered_set<std::string>& a_initialSet, Callback a_callback);
        static void Hide();
        static bool IsOpen();

        // Input actions
        static void NavigateUp();
        static void NavigateDown();
        static void NavigateLeft();
        static void NavigateRight();
        static void ToggleCheck();
        static void Confirm();      // OK
        static void Cancel();
        static void SetDefault();
        static void ClearAll();

        // Mouse
        static void OnMouseMove();
        static void OnMouseDown();

    private:
        static Callback s_callback;
        static std::unordered_set<std::string> s_initialSet;

        // Grid state: which filters are enabled
        std::unordered_set<std::string> m_enabledFilters;

        // ChecklistGrid component
        ChecklistGrid::Grid m_grid;

        // Build grid items from filter registry
        std::vector<ChecklistGrid::Item> BuildGridItems() const;

        // Navigation
        bool m_inGrid = false;    // start on button bar with OK selected
        int m_btnIndex = 0;       // 0=OK, 1=Default, 2=Clear, 3=Cancel

        // Mouse hover (buttons only — grid hover handled by m_grid)
        int m_hoverBtnIndex = -1;

        // Drawing
        void DrawPopup();
        void DrawGuideText();
        void DrawButtons();
        void UpdateButtons();
        void UpdateGuideText();
        // Hit testing
        std::pair<float, float> GetMousePos() const;

        // Cached geometry
        double m_popupX = 0.0;
        double m_popupY = 0.0;
        double m_popupW = 0.0;
        double m_popupH = 0.0;
        double m_gridStartX = 0.0;
        double m_gridStartY = 0.0;
        double m_btnStartX = 0.0;
        double m_btnY = 0.0;
        double m_guideY = 0.0;
        int    m_overlayDepth = 300;  // base depth for guide/buttons (above grid)
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

        // Directional input with repeat
        DirectionalInput::ThumbstickState m_thumbState;
        DirectionalInput::RepeatState m_repeatState;
    };
}
