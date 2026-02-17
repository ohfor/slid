#include "FilterPanel.h"
#include "ConfigState.h"
#include "ConfirmDialog.h"
#include "ContainerScanner.h"
#include "Distributor.h"
#include "FilterRegistry.h"
#include "FilterRow.h"
#include "HoldRemove.h"
#include "NetworkManager.h"
#include "ScaleformUtil.h"
#include "TranslationService.h"

namespace FilterPanel {

    // --- Static module state ---

    static RE::GFxMovieView* s_movie = nullptr;

    static std::vector<FilterRow> s_filterRows;       // family-level model (one per root)
    static std::vector<DisplayRow> s_displayRows;     // flattened display list (roots + expanded children)
    static int s_selectedIndex = -1;                  // index into s_displayRows (or == displayRows.size() for add row)
    static int s_scrollOffset = 0;                    // index into s_displayRows
    static bool s_isReordering = false;
    static int s_liftedFamilyIndex = -1;              // index into s_filterRows of the lifted family
    static bool s_rowsReady = false;

    static RE::GFxValue s_slotClips[TOTAL_ROW_SLOTS];
    static RE::GFxValue s_addRow;
    static RE::GFxValue s_scrollThumb;
    static RE::GFxValue s_scrollTrack;

    static RowAnim s_rowAnims[MAX_VISIBLE_ROWS] = {};
    static bool s_slotLifted[MAX_VISIBLE_ROWS] = {};

    static bool s_countFlashActive = false;
    static std::chrono::steady_clock::time_point s_countFlashStart;
    static std::set<int> s_countFlashIndices;         // display row indices

    static bool s_dragActive = false;
    static bool s_dragPending = false;
    static int s_dragSourceIndex = -1;                // display row index
    static int s_dragPendingIndex = -1;               // display row index
    static float s_dragStartX = 0.0f;
    static float s_dragStartY = 0.0f;
    static float s_dragOffsetY = 0.0f;               // mouse offset from row top at drag start
    static bool s_dragPendingExpandable = false;      // pending drag was on an expandable root
    static std::chrono::steady_clock::time_point s_dragStartTime;
    static std::chrono::steady_clock::time_point s_lastDragScroll;

    static std::chrono::steady_clock::time_point s_lastClickTime;
    static int s_lastClickIndex = -1;

    static bool s_holdAActive = false;
    static std::chrono::steady_clock::time_point s_holdAStart;

    static bool s_hoverActive = false;
    static int s_hoverIndex = -1;                     // display row index
    static int s_hoverChestIndex = -1;                // display row index

    static int s_predictedOriginCount = -1;

    // Contest data — per flat pipeline index
    static std::vector<std::unordered_map<size_t, int32_t>> s_contestedByMaps;
    // Mapping from flat pipeline index → display row index (rebuilt in BuildDisplayRows)
    static std::unordered_map<int, int> s_flatIndexToDisplayRow;
    // Set of display row indices to accent (rows stealing from focused row)
    static std::set<int> s_contestAccentSet;

    // --- Contest count animation ---
    constexpr float CONTEST_ANIM_DURATION  = 1.5f;   // seconds to count up/down
    constexpr float CONTEST_FLASH_DURATION = 0.2f;   // brief hold at zero before fade
    constexpr float CONTEST_FADE_DURATION  = 0.6f;   // fade out after reaching zero
    constexpr uint32_t CONTEST_FLASH_COLOR = 0xFFEE88;  // bright amber for zero-out flash

    struct ContestAnim {
        int target = 0;
        float display = 0.0f;
        float startDisplay = 0.0f;
        std::chrono::steady_clock::time_point startTime;
        bool counting = false;

        enum Phase { kNormal, kFlash, kFade, kDone };
        Phase phase = kDone;
        std::chrono::steady_clock::time_point phaseStart;
    };

    static std::unordered_map<std::string, ContestAnim> s_contestAnims;

    static SubFocus s_subFocus = SubFocus::kNone;
    static SubFocus s_savedSubFocus = SubFocus::kNone;
    static bool s_autoExpanded = false;  // true if EnterRow auto-expanded a family
    static std::set<std::string> s_savedExpandedIDs;  // filterIDs of expanded families (for container browse)

    static bool s_pendingReopen = false;
    static bool s_deferredRecalc = false;
    static int s_savedScrollOffset = 0;
    static int s_savedSelectedIndex = -1;
    static int s_savedFocusTarget = 2;   // default: kActionBar (=2)
    static int s_savedActionIndex = 1;

    static Callbacks s_callbacks;

    // --- Forward declarations of internal helpers ---
    static void InitRows();
    static void InitAddRow();
    static void UpdateAddRow();
    static void CreateSlotTextFields(RE::GFxValue& a_slot, int a_index);
    static void BuildDisplayRows();
    static void PopulateList();
    static void SetSlotVisible(int a_index, bool a_visible);
    static void UpdateSelection(int a_oldIndex, int a_newIndex);
    static void DrawScrollbar();
    static void UpdateScrollbar();
    // (BuildStagesFromNetwork is in orchestrator)
    static void BuildDefaultFilters();
    static void UpdateOriginRow();
    static FilterRow::DropdownContext MakeDropdownContext();
    static void BeginAddFilter();
    static FilterRow::DropdownContext HandleSetupRefresh(int a_familyIndex);
    static void HandleSetupCancelled(int a_familyIndex);
    static void HandleContainerResult(int a_familyIndex, int a_childIndex, bool a_confirmed);
    static void HandleRemoveRequest(int a_familyIndex);
    static HoldRemove::Callback MakeRemoveCallback();
    static RE::GFxValue* GetSlotClipForDisplayIndex(int a_displayIndex);
    static std::string GetSlotClipPath(int a_slotIndex);
    static void StartRowAnim(int a_slot, double a_fromY, double a_toY);
    static void UpdateContestTarget(const std::string& filterID, int rawCount);
    static bool TickContestAnimations();
    static void ApplyContestAnimations();
    static void ClearAllLiftVisuals();
    static void ApplyLiftVisualToFamily();
    static void RelocateLiftVisual();
    static void AnimateDisplacedFamily(int a_displacedFamilyIndex, double a_deltaY);
    static void UpdateRowAnimations();
    static void SnapAllAnimations();
    static void ApplyLiftVisual(int a_slot);
    static void ClearLiftVisual(int a_slot);
    static void UpdateCountFlash();
    static void DrawHoldAProgress(float a_ratio);
    static void ClearHoldAProgress();

    // --- Helper: resolve display row to container FormID ---
    static RE::FormID GetDisplayRowContainerFormID(int a_displayIndex) {
        if (a_displayIndex < 0 || a_displayIndex >= static_cast<int>(s_displayRows.size()))
            return 0;
        const auto& dr = s_displayRows[a_displayIndex];
        if (dr.familyIndex < 0 || dr.familyIndex >= static_cast<int>(s_filterRows.size()))
            return 0;
        const auto& family = s_filterRows[dr.familyIndex];
        if (dr.childIndex < 0) {
            return family.GetData().containerFormID;
        } else {
            const auto& children = family.GetChildren();
            if (dr.childIndex < static_cast<int>(children.size()))
                return children[dr.childIndex].containerFormID;
        }
        return 0;
    }

    // --- Helper: resolve display row to a display name ---
    static std::string GetDisplayRowName(int a_displayIndex) {
        if (a_displayIndex < 0 || a_displayIndex >= static_cast<int>(s_displayRows.size()))
            return "";
        const auto& dr = s_displayRows[a_displayIndex];
        if (dr.familyIndex < 0 || dr.familyIndex >= static_cast<int>(s_filterRows.size()))
            return "";
        const auto& family = s_filterRows[dr.familyIndex];
        if (dr.childIndex < 0) {
            return family.GetData().name;
        } else {
            const auto& children = family.GetChildren();
            if (dr.childIndex < static_cast<int>(children.size()))
                return children[dr.childIndex].name;
        }
        return "";
    }

    // --- Helper: find the display row index for a family root ---
    static int FindDisplayIndexForFamily(int a_familyIndex) {
        for (int i = 0; i < static_cast<int>(s_displayRows.size()); ++i) {
            if (s_displayRows[i].familyIndex == a_familyIndex && s_displayRows[i].childIndex == -1)
                return i;
        }
        return -1;
    }

    // --- Helper: resolve display row to flat pipeline index ---
    // Returns the flat pipeline index for a given display row, or -1 if invalid.
    // Flat order matches ToFilterStages: children-before-root per family.
    static int GetFlatIndexForDisplayRow(int a_displayIndex) {
        if (a_displayIndex < 0 || a_displayIndex >= static_cast<int>(s_displayRows.size()))
            return -1;
        const auto& dr = s_displayRows[a_displayIndex];
        if (dr.familyIndex < 0 || dr.familyIndex >= static_cast<int>(s_filterRows.size()))
            return -1;

        // Sum all stages from families before this one
        int flatIdx = 0;
        for (int fi = 0; fi < dr.familyIndex; ++fi) {
            flatIdx += static_cast<int>(s_filterRows[fi].GetChildren().size()) + 1;  // children + root
        }

        if (dr.childIndex < 0) {
            // Root: after all children of this family
            flatIdx += static_cast<int>(s_filterRows[dr.familyIndex].GetChildren().size());
        } else {
            // Child: at childIndex offset
            flatIdx += dr.childIndex;
        }
        return flatIdx;
    }

    // --- Helper: rebuild contest accent set for the currently focused row ---
    // --- Contest animation helpers ---

    static void UpdateContestTarget(const std::string& filterID, int rawCount) {
        auto& anim = s_contestAnims[filterID];
        if (rawCount != anim.target) {
            anim.startDisplay = anim.display;
            anim.startTime = std::chrono::steady_clock::now();
            anim.target = rawCount;
            anim.counting = true;
            if (rawCount > 0) {
                anim.phase = ContestAnim::kNormal;
            }
            // If target becomes 0, keep counting down; phase transition on completion
        }
        // If this is a new entry and target > 0, ensure it's visible
        if (rawCount > 0 && anim.phase == ContestAnim::kDone) {
            anim.phase = ContestAnim::kNormal;
        }
    }

    static bool TickContestAnimations() {
        bool anyActive = false;
        auto now = std::chrono::steady_clock::now();

        for (auto& [id, anim] : s_contestAnims) {
            if (anim.counting) {
                float elapsed = std::chrono::duration<float>(now - anim.startTime).count();
                float t = std::min(elapsed / CONTEST_ANIM_DURATION, 1.0f);
                // Ease-out: t' = 1 - (1-t)^2
                float eased = 1.0f - (1.0f - t) * (1.0f - t);
                anim.display = anim.startDisplay + (static_cast<float>(anim.target) - anim.startDisplay) * eased;

                if (t >= 1.0f) {
                    anim.display = static_cast<float>(anim.target);
                    anim.counting = false;
                    // Transition to flash when reaching zero from a positive value
                    if (anim.target == 0 && anim.phase == ContestAnim::kNormal) {
                        anim.phase = ContestAnim::kFlash;
                        anim.phaseStart = now;
                    }
                }
                anyActive = true;
            }

            if (anim.phase == ContestAnim::kFlash) {
                float elapsed = std::chrono::duration<float>(now - anim.phaseStart).count();
                if (elapsed >= CONTEST_FLASH_DURATION) {
                    anim.phase = ContestAnim::kFade;
                    anim.phaseStart = now;
                }
                anyActive = true;
            }

            if (anim.phase == ContestAnim::kFade) {
                float elapsed = std::chrono::duration<float>(now - anim.phaseStart).count();
                if (elapsed >= CONTEST_FADE_DURATION) {
                    anim.phase = ContestAnim::kDone;
                }
                anyActive = true;
            }
        }

        return anyActive;
    }

    static int ComputeContestAlpha(const ContestAnim& anim) {
        switch (anim.phase) {
            case ContestAnim::kNormal: return 100;
            case ContestAnim::kFlash: return 100;
            case ContestAnim::kFade: {
                auto now = std::chrono::steady_clock::now();
                float elapsed = std::chrono::duration<float>(now - anim.phaseStart).count();
                float t = std::min(elapsed / CONTEST_FADE_DURATION, 1.0f);
                return static_cast<int>(100.0f * (1.0f - t));
            }
            case ContestAnim::kDone: return 0;
        }
        return 0;
    }

    static uint32_t ComputeContestColor(const ContestAnim& anim) {
        if (anim.phase == ContestAnim::kFlash) return CONTEST_FLASH_COLOR;
        return 0;  // use default COLOR_CONTEST
    }

    static void ApplyContestAnimations() {
        for (auto& row : s_filterRows) {
            auto it = s_contestAnims.find(row.GetData().filterID);
            if (it != s_contestAnims.end()) {
                auto& anim = it->second;
                row.MutableData().contestedCount = std::max(0, static_cast<int>(std::round(anim.display)));
                row.MutableData().contestAlpha = ComputeContestAlpha(anim);
                row.MutableData().contestColor = ComputeContestColor(anim);
            } else {
                row.MutableData().contestedCount = 0;
                row.MutableData().contestAlpha = 0;
                row.MutableData().contestColor = 0;
            }
            for (auto& child : row.MutableChildren()) {
                auto cit = s_contestAnims.find(child.filterID);
                if (cit != s_contestAnims.end()) {
                    auto& anim = cit->second;
                    child.contestedCount = std::max(0, static_cast<int>(std::round(anim.display)));
                    child.contestAlpha = ComputeContestAlpha(anim);
                    child.contestColor = ComputeContestColor(anim);
                } else {
                    child.contestedCount = 0;
                    child.contestAlpha = 0;
                    child.contestColor = 0;
                }
            }
        }
    }

