#pragma once

#include "NetworkManager.h"
#include "ScaleformUtil.h"

#include <array>
#include <chrono>

namespace SellOverview {
    constexpr std::string_view MENU_NAME = "SLIDSellOverviewMenu";
    constexpr std::string_view FILE_NAME = "SLIDConfig";  // reuses same font-only SWF

    // Layout (centered popup)
    constexpr double POPUP_W = 600.0;
    constexpr double POPUP_H = 600.0;

    // Stats area
    constexpr double STATS_Y = 60.0;       // Y offset within popup
    constexpr double STATS_H = 52.0;

    // Vendor schedule section (between stats and log)
    constexpr double VENDOR_SCHED_Y = STATS_Y + STATS_H + 4.0;
    constexpr double VENDOR_LINE_H  = 19.0;
    constexpr int    MAX_VENDOR_LINES = 4;
    constexpr double VENDOR_INFO_H  = 18.0;  // info line below vendor list
    // Dynamic height: heading(24) + lines(N*19) + infoline(18) + padding(6)
    // Computed at runtime based on vendor count

    // Colors for vendor schedule
    constexpr uint32_t COLOR_VENDOR_NAME     = 0xCCCCCC;
    constexpr uint32_t COLOR_VENDOR_STORE    = 0x888888;
    constexpr uint32_t COLOR_VENDOR_TIMER    = 0xFFFFFF;
    constexpr uint32_t COLOR_VENDOR_INFO     = 0x999999;
    constexpr uint32_t COLOR_VENDOR_SELECTED = 0xFFCC44;  // gold for selected vendor name
    constexpr uint32_t COLOR_LOG_HIGHLIGHT   = 0xFFCC44;  // gold for highlighted log rows
    constexpr uint32_t COLOR_LOG_FLASH       = 0xFFFFFF;  // white flash on initial selection

    // Transaction log
    constexpr double LOG_HEADING_H  = 24.0;
    constexpr double LOG_HEADER_H   = 28.0;
    constexpr double LOG_ROW_H      = 22.0;   // run header row height
    constexpr double DETAIL_ROW_H   = 15.0;   // detail item row height (compact)
    constexpr double BTN_ZONE_TOP   = POPUP_H - 52.0;  // log must not cross this line

    // Expand icon (Windows-style [+]/[-] square on run headers)
    constexpr double EXPAND_ICON_SIZE = 12.0;
    constexpr double EXPAND_ICON_PAD  = 5.0;   // gap between icon and vendor text
    constexpr double VENDOR_INDENT    = EXPAND_ICON_SIZE + EXPAND_ICON_PAD + 1.0;  // ~18

    // Column widths (within log area) — total = 560 (POPUP_W - 40 margins)
    // Run headers:  [+] Vendor (spans vendor+item)  Qty  —  Total  Time
    // Detail rows:  —   Item                        Qty  Price  Total  —
    constexpr double LOG_COL_VENDOR_X = VENDOR_INDENT;
    constexpr double LOG_COL_VENDOR_W = 112.0;
    constexpr double LOG_COL_ITEM_X   = 130.0;
    constexpr double LOG_COL_ITEM_W   = 180.0;
    constexpr double LOG_COL_QTY_X    = 310.0;
    constexpr double LOG_COL_QTY_W    = 40.0;
    constexpr double LOG_COL_PRICE_X  = 350.0;
    constexpr double LOG_COL_PRICE_W  = 40.0;
    constexpr double LOG_COL_TOTAL_X  = 390.0;
    constexpr double LOG_COL_TOTAL_W  = 55.0;
    constexpr double LOG_COL_TIME_X   = 460.0;
    constexpr double LOG_COL_TIME_W   = 100.0;

    // Colors
    constexpr uint32_t COLOR_BG             = 0x0A0A0A;
    constexpr uint32_t COLOR_BORDER         = 0x666666;
    constexpr uint32_t COLOR_TITLE          = 0xFFCC44;  // gold title
    constexpr uint32_t COLOR_STAT_LABEL     = 0x888888;
    constexpr uint32_t COLOR_STAT_VALUE     = 0xFFFFFF;
    constexpr uint32_t COLOR_HEADER         = 0x888888;
    constexpr uint32_t COLOR_EMPTY          = 0x555555;
    constexpr uint32_t COLOR_BTN_NORMAL     = 0x1A1A1A;
    constexpr uint32_t COLOR_BTN_SELECT     = 0x444444;
    constexpr uint32_t COLOR_BTN_HOVER      = 0x2A2A2A;
    constexpr uint32_t COLOR_BTN_LABEL      = 0xCCCCCC;
    constexpr uint32_t COLOR_RUN_HEADER     = 0xCCCCCC;
    constexpr uint32_t COLOR_RUN_EXPANDED   = 0xDDDDDD;
    constexpr uint32_t COLOR_RUN_DETAIL     = 0x999999;
    constexpr uint32_t COLOR_RUN_PREFIX     = 0x888888;
    constexpr uint32_t COLOR_HEADING        = 0xAAAAAA;
    constexpr uint32_t COLOR_CURSOR_BG       = 0x222222;
    constexpr uint32_t COLOR_EXPAND_BG       = 0x151515;
    constexpr uint32_t COLOR_EXPAND_BORDER   = 0x555555;
    constexpr uint32_t COLOR_EXPAND_SYMBOL   = 0xBBBBBB;
    constexpr uint32_t COLOR_SCROLLBAR_TRACK = 0x333333;
    constexpr uint32_t COLOR_SCROLLBAR_THUMB = 0x777777;
    constexpr int ALPHA_DIM          = 50;
    constexpr int ALPHA_BG           = 95;
    constexpr int ALPHA_BTN_NORMAL   = 70;
    constexpr int ALPHA_BTN_SELECT   = 90;
    constexpr int ALPHA_BTN_HOVER    = 80;
    constexpr int ALPHA_TRACK        = 80;
    constexpr int ALPHA_CURSOR       = 40;

