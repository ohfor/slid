#pragma once

#include "ScaleformUtil.h"

#include <string>
#include <unordered_set>
#include <vector>

namespace ChecklistGrid {

    struct Item {
        std::string id;           // caller-defined identifier (e.g., filter ID string)
        std::string label;        // display text
        std::string description;  // optional — caller reads via GetCursorItem()
        bool        checked;
        bool        partial = false;  // group roots only: some (not all) children checked

        // Group fields for family hierarchy
        bool isGroupRoot = false;           // bold rendering, toggle propagates to children
        std::vector<int> groupChildren;     // linear indices of child items
        int groupParent = -1;               // linear index of parent (-1 = none / standalone)
        int indent = 0;                     // pixel indent for children
    };

    struct Config {
        int      columns       = 3;
        double   colWidth      = 176.0;
        double   rowHeight     = 22.0;
        double   checkSize     = 14.0;
        double   checkGap      = 6.0;
        int      maxLabelLen   = 22;
        int      maxVisibleRows = 0;    // 0 = show all, >0 = scroll when exceeded
        int      fontSize      = 12;

        // Checkbox colors
        uint32_t colorCheckBg      = 0x1A1A1A;
        uint32_t colorCheckBorder  = 0x666666;
        uint32_t colorCheckFill    = 0x88CC88;
        uint32_t colorCheckPartial = 0x88CC88;  // partial (some children) — same hue, lower alpha
        int      alphaCheck        = 80;
        int      alphaCheckPartial = 40;

        // Label colors
        uint32_t colorLabel        = 0xCCCCCC;
        uint32_t colorLabelDim     = 0x777777;

        // Cursor / hover
        uint32_t colorCursor       = 0x444444;
        uint32_t colorHover        = 0x333333;
        int      alphaCursor       = 60;

        // Scrollbar
        uint32_t colorScrollTrack  = 0x333333;
        uint32_t colorScrollThumb  = 0x777777;
        int      alphaScrollTrack  = 50;
        int      alphaScrollThumb  = 80;
    };

    class Grid {
    public:
        Grid() = default;
        Grid(RE::GFxMovieView* a_movie, const char* a_prefix, int a_baseDepth);

        void SetConfig(const Config& a_config);
        void SetItems(std::vector<Item> a_items);

        // Draw the grid at the given origin. Returns total height consumed.
        double Draw(double a_originX, double a_originY);

        // Redraw only changed elements (check states, cursor, hover).
        void Update();

        // Remove all Scaleform clips created by this grid.
        void Destroy();

        // Navigation (called by owning menu's input handler)
        void NavigateUp();
        void NavigateDown();
        void NavigateLeft();
        void NavigateRight();
        void Toggle();

        // Edge queries for menu to handle grid ↔ button transitions
        bool IsAtTop() const;
        bool IsAtBottom() const;
        void NavigateToBottom();

        // Current cursor item (for guide text, etc.)
        const Item* GetCursorItem() const;

        // Checked state as set of IDs
        std::unordered_set<std::string> GetCheckedIDs() const;
        void SetCheckedIDs(const std::unordered_set<std::string>& a_ids);
        void SetAll(bool a_checked);

        const std::vector<Item>& GetItems() const { return m_items; }
        int GetItemCount() const { return static_cast<int>(m_items.size()); }
        double GetComputedHeight() const { return m_maxRowsInAnyCol * m_config.rowHeight; }

        // Mouse support
        bool HitTest(float a_mx, float a_my) const;
        bool UpdateHover(float a_mx, float a_my);
        bool HandleClick(float a_mx, float a_my);
        void ClearHover();
        void ClearCursor();

        // Cursor position (for restoring after button bar return)
        int GetCursorCol() const { return m_cursorCol; }
        void SetCursorCol(int a_col);

    private:
        RE::GFxMovieView* m_movie = nullptr;
        std::string       m_prefix;
        int               m_baseDepth = 0;
        Config            m_config;
        std::vector<Item> m_items;

        // Computed layout
        std::vector<int> m_colCounts;
        int m_maxRowsInAnyCol = 0;

        // Position
        double m_originX = 0.0;
        double m_originY = 0.0;

        // Cursor
        int m_cursorCol = 0;
        int m_cursorRow = 0;

        // Scroll
        int m_scrollOffset = 0;

        // Hover
        int m_hoverIndex = -1;  // linear index or -1

        // Layout computation
        void ComputeLayout();

        // Index helpers
        int LinearIndex(int a_col, int a_row) const;
        std::pair<int, int> LinearToGrid(int a_index) const;
        const Item* ItemAt(int a_col, int a_row) const;
        Item* MutableItemAt(int a_col, int a_row);

        // Geometry helpers
        double CellX(int a_col) const;
        double CellY(int a_row) const;
        int VisibleRowCount() const;
        int MaxScrollOffset() const;

        // Drawing
        void DrawCell(int a_col, int a_row);
        void DrawScrollbar();
        void RemoveClip(const std::string& a_name);
    };
}