    static void RebuildContestAccentSet() {
        s_contestAccentSet.clear();
        if (s_contestedByMaps.empty()) return;

        int displayCount = static_cast<int>(s_displayRows.size());
        if (s_selectedIndex < 0 || s_selectedIndex >= displayCount) return;

        int flatIdx = GetFlatIndexForDisplayRow(s_selectedIndex);
        if (flatIdx < 0 || flatIdx >= static_cast<int>(s_contestedByMaps.size())) return;

        const auto& contestMap = s_contestedByMaps[flatIdx];
        for (const auto& [stealerFlatIdx, count] : contestMap) {
            if (count <= 0) continue;
            auto it = s_flatIndexToDisplayRow.find(static_cast<int>(stealerFlatIdx));
            if (it != s_flatIndexToDisplayRow.end() && it->second >= 0) {
                s_contestAccentSet.insert(it->second);
            }
        }
    }

    // --- Build the flattened display row list from s_filterRows ---
    static void BuildDisplayRows() {
        s_displayRows.clear();
        s_flatIndexToDisplayRow.clear();

        // Build display rows and flat-pipeline-index → display-row mapping.
        // Flat pipeline order matches ToFilterStages: children-before-root per family.
        int flatIdx = 0;
        for (int fi = 0; fi < static_cast<int>(s_filterRows.size()); ++fi) {
            const auto& family = s_filterRows[fi];

            // Children first in flat order
            int childCount = static_cast<int>(family.GetChildren().size());
            for (int ci = 0; ci < childCount; ++ci) {
                // Display row only exists if family is expanded
                // but we always track flat index for mapping
                s_flatIndexToDisplayRow[flatIdx] = -1;  // default: not visible
                ++flatIdx;
            }

            // Root in flat order
            s_flatIndexToDisplayRow[flatIdx] = -1;
            ++flatIdx;

            // Root display row
            s_displayRows.push_back({fi, -1});

            // Expanded children display rows
            if (family.IsExpanded() && family.HasChildren()) {
                const auto& children = family.GetChildren();
                for (int ci = 0; ci < static_cast<int>(children.size()); ++ci) {
                    s_displayRows.push_back({fi, ci});
                }
            }
        }

        // Second pass: map flat indices to actual display row indices.
        // Walk display rows and correlate with flat order.
        // Flat order: for each family, children first then root.
        // Display order: root first, then expanded children.
        flatIdx = 0;
        for (int fi = 0; fi < static_cast<int>(s_filterRows.size()); ++fi) {
            const auto& family = s_filterRows[fi];
            int childCount = static_cast<int>(family.GetChildren().size());

            // Find root's display index
            int rootDisplayIdx = -1;
            for (int di = 0; di < static_cast<int>(s_displayRows.size()); ++di) {
                if (s_displayRows[di].familyIndex == fi && s_displayRows[di].childIndex == -1) {
                    rootDisplayIdx = di;
                    break;
                }
            }

            // Children (flat order)
            for (int ci = 0; ci < childCount; ++ci) {
                // Find this child's display index (if expanded)
                int childDisplayIdx = -1;
                for (int di = 0; di < static_cast<int>(s_displayRows.size()); ++di) {
                    if (s_displayRows[di].familyIndex == fi && s_displayRows[di].childIndex == ci) {
                        childDisplayIdx = di;
                        break;
                    }
                }
                s_flatIndexToDisplayRow[flatIdx] = childDisplayIdx >= 0 ? childDisplayIdx : rootDisplayIdx;
                ++flatIdx;
            }

            // Root (flat order)
            s_flatIndexToDisplayRow[flatIdx] = rootDisplayIdx;
            ++flatIdx;
        }
    }

    // --- Public API: Lifecycle ---

    void Init(RE::GFxMovieView* a_movie, const Callbacks& a_callbacks) {
        s_movie = a_movie;
        s_callbacks = a_callbacks;
        ClearPredictions();
        s_callbacks.buildStagesFromNetwork();
    }

    void Destroy() {
        s_subFocus = SubFocus::kNone;
        SnapAllAnimations();
        HoldRemove::Destroy();
        // Destroy any open dropdown (owned by FilterRow instances)
        if (auto* dd = Dropdown::GetOpen()) dd->Destroy();

        s_rowsReady = false;
        s_movie = nullptr;
        for (auto& clip : s_slotClips) clip = RE::GFxValue();
        s_addRow = RE::GFxValue();
        s_scrollThumb = RE::GFxValue();
        s_scrollTrack = RE::GFxValue();
        s_contestedByMaps.clear();
        s_flatIndexToDisplayRow.clear();
        s_contestAccentSet.clear();
        s_contestAnims.clear();
    }

    void Draw() {
        InitRows();
        PopulateList();
        DrawScrollbar();

        if (s_pendingReopen) {
            s_pendingReopen = false;
            RestoreState();
            PopulateList();
            logger::info("FilterPanel: restored state after container browse (scroll={}, sel={})",
                         s_scrollOffset, s_selectedIndex);
        }

        // Defer prediction recalc to first Update() tick -- safer than running
        // GetInventory on all containers during PostCreate
        if (!s_filterRows.empty()) {
            s_deferredRecalc = true;
        }
    }

    bool Update() {
        bool predictionsRecalculated = false;
        if (s_deferredRecalc) {
            s_deferredRecalc = false;
            s_callbacks.recalcPredictions();
            predictionsRecalculated = true;
        }
        UpdateCountFlash();
        UpdateRowAnimations();
        if (TickContestAnimations()) {
            PopulateList();
        }
        if (s_holdAActive) {
            auto now = std::chrono::steady_clock::now();
            float elapsed = std::chrono::duration<float>(now - s_holdAStart).count();
            if (elapsed >= HOLD_OPEN_DURATION) {
                s_holdAActive = false;
                ClearHoldAProgress();
                OpenLinkedContainer();
            } else if (elapsed >= HOLD_VISUAL_DELAY) {
                float ratio = (elapsed - HOLD_VISUAL_DELAY) / (HOLD_OPEN_DURATION - HOLD_VISUAL_DELAY);
                DrawHoldAProgress(ratio);
            }
        }
        HoldRemove::Update();
        return predictionsRecalculated;
    }

    // --- Public API: Save/Restore ---

    void SaveState() {
        s_savedScrollOffset = s_scrollOffset;
        s_savedSelectedIndex = s_selectedIndex;
        s_savedSubFocus = s_subFocus;
        s_savedExpandedIDs.clear();
        for (const auto& row : s_filterRows) {
            if (row.IsExpanded()) s_savedExpandedIDs.insert(row.GetData().filterID);
        }
        s_pendingReopen = true;
        if (s_callbacks.saveOrchestratorFocus) s_callbacks.saveOrchestratorFocus();
    }

    void RestoreState() {
        s_scrollOffset = s_savedScrollOffset;
        s_selectedIndex = s_savedSelectedIndex;
        s_subFocus = s_savedSubFocus;

        // Restore expanded state by filterID
        for (auto& row : s_filterRows) {
            if (s_savedExpandedIDs.count(row.GetData().filterID)) {
                row.SetExpanded(true);
            }
        }
        s_savedExpandedIDs.clear();

        BuildDisplayRows();
        int displayCount = static_cast<int>(s_displayRows.size());
        int totalCount = displayCount + 1;  // display rows + add row
        int maxOffset = std::max(0, displayCount - MAX_VISIBLE_ROWS);
        s_scrollOffset = std::clamp(s_scrollOffset, 0, maxOffset);
        if (s_selectedIndex >= totalCount) s_selectedIndex = totalCount - 1;
        if (s_selectedIndex < 0) s_selectedIndex = 0;
    }

    // --- Public API: Queries ---

    bool IsReordering() { return s_isReordering; }
    int  GetSelectedIndex() { return s_selectedIndex; }
    int  GetFilterCount() { return static_cast<int>(s_filterRows.size()); }

    int  GetDisplayRowCount() {
        return static_cast<int>(s_displayRows.size()) + 1;  // display rows + add row
    }

    bool IsOnAddRow() {
        return s_selectedIndex == static_cast<int>(s_displayRows.size());
    }

    bool IsOnFilterRow() {
        return s_selectedIndex >= 0 && s_selectedIndex < static_cast<int>(s_displayRows.size());
    }

    bool IsOnChildRow() {
        if (s_selectedIndex < 0 || s_selectedIndex >= static_cast<int>(s_displayRows.size()))
            return false;
        return s_displayRows[s_selectedIndex].childIndex >= 0;
    }

    bool IsOnExpandableRoot() {
        if (s_selectedIndex < 0 || s_selectedIndex >= static_cast<int>(s_displayRows.size()))
            return false;
        const auto& dr = s_displayRows[s_selectedIndex];
        if (dr.childIndex >= 0) return false;  // it's a child, not a root
        if (dr.familyIndex < 0 || dr.familyIndex >= static_cast<int>(s_filterRows.size()))
            return false;
        return s_filterRows[dr.familyIndex].HasChildren();
    }

    bool IsPendingReopen() { return s_pendingReopen; }
    int  GetSavedActionIndex() { return s_savedActionIndex; }
    int  GetSavedFocusTarget() { return s_savedFocusTarget; }
    void SaveOrchestratorFocus(int a_focusTarget, int a_actionIndex) {
        s_savedFocusTarget = a_focusTarget;
        s_savedActionIndex = a_actionIndex;
    }
    int GetPredictedOriginCount() { return s_predictedOriginCount; }
    int GetCurrentOriginCount() {
        int count = 0;
        auto* masterRef = RE::TESForm::LookupByID<RE::TESObjectREFR>(ConfigState::GetMasterFormID());
        if (masterRef) {
            auto inv = masterRef->GetInventory();
            for (auto& [item, data] : inv) {
                if (!item || data.first <= 0 || IsPhantomItem(item)) continue;
                count += data.first;
            }
        }
        return count;
    }

    bool SelectedRowNeedsHold() {
        if (s_isReordering) return false;
        if (s_selectedIndex < 0) return false;
        if (s_selectedIndex >= static_cast<int>(s_displayRows.size())) return false;

        return (GetDisplayRowContainerFormID(s_selectedIndex) != 0);
    }

    bool SelectedRowIsFilter() {
        if (s_isReordering) return false;
        return IsOnFilterRow();
    }

    // --- Public API: Hold mechanics ---

    bool IsHoldingA() { return s_holdAActive; }

    void StartHoldA() {
        s_holdAActive = true;
        s_holdAStart = std::chrono::steady_clock::now();
    }

    void CancelHoldA() {
        s_holdAActive = false;
        ClearHoldAProgress();
    }

    void StartHoldRemove() {
        if (s_isReordering) return;
        if (!IsOnFilterRow()) return;
        // Only allow remove on root rows
        if (IsOnChildRow()) return;

        int displayIdx = s_selectedIndex;
        RE::GFxValue* slotClip = GetSlotClipForDisplayIndex(displayIdx);
        if (!slotClip) return;

        int familyIdx = s_displayRows[displayIdx].familyIndex;
        HoldRemove::Start(s_movie, familyIdx,
                          slotClip, ROW_W, ROW_HEIGHT,
                          MakeRemoveCallback());
    }

    // --- Public API: Hover ---

    bool GetHoverActive() { return s_hoverActive; }

    std::pair<float, float> GetMousePos() {
        if (!s_movie) return {0.0f, 0.0f};
        RE::GFxValue xVal, yVal;
        s_movie->GetVariable(&xVal, "_root._xmouse");
        s_movie->GetVariable(&yVal, "_root._ymouse");
        float mx = xVal.IsNumber() ? static_cast<float>(xVal.GetNumber()) : 0.0f;
        float my = yVal.IsNumber() ? static_cast<float>(yVal.GetNumber()) : 0.0f;
        return {mx, my};
    }

    // --- Public API: Actions ---

    void BuildDefaultsAndCommit() {
        BuildDefaultFilters();
        CommitToNetwork();
        s_callbacks.recalcPredictions();
        s_selectedIndex = -1;
        s_scrollOffset = 0;
        s_isReordering = false;
        s_liftedFamilyIndex = -1;
        s_subFocus = SubFocus::kNone;
    }

    // --- Public API: Guide text ---

    std::string GetGuideText() {
        int displayCount = static_cast<int>(s_displayRows.size());

        // When mouse hover is active, show description for the hovered row
        int effectiveIndex = (s_hoverActive && s_hoverIndex >= 0) ? s_hoverIndex : s_selectedIndex;

        if (effectiveIndex == displayCount) {
            // Add row
            return T("$SLID_GuideAddFilter");
        }
        if (effectiveIndex >= 0 && effectiveIndex < displayCount) {
            if (s_isReordering) {
                return T("$SLID_GuideReorder");
            }

            const auto& dr = s_displayRows[effectiveIndex];
            const auto& family = s_filterRows[dr.familyIndex];
            if (dr.childIndex < 0) {
                // Root row
                auto& data = family.GetData();
                auto* filter = FilterRegistry::GetSingleton()->GetFilter(data.filterID);
                std::string desc = filter ? std::string(filter->GetDescription()) : data.name;
                if (data.containerFormID != 0) {
                    return desc + "  (" + data.containerName + ")";
                }
                return desc + "  (unlinked)";
            } else {
                // Child row
                const auto& children = family.GetChildren();
                if (dr.childIndex < static_cast<int>(children.size())) {
                    const auto& child = children[dr.childIndex];
                    auto* filter = FilterRegistry::GetSingleton()->GetFilter(child.filterID);
                    std::string desc = filter ? std::string(filter->GetDescription()) : child.name;
                    if (child.containerFormID != 0) {
                        return desc + "  (" + child.containerName + ")";
                    }
                    return desc + "  (unlinked)";
                }
            }
        }
        return "";
    }

    // --- Public API: Network context ---

    void ResetInputRepeat() {
        if (s_callbacks.resetRepeat) s_callbacks.resetRepeat();
    }

    // --- Public API: Sub-focus ---

