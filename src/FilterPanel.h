#pragma once

#include "FilterRow.h"
#include "MenuLayout.h"
#include "Network.h"
#include <functional>
#include <set>
#include <unordered_map>

namespace FilterPanel {

    using namespace MenuLayout;

    // FilterPanel-private constants
    constexpr int MAX_ROWS = 14;
    constexpr float DRAG_SCROLL_INTERVAL = 0.2f;
    constexpr float DRAG_START_DELAY = 0.2f;
    constexpr float DOUBLE_CLICK_TIME = 0.4f;
    constexpr float ANIM_DURATION = 0.35f;
    constexpr double LIFT_SCALE = 102.0;
    constexpr double LIFT_SHADOW_OFFSET = 4.0;
    constexpr uint32_t LIFT_SHADOW_COLOR = 0x000000;
    constexpr int LIFT_SHADOW_ALPHA = 30;
    constexpr int LIFT_DEPTH = 300;
    constexpr int DEFAULTS_ACTION_INDEX = 3;

    // Display row — maps a visible line to a family row and child index
    struct DisplayRow {
        int familyIndex = -1;   // index into s_filterRows
        int childIndex = -1;    // -1 = root, 0..N = child within family
    };

    // Row slide animation state
    struct RowAnim {
        bool active = false;
        double startY = 0.0;
        double endY = 0.0;
        std::chrono::steady_clock::time_point startTime;
    };

    // Hit-test zones for mouse interaction
    enum class HitZone {
        kNone,
        kFilterRow,
        kScrollTrack,
        kChestIcon,
        kAddRow,
        kDropdown
    };

    // Sub-focus state within a row (keyboard/gamepad)
    enum class SubFocus { kNone, kDropdown };

    // --- Focus signals ---
    enum class FocusSignal { kNone, kToActionBar, kFromActionBar };

    // --- Callbacks from orchestrator ---
    struct Callbacks {
        std::function<void()> hideMenu;
        std::function<void(const std::string&)> showMenu;
        std::function<void()> resetRepeat;
        std::function<void()> recalcPredictions;
        std::function<void()> buildStagesFromNetwork;
        std::function<void()> runSort;
        std::function<void()> runWhoosh;
        std::function<RE::FormID()> getCatchAllFormID;
        std::function<void()> saveOrchestratorFocus;
        std::function<void(RE::FormID)> setBypass;
    };

    // --- Lifecycle ---

    void Init(RE::GFxMovieView* a_movie, const Callbacks& a_callbacks);
    void Destroy();
    void Draw();         // initial row creation, scrollbar, add row
    bool Update();       // per-frame: animations, count flash, hold-A, hold-remove. Returns true if predictions were recalculated.

    // --- Save/Restore ---
    void SaveState();
    void RestoreState();

    // --- Navigation ---
    FocusSignal SelectPrev();
    FocusSignal SelectNext();
    void SelectLast();   // Focus the last row (add row) — used when transitioning from catch-all
    void ClearSelection();  // Remove selection highlight (when focus leaves panel)
    void ToggleLift();
    void ToggleExpand();     // expand/collapse family (on root rows with children)
    void SelectChest();
    void OpenLinkedContainer();
    void OnContainerClosed();

    // --- Sub-focus ---
    bool IsSubFocused();
    void EnterRow();           // Right: set kDropdown, auto-expand family
    void ExitRow();            // Left/ESC: clear to kNone
    void CollapseRow();        // Left (not sub-focused): collapse expanded root, or jump to parent
    void ActivateSubFocus();   // A/Enter/Down: open container dropdown
    void TabToNextChild();     // Tab: next child's dropdown or exit

    // --- Queries ---
    bool IsReordering();
    int  GetSelectedIndex();      // display row index
    int  GetFilterCount();        // number of family rows (FilterRow objects)
    int  GetDisplayRowCount();    // total display rows (roots + visible children + add row)
    bool IsOnAddRow();
    bool IsOnFilterRow();         // on any filter display row (root or child)
    bool IsOnChildRow();          // on a child display row
    bool IsOnExpandableRoot();    // on a root with children
    bool SelectedRowNeedsHold();
    bool SelectedRowIsFilter();

    // --- Hold mechanics ---
    bool IsHoldingA();
    void StartHoldA();
    void CancelHoldA();
    void StartHoldRemove();

    // --- Mouse ---
    enum class MouseSignal {
        kNone,
        kFocusToPanel      // clicked a row (unfocuses action bar)
    };
    struct MouseResult {
        MouseSignal signal = MouseSignal::kNone;
        int index = -1;
    };

    void OnMouseMove();
    MouseResult OnMouseDown();
    void OnMouseUp();
    void OnRightClick();
    void OnScrollWheel(int a_direction);
    HitZone HitTestMouse(float a_mx, float a_my, int& outIndex);
    void UpdateHover(float a_mx, float a_my);
    void ClearHover();
    bool GetHoverActive();
    std::pair<float, float> GetMousePos();

    // --- Data loading (from orchestrator) ---
    void LoadStages(std::vector<FilterRow::Data> a_stages);
    void SetPredictions(const std::vector<int>& a_filterCounts,
                        const std::vector<int>& a_contestedCounts,
                        const std::vector<std::unordered_map<size_t, int32_t>>& a_contestedByMaps,
                        int a_originCount);
    void ClearPredictions();
    void RefreshAfterSort(const std::set<int>& a_flashIndices);
    void RefreshAfterWhoosh();
    void BuildDefaultsAndCommit();
    void CommitToNetwork();

    // --- Data accessors (for orchestrator) ---
    std::vector<FilterStage> BuildFilterStages();
    const std::vector<FilterRow>& GetFilterRows();

    // --- Guide text ---
    std::string GetGuideText();

    // --- For InputHandler ---
    void ResetInputRepeat();

    // --- Save/restore statics (for menu reopen) ---
    bool IsPendingReopen();
    int  GetSavedActionIndex();
    int  GetSavedFocusTarget();  // returns int cast of FocusTarget
    void SaveOrchestratorFocus(int a_focusTarget, int a_actionIndex);

    // --- Origin panel data ---
    int GetPredictedOriginCount();
    int GetCurrentOriginCount();

}