    // Close button
    constexpr double BTN_W = 100.0;
    constexpr double BTN_H = 28.0;

    // Scrollbar
    constexpr double SCROLLBAR_W          = 4.0;
    constexpr double SCROLLBAR_MIN_THUMB  = 20.0;
    constexpr double SCROLLBAR_RIGHT_PAD  = 6.0;

    // Run grouping (local to menu display)
    struct TransactionRun {
        std::string vendorName;
        std::string vendorAssortment;
        float       gameTime = 0.0f;
        int32_t     totalItems = 0;
        int32_t     totalGold = 0;
        std::vector<const SaleTransaction*> items;
        bool        expanded = false;
    };

    enum class RowType { kRunHeader, kDetailItem };

    struct VisibleRow {
        RowType type;
        int     runIndex;
        int     itemIndex;  // only for kDetailItem
    };

    // Vendor schedule entry (stored for navigation)
    struct VendorScheduleEntry {
        std::string  name;
        std::string  store;
        float        remainingHours;
        RE::FormID   factionFormID;  // 0 for general vendor
        bool         isGeneral;
        bool         invested;
    };

    // Focus zones for cursor navigation
    enum class FocusZone { kVendorSchedule, kTransactionLog };

    class Menu : public RE::IMenu {
    public:
        static void Register();
        static RE::IMenu* Create();

        Menu();
        ~Menu() override = default;

        void PostCreate() override;
        RE::UI_MESSAGE_RESULTS ProcessMessage(RE::UIMessage& a_message) override;

        static void Show();
        static void Hide();
        static bool IsOpen();

        // Input actions
        static void ScrollUp();
        static void ScrollDown();
        static void CursorUp();
        static void CursorDown();
        static void ActivateRow();
        static void Close();

        // Mouse
        static void OnMouseMove();
        static void OnMouseDown();

    private:
        void DrawPopup();
        void DrawStats();
        void DrawVendorSchedule();
        void RedrawVendorSchedule();
        void UpdateVendorTimers();
        void DrawVendorInfo();
        void DrawLogHeading();
        void DrawLogHeader();
        void DrawLogRows();
        void UpdateLogRows();
        void DrawCloseButton();
        void UpdateCloseButton();
        void DrawScrollbar();
        void UpdateScrollbar();
        void DrawCursorHighlight();

        void BuildRuns();
        void BuildVisibleRows();
        void BuildVendorEntries();
        int  TotalFlattenedRows() const;
        void EnsureCursorVisible();
        int  HitTestLogRow(float a_mx, float a_my) const;
        int  HitTestVendorRow(float a_mx, float a_my) const;

        std::pair<float, float> GetMousePos() const;

        // Run data
        std::vector<TransactionRun> m_runs;
        std::vector<VisibleRow>     m_visibleRows;

        int m_logScrollOffset = 0;
        int m_selectedRow     = 0;   // cursor in flattened row space
        int m_hoverButton     = -1;  // -1=none, 0=close
        bool m_usingCursor    = false; // true when gamepad/keyboard navigating

        // Cached geometry
        double m_popupX    = 0.0;
        double m_popupY    = 0.0;
        double m_logAreaY  = 0.0;  // Y where log rows start (after column headers)
        double m_logAreaH  = 0.0;  // available height for log rows
        double m_btnX      = 0.0;
        double m_btnY      = 0.0;

        // Vendor schedule
        bool m_hasVendorSchedule = false;
        int  m_vendorScheduleCount = 0;
        double m_vendorSchedH = 0.0;  // computed height of vendor schedule section
        std::vector<VendorScheduleEntry> m_vendorEntries;
        std::array<std::string, MAX_VENDOR_LINES> m_cachedTimerTexts;

        // Vendor schedule geometry (for hit testing)
        double m_vendorRowsY = 0.0;  // Y of first vendor row

        // Focus zone navigation
        FocusZone m_focusZone       = FocusZone::kTransactionLog;
        int       m_vendorCursorIdx = -1;  // -1 = none selected
        std::string m_highlightVendorName;  // vendor name for log highlighting
        int       m_vendorFlashFrames = 0;  // countdown for flash effect

        // Live timer simulation (game time is frozen while menu is open)
        std::chrono::steady_clock::time_point m_menuOpenTime;
        float  m_gameHoursAtOpen = 0.0f;  // Calendar hours when menu opened
        float  m_timeScale       = 20.0f; // cached game timescale

        // Live sales detection
        size_t m_lastLogSize   = 0;   // track transaction log size for live updates
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

        bool m_thumbUp   = false;
        bool m_thumbDown = false;
    };
}