    bool IsSubFocused() { return s_subFocus != SubFocus::kNone; }

    void EnterRow() {
        if (s_isReordering || !IsOnFilterRow()) return;

        // Auto-expand collapsed family roots
        s_autoExpanded = false;
        if (IsOnExpandableRoot()) {
            const auto& dr = s_displayRows[s_selectedIndex];
            auto& family = s_filterRows[dr.familyIndex];
            if (!family.IsExpanded()) {
                ToggleExpand();
                s_autoExpanded = true;
            }
        }

        s_subFocus = SubFocus::kDropdown;
        PopulateList();
    }

    void ExitRow() {
        s_subFocus = SubFocus::kNone;

        // Undo auto-expand if EnterRow triggered it
        if (s_autoExpanded && IsOnExpandableRoot()) {
            const auto& dr = s_displayRows[s_selectedIndex];
            auto& family = s_filterRows[dr.familyIndex];
            if (family.IsExpanded()) {
                ToggleExpand();
            }
            s_autoExpanded = false;
        }

        PopulateList();
    }

    void CollapseRow() {
        if (s_isReordering) return;
        if (!IsOnFilterRow()) return;

        int displayCount = static_cast<int>(s_displayRows.size());
        if (s_selectedIndex < 0 || s_selectedIndex >= displayCount) return;

        const auto& dr = s_displayRows[s_selectedIndex];
        auto& family = s_filterRows[dr.familyIndex];

        if (dr.childIndex >= 0) {
            // On a child row — jump to parent root
            int rootIdx = FindDisplayIndexForFamily(dr.familyIndex);
            if (rootIdx >= 0) {
                s_selectedIndex = rootIdx;
                // Scroll to keep root visible
                if (s_selectedIndex < s_scrollOffset) {
                    s_scrollOffset = s_selectedIndex;
                }
                PopulateList();
            }
            return;
        }

        // On a root — collapse if expanded
        if (family.HasChildren() && family.IsExpanded()) {
            ToggleExpand();
        }
    }

    void ActivateSubFocus() {
        if (s_subFocus != SubFocus::kDropdown) return;
        if (!IsOnFilterRow()) return;

        int displayCount = static_cast<int>(s_displayRows.size());
        if (s_selectedIndex < 0 || s_selectedIndex >= displayCount) return;

        const auto& dr = s_displayRows[s_selectedIndex];

        if (dr.childIndex < 0) {
            // Root row — open container dropdown (skip expand/collapse)
            auto ctx = MakeDropdownContext();
            int famIdx = dr.familyIndex;
            s_filterRows[famIdx].OpenContainerDropdown(ctx, -1,
                [famIdx](bool confirmed) { HandleContainerResult(famIdx, -1, confirmed); });
        } else {
            // Child row — open container dropdown
            auto ctx = MakeDropdownContext();
            int famIdx = dr.familyIndex;
            int childIdx = dr.childIndex;
            s_filterRows[famIdx].OpenContainerDropdown(ctx, childIdx,
                [famIdx, childIdx](bool confirmed) { HandleContainerResult(famIdx, childIdx, confirmed); });
        }
    }

    void TabToNextChild() {
        if (s_subFocus != SubFocus::kDropdown) return;
        if (!IsOnFilterRow()) return;

        int displayCount = static_cast<int>(s_displayRows.size());
        if (s_selectedIndex < 0 || s_selectedIndex >= displayCount) return;

        const auto& dr = s_displayRows[s_selectedIndex];
        const auto& family = s_filterRows[dr.familyIndex];

        if (dr.childIndex < 0) {
            // On root — if expanded with children, move to first child
            if (family.IsExpanded() && family.HasChildren()) {
                int nextIdx = s_selectedIndex + 1;
                if (nextIdx < displayCount && s_displayRows[nextIdx].familyIndex == dr.familyIndex &&
                    s_displayRows[nextIdx].childIndex >= 0)
                {
                    s_selectedIndex = nextIdx;
                    // Scroll if needed
                    if (s_selectedIndex >= s_scrollOffset + MAX_VISIBLE_ROWS) {
                        s_scrollOffset = s_selectedIndex - MAX_VISIBLE_ROWS + 1;
                        int maxOffset = std::max(0, displayCount - MAX_VISIBLE_ROWS);
                        s_scrollOffset = std::clamp(s_scrollOffset, 0, maxOffset);
                    }
                    PopulateList();
                    return;
                }
            }
            // No children to tab to — exit sub-focus
            ExitRow();
        } else {
            // On child — move to next sibling child
            int nextIdx = s_selectedIndex + 1;
            if (nextIdx < displayCount && s_displayRows[nextIdx].familyIndex == dr.familyIndex &&
                s_displayRows[nextIdx].childIndex >= 0)
            {
                s_selectedIndex = nextIdx;
                if (s_selectedIndex >= s_scrollOffset + MAX_VISIBLE_ROWS) {
                    s_scrollOffset = s_selectedIndex - MAX_VISIBLE_ROWS + 1;
                    int maxOffset = std::max(0, displayCount - MAX_VISIBLE_ROWS);
                    s_scrollOffset = std::clamp(s_scrollOffset, 0, maxOffset);
                }
                PopulateList();
            } else {
                // Last child — exit sub-focus
                ExitRow();
            }
        }
    }

    // (BuildStagesFromNetwork moved to orchestrator)

    static void BuildDefaultFilters() {
        // Build-up model: start with empty filter list
        s_filterRows.clear();
        BuildDisplayRows();
    }

    std::vector<FilterStage> BuildFilterStages() {
        std::vector<FilterStage> filters;
        for (const auto& row : s_filterRows) {
            auto stages = row.ToFilterStages();
            filters.insert(filters.end(), stages.begin(), stages.end());
        }
        return filters;
    }

    const std::vector<FilterRow>& GetFilterRows() {
        return s_filterRows;
    }

    void CommitToNetwork() {
        RE::FormID catchAllFormID = s_callbacks.getCatchAllFormID ? s_callbacks.getCatchAllFormID() : 0;
        ConfigState::CommitToNetwork(ConfigState::GetNetworkName(), BuildFilterStages(), catchAllFormID);
    }

    void LoadStages(std::vector<FilterRow::Data> a_stages) {
        auto* registry = FilterRegistry::GetSingleton();
        s_filterRows.clear();

        // Group flat stages into families using the FilterRegistry parent info.
        // Maintain the order of first occurrence for family roots.
        // A stage whose filter has a parent is scooped into that parent's family
        // as a child (if the parent root exists). If the parent root doesn't exist
        // yet in s_filterRows, we create it (unlinked).

        // Map from root filterID -> index into s_filterRows
        std::unordered_map<std::string, int> rootIndexMap;

        for (auto& d : a_stages) {
            const IFilter* filter = registry->GetFilter(d.filterID);
            const IFilter* parent = filter ? filter->GetParent() : nullptr;

            if (parent) {
                // This is a child filter -- scoop into its parent's family
                std::string parentID(parent->GetID());
                auto it = rootIndexMap.find(parentID);
                int rootIdx;
                if (it != rootIndexMap.end()) {
                    rootIdx = it->second;
                } else {
                    // Parent root not yet in the list -- create it unlinked
                    FilterRow::Data rootData;
                    rootData.filterID = parentID;
                    rootData.name = std::string(parent->GetDisplayName());
                    rootData.containerName = "unlinked";
                    rootData.containerFormID = 0;
                    rootIdx = static_cast<int>(s_filterRows.size());
                    rootIndexMap[parentID] = rootIdx;
                    s_filterRows.emplace_back(FilterRow(std::move(rootData)));
                }

                // Add as child
                FilterRow::ChildData cd;
                cd.filterID = d.filterID;
                cd.name = d.name;
                cd.containerName = d.containerName;
                cd.location = d.location;
                cd.containerFormID = d.containerFormID;
                cd.count = d.count;
                cd.predictedCount = d.predictedCount;
                s_filterRows[rootIdx].MutableChildren().push_back(std::move(cd));
            } else {
                // This is a root filter
                auto it = rootIndexMap.find(d.filterID);
                if (it != rootIndexMap.end()) {
                    // Root already exists (created earlier by a child) -- update its data
                    int idx = it->second;
                    s_filterRows[idx].MutableData() = d;
                } else {
                    rootIndexMap[d.filterID] = static_cast<int>(s_filterRows.size());
                    s_filterRows.emplace_back(FilterRow(std::move(d)));
                }
            }
        }

        // Merge registry children into roots. Saved data may have a subset of
        // registry children (or none). Add any missing ones as unlinked entries
        // so the full family is always visible in the UI.
        for (int fi = 0; fi < static_cast<int>(s_filterRows.size()); ++fi) {
            auto& row = s_filterRows[fi];
            const std::string& rootID = row.GetData().filterID;
            const auto& registryChildren = registry->GetChildren(rootID);
            if (registryChildren.empty()) continue;

            // Build set of child IDs already present from saved data
            std::unordered_set<std::string> existingChildIDs;
            for (const auto& child : row.GetChildren()) {
                existingChildIDs.insert(child.filterID);
            }

            // Add registry children not already present
            for (const auto& childID : registryChildren) {
                if (existingChildIDs.count(childID)) continue;
                const IFilter* childFilter = registry->GetFilter(childID);
                if (!childFilter) continue;
                FilterRow::ChildData cd;
                cd.filterID = childID;
                cd.name = std::string(childFilter->GetDisplayName());
                cd.containerName = "unlinked";
                cd.containerFormID = 0;
                cd.count = 0;
                cd.predictedCount = -1;
                row.MutableChildren().push_back(std::move(cd));
            }
        }

        BuildDisplayRows();
    }

    void SetPredictions(const std::vector<int>& a_filterCounts,
                        const std::vector<int>& a_contestedCounts,
                        const std::vector<std::unordered_map<size_t, int32_t>>& a_contestedByMaps,
                        int a_originCount) {
        // a_filterCounts is indexed by the flat pipeline order (same order as
        // BuildFilterStages output: children-before-root per family).
        // ToFilterStages emits ALL stages including unlinked, so flatIdx must
        // advance for every stage regardless of linked state.
        s_contestedByMaps = a_contestedByMaps;

        int flatIdx = 0;
        for (auto& row : s_filterRows) {
            // Children first (matching ToFilterStages order)
            for (auto& child : row.MutableChildren()) {
                if (child.containerFormID != 0) {
                    child.predictedCount = (flatIdx < static_cast<int>(a_filterCounts.size()))
                                           ? a_filterCounts[flatIdx] : 0;
                    int rawContest = (flatIdx < static_cast<int>(a_contestedCounts.size()))
                                    ? a_contestedCounts[flatIdx] : 0;
                    UpdateContestTarget(child.filterID, rawContest);
                } else {
                    child.predictedCount = -1;  // unlinked children have no prediction
                    UpdateContestTarget(child.filterID, 0);
                }
                ++flatIdx;
            }
            // Then root
            if (row.GetData().containerFormID != 0) {
                row.MutableData().predictedCount = (flatIdx < static_cast<int>(a_filterCounts.size()))
                                                   ? a_filterCounts[flatIdx] : 0;
                int rawContest = (flatIdx < static_cast<int>(a_contestedCounts.size()))
                                ? a_contestedCounts[flatIdx] : 0;
                UpdateContestTarget(row.GetData().filterID, rawContest);
            } else {
                row.MutableData().predictedCount = -1;
                UpdateContestTarget(row.GetData().filterID, 0);
            }
            ++flatIdx;
        }

        s_predictedOriginCount = a_originCount;

        PopulateList();
        UpdateOriginRow();
    }

    void RefreshAfterSort(const std::set<int>& a_flashIndices) {
        ClearPredictions();

        s_countFlashIndices = a_flashIndices;
        if (!s_countFlashIndices.empty()) {
            s_countFlashActive = true;
            s_countFlashStart = std::chrono::steady_clock::now();
        }

        PopulateList();

        // Apply flash color to visible filter rows
        for (int idx : s_countFlashIndices) {
            if (idx < 0) continue;
            int slot = idx - s_scrollOffset;
            if (slot >= 0 && slot < MAX_VISIBLE_ROWS && s_movie) {
                std::string rowName = "row" + std::to_string(slot);
                ScaleformUtil::SetTextFieldFormat(s_movie, "_root." + rowName + ".countText", 14, COLOR_COUNT_FLASH);
            }
        }
    }

    void RefreshAfterWhoosh() {
        PopulateList();
    }

    void ClearPredictions() {
        for (auto& row : s_filterRows) {
            row.MutableData().predictedCount = -1;
            row.MutableData().contestedCount = 0;
            row.MutableData().contestAlpha = 0;
            row.MutableData().contestColor = 0;
            for (auto& child : row.MutableChildren()) {
                child.predictedCount = -1;
                child.contestedCount = 0;
                child.contestAlpha = 0;
                child.contestColor = 0;
            }
        }
        s_predictedOriginCount = -1;
        s_contestedByMaps.clear();
        s_contestAccentSet.clear();
        s_contestAnims.clear();
    }

    static void UpdateOriginRow() {
        // Origin count data is used by orchestrator to update OriginPanel
    }

    // --- Internal: Row management ---

