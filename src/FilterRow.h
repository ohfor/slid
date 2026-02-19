#pragma once

#include "Dropdown.h"
#include "MenuLayout.h"
#include "Network.h"

class FilterRow {
public:
    // Context passed by the layout manager so FilterRow can open dropdowns
    // without knowing about panel internals.
    struct DropdownContext {
        RE::GFxMovieView* movie = nullptr;
        double anchorX = 0.0;
        double anchorY = 0.0;
    };

    // Called when FilterRow's data changes. The layout manager repaints/commits
    // and returns a fresh DropdownContext for any subsequent dropdown.
    using OnRefresh = std::function<DropdownContext()>;

    // Called when a container dropdown closes.
    // a_confirmed: true = container was selected, false = cancelled.
    using OnContainerResult = std::function<void(bool a_confirmed)>;

    struct Data {
        std::string filterID;
        std::string name;           // display name
        std::string containerName;
        std::string location;
        RE::FormID containerFormID = 0;
        int count = 0;
        int predictedCount = -1;
        int contestedCount = 0;     // animated display value (set by FilterPanel)
        int contestAlpha = 0;       // 0-100 text alpha (for fade animation)
        uint32_t contestColor = 0;  // 0 = default COLOR_CONTEST, else override
    };

    FilterRow() = default;
    explicit FilterRow(Data a_data);

    // --- Rendering ---

    // Render root row into a slot clip
    void RenderRoot(RE::GFxMovieView* a_movie, RE::GFxValue& a_clip,
                    const std::string& a_clipPath,
                    int a_slotIndex, int a_displayNum,
                    bool a_selected, bool a_hovered, bool a_lifted,
                    bool a_chestHover, bool a_dropdownFocused = false,
                    bool a_contested = false) const;

    // Render a child row into a slot clip (indented, different tint)
    void RenderChild(RE::GFxMovieView* a_movie, RE::GFxValue& a_clip,
                     const std::string& a_clipPath,
                     int a_childIndex,
                     bool a_selected, bool a_hovered,
                     bool a_chestHover, bool a_dropdownFocused = false,
                     bool a_contested = false) const;

    // Legacy single-row render (calls RenderRoot)
    void Render(RE::GFxMovieView* a_movie, RE::GFxValue& a_clip,
                const std::string& a_clipPath,
                int a_slotIndex, int a_displayNum,
                bool a_selected, bool a_hovered, bool a_lifted,
                bool a_chestHover, bool a_dropdownFocused = false) const;

    // --- Family ---
    bool HasChildren() const;
    bool IsExpanded() const;
    void SetExpanded(bool a_expanded);
    const std::vector<Data>& GetChildren() const;
    std::vector<Data>& MutableChildren();
    void SetChildren(std::vector<Data> a_children);

    // Number of display rows this family occupies (1 if collapsed, 1+children if expanded)
    int GetDisplayRowCount() const;

    // --- Dropdown ---

    // Begin the add-filter setup flow on an already-placed empty FilterRow.
    // Opens filter dropdown → populates self → chains into container dropdown.
    // a_onRefresh: called when data changes (layout manager repaints, returns fresh context)
    // a_onCancelled: called if user cancels at filter selection (layout manager removes the row)
    void BeginSetup(const DropdownContext& a_ctx,
                    const std::vector<FilterRow>& a_existingRows,
                    OnRefresh a_onRefresh,
                    std::function<void()> a_onCancelled);

    // Open a container dropdown for this row's root (a_childIndex == -1) or a child.
    // On confirm, updates the row's container data and calls a_onResult.
    void OpenContainerDropdown(const DropdownContext& a_ctx, int a_childIndex,
                               OnContainerResult a_onResult);

    // --- Pipeline output ---

    // Flatten family to FilterStages.
    // Children before root — children specialize, root catches remainder.
    std::vector<FilterStage> ToFilterStages() const;

    // Legacy single-stage output (root only, no children)
    FilterStage ToFilterStage() const;

    // --- Data access ---
    const Data& GetData() const;
    Data& MutableData();

private:
    void DrawBackground(RE::GFxValue& a_clip, uint32_t a_color, int a_alpha) const;
    void DrawText(RE::GFxMovieView* a_movie, const std::string& a_clipPath,
                  const std::string& a_name,
                  int a_count, int a_predictedCount,
                  int a_contestedCount, int a_contestAlpha, uint32_t a_contestColor,
                  int a_displayNum, double a_nameIndent,
                  int a_fontSize = 14, uint32_t a_nameColor = 0xDDDDDD,
                  bool a_aggregate = false) const;
    void DrawChestIcon(RE::GFxValue& a_clip, bool a_linked, bool a_hover) const;
    void ClearChestIcon(RE::GFxValue& a_clip) const;
    void DrawExpandIndicator(RE::GFxValue& a_clip, bool a_expanded) const;
    void ClearExpandIndicator(RE::GFxValue& a_clip) const;

    Data m_data;
    std::vector<Data> m_children;
    bool m_expanded = false;
    mutable Dropdown m_dropdown;  // shared instance for filter/container popups (only one open at a time)
};