    static void InitRows() {
        RE::GFxValue root;
        s_movie->GetVariable(&root, "_root");
        if (root.IsUndefined()) {
            logger::error("InitRows: _root not found");
            return;
        }

        double filterStartY = ROW_Y + FILTER_OFFSET;

        for (int i = 0; i < MAX_VISIBLE_ROWS; i++) {
            std::string rowName = "row" + std::to_string(i);
            RE::GFxValue args[2];
            args[0].SetString(rowName.c_str());
            args[1].SetNumber(static_cast<double>(200 + i));
            root.Invoke("createEmptyMovieClip", &s_slotClips[i], args, 2);

            if (s_slotClips[i].IsUndefined()) {
                logger::warn("InitRows: failed to create {}", rowName);
                continue;
            }

            RE::GFxValue posX, posY;
            posX.SetNumber(ROW_X);
            double rowY = filterStartY + i * ROW_HEIGHT;
            posY.SetNumber(rowY);
            s_slotClips[i].SetMember("_x", posX);
            s_slotClips[i].SetMember("_y", posY);
        }

        for (int i = 0; i < MAX_VISIBLE_ROWS; i++) {
            if (!s_slotClips[i].IsUndefined()) {
                CreateSlotTextFields(s_slotClips[i], i);
            }
        }

        InitAddRow();
        s_rowsReady = true;
        logger::info("InitRows: created origin row + {} filter row slots + add row",
                     MAX_VISIBLE_ROWS);
    }

    static void InitAddRow() {
        RE::GFxValue root;
        s_movie->GetVariable(&root, "_root");
        if (root.IsUndefined()) return;

        RE::GFxValue args[2];
        args[0].SetString("addRow");
        args[1].SetNumber(198.0);
        root.Invoke("createEmptyMovieClip", &s_addRow, args, 2);

        if (s_addRow.IsUndefined()) {
            logger::warn("InitAddRow: failed to create addRow clip");
            return;
        }

        RE::GFxValue posX;
        posX.SetNumber(ROW_X);
        s_addRow.SetMember("_x", posX);

        // Background clip (will be drawn by UpdateAddRow)
        RE::GFxValue bgClip;
        RE::GFxValue bgArgs[2];
        bgArgs[0].SetString("_bg"); bgArgs[1].SetNumber(1.0);
        s_addRow.Invoke("createEmptyMovieClip", &bgClip, bgArgs, 2);

        // Text field for the add row label (created on the addRow clip)
        RE::GFxValue tfArgs[6];
        tfArgs[0].SetString("labelText"); tfArgs[1].SetNumber(10.0);
        tfArgs[2].SetNumber(0.0); tfArgs[3].SetNumber(0.0);
        tfArgs[4].SetNumber(ROW_W); tfArgs[5].SetNumber(ROW_HEIGHT);
        s_addRow.Invoke("createTextField", nullptr, tfArgs, 6);

        ScaleformUtil::SetTextFieldFormat(s_movie, "_root.addRow.labelText", 14, COLOR_HINT);

        // Center-align the label
        RE::GFxValue tf;
        s_movie->GetVariable(&tf, "_root.addRow.labelText");
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

        UpdateAddRow();
    }

    static void UpdateAddRow() {
        if (s_addRow.IsUndefined()) return;

        int displayCount = static_cast<int>(s_displayRows.size());
        int visibleDisplayRows = std::min(displayCount - s_scrollOffset, MAX_VISIBLE_ROWS);
        double filterStartY = ROW_Y + FILTER_OFFSET;
        double addRowY = filterStartY + visibleDisplayRows * ROW_HEIGHT;

        bool isEmpty = (s_filterRows.empty());
        double rowH = isEmpty ? (ROW_HEIGHT * 1.5) : ROW_HEIGHT;
        int maxFamilyRoots = static_cast<int>(FilterRegistry::GetSingleton()->GetFamilyRoots().size());
        bool allUsed = (static_cast<int>(s_filterRows.size()) >= maxFamilyRoots);

        // Hide add row if it would overlap the fixed catch-all band
        bool addRowVisible = (addRowY + rowH <= CATCHALL_BAND_Y);
        RE::GFxValue visVal;
        visVal.SetBoolean(addRowVisible);
        s_addRow.SetMember("_visible", visVal);
        if (!addRowVisible) return;

        RE::GFxValue posY;
        posY.SetNumber(addRowY);
        s_addRow.SetMember("_y", posY);

        bool isSelected = (s_selectedIndex == displayCount);
        bool isHovered = (s_hoverActive && s_hoverIndex == displayCount);

        // Draw background
        RE::GFxValue bgClip;
        s_addRow.GetMember("_bg", &bgClip);
        if (bgClip.IsUndefined()) {
            RE::GFxValue bgArgs[2];
            bgArgs[0].SetString("_bg");
            bgArgs[1].SetNumber(1.0);
            s_addRow.Invoke("createEmptyMovieClip", &bgClip, bgArgs, 2);
        }
        if (!bgClip.IsUndefined()) {
            bgClip.Invoke("clear", nullptr, nullptr, 0);

            uint32_t bgColor = isSelected ? COLOR_ROW_SELECT :
                              (isHovered ? COLOR_ROW_HOVER : COLOR_ROW_NORMAL);
            int bgAlpha = isSelected ? ALPHA_ROW_SELECT :
                         (isHovered ? ALPHA_ROW_HOVER : ALPHA_ROW_NORMAL);

            RE::GFxValue fillArgs[2];
            fillArgs[0].SetNumber(static_cast<double>(bgColor));
            fillArgs[1].SetNumber(static_cast<double>(bgAlpha));
            bgClip.Invoke("beginFill", nullptr, fillArgs, 2);

            RE::GFxValue pt[2];
            pt[0].SetNumber(0.0); pt[1].SetNumber(0.0);
            bgClip.Invoke("moveTo", nullptr, pt, 2);
            pt[0].SetNumber(ROW_W);
            bgClip.Invoke("lineTo", nullptr, pt, 2);
            pt[1].SetNumber(rowH);
            bgClip.Invoke("lineTo", nullptr, pt, 2);
            pt[0].SetNumber(0.0);
            bgClip.Invoke("lineTo", nullptr, pt, 2);
            pt[1].SetNumber(0.0);
            bgClip.Invoke("lineTo", nullptr, pt, 2);
            bgClip.Invoke("endFill", nullptr, nullptr, 0);
        }

        // Label text depends on state
        std::string label;
        if (allUsed) {
            label = T("$SLID_AllFiltersConfigured");
        } else if (isEmpty) {
            label = T("$SLID_AddFilterToBegin");
        } else {
            label = T("$SLID_AddFilterShortPlus");
        }
        uint32_t labelColor = allUsed ? 0x555555 : (isSelected ? COLOR_FILTER : COLOR_HINT);
        int fontSize = isEmpty ? 16 : 14;

        RE::GFxValue tf;
        s_movie->GetVariable(&tf, "_root.addRow.labelText");
        if (!tf.IsUndefined()) {
            // Adjust text field height and vertical position to match row height
            RE::GFxValue hVal;
            hVal.SetNumber(rowH);
            tf.SetMember("_height", hVal);

            RE::GFxValue yVal;
            yVal.SetNumber(isEmpty ? (rowH - 20.0) / 2.0 : 5.0);
            tf.SetMember("_y", yVal);

            // Apply formatting (font size, color)
            ScaleformUtil::SetTextFieldFormat(s_movie, "_root.addRow.labelText", fontSize, labelColor);

            // Re-apply center alignment (SetTextFieldFormat resets it)
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

            // Set text after formatting (setTextFormat can clear it)
            RE::GFxValue textVal;
            textVal.SetString(label.c_str());
            tf.SetMember("text", textVal);
        }
    }

    static void CreateSlotTextFields(RE::GFxValue& a_slot, int a_index) {
        if (a_slot.IsUndefined()) return;

        // numText -- row number
        {
            RE::GFxValue args[6];
            args[0].SetString("numText");
            args[1].SetNumber(10.0);
            args[2].SetNumber(COL_NUM_X);
            args[3].SetNumber(6.0);
            args[4].SetNumber(COL_NUM_W);
            args[5].SetNumber(22.0);
            a_slot.Invoke("createTextField", nullptr, args, 6);
        }

        // nameText -- filter name
        {
            RE::GFxValue args[6];
            args[0].SetString("nameText");
            args[1].SetNumber(11.0);
            args[2].SetNumber(COL_FILTER_X);
            args[3].SetNumber(5.0);
            args[4].SetNumber(COL_FILTER_W);
            args[5].SetNumber(24.0);
            a_slot.Invoke("createTextField", nullptr, args, 6);
        }

        // containerText -- container name (depth 30: above _dd dropdown clip at depth 25)
        {
            RE::GFxValue args[6];
            args[0].SetString("containerText");
            args[1].SetNumber(30.0);
            args[2].SetNumber(COL_CONTAINER_X);
            args[3].SetNumber(5.0);
            args[4].SetNumber(COL_CONTAINER_W);
            args[5].SetNumber(24.0);
            a_slot.Invoke("createTextField", nullptr, args, 6);
        }

        // countText -- item count
        {
            RE::GFxValue args[6];
            args[0].SetString("countText");
            args[1].SetNumber(12.0);
            args[2].SetNumber(COL_ITEMS_X);
            args[3].SetNumber(6.0);
            args[4].SetNumber(COL_ITEMS_W);
            args[5].SetNumber(22.0);
            a_slot.Invoke("createTextField", nullptr, args, 6);
        }

        // contestText -- contested item count (amber), own column after count
        {
            RE::GFxValue args[6];
            args[0].SetString("contestText");
            args[1].SetNumber(13.0);
            args[2].SetNumber(COL_ITEMS_X + COL_ITEMS_W + 2.0);  // 702px — after count column
            args[3].SetNumber(8.0);   // vertically centered for 12pt in 34px row
            args[4].SetNumber(34.0);  // fits before chest icon at 738
            args[5].SetNumber(20.0);
            a_slot.Invoke("createTextField", nullptr, args, 6);
        }

        // Apply formatting
        std::string clipPath = GetSlotClipPath(a_index);
        ScaleformUtil::SetTextFieldFormat(s_movie, clipPath + ".numText",       14, COLOR_ROW_NUM);
        ScaleformUtil::SetTextFieldFormat(s_movie, clipPath + ".nameText",      15, COLOR_FILTER);
        ScaleformUtil::SetTextFieldFormat(s_movie, clipPath + ".containerText", 14, COLOR_CONTAINER);
        ScaleformUtil::SetTextFieldFormat(s_movie, clipPath + ".countText",     14, COLOR_COUNT);
        ScaleformUtil::SetTextFieldFormat(s_movie, clipPath + ".contestText",   12, COLOR_CONTEST);

    }

    static void SetSlotVisible(int a_index, bool a_visible) {
        if (a_index < 0 || a_index >= TOTAL_ROW_SLOTS || s_slotClips[a_index].IsUndefined()) return;
        RE::GFxValue vis;
        vis.SetBoolean(a_visible);
        s_slotClips[a_index].SetMember("_visible", vis);
    }

    static void PopulateList() {
        if (!s_rowsReady || !s_movie) return;

        BuildDisplayRows();
        ApplyContestAnimations();
        RebuildContestAccentSet();

        int displayCount = static_cast<int>(s_displayRows.size());
        int visibleRows = std::min(displayCount - s_scrollOffset, MAX_VISIBLE_ROWS);

        // Compute a running display number for root rows
        // (display number = 1-based position among family roots only)
        // We need to know the display number for each root that appears in the visible window
        // Build a map from familyIndex -> display number
        std::unordered_map<int, int> familyDisplayNum;
        {
            int num = 1;
            for (int fi = 0; fi < static_cast<int>(s_filterRows.size()); ++fi) {
                familyDisplayNum[fi] = num++;
            }
        }

        // Scrollable rows
        for (int i = 0; i < MAX_VISIBLE_ROWS; i++) {
            if (i < visibleRows) {
                int dispIdx = s_scrollOffset + i;
                SetSlotVisible(i, true);

                const auto& dr = s_displayRows[dispIdx];
                const auto& family = s_filterRows[dr.familyIndex];

                bool isSelected = (!s_hoverActive && s_selectedIndex == dispIdx);
                bool isHovered = (s_hoverActive && s_hoverIndex == dispIdx);
                bool chestHover = (s_hoverActive && s_hoverChestIndex == dispIdx);
                bool dropdownFocused = (isSelected && s_subFocus == SubFocus::kDropdown);
                bool isContested = (s_contestAccentSet.count(dispIdx) > 0);

                if (dr.childIndex < 0) {
                    // Root row
                    bool isLifted = (s_isReordering && s_liftedFamilyIndex == dr.familyIndex);
                    int displayNum = familyDisplayNum.count(dr.familyIndex)
                                     ? familyDisplayNum[dr.familyIndex] : 0;
                    family.RenderRoot(s_movie, s_slotClips[i],
                        GetSlotClipPath(i), i, displayNum,
                        isSelected, isHovered, isLifted, chestHover, dropdownFocused,
                        isContested);
                } else {
                    // Child row
                    family.RenderChild(s_movie, s_slotClips[i],
                        GetSlotClipPath(i), dr.childIndex,
                        isSelected, isHovered, chestHover, dropdownFocused,
                        isContested);
                }
            } else {
                SetSlotVisible(i, false);
            }
        }

        UpdateAddRow();
        UpdateScrollbar();
    }

    static void UpdateSelection(int a_oldIndex, int a_newIndex) {
        if (a_newIndex == a_oldIndex) return;
        s_selectedIndex = a_newIndex;
        PopulateList();
    }

    // --- Internal: Scrollbar ---

    static void DrawScrollbar() {
        if (!s_movie) return;

        int displayCount = static_cast<int>(s_displayRows.size());
        if (displayCount <= MAX_VISIBLE_ROWS) return;

        double trackX = PANEL_RIGHT - 14.0;
        double trackY = ROW_Y + FILTER_OFFSET;
        double trackH = MAX_VISIBLE_ROWS * ROW_HEIGHT;

        ScaleformUtil::DrawFilledRect(s_movie, "_scrollTrack", 100, trackX, trackY, 12.0, trackH, 0x222222, 40);

        double thumbH = std::max(20.0, trackH * MAX_VISIBLE_ROWS / displayCount);
        double thumbY = trackY;

        RE::GFxValue root;
        s_movie->GetVariable(&root, "_root");
        if (root.IsUndefined()) return;

        RE::GFxValue args[2];
        args[0].SetString("_scrollThumb");
        args[1].SetNumber(101.0);
        root.Invoke("createEmptyMovieClip", &s_scrollThumb, args, 2);

        if (!s_scrollThumb.IsUndefined()) {
            RE::GFxValue posX, posY;
            posX.SetNumber(trackX);
            posY.SetNumber(thumbY);
            s_scrollThumb.SetMember("_x", posX);
            s_scrollThumb.SetMember("_y", posY);

            RE::GFxValue fillArgs[2];
            fillArgs[0].SetNumber(static_cast<double>(0x555555));
            fillArgs[1].SetNumber(60.0);
            s_scrollThumb.Invoke("beginFill", nullptr, fillArgs, 2);

            RE::GFxValue pt[2];
            pt[0].SetNumber(0.0); pt[1].SetNumber(0.0);
            s_scrollThumb.Invoke("moveTo", nullptr, pt, 2);
            pt[0].SetNumber(12.0);
            s_scrollThumb.Invoke("lineTo", nullptr, pt, 2);
            pt[1].SetNumber(thumbH);
            s_scrollThumb.Invoke("lineTo", nullptr, pt, 2);
            pt[0].SetNumber(0.0);
            s_scrollThumb.Invoke("lineTo", nullptr, pt, 2);
            pt[1].SetNumber(0.0);
            s_scrollThumb.Invoke("lineTo", nullptr, pt, 2);
            s_scrollThumb.Invoke("endFill", nullptr, nullptr, 0);
        }
    }

    static void UpdateScrollbar() {
        if (s_scrollThumb.IsUndefined()) return;

        int displayCount = static_cast<int>(s_displayRows.size());
        if (displayCount <= MAX_VISIBLE_ROWS) {
            RE::GFxValue vis;
            vis.SetBoolean(false);
            s_scrollThumb.SetMember("_visible", vis);
            return;
        }

        RE::GFxValue vis;
        vis.SetBoolean(true);
        s_scrollThumb.SetMember("_visible", vis);

        double trackY = ROW_Y + FILTER_OFFSET;
        double trackH = MAX_VISIBLE_ROWS * ROW_HEIGHT;
        double thumbH = std::max(20.0, trackH * MAX_VISIBLE_ROWS / displayCount);
        int maxOffset = displayCount - MAX_VISIBLE_ROWS;
        double ratio = (maxOffset > 0) ? static_cast<double>(s_scrollOffset) / maxOffset : 0.0;
        double thumbY = trackY + ratio * (trackH - thumbH);

        RE::GFxValue posY;
        posY.SetNumber(thumbY);
        s_scrollThumb.SetMember("_y", posY);
    }

    // --- Internal: Slot clip lookup ---

    static std::string GetSlotClipPath(int a_slotIndex) {
        return "_root.row" + std::to_string(a_slotIndex);
    }

    static RE::GFxValue* GetSlotClipForDisplayIndex(int a_displayIndex) {
        int slot = a_displayIndex - s_scrollOffset;
        if (slot < 0 || slot >= MAX_VISIBLE_ROWS) return nullptr;
        if (s_slotClips[slot].IsUndefined()) return nullptr;
        return &s_slotClips[slot];
    }

    // --- Internal: Hold-remove ---

    static int CountFamilyItems(int a_familyIndex) {
        if (a_familyIndex < 0 || a_familyIndex >= static_cast<int>(s_filterRows.size())) return 0;
        auto& row = s_filterRows[a_familyIndex];
        int total = 0;

        // Count items in root container
        RE::FormID rootContainer = row.GetData().containerFormID;
        if (rootContainer != 0) {
            auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(rootContainer);
            if (ref) {
                auto inv = ref->GetInventory();
                for (auto& [item, data] : inv) {
                    if (!item || data.first <= 0 || IsPhantomItem(item)) continue;
                    total += data.first;
                }
            }
        }

        // Count items in children containers
        for (const auto& child : row.GetChildren()) {
            if (child.containerFormID != 0) {
                auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(child.containerFormID);
                if (ref) {
                    auto inv = ref->GetInventory();
                    for (auto& [item, data] : inv) {
                        if (!item || data.first <= 0 || IsPhantomItem(item)) continue;
                        total += data.first;
                    }
                }
            }
        }
        return total;
    }

    static int GatherFamilyToMaster(int a_familyIndex) {
        if (a_familyIndex < 0 || a_familyIndex >= static_cast<int>(s_filterRows.size())) return 0;
        auto masterFormID = ConfigState::GetMasterFormID();
        auto* masterRef = RE::TESForm::LookupByID<RE::TESObjectREFR>(masterFormID);
        if (!masterRef) return 0;

        auto& row = s_filterRows[a_familyIndex];
        int totalMoved = 0;

        auto gatherFrom = [&](RE::FormID containerID) {
            if (containerID == 0 || containerID == masterFormID) return;
            auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(containerID);
            if (!ref) return;
            auto inv = ref->GetInventory();
            for (auto& [item, data] : inv) {
                if (!item || data.first <= 0 || IsPhantomItem(item)) continue;
                ref->RemoveItem(item, data.first, RE::ITEM_REMOVE_REASON::kStoreInContainer, nullptr, masterRef);
                totalMoved += data.first;
            }
        };

        gatherFrom(row.GetData().containerFormID);
        for (const auto& child : row.GetChildren()) {
            gatherFrom(child.containerFormID);
        }
        return totalMoved;
    }

    static void ExecuteRemoveFamily(int a_familyIndex) {
        if (a_familyIndex < 0 || a_familyIndex >= static_cast<int>(s_filterRows.size())) return;
        logger::info("Remove: removing filter '{}' at family index {}",
                     s_filterRows[a_familyIndex].GetData().name, a_familyIndex);
        s_filterRows.erase(s_filterRows.begin() + a_familyIndex);

        s_subFocus = SubFocus::kNone;

        BuildDisplayRows();
        int displayCount = static_cast<int>(s_displayRows.size());
        int totalCount = displayCount + 1;  // + add row
        if (s_selectedIndex >= totalCount) s_selectedIndex = totalCount - 1;
        if (s_selectedIndex < 0) s_selectedIndex = 0;

        CommitToNetwork();

        int maxOff = std::max(0, displayCount - MAX_VISIBLE_ROWS);
        s_scrollOffset = std::clamp(s_scrollOffset, 0, maxOff);

        s_callbacks.recalcPredictions();
    }

    static void HandleRemoveRequest(int a_familyIndex) {
        int itemCount = CountFamilyItems(a_familyIndex);

        if (itemCount == 0) {
            // No items — delete silently
            ExecuteRemoveFamily(a_familyIndex);
            HoldRemove::ClearHoldIndex();
            s_callbacks.resetRepeat();
            return;
        }

        // Items present — show contextual 3-option dialog
        std::string familyName = (a_familyIndex >= 0 && a_familyIndex < static_cast<int>(s_filterRows.size()))
            ? s_filterRows[a_familyIndex].GetData().name : T("$SLID_Filter");
        std::string title = TF("$SLID_ConfirmDeleteWithItems", familyName, std::to_string(itemCount));

        ConfirmDialog::Show(s_movie,
            {.title = title,
             .buttons = {T("$SLID_PullToMaster"), T("$SLID_LeaveItems"), T("$SLID_Cancel")},
             .popupW = 380.0,
             .defaultIndex = 2},
            [a_familyIndex](int idx) {
                if (idx == 0) {
                    // Pull to master then delete
                    int moved = GatherFamilyToMaster(a_familyIndex);
                    logger::info("Remove: pulled {} items to master before removing family {}", moved, a_familyIndex);
                    ExecuteRemoveFamily(a_familyIndex);
                } else if (idx == 1) {
                    // Leave items, just delete
                    ExecuteRemoveFamily(a_familyIndex);
                }
                // idx == 2: Cancel — no-op
                HoldRemove::ClearHoldIndex();
                s_callbacks.resetRepeat();
            });
    }

    static HoldRemove::Callback MakeRemoveCallback() {
        return [](int familyIndex) {
            HandleRemoveRequest(familyIndex);
        };
    }

    // --- Internal: Row animations ---

    static void StartRowAnim(int a_slot, double a_fromY, double a_toY) {
        if (a_slot < 0 || a_slot >= MAX_VISIBLE_ROWS) return;
        auto& anim = s_rowAnims[a_slot];
        if (anim.active) {
            float elapsed = std::chrono::duration<float>(
                std::chrono::steady_clock::now() - anim.startTime).count();
            float t = std::min(elapsed / ANIM_DURATION, 1.0f);
            float ease = 1.0f - (1.0f - t) * (1.0f - t);
            a_fromY = anim.startY + (anim.endY - anim.startY) * ease;
        }
        anim.active = true;
        anim.startY = a_fromY;
        anim.endY = a_toY;
        anim.startTime = std::chrono::steady_clock::now();
    }

    static void ClearAllLiftVisuals() {
        for (int i = 0; i < MAX_VISIBLE_ROWS; i++) {
            if (s_slotLifted[i]) ClearLiftVisual(i);
        }
    }

    static void ApplyLiftVisualToFamily() {
        if (s_liftedFamilyIndex < 0) return;
        for (int di = 0; di < static_cast<int>(s_displayRows.size()); ++di) {
            if (s_displayRows[di].familyIndex != s_liftedFamilyIndex) continue;
            int slot = di - s_scrollOffset;
            if (slot >= 0 && slot < MAX_VISIBLE_ROWS) {
                ApplyLiftVisual(slot);
            }
        }
    }

    static void RelocateLiftVisual() {
        ClearAllLiftVisuals();
        ApplyLiftVisualToFamily();
    }

    static void AnimateDisplacedFamily(int a_displacedFamilyIndex, double a_deltaY) {
        double filterStartY = ROW_Y + FILTER_OFFSET;
        for (int di = 0; di < static_cast<int>(s_displayRows.size()); ++di) {
            if (s_displayRows[di].familyIndex != a_displacedFamilyIndex) continue;
            int slot = di - s_scrollOffset;
            if (slot < 0 || slot >= MAX_VISIBLE_ROWS) continue;
            double toY = filterStartY + slot * ROW_HEIGHT;
            double fromY = toY + a_deltaY;
            StartRowAnim(slot, fromY, toY);
        }
    }

    static void UpdateRowAnimations() {
        auto now = std::chrono::steady_clock::now();
        for (int i = 0; i < MAX_VISIBLE_ROWS; i++) {
            auto& anim = s_rowAnims[i];
            if (!anim.active) continue;
            float elapsed = std::chrono::duration<float>(now - anim.startTime).count();
            float t = std::min(elapsed / ANIM_DURATION, 1.0f);
            float ease = 1.0f - (1.0f - t) * (1.0f - t);
            double currentY = anim.startY + (anim.endY - anim.startY) * ease;
            if (!s_slotClips[i].IsUndefined()) {
                RE::GFxValue posY;
                posY.SetNumber(currentY);
                s_slotClips[i].SetMember("_y", posY);
            }
            if (t >= 1.0f) anim.active = false;
        }
    }

    static void SnapAllAnimations() {
        double filterStartY = ROW_Y + FILTER_OFFSET;
        for (int i = 0; i < MAX_VISIBLE_ROWS; i++) {
            s_rowAnims[i].active = false;
            if (!s_slotClips[i].IsUndefined()) {
                RE::GFxValue posY;
                posY.SetNumber(filterStartY + i * ROW_HEIGHT);
                s_slotClips[i].SetMember("_y", posY);
            }
        }
    }

    // --- Internal: Lift visual ---

    static void ApplyLiftVisual(int a_slot) {
        if (a_slot < 0 || a_slot >= MAX_VISIBLE_ROWS || s_slotClips[a_slot].IsUndefined()) return;
        auto& row = s_slotClips[a_slot];

        RE::GFxValue scale;
        scale.SetNumber(LIFT_SCALE);
        row.SetMember("_xscale", scale);
        row.SetMember("_yscale", scale);

        double growth = ROW_W * (LIFT_SCALE - 100.0) / 100.0;
        RE::GFxValue posX;
        posX.SetNumber(ROW_X - growth / 2.0);
        row.SetMember("_x", posX);

        RE::GFxValue depthArg;
        depthArg.SetNumber(static_cast<double>(LIFT_DEPTH));
        row.Invoke("swapDepths", nullptr, &depthArg, 1);

        RE::GFxValue shadowClip;
        RE::GFxValue shadowArgs[2];
        shadowArgs[0].SetString("_shadow");
        shadowArgs[1].SetNumber(0.0);
        row.Invoke("createEmptyMovieClip", &shadowClip, shadowArgs, 2);

        if (!shadowClip.IsUndefined()) {
            RE::GFxValue fillArgs[2];
            fillArgs[0].SetNumber(static_cast<double>(LIFT_SHADOW_COLOR));
            fillArgs[1].SetNumber(static_cast<double>(LIFT_SHADOW_ALPHA));
            shadowClip.Invoke("beginFill", nullptr, fillArgs, 2);

            double sx = LIFT_SHADOW_OFFSET;
            double sy = LIFT_SHADOW_OFFSET;
            RE::GFxValue pt[2];
            pt[0].SetNumber(sx); pt[1].SetNumber(sy);
            shadowClip.Invoke("moveTo", nullptr, pt, 2);
            pt[0].SetNumber(ROW_W + sx);
            shadowClip.Invoke("lineTo", nullptr, pt, 2);
            pt[1].SetNumber(ROW_HEIGHT - 2.0 + sy);
            shadowClip.Invoke("lineTo", nullptr, pt, 2);
            pt[0].SetNumber(sx);
            shadowClip.Invoke("lineTo", nullptr, pt, 2);
            pt[1].SetNumber(sy);
            shadowClip.Invoke("lineTo", nullptr, pt, 2);
            shadowClip.Invoke("endFill", nullptr, nullptr, 0);
        }
        s_slotLifted[a_slot] = true;
    }

    static void ClearLiftVisual(int a_slot) {
        if (a_slot < 0 || a_slot >= MAX_VISIBLE_ROWS || s_slotClips[a_slot].IsUndefined()) return;
        auto& row = s_slotClips[a_slot];

        RE::GFxValue scale;
        scale.SetNumber(100.0);
        row.SetMember("_xscale", scale);
        row.SetMember("_yscale", scale);

        RE::GFxValue posX;
        posX.SetNumber(ROW_X);
        row.SetMember("_x", posX);

        RE::GFxValue depthArg;
        depthArg.SetNumber(200.0 + a_slot);
        row.Invoke("swapDepths", nullptr, &depthArg, 1);

        RE::GFxValue shadow;
        row.GetMember("_shadow", &shadow);
        if (!shadow.IsUndefined()) {
            shadow.Invoke("removeMovieClip", nullptr, nullptr, 0);
        }
        s_slotLifted[a_slot] = false;
    }

    // --- Internal: Count flash ---

    static void UpdateCountFlash() {
        if (!s_countFlashActive) return;
        auto elapsed = std::chrono::duration<float>(
            std::chrono::steady_clock::now() - s_countFlashStart).count();
        if (elapsed >= COUNT_FLASH_DURATION) {
            s_countFlashActive = false;
            s_countFlashIndices.clear();
            PopulateList();
        }
    }

    // --- Internal: Hold-A progress fill ---

    static void DrawHoldAProgress(float a_ratio) {
        RE::GFxValue* rowClip = GetSlotClipForDisplayIndex(s_selectedIndex);
        if (!rowClip || rowClip->IsUndefined()) return;

        RE::GFxValue fillClip;
        rowClip->GetMember("_holdAFill", &fillClip);
        if (fillClip.IsUndefined()) {
            RE::GFxValue args[2];
            args[0].SetString("_holdAFill");
            args[1].SetNumber(5.0);
            rowClip->Invoke("createEmptyMovieClip", &fillClip, args, 2);
        }
        if (fillClip.IsUndefined()) return;

        fillClip.Invoke("clear", nullptr, nullptr, 0);

        double fillW = ROW_W * static_cast<double>(a_ratio);
        if (fillW < 1.0) return;

        RE::GFxValue fillArgs[2];
        fillArgs[0].SetNumber(static_cast<double>(0x448844));  // green fill
        fillArgs[1].SetNumber(80.0);
        fillClip.Invoke("beginFill", nullptr, fillArgs, 2);

        RE::GFxValue pt[2];
        pt[0].SetNumber(0.0); pt[1].SetNumber(0.0);
        fillClip.Invoke("moveTo", nullptr, pt, 2);
        pt[0].SetNumber(fillW);
        fillClip.Invoke("lineTo", nullptr, pt, 2);
        pt[1].SetNumber(ROW_HEIGHT - 2.0);
        fillClip.Invoke("lineTo", nullptr, pt, 2);
        pt[0].SetNumber(0.0);
        fillClip.Invoke("lineTo", nullptr, pt, 2);
        pt[1].SetNumber(0.0);
        fillClip.Invoke("lineTo", nullptr, pt, 2);
        fillClip.Invoke("endFill", nullptr, nullptr, 0);
    }

    static void ClearHoldAProgress() {
        // Clear from all visible row slots (in case selection moved)
        for (int i = 0; i < MAX_VISIBLE_ROWS; i++) {
            if (s_slotClips[i].IsUndefined()) continue;
            RE::GFxValue fillClip;
            s_slotClips[i].GetMember("_holdAFill", &fillClip);
            if (!fillClip.IsUndefined()) {
                fillClip.Invoke("clear", nullptr, nullptr, 0);
            }
        }
    }

    // (Guide text rendering now owned by orchestrator)

    // --- Public API: Navigation ---

    void SelectLast() {
        int displayCount = static_cast<int>(s_displayRows.size());
        s_selectedIndex = displayCount;  // add row (last row in filter panel)
        PopulateList();
    }

    void ClearSelection() {
        s_subFocus = SubFocus::kNone;
        s_selectedIndex = -1;
        PopulateList();
    }

    FocusSignal SelectPrev() {
        int displayCount = static_cast<int>(s_displayRows.size());
        int newIndex = s_selectedIndex - 1;

        if (newIndex < 0) newIndex = 0;

        if (s_isReordering && s_liftedFamilyIndex >= 0) {
            // Reorder mode: we move families in s_filterRows, then rebuild display rows.
            // Only allow reorder when current and target are both root rows.
            bool currentIsRoot = (s_selectedIndex < displayCount &&
                                  s_displayRows[s_selectedIndex].childIndex == -1);
            bool targetIsRoot = (newIndex < displayCount &&
                                 s_displayRows[newIndex].childIndex == -1);

            if (currentIsRoot && targetIsRoot) {
                int curFamily = s_displayRows[s_selectedIndex].familyIndex;
                int newFamily = s_displayRows[newIndex].familyIndex;
                if (curFamily != newFamily && curFamily >= 0 &&
                    curFamily < static_cast<int>(s_filterRows.size()) &&
                    newFamily >= 0 && newFamily < static_cast<int>(s_filterRows.size()))
                {
                    // Count lifted family's display rows before swap (for animation delta)
                    int liftedRowCount = 0;
                    for (const auto& dr2 : s_displayRows) {
                        if (dr2.familyIndex == curFamily) liftedRowCount++;
                    }

                    std::swap(s_filterRows[curFamily], s_filterRows[newFamily]);
                    if (s_liftedFamilyIndex == curFamily) s_liftedFamilyIndex = newFamily;
                    else if (s_liftedFamilyIndex == newFamily) s_liftedFamilyIndex = curFamily;

                    BuildDisplayRows();
                    // Find the new display index for the lifted family
                    newIndex = FindDisplayIndexForFamily(s_liftedFamilyIndex);
                    if (newIndex < 0) newIndex = 0;

                    // Animate all rows of the displaced family (moved down by lifted family's size)
                    int displacedFamily = (s_liftedFamilyIndex == newFamily) ? curFamily : newFamily;
                    // SelectPrev: lifted moved up, displaced pushed down → displaced came from above
                    AnimateDisplacedFamily(displacedFamily, -liftedRowCount * ROW_HEIGHT);
                    RelocateLiftVisual();
                    s_callbacks.recalcPredictions();

                    // Scroll if needed
                    if (newIndex < s_scrollOffset) {
                        s_scrollOffset = newIndex;
                        SnapAllAnimations();
                    }
                }
            }
            // If target is a child row, skip over it (don't move)
            else if (!targetIsRoot && currentIsRoot) {
                // Skip up past all child rows to the previous root
                while (newIndex >= 0 && newIndex < displayCount &&
                       s_displayRows[newIndex].childIndex >= 0) {
                    newIndex--;
                }
                if (newIndex < 0) newIndex = 0;
                // Now try swapping again if it's a root
                if (newIndex < displayCount && s_displayRows[newIndex].childIndex == -1) {
                    int curFamily = s_displayRows[s_selectedIndex].familyIndex;
                    int newFamily = s_displayRows[newIndex].familyIndex;
                    if (curFamily != newFamily) {
                        int liftedRowCount = 0;
                        for (const auto& dr2 : s_displayRows) {
                            if (dr2.familyIndex == curFamily) liftedRowCount++;
                        }

                        std::swap(s_filterRows[curFamily], s_filterRows[newFamily]);
                        if (s_liftedFamilyIndex == curFamily) s_liftedFamilyIndex = newFamily;
                        else if (s_liftedFamilyIndex == newFamily) s_liftedFamilyIndex = curFamily;

                        BuildDisplayRows();
                        newIndex = FindDisplayIndexForFamily(s_liftedFamilyIndex);
                        if (newIndex < 0) newIndex = 0;

                        int displacedFamily = (s_liftedFamilyIndex == newFamily) ? curFamily : newFamily;
                        AnimateDisplacedFamily(displacedFamily, -liftedRowCount * ROW_HEIGHT);
                        RelocateLiftVisual();
                        s_callbacks.recalcPredictions();
                    }
                }
                if (newIndex < s_scrollOffset) {
                    s_scrollOffset = newIndex;
                    SnapAllAnimations();
                }
            }
        } else {
            // Normal scroll
            if (newIndex < s_scrollOffset && s_scrollOffset > 0) {
                s_scrollOffset--;
            }
        }

        s_selectedIndex = newIndex;
        s_autoExpanded = false;  // auto-expand is per-row; moving invalidates it
        // Clear sub-focus when landing on non-filter row (e.g. Add row)
        int dc = static_cast<int>(s_displayRows.size());
        if (s_subFocus != SubFocus::kNone && (newIndex < 0 || newIndex >= dc)) {
            s_subFocus = SubFocus::kNone;
        }
        PopulateList();
        return FocusSignal::kNone;
    }

    FocusSignal SelectNext() {
        int displayCount = static_cast<int>(s_displayRows.size());
        int totalCount = displayCount + 1;  // display rows + add row
        int newIndex = s_selectedIndex + 1;

        if (newIndex >= totalCount) {
            // Transition to action bar
            PopulateList();
            return FocusSignal::kToActionBar;
        }

        if (s_isReordering && s_liftedFamilyIndex >= 0) {
            // Reorder mode: only move between root rows
            bool currentIsRoot = (s_selectedIndex < displayCount &&
                                  s_displayRows[s_selectedIndex].childIndex == -1);
            bool targetIsRoot = (newIndex < displayCount &&
                                 s_displayRows[newIndex].childIndex == -1);

            if (currentIsRoot && targetIsRoot) {
                int curFamily = s_displayRows[s_selectedIndex].familyIndex;
                int newFamily = s_displayRows[newIndex].familyIndex;
                if (curFamily != newFamily && curFamily >= 0 &&
                    curFamily < static_cast<int>(s_filterRows.size()) &&
                    newFamily >= 0 && newFamily < static_cast<int>(s_filterRows.size()))
                {
                    // Count lifted family's display rows before swap (for animation delta)
                    int liftedRowCount = 0;
                    for (const auto& dr2 : s_displayRows) {
                        if (dr2.familyIndex == curFamily) liftedRowCount++;
                    }

                    std::swap(s_filterRows[curFamily], s_filterRows[newFamily]);
                    if (s_liftedFamilyIndex == curFamily) s_liftedFamilyIndex = newFamily;
                    else if (s_liftedFamilyIndex == newFamily) s_liftedFamilyIndex = curFamily;

                    BuildDisplayRows();
                    newIndex = FindDisplayIndexForFamily(s_liftedFamilyIndex);
                    if (newIndex < 0) newIndex = s_selectedIndex;

                    // Animate all rows of the displaced family (moved up by lifted family's size)
                    int displacedFamily = (s_liftedFamilyIndex == newFamily) ? curFamily : newFamily;
                    // SelectNext: lifted moved down, displaced pushed up → displaced came from below
                    AnimateDisplacedFamily(displacedFamily, liftedRowCount * ROW_HEIGHT);
                    RelocateLiftVisual();
                    s_callbacks.recalcPredictions();

                    int maxOffset = std::max(0, static_cast<int>(s_displayRows.size()) - MAX_VISIBLE_ROWS);
                    if (newIndex >= s_scrollOffset + MAX_VISIBLE_ROWS && s_scrollOffset < maxOffset) {
                        s_scrollOffset++;
                        SnapAllAnimations();
                    }
                }
            }
            // If target is a child row, skip past it to the next root
            else if (!targetIsRoot && currentIsRoot && newIndex < displayCount) {
                while (newIndex < displayCount && s_displayRows[newIndex].childIndex >= 0) {
                    newIndex++;
                }
                // newIndex is now either a root or == displayCount (add row)
                if (newIndex < displayCount && s_displayRows[newIndex].childIndex == -1) {
                    int curFamily = s_displayRows[s_selectedIndex].familyIndex;
                    int newFamily = s_displayRows[newIndex].familyIndex;
                    if (curFamily != newFamily) {
                        int liftedRowCount = 0;
                        for (const auto& dr2 : s_displayRows) {
                            if (dr2.familyIndex == curFamily) liftedRowCount++;
                        }

                        std::swap(s_filterRows[curFamily], s_filterRows[newFamily]);
                        if (s_liftedFamilyIndex == curFamily) s_liftedFamilyIndex = newFamily;
                        else if (s_liftedFamilyIndex == newFamily) s_liftedFamilyIndex = curFamily;

                        BuildDisplayRows();
                        newIndex = FindDisplayIndexForFamily(s_liftedFamilyIndex);
                        if (newIndex < 0) newIndex = s_selectedIndex;

                        int displacedFamily = (s_liftedFamilyIndex == newFamily) ? curFamily : newFamily;
                        AnimateDisplacedFamily(displacedFamily, liftedRowCount * ROW_HEIGHT);
                        RelocateLiftVisual();
                        s_callbacks.recalcPredictions();
                    }
                }
                int maxOffset = std::max(0, static_cast<int>(s_displayRows.size()) - MAX_VISIBLE_ROWS);
                if (newIndex >= s_scrollOffset + MAX_VISIBLE_ROWS && s_scrollOffset < maxOffset) {
                    s_scrollOffset++;
                    SnapAllAnimations();
                }
            }
        } else {
            int maxOffset = std::max(0, displayCount - MAX_VISIBLE_ROWS);
            if (newIndex < displayCount && newIndex >= s_scrollOffset + MAX_VISIBLE_ROWS && s_scrollOffset < maxOffset) {
                s_scrollOffset++;
            }
        }

        s_selectedIndex = newIndex;
        s_autoExpanded = false;  // auto-expand is per-row; moving invalidates it
        // Clear sub-focus when landing on non-filter row (e.g. Add row)
        int dc2 = static_cast<int>(s_displayRows.size());
        if (s_subFocus != SubFocus::kNone && (newIndex < 0 || newIndex >= dc2)) {
            s_subFocus = SubFocus::kNone;
        }
        PopulateList();
        return FocusSignal::kNone;
    }

    void ToggleExpand() {
        if (s_isReordering) return;
        if (!IsOnExpandableRoot()) return;

        const auto& dr = s_displayRows[s_selectedIndex];
        auto& family = s_filterRows[dr.familyIndex];
        bool expanding = !family.IsExpanded();
        family.SetExpanded(expanding);

        BuildDisplayRows();

        // Clamp scroll after display row count change
        int displayCount = static_cast<int>(s_displayRows.size());
        int maxOffset = std::max(0, displayCount - MAX_VISIBLE_ROWS);
        s_scrollOffset = std::clamp(s_scrollOffset, 0, maxOffset);

        // Keep selection on the same root row
        int newDispIdx = FindDisplayIndexForFamily(dr.familyIndex);
        if (newDispIdx >= 0) s_selectedIndex = newDispIdx;

        logger::info("ToggleExpand: '{}' now {}", family.GetData().name,
                     expanding ? "expanded" : "collapsed");
        PopulateList();
    }

    void ToggleLift() {
        s_subFocus = SubFocus::kNone;

        int displayCount = static_cast<int>(s_displayRows.size());
        if (s_selectedIndex < 0 || s_selectedIndex >= displayCount) {
            // Add row: activate instead
            SelectChest();
            return;
        }

        // Children are not draggable
        if (IsOnChildRow()) {
            SelectChest();
            return;
        }

        if (s_isReordering) {
            // Drop
            ClearAllLiftVisuals();
            s_isReordering = false;
            s_liftedFamilyIndex = -1;
            CommitToNetwork();
            s_callbacks.recalcPredictions();
            logger::info("Drop: placed '{}' at position {}",
                         GetDisplayRowName(s_selectedIndex), s_selectedIndex + 1);
        } else {
            // Lift — keep expanded state as-is
            s_isReordering = true;
            s_liftedFamilyIndex = s_displayRows[s_selectedIndex].familyIndex;
            ApplyLiftVisualToFamily();
            logger::info("Lift: picked up '{}' from position {}",
                         GetDisplayRowName(s_selectedIndex), s_selectedIndex + 1);
        }
        PopulateList();
    }

    void SelectChest() {
        int displayCount = static_cast<int>(s_displayRows.size());

        if (s_selectedIndex == displayCount) {
            // Add row -- create empty FilterRow and let it self-setup
            int familyCount = static_cast<int>(s_filterRows.size());
            int maxFamilyRoots = static_cast<int>(FilterRegistry::GetSingleton()->GetFamilyRoots().size());
            if (familyCount < maxFamilyRoots) {
                BeginAddFilter();
            }
            return;
        }

        if (s_selectedIndex >= 0 && s_selectedIndex < displayCount) {
            const auto& dr = s_displayRows[s_selectedIndex];
            if (dr.childIndex < 0) {
                // Root row -- if expandable, toggle expand instead of opening dropdown
                if (s_filterRows[dr.familyIndex].HasChildren()) {
                    ToggleExpand();
                    return;
                }
                // Single root (no children) -- open container dropdown
                auto ctx = MakeDropdownContext();
                int famIdx = dr.familyIndex;
                s_filterRows[famIdx].OpenContainerDropdown(ctx, -1,
                    [famIdx](bool confirmed) { HandleContainerResult(famIdx, -1, confirmed); });
            } else {
                // Child row -- open container dropdown for the child
                auto ctx = MakeDropdownContext();
                int famIdx = dr.familyIndex;
                int childIdx = dr.childIndex;
                s_filterRows[famIdx].OpenContainerDropdown(ctx, childIdx,
                    [famIdx, childIdx](bool confirmed) { HandleContainerResult(famIdx, childIdx, confirmed); });
            }
            return;
        }
    }

    void OpenLinkedContainer() {
        if (Dropdown::IsAnyOpen() || s_isReordering) return;
        if (s_selectedIndex < 0) return;

        int displayCount = static_cast<int>(s_displayRows.size());
        if (s_selectedIndex >= displayCount) return;  // add row or beyond

        RE::FormID containerFormID = GetDisplayRowContainerFormID(s_selectedIndex);
        if (containerFormID == 0) return;

        logger::info("OpenLinkedContainer: opening container {:08X} from display row {}", containerFormID, s_selectedIndex);

        SaveState();
        s_pendingReopen = true;
        s_callbacks.hideMenu();

        auto formID = containerFormID;
        SKSE::GetTaskInterface()->AddTask([formID]() {
            auto* container = RE::TESForm::LookupByID<RE::TESObjectREFR>(formID);
            if (!container) {
                logger::error("OpenLinkedContainer: container {:08X} not found", formID);
                s_pendingReopen = false;
                return;
            }
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) {
                s_pendingReopen = false;
                return;
            }
            if (s_callbacks.setBypass) s_callbacks.setBypass(formID);
            container->ActivateRef(player, 0, nullptr, 0, false);
        });
    }

    void OnContainerClosed() {
        if (!s_pendingReopen) return;
        auto networkName = ConfigState::GetNetworkName();
        SKSE::GetTaskInterface()->AddTask([networkName]() {
            s_callbacks.showMenu(networkName);
        });
    }

    // --- Internal: Dropdown context and result handlers ---

    static FilterRow::DropdownContext MakeDropdownContext() {
        int slot = s_selectedIndex - s_scrollOffset;
        double anchorY = ROW_Y + FILTER_OFFSET + slot * ROW_HEIGHT;
        return { s_movie, ROW_X, anchorY };
    }

    // Flash helper shared by both handlers
    static void FlashAndRepaint(int a_familyIndex) {
        s_countFlashIndices.clear();
        int flashDispIdx = FindDisplayIndexForFamily(a_familyIndex);
        if (flashDispIdx >= 0) s_countFlashIndices.insert(flashDispIdx);
        s_countFlashActive = true;
        s_countFlashStart = std::chrono::steady_clock::now();

        s_callbacks.recalcPredictions();
        PopulateList();

        if (s_countFlashActive) {
            for (int idx : s_countFlashIndices) {
                if (idx < 0) continue;
                int slot = idx - s_scrollOffset;
                if (slot >= 0 && slot < MAX_VISIBLE_ROWS && s_movie) {
                    std::string rowName = "row" + std::to_string(slot);
                    ScaleformUtil::SetTextFieldFormat(s_movie, "_root." + rowName + ".nameText", 14, COLOR_COUNT_FLASH);
                    ScaleformUtil::SetTextFieldFormat(s_movie, "_root." + rowName + ".containerText", 14, COLOR_COUNT_FLASH);
                    ScaleformUtil::SetTextFieldFormat(s_movie, "_root." + rowName + ".countText", 14, COLOR_COUNT_FLASH);
                }
            }
        }
    }

    // Place an empty FilterRow and tell it to self-setup
    static void BeginAddFilter() {
        s_filterRows.emplace_back();
        int newFamilyIdx = static_cast<int>(s_filterRows.size()) - 1;

        BuildDisplayRows();
        s_selectedIndex = FindDisplayIndexForFamily(newFamilyIdx);
        if (s_selectedIndex < 0) s_selectedIndex = static_cast<int>(s_displayRows.size());

        // Adjust scroll
        int displayCount = static_cast<int>(s_displayRows.size());
        if (s_selectedIndex >= 0 && s_selectedIndex < displayCount) {
            if (s_selectedIndex < s_scrollOffset) s_scrollOffset = s_selectedIndex;
            if (s_selectedIndex >= s_scrollOffset + MAX_VISIBLE_ROWS)
                s_scrollOffset = s_selectedIndex - MAX_VISIBLE_ROWS + 1;
            int maxOffset = std::max(0, displayCount - MAX_VISIBLE_ROWS);
            s_scrollOffset = std::clamp(s_scrollOffset, 0, maxOffset);
        }

        PopulateList();

        auto ctx = MakeDropdownContext();
        int famIdx = newFamilyIdx;
        s_filterRows[famIdx].BeginSetup(ctx, s_filterRows,
            [famIdx]() -> FilterRow::DropdownContext { return HandleSetupRefresh(famIdx); },
            [famIdx]() { HandleSetupCancelled(famIdx); });
    }

    // Called by FilterRow::BeginSetup when data changes (filter selected, container selected)
    static FilterRow::DropdownContext HandleSetupRefresh(int a_familyIndex) {
        CommitToNetwork();
        FlashAndRepaint(a_familyIndex);
        return MakeDropdownContext();
    }

    // Called by FilterRow::BeginSetup if user cancels at filter selection
    static void HandleSetupCancelled(int a_familyIndex) {
        s_subFocus = SubFocus::kNone;
        if (a_familyIndex >= 0 && a_familyIndex < static_cast<int>(s_filterRows.size())) {
            s_filterRows.erase(s_filterRows.begin() + a_familyIndex);
        }
        BuildDisplayRows();
        int displayCount = static_cast<int>(s_displayRows.size());
        s_selectedIndex = displayCount;  // back to add row
        int maxOffset = std::max(0, displayCount - MAX_VISIBLE_ROWS);
        s_scrollOffset = std::clamp(s_scrollOffset, 0, maxOffset);
        PopulateList();
    }

    // Called by FilterRow::OpenContainerDropdown callback
    static void HandleContainerResult(int a_familyIndex, int a_childIndex, bool a_confirmed) {
        if (!a_confirmed) {
            PopulateList();
            return;
        }

        CommitToNetwork();

        // Find the correct display row to flash (child or root)
        int flashDispIdx = -1;
        if (a_childIndex >= 0) {
            for (int i = 0; i < static_cast<int>(s_displayRows.size()); ++i) {
                if (s_displayRows[i].familyIndex == a_familyIndex &&
                    s_displayRows[i].childIndex == a_childIndex) {
                    flashDispIdx = i;
                    break;
                }
            }
        }
        if (flashDispIdx < 0) {
            flashDispIdx = FindDisplayIndexForFamily(a_familyIndex);
        }

        s_countFlashIndices.clear();
        if (flashDispIdx >= 0) s_countFlashIndices.insert(flashDispIdx);
        s_countFlashActive = true;
        s_countFlashStart = std::chrono::steady_clock::now();

        s_callbacks.recalcPredictions();
        PopulateList();

        if (s_countFlashActive) {
            for (int idx : s_countFlashIndices) {
                if (idx < 0) continue;
                int slot = idx - s_scrollOffset;
                if (slot >= 0 && slot < MAX_VISIBLE_ROWS && s_movie) {
                    std::string rowName = "row" + std::to_string(slot);
                    int fontSize = (s_displayRows[idx].childIndex >= 0) ? 12 : 14;
                    ScaleformUtil::SetTextFieldFormat(s_movie, "_root." + rowName + ".nameText", fontSize, COLOR_COUNT_FLASH);
                    ScaleformUtil::SetTextFieldFormat(s_movie, "_root." + rowName + ".containerText", fontSize, COLOR_COUNT_FLASH);
                    ScaleformUtil::SetTextFieldFormat(s_movie, "_root." + rowName + ".countText", fontSize, COLOR_COUNT_FLASH);
                }
            }
        }
    }

    // --- Public API: Mouse support ---

    HitZone HitTestMouse(float mx, float my, int& outIndex) {
        outIndex = -1;

        if (Dropdown::IsAnyOpen()) return HitZone::kNone;

        int displayCount = static_cast<int>(s_displayRows.size());
        int visibleRows = std::min(displayCount - s_scrollOffset, MAX_VISIBLE_ROWS);
        double filterStartY = ROW_Y + FILTER_OFFSET;

        for (int i = 0; i < visibleRows; i++) {
            double rowY = filterStartY + i * ROW_HEIGHT;
            if (mx >= ROW_X && mx <= ROW_X + ROW_W && my >= rowY && my <= rowY + ROW_HEIGHT) {
                int dispIdx = s_scrollOffset + i;
                RE::FormID containerID = GetDisplayRowContainerFormID(dispIdx);
                if (containerID != 0) {
                    double iconCenterX = ROW_X + ICON_CHEST_X + ICON_CHEST_SIZE / 2.0;
                    double iconCenterY = rowY + ICON_CHEST_Y + ICON_CHEST_SIZE / 2.0;
                    double halfHit = ICON_CHEST_HIT_SIZE / 2.0;
                    if (mx >= iconCenterX - halfHit && mx <= iconCenterX + halfHit &&
                        my >= iconCenterY - halfHit && my <= iconCenterY + halfHit) {
                        outIndex = dispIdx;
                        return HitZone::kChestIcon;
                    }
                }

                // Dropdown zone (container column area)
                {
                    double ddLeft = ROW_X + COL_CONTAINER_X;
                    double ddRight = ROW_X + COL_CONTAINER_X + COL_CONTAINER_W - 30.0;
                    if (mx >= ddLeft && mx <= ddRight) {
                        outIndex = dispIdx;
                        return HitZone::kDropdown;
                    }
                }

                outIndex = dispIdx;
                return HitZone::kFilterRow;
            }
        }

        {
            bool isEmpty = s_filterRows.empty();
            double addRowH = isEmpty ? (ROW_HEIGHT * 1.5) : ROW_HEIGHT;
            double addRowY = filterStartY + visibleRows * ROW_HEIGHT;
            if (mx >= ROW_X && mx <= ROW_X + ROW_W && my >= addRowY && my <= addRowY + addRowH) {
                outIndex = displayCount;
                return HitZone::kAddRow;
            }
        }

        {
            double trackX = PANEL_RIGHT - 14.0;
            double trackY = ROW_Y + FILTER_OFFSET;
            double trackH = MAX_VISIBLE_ROWS * ROW_HEIGHT;
            if (mx >= trackX && mx <= trackX + 12.0 && my >= trackY && my <= trackY + trackH) {
                return HitZone::kScrollTrack;
            }
        }

        return HitZone::kNone;
    }

    void UpdateHover(float mx, float my) {
        int oldHoverIndex = s_hoverIndex;
        int oldHoverChestIndex = s_hoverChestIndex;

        s_hoverIndex = -1;
        s_hoverChestIndex = -1;
        s_hoverActive = true;

        int hitIndex = -1;
        HitZone zone = HitTestMouse(mx, my, hitIndex);

        switch (zone) {
            case HitZone::kFilterRow:
            case HitZone::kAddRow:
            case HitZone::kDropdown:
                s_hoverIndex = hitIndex;
                break;
            case HitZone::kChestIcon:
                s_hoverIndex = hitIndex;
                s_hoverChestIndex = hitIndex;
                break;
            default:
                break;
        }

        bool changed = (s_hoverIndex != oldHoverIndex ||
                       s_hoverChestIndex != oldHoverChestIndex);

        if (changed) {
            PopulateList();
        }
    }

    void ClearHover() {
        if (!s_hoverActive) return;
        s_hoverActive = false;
        s_hoverIndex = -1;
        s_hoverChestIndex = -1;
    }

    void OnMouseMove() {
        auto [mx, my] = GetMousePos();

        if (s_dragPending && !s_dragActive) {
            auto elapsed = std::chrono::duration<float>(
                std::chrono::steady_clock::now() - s_dragStartTime).count();
            if (elapsed >= DRAG_START_DELAY) {
                int idx = s_dragPendingIndex;
                s_dragPending = false;
                s_dragPendingExpandable = false;  // drag won — suppress deferred expand

                // Only allow drag on root rows
                if (idx < static_cast<int>(s_displayRows.size()) &&
                    s_displayRows[idx].childIndex >= 0) {
                    // Child row -- cancel drag
                    return;
                }

                s_dragActive = true;
                s_dragSourceIndex = idx;
                s_hoverIndex = -1;
                s_isReordering = true;
                s_liftedFamilyIndex = s_displayRows[idx].familyIndex;
                s_lastDragScroll = std::chrono::steady_clock::now();

                // Compute cursor offset from row top for smooth follow
                double filterStartY = ROW_Y + FILTER_OFFSET;
                double rowTopY = filterStartY + (idx - s_scrollOffset) * ROW_HEIGHT;
                s_dragOffsetY = my - static_cast<float>(rowTopY);

                PopulateList();
                logger::info("Drag: engaged on '{}' at position {}",
                             GetDisplayRowName(idx), idx + 1);
            }
        }

        if (s_dragActive) {
            int displayCount = static_cast<int>(s_displayRows.size());
            double filterStartY = ROW_Y + FILTER_OFFSET;

            // Use center-of-row threshold for more natural swap trigger
            double relY = (my - filterStartY) / ROW_HEIGHT + 0.5;
            int targetSlot = static_cast<int>(relY) + s_scrollOffset;
            targetSlot = std::clamp(targetSlot, 0, displayCount - 1);

            // Only swap when cursor is directly on a root row — don't snap child→root.
            // This prevents oscillation when dragging past expanded families: the user
            // must drag past all children to reach the next root before a swap triggers.
            if (targetSlot != s_selectedIndex && targetSlot < displayCount &&
                s_displayRows[targetSlot].childIndex == -1)
            {
                int curFamily = s_displayRows[s_selectedIndex].familyIndex;
                int tgtFamily = s_displayRows[targetSlot].familyIndex;
                if (curFamily != tgtFamily && curFamily >= 0 && tgtFamily >= 0 &&
                    curFamily < static_cast<int>(s_filterRows.size()) &&
                    tgtFamily < static_cast<int>(s_filterRows.size()))
                {
                    // Count lifted family rows for animation delta
                    int liftedRowCount = 0;
                    for (const auto& dr2 : s_displayRows) {
                        if (dr2.familyIndex == curFamily) liftedRowCount++;
                    }

                    bool movedDown = (targetSlot > s_selectedIndex);

                    std::swap(s_filterRows[curFamily], s_filterRows[tgtFamily]);
                    if (s_liftedFamilyIndex == curFamily) s_liftedFamilyIndex = tgtFamily;
                    else if (s_liftedFamilyIndex == tgtFamily) s_liftedFamilyIndex = curFamily;

                    BuildDisplayRows();
                    int newIdx = FindDisplayIndexForFamily(s_liftedFamilyIndex);
                    if (newIdx >= 0) s_selectedIndex = newIdx;

                    // Animate displaced family sliding into vacated position
                    int displacedFamily = (s_liftedFamilyIndex == tgtFamily) ? curFamily : tgtFamily;
                    double delta = movedDown ? (liftedRowCount * ROW_HEIGHT) : (-liftedRowCount * ROW_HEIGHT);
                    AnimateDisplacedFamily(displacedFamily, delta);
                    RelocateLiftVisual();

                    s_callbacks.recalcPredictions();
                    PopulateList();
                }
            }

            // Smooth follow: clamp base Y to visible filter area, then position lifted family
            double baseY = static_cast<double>(my) - s_dragOffsetY;
            double minY = filterStartY;
            double maxY = filterStartY + (MAX_VISIBLE_ROWS - 1) * ROW_HEIGHT;
            baseY = std::clamp(baseY, minY, maxY);

            for (int di = 0; di < displayCount; ++di) {
                if (s_displayRows[di].familyIndex != s_liftedFamilyIndex) continue;
                int slot = di - s_scrollOffset;
                if (slot < 0 || slot >= MAX_VISIBLE_ROWS) continue;
                if (s_slotClips[slot].IsUndefined()) continue;
                int rowWithinFamily = 0;
                for (int k = 0; k < di; ++k) {
                    if (s_displayRows[k].familyIndex == s_liftedFamilyIndex) rowWithinFamily++;
                }
                double smoothY = baseY + rowWithinFamily * ROW_HEIGHT;
                // Cancel any active animation on this slot so it doesn't fight
                s_rowAnims[slot].active = false;
                RE::GFxValue posY;
                posY.SetNumber(smoothY);
                s_slotClips[slot].SetMember("_y", posY);
            }

            auto now = std::chrono::steady_clock::now();
            float elapsed = std::chrono::duration<float>(now - s_lastDragScroll).count();
            if (elapsed >= DRAG_SCROLL_INTERVAL) {
                int maxOffset = std::max(0, displayCount - MAX_VISIBLE_ROWS);
                if (my < filterStartY + ROW_HEIGHT && s_scrollOffset > 0) {
                    s_scrollOffset--;
                    s_lastDragScroll = now;
                    SnapAllAnimations();
                    PopulateList();
                } else if (my > filterStartY + (MAX_VISIBLE_ROWS - 1) * ROW_HEIGHT && s_scrollOffset < maxOffset) {
                    s_scrollOffset++;
                    s_lastDragScroll = now;
                    SnapAllAnimations();
                    PopulateList();
                }
            }
            return;
        }

        UpdateHover(mx, my);
    }

    MouseResult OnMouseDown() {
        auto [mx, my] = GetMousePos();
        int hitIndex = -1;
        HitZone zone = HitTestMouse(mx, my, hitIndex);

        if (HoldRemove::IsHolding()) { HoldRemove::Cancel(); return {}; }

        if (Dropdown::IsAnyOpen()) {
            Dropdown::GetOpen()->OnMouseClick(mx, my);
            return {};
        }

        auto now = std::chrono::steady_clock::now();
        int displayCount = static_cast<int>(s_displayRows.size());

        switch (zone) {
            case HitZone::kChestIcon:
                if (hitIndex >= 0) {
                    s_subFocus = SubFocus::kNone;
                    UpdateSelection(s_selectedIndex, hitIndex);
                    OpenLinkedContainer();
                }
                return {MouseSignal::kFocusToPanel, hitIndex};

            case HitZone::kDropdown:
                if (hitIndex >= 0 && hitIndex < displayCount && !s_isReordering) {
                    UpdateSelection(s_selectedIndex, hitIndex);
                    s_subFocus = SubFocus::kNone;
                    // Open container dropdown directly
                    const auto& dr = s_displayRows[hitIndex];
                    if (dr.childIndex < 0) {
                        auto ctx = MakeDropdownContext();
                        int famIdx = dr.familyIndex;
                        s_filterRows[famIdx].OpenContainerDropdown(ctx, -1,
                            [famIdx](bool confirmed) { HandleContainerResult(famIdx, -1, confirmed); });
                    } else {
                        auto ctx = MakeDropdownContext();
                        int famIdx = dr.familyIndex;
                        int childIdx = dr.childIndex;
                        s_filterRows[famIdx].OpenContainerDropdown(ctx, childIdx,
                            [famIdx, childIdx](bool confirmed) { HandleContainerResult(famIdx, childIdx, confirmed); });
                    }
                }
                return {MouseSignal::kFocusToPanel, hitIndex};

            case HitZone::kAddRow:
                if (hitIndex >= 0) {
                    s_subFocus = SubFocus::kNone;
                    UpdateSelection(s_selectedIndex, displayCount);
                    int familyCount = static_cast<int>(s_filterRows.size());
                    int maxFamilyRoots = static_cast<int>(FilterRegistry::GetSingleton()->GetFamilyRoots().size());
                    if (familyCount < maxFamilyRoots) {
                        BeginAddFilter();
                    }
                }
                return {MouseSignal::kFocusToPanel, hitIndex};

            case HitZone::kFilterRow:
                if (hitIndex >= 0) {
                    s_subFocus = SubFocus::kNone;
                    float sinceLast = std::chrono::duration<float>(now - s_lastClickTime).count();
                    if (hitIndex == s_lastClickIndex && sinceLast < DOUBLE_CLICK_TIME) {
                        s_lastClickIndex = -1;
                        if (!s_isReordering) {
                            UpdateSelection(s_selectedIndex, hitIndex);
                            // Double-click: open linked container
                            if (hitIndex < displayCount) {
                                RE::FormID containerID = GetDisplayRowContainerFormID(hitIndex);
                                if (containerID != 0) {
                                    OpenLinkedContainer();
                                }
                            }
                        }
                        return {MouseSignal::kFocusToPanel, hitIndex};
                    }
                    s_lastClickTime = now;
                    s_lastClickIndex = hitIndex;
                    UpdateSelection(s_selectedIndex, hitIndex);

                    // Defer expand/collapse — let drag win if user holds
                    s_dragPendingExpandable = false;
                    if (!s_isReordering && hitIndex < displayCount) {
                        const auto& dr = s_displayRows[hitIndex];
                        if (dr.childIndex < 0 && s_filterRows[dr.familyIndex].HasChildren()) {
                            s_dragPendingExpandable = true;
                        }
                    }

                    // Only allow drag on root rows
                    if (hitIndex < displayCount && s_displayRows[hitIndex].childIndex == -1) {
                        s_dragPending = true;
                        s_dragPendingIndex = hitIndex;
                        auto [mx2, my2] = GetMousePos();
                        s_dragStartX = mx2;
                        s_dragStartY = my2;
                        s_dragStartTime = now;
                    }
                }
                return {MouseSignal::kFocusToPanel, hitIndex};

            default:
                s_lastClickIndex = -1;
                break;
        }
        return {};
    }

    void OnMouseUp() {
        bool wasPending = s_dragPending;
        s_dragPending = false;
        if (s_dragActive) {
            s_dragActive = false;
            s_isReordering = false;
            logger::info("Drag-drop: placed '{}' at position {}",
                         GetDisplayRowName(s_selectedIndex), s_selectedIndex + 1);
            s_liftedFamilyIndex = -1;
            SnapAllAnimations();
            ClearAllLiftVisuals();
            PopulateList();
            CommitToNetwork();
            s_callbacks.recalcPredictions();
        } else if (wasPending && s_dragPendingExpandable) {
            // Click-release without drag on an expandable root — deferred expand/collapse
            ToggleExpand();
        }
        s_dragPendingExpandable = false;
    }

    void OnRightClick() {
        if (HoldRemove::IsHolding()) { HoldRemove::Cancel(); return; }
        if (Dropdown::IsAnyOpen()) { Dropdown::GetOpen()->Cancel(); return; }

        auto [mx, my] = GetMousePos();
        int hitIndex = -1;
        auto zone = HitTestMouse(mx, my, hitIndex);
        int displayCount = static_cast<int>(s_displayRows.size());

        if (zone == HitZone::kFilterRow && hitIndex >= 0 && hitIndex < displayCount) {
            // Only allow right-click remove on root rows
            if (s_displayRows[hitIndex].childIndex == -1) {
                s_selectedIndex = hitIndex;
                PopulateList();
                s_callbacks.resetRepeat();
                int familyIdx = s_displayRows[hitIndex].familyIndex;
                HoldRemove::TriggerImmediate(familyIdx, MakeRemoveCallback());
                return;
            }
        }

        s_callbacks.hideMenu();
    }

    void OnScrollWheel(int a_direction) {
        if (Dropdown::IsAnyOpen()) {
            auto* dd = Dropdown::GetOpen();
            if (a_direction < 0) dd->Prev();
            else dd->Next();
            return;
        }

        int displayCount = static_cast<int>(s_displayRows.size());
        int maxOffset = std::max(0, displayCount - MAX_VISIBLE_ROWS);
        int newOffset = std::clamp(s_scrollOffset + a_direction, 0, maxOffset);

        if (newOffset != s_scrollOffset) {
            s_scrollOffset = newOffset;
            SnapAllAnimations();
            PopulateList();
            UpdateScrollbar();
        }
    }

}  // namespace FilterPanel
