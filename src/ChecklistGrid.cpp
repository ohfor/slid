#include "ChecklistGrid.h"

#include <algorithm>

namespace ChecklistGrid {

    Grid::Grid(RE::GFxMovieView* a_movie, const char* a_prefix, int a_baseDepth)
        : m_movie(a_movie), m_prefix(a_prefix), m_baseDepth(a_baseDepth) {}

    void Grid::SetConfig(const Config& a_config) {
        m_config = a_config;
    }

    void Grid::SetItems(std::vector<Item> a_items) {
        m_items = std::move(a_items);
        ComputeLayout();
        m_cursorCol = 0;
        m_cursorRow = 0;
        m_scrollOffset = 0;
        m_hoverIndex = -1;
    }

    // --- Layout computation ---

    void Grid::ComputeLayout() {
        int cols = m_config.columns;
        int total = static_cast<int>(m_items.size());

        m_colCounts.clear();
        m_colCounts.resize(cols, 0);

        if (total == 0 || cols == 0) {
            m_maxRowsInAnyCol = 0;
            return;
        }

        // Identify contiguous blocks (family groups).
        // A block starts at a group root and includes all consecutive children.
        // Standalone items (no parent, not a root) are single-item blocks.
        struct Block { int start; int count; };
        std::vector<Block> blocks;
        for (int i = 0; i < total; ) {
            if (m_items[i].isGroupRoot && !m_items[i].groupChildren.empty()) {
                // Root + consecutive children = one block
                int blockSize = 1 + static_cast<int>(m_items[i].groupChildren.size());
                blocks.push_back({i, blockSize});
                i += blockSize;
            } else {
                blocks.push_back({i, 1});
                i++;
            }
        }

        // Distribute blocks across columns using LPT (largest-first) heuristic.
        // Sorting by descending block size before greedy assignment minimizes
        // the height difference between columns.
        std::vector<int> colHeights(cols, 0);
        std::vector<Item> reordered;
        reordered.reserve(total);

        // Sort block indices by descending size (LPT heuristic for balanced columns)
        std::vector<size_t> sortedIdx(blocks.size());
        for (size_t i = 0; i < sortedIdx.size(); i++) sortedIdx[i] = i;
        std::sort(sortedIdx.begin(), sortedIdx.end(),
            [&](size_t a, size_t b) { return blocks[a].count > blocks[b].count; });

        // Assign blocks to shortest column
        std::vector<int> blockCols(blocks.size());
        for (size_t si = 0; si < sortedIdx.size(); si++) {
            size_t b = sortedIdx[si];
            int minCol = 0;
            for (int c = 1; c < cols; c++) {
                if (colHeights[c] < colHeights[minCol]) minCol = c;
            }
            blockCols[b] = minCol;
            colHeights[minCol] += blocks[b].count;
            m_colCounts[minCol] += blocks[b].count;
        }

        // Reorder items: all column 0 blocks first, then column 1, etc.
        reordered.clear();
        for (int c = 0; c < cols; c++) {
            for (size_t b = 0; b < blocks.size(); b++) {
                if (blockCols[b] == c) {
                    for (int i = blocks[b].start; i < blocks[b].start + blocks[b].count; i++) {
                        reordered.push_back(m_items[i]);
                    }
                }
            }
        }

        // Fix up group parent/child indices after reorder
        // Build old-to-new index map
        std::unordered_map<int, int> indexMap;
        {
            int newIdx = 0;
            for (int c = 0; c < cols; c++) {
                for (size_t b = 0; b < blocks.size(); b++) {
                    if (blockCols[b] == c) {
                        for (int i = blocks[b].start; i < blocks[b].start + blocks[b].count; i++) {
                            indexMap[i] = newIdx++;
                        }
                    }
                }
            }
        }

        // Remap indices
        for (auto& item : reordered) {
            if (item.groupParent >= 0) {
                auto it = indexMap.find(item.groupParent);
                item.groupParent = (it != indexMap.end()) ? it->second : -1;
            }
            for (auto& childIdx : item.groupChildren) {
                auto it = indexMap.find(childIdx);
                childIdx = (it != indexMap.end()) ? it->second : childIdx;
            }
        }

        m_items = std::move(reordered);
        m_maxRowsInAnyCol = *std::max_element(m_colCounts.begin(), m_colCounts.end());
    }

    // --- Index helpers ---

    int Grid::LinearIndex(int a_col, int a_row) const {
        int offset = 0;
        for (int c = 0; c < a_col && c < static_cast<int>(m_colCounts.size()); ++c) {
            offset += m_colCounts[c];
        }
        return offset + a_row;
    }

    std::pair<int, int> Grid::LinearToGrid(int a_index) const {
        int offset = 0;
        for (int c = 0; c < static_cast<int>(m_colCounts.size()); ++c) {
            if (a_index < offset + m_colCounts[c]) {
                return {c, a_index - offset};
            }
            offset += m_colCounts[c];
        }
        // Fallback: last cell
        int lastCol = static_cast<int>(m_colCounts.size()) - 1;
        return {lastCol, m_colCounts[lastCol] - 1};
    }

    const Item* Grid::ItemAt(int a_col, int a_row) const {
        int idx = LinearIndex(a_col, a_row);
        if (idx < 0 || idx >= static_cast<int>(m_items.size())) return nullptr;
        return &m_items[idx];
    }

    Item* Grid::MutableItemAt(int a_col, int a_row) {
        int idx = LinearIndex(a_col, a_row);
        if (idx < 0 || idx >= static_cast<int>(m_items.size())) return nullptr;
        return &m_items[idx];
    }

    // --- Geometry helpers ---

    double Grid::CellX(int a_col) const {
        return m_originX + a_col * m_config.colWidth;
    }

    double Grid::CellY(int a_row) const {
        return m_originY + (a_row - m_scrollOffset) * m_config.rowHeight;
    }

    int Grid::VisibleRowCount() const {
        if (m_config.maxVisibleRows > 0 && m_maxRowsInAnyCol > m_config.maxVisibleRows) {
            return m_config.maxVisibleRows;
        }
        return m_maxRowsInAnyCol;
    }

    int Grid::MaxScrollOffset() const {
        if (m_config.maxVisibleRows <= 0) return 0;
        return std::max(0, m_maxRowsInAnyCol - m_config.maxVisibleRows);
    }

    // --- Drawing ---

    void Grid::RemoveClip(const std::string& a_name) {
        if (!m_movie) return;
        RE::GFxValue root;
        m_movie->GetVariable(&root, "_root");
        if (root.IsUndefined()) return;
        RE::GFxValue existing;
        root.GetMember(a_name.c_str(), &existing);
        if (!existing.IsUndefined()) {
            existing.Invoke("removeMovieClip", nullptr, nullptr, 0);
        }
    }

    double Grid::Draw(double a_originX, double a_originY) {
        m_originX = a_originX;
        m_originY = a_originY;

        int cols = static_cast<int>(m_colCounts.size());
        int visRows = VisibleRowCount();

        for (int col = 0; col < cols; ++col) {
            for (int row = m_scrollOffset; row < m_scrollOffset + visRows && row < m_colCounts[col]; ++row) {
                DrawCell(col, row);
            }
        }

        if (MaxScrollOffset() > 0) {
            DrawScrollbar();
        }

        return visRows * m_config.rowHeight;
    }

    void Grid::Update() {
        int cols = static_cast<int>(m_colCounts.size());
        int visRows = VisibleRowCount();

        for (int col = 0; col < cols; ++col) {
            for (int row = m_scrollOffset; row < m_scrollOffset + visRows && row < m_colCounts[col]; ++row) {
                DrawCell(col, row);
            }
            // Clear cells that are no longer visible (short columns or scroll past end)
            for (int row = m_colCounts[col]; row < m_scrollOffset + visRows; ++row) {
                int idx = LinearIndex(col, row);
                RemoveClip(m_prefix + "HL" + std::to_string(idx));
                RemoveClip(m_prefix + "Chk" + std::to_string(idx));
                RemoveClip(m_prefix + "Lbl" + std::to_string(idx));
            }
        }

        if (MaxScrollOffset() > 0) {
            DrawScrollbar();
        }
    }

    void Grid::DrawCell(int a_col, int a_row) {
        if (!m_movie) return;

        int idx = LinearIndex(a_col, a_row);
        const Item* item = ItemAt(a_col, a_row);
        if (!item) return;

        double x = CellX(a_col);
        double y = CellY(a_row);

        // Skip cells outside visible range
        if (a_row < m_scrollOffset || a_row >= m_scrollOffset + VisibleRowCount()) return;

        bool selected = (m_cursorCol == a_col && m_cursorRow == a_row);
        bool hovered = (m_hoverIndex == idx);

        // --- Highlight background ---
        std::string hlName = m_prefix + "HL" + std::to_string(idx);
        if (selected || hovered) {
            ScaleformUtil::DrawFilledRect(m_movie, hlName.c_str(), m_baseDepth + idx,
                x - 2.0, y, m_config.colWidth, m_config.rowHeight,
                selected ? m_config.colorCursor : m_config.colorHover,
                selected ? m_config.alphaCursor : (m_config.alphaCursor - 10));
        } else {
            RemoveClip(hlName);
        }

        // --- Checkbox ---
        std::string chkName = m_prefix + "Chk" + std::to_string(idx);
        {
            RE::GFxValue root;
            m_movie->GetVariable(&root, "_root");
            if (root.IsUndefined()) return;

            // Remove old
            RE::GFxValue existing;
            root.GetMember(chkName.c_str(), &existing);
            if (!existing.IsUndefined()) {
                existing.Invoke("removeMovieClip", nullptr, nullptr, 0);
            }

            int N = static_cast<int>(m_items.size());
            RE::GFxValue clip;
            RE::GFxValue args[2];
            args[0].SetString(chkName.c_str());
            args[1].SetNumber(static_cast<double>(m_baseDepth + N + idx));
            root.Invoke("createEmptyMovieClip", &clip, args, 2);
            if (clip.IsUndefined()) return;

            // Box background
            RE::GFxValue fillArgs[2];
            fillArgs[0].SetNumber(static_cast<double>(m_config.colorCheckBg));
            fillArgs[1].SetNumber(static_cast<double>(m_config.alphaCheck));
            clip.Invoke("beginFill", nullptr, fillArgs, 2);

            double cx = x + 2.0;
            double cy = y + (m_config.rowHeight - m_config.checkSize) / 2.0;
            RE::GFxValue pt[2];
            pt[0].SetNumber(cx); pt[1].SetNumber(cy);
            clip.Invoke("moveTo", nullptr, pt, 2);
            pt[0].SetNumber(cx + m_config.checkSize);
            clip.Invoke("lineTo", nullptr, pt, 2);
            pt[1].SetNumber(cy + m_config.checkSize);
            clip.Invoke("lineTo", nullptr, pt, 2);
            pt[0].SetNumber(cx);
            clip.Invoke("lineTo", nullptr, pt, 2);
            pt[1].SetNumber(cy);
            clip.Invoke("lineTo", nullptr, pt, 2);
            clip.Invoke("endFill", nullptr, nullptr, 0);

            // Box border
            RE::GFxValue styleArgs[3];
            styleArgs[0].SetNumber(1.0);
            styleArgs[1].SetNumber(static_cast<double>(m_config.colorCheckBorder));
            styleArgs[2].SetNumber(80.0);
            clip.Invoke("lineStyle", nullptr, styleArgs, 3);
            pt[0].SetNumber(cx); pt[1].SetNumber(cy);
            clip.Invoke("moveTo", nullptr, pt, 2);
            pt[0].SetNumber(cx + m_config.checkSize);
            clip.Invoke("lineTo", nullptr, pt, 2);
            pt[1].SetNumber(cy + m_config.checkSize);
            clip.Invoke("lineTo", nullptr, pt, 2);
            pt[0].SetNumber(cx);
            clip.Invoke("lineTo", nullptr, pt, 2);
            pt[1].SetNumber(cy);
            clip.Invoke("lineTo", nullptr, pt, 2);

            // Checkmark: filled square (checked) or horizontal dash (partial)
            if (item->checked || item->partial) {
                double inset = 3.0;
                RE::GFxValue chkFill[2];
                if (item->checked) {
                    chkFill[0].SetNumber(static_cast<double>(m_config.colorCheckFill));
                    chkFill[1].SetNumber(90.0);
                } else {
                    chkFill[0].SetNumber(static_cast<double>(m_config.colorCheckPartial));
                    chkFill[1].SetNumber(static_cast<double>(m_config.alphaCheckPartial));
                }
                clip.Invoke("beginFill", nullptr, chkFill, 2);

                if (item->checked) {
                    // Full inset square
                    pt[0].SetNumber(cx + inset); pt[1].SetNumber(cy + inset);
                    clip.Invoke("moveTo", nullptr, pt, 2);
                    pt[0].SetNumber(cx + m_config.checkSize - inset);
                    clip.Invoke("lineTo", nullptr, pt, 2);
                    pt[1].SetNumber(cy + m_config.checkSize - inset);
                    clip.Invoke("lineTo", nullptr, pt, 2);
                    pt[0].SetNumber(cx + inset);
                    clip.Invoke("lineTo", nullptr, pt, 2);
                    pt[1].SetNumber(cy + inset);
                    clip.Invoke("lineTo", nullptr, pt, 2);
                } else {
                    // Horizontal dash (partial state)
                    double dashY = cy + m_config.checkSize / 2.0 - 1.5;
                    pt[0].SetNumber(cx + inset); pt[1].SetNumber(dashY);
                    clip.Invoke("moveTo", nullptr, pt, 2);
                    pt[0].SetNumber(cx + m_config.checkSize - inset);
                    clip.Invoke("lineTo", nullptr, pt, 2);
                    pt[1].SetNumber(dashY + 3.0);
                    clip.Invoke("lineTo", nullptr, pt, 2);
                    pt[0].SetNumber(cx + inset);
                    clip.Invoke("lineTo", nullptr, pt, 2);
                    pt[1].SetNumber(dashY);
                    clip.Invoke("lineTo", nullptr, pt, 2);
                }
                clip.Invoke("endFill", nullptr, nullptr, 0);
            }
        }

        // --- Label ---
        std::string lblName = m_prefix + "Lbl" + std::to_string(idx);
        std::string label = item->label;
        if (static_cast<int>(label.length()) > m_config.maxLabelLen) {
            label = label.substr(0, m_config.maxLabelLen - 2) + "..";
        }

        double itemIndent = static_cast<double>(item->indent);
        int fontSize = m_config.fontSize;
        uint32_t labelColor = (item->checked || item->partial) ? m_config.colorLabel : m_config.colorLabelDim;

        // Group roots get slightly brighter text when fully checked
        if (item->isGroupRoot && !item->groupChildren.empty()) {
            if (item->checked) labelColor = 0xEEEEEE;
        }

        int N = static_cast<int>(m_items.size());
        ScaleformUtil::CreateLabel(m_movie, lblName.c_str(), m_baseDepth + 2 * N + idx,
            x + m_config.checkSize + m_config.checkGap + 4.0 + itemIndent, y + 2.0,
            m_config.colWidth - m_config.checkSize - m_config.checkGap - 8.0 - itemIndent, m_config.rowHeight,
            label.c_str(), fontSize, labelColor);
    }

    void Grid::DrawScrollbar() {
        if (!m_movie || MaxScrollOffset() <= 0) return;

        int N = static_cast<int>(m_items.size());
        int cols = static_cast<int>(m_colCounts.size());
        int visRows = VisibleRowCount();

        double trackX = m_originX + cols * m_config.colWidth + 4.0;
        double trackY = m_originY;
        double trackH = visRows * m_config.rowHeight;
        double trackW = 4.0;

        // Track
        std::string trackName = m_prefix + "SBTrack";
        ScaleformUtil::DrawFilledRect(m_movie, trackName.c_str(), m_baseDepth + 3 * N,
            trackX, trackY, trackW, trackH,
            m_config.colorScrollTrack, m_config.alphaScrollTrack);

        // Thumb
        double thumbRatio = static_cast<double>(visRows) / static_cast<double>(m_maxRowsInAnyCol);
        double thumbH = std::max(20.0, trackH * thumbRatio);
        double scrollRange = trackH - thumbH;
        int maxOff = MaxScrollOffset();
        double thumbY = trackY + (maxOff > 0 ? scrollRange * m_scrollOffset / maxOff : 0.0);

        std::string thumbName = m_prefix + "SBThumb";
        ScaleformUtil::DrawFilledRect(m_movie, thumbName.c_str(), m_baseDepth + 3 * N + 1,
            trackX, thumbY, trackW, thumbH,
            m_config.colorScrollThumb, m_config.alphaScrollThumb);
    }

    void Grid::Destroy() {
        if (!m_movie) return;

        int N = static_cast<int>(m_items.size());
        for (int i = 0; i < N; ++i) {
            RemoveClip(m_prefix + "HL" + std::to_string(i));
            RemoveClip(m_prefix + "Chk" + std::to_string(i));
            RemoveClip(m_prefix + "Lbl" + std::to_string(i));
        }
        RemoveClip(m_prefix + "SBTrack");
        RemoveClip(m_prefix + "SBThumb");
    }

    // --- Navigation ---

    void Grid::NavigateUp() {
        if (m_cursorRow > m_scrollOffset) {
            m_cursorRow--;
        } else if (m_scrollOffset > 0) {
            m_scrollOffset--;
            m_cursorRow--;
        }
    }

    void Grid::NavigateDown() {
        int colHeight = (m_cursorCol < static_cast<int>(m_colCounts.size()))
                        ? m_colCounts[m_cursorCol] : 0;
        if (m_cursorRow < colHeight - 1) {
            m_cursorRow++;
            if (m_cursorRow >= m_scrollOffset + VisibleRowCount()) {
                m_scrollOffset++;
            }
        }
    }

    void Grid::NavigateLeft() {
        if (m_cursorCol > 0) {
            m_cursorCol--;
            if (m_cursorRow >= m_colCounts[m_cursorCol]) {
                m_cursorRow = m_colCounts[m_cursorCol] - 1;
            }
        }
    }

    void Grid::NavigateRight() {
        if (m_cursorCol < static_cast<int>(m_colCounts.size()) - 1) {
            m_cursorCol++;
            if (m_cursorRow >= m_colCounts[m_cursorCol]) {
                m_cursorRow = m_colCounts[m_cursorCol] - 1;
            }
        }
    }

    // Helper: recompute a group root's checked/partial state from children
    static void UpdateGroupRootState(std::vector<Item>& a_items, int a_parentIdx) {
        auto& parent = a_items[a_parentIdx];
        bool anyChecked = false;
        bool allChecked = true;
        for (int childIdx : parent.groupChildren) {
            if (childIdx >= 0 && childIdx < static_cast<int>(a_items.size())) {
                if (a_items[childIdx].checked) {
                    anyChecked = true;
                } else {
                    allChecked = false;
                }
            }
        }
        parent.checked = allChecked && anyChecked;
        parent.partial = anyChecked && !allChecked;
    }

    void Grid::Toggle() {
        Item* item = MutableItemAt(m_cursorCol, m_cursorRow);
        if (!item) return;

        if (item->isGroupRoot) {
            // Checked or partial → uncheck all; unchecked → check all
            bool newState = !item->checked && !item->partial;
            item->checked = newState;
            item->partial = false;
            for (int childIdx : item->groupChildren) {
                if (childIdx >= 0 && childIdx < static_cast<int>(m_items.size())) {
                    m_items[childIdx].checked = newState;
                }
            }
        } else {
            item->checked = !item->checked;
            if (item->groupParent >= 0 && item->groupParent < static_cast<int>(m_items.size())) {
                UpdateGroupRootState(m_items, item->groupParent);
            }
        }
    }

    bool Grid::IsAtTop() const {
        return m_cursorRow == 0;
    }

    bool Grid::IsAtBottom() const {
        int colHeight = (m_cursorCol < static_cast<int>(m_colCounts.size()))
                        ? m_colCounts[m_cursorCol] : 0;
        return m_cursorRow >= colHeight - 1;
    }

    void Grid::NavigateToBottom() {
        if (m_cursorCol < 0) m_cursorCol = 0;
        int colHeight = (m_cursorCol < static_cast<int>(m_colCounts.size()))
                        ? m_colCounts[m_cursorCol] : 0;
        m_cursorRow = std::max(0, colHeight - 1);
        // Ensure visible
        if (m_cursorRow >= m_scrollOffset + VisibleRowCount()) {
            m_scrollOffset = m_cursorRow - VisibleRowCount() + 1;
        }
    }

    const Item* Grid::GetCursorItem() const {
        return ItemAt(m_cursorCol, m_cursorRow);
    }

    void Grid::SetCursorCol(int a_col) {
        if (a_col >= 0 && a_col < static_cast<int>(m_colCounts.size())) {
            m_cursorCol = a_col;
            if (m_cursorRow >= m_colCounts[m_cursorCol]) {
                m_cursorRow = m_colCounts[m_cursorCol] - 1;
            }
        }
    }

    // --- Checked state ---

    std::unordered_set<std::string> Grid::GetCheckedIDs() const {
        std::unordered_set<std::string> result;
        for (const auto& item : m_items) {
            if (item.checked) {
                result.insert(item.id);
            }
        }
        return result;
    }

    void Grid::SetCheckedIDs(const std::unordered_set<std::string>& a_ids) {
        for (auto& item : m_items) {
            item.checked = a_ids.count(item.id) > 0;
            item.partial = false;
        }
        // Update root checked/partial state based on children
        for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
            if (m_items[i].isGroupRoot && !m_items[i].groupChildren.empty()) {
                UpdateGroupRootState(m_items, i);
            }
        }
    }

    void Grid::SetAll(bool a_checked) {
        for (auto& item : m_items) {
            item.checked = a_checked;
            item.partial = false;
        }
    }

    // --- Mouse ---

    bool Grid::HitTest(float a_mx, float a_my) const {
        int cols = static_cast<int>(m_colCounts.size());
        int visRows = VisibleRowCount();
        double totalW = cols * m_config.colWidth;
        double totalH = visRows * m_config.rowHeight;

        return a_mx >= m_originX && a_mx < m_originX + totalW &&
               a_my >= m_originY && a_my < m_originY + totalH;
    }

    bool Grid::UpdateHover(float a_mx, float a_my) {
        int oldHover = m_hoverIndex;
        m_hoverIndex = -1;

        int cols = static_cast<int>(m_colCounts.size());
        int visRows = VisibleRowCount();

        for (int col = 0; col < cols; ++col) {
            for (int visRow = 0; visRow < visRows; ++visRow) {
                int dataRow = m_scrollOffset + visRow;
                if (dataRow >= m_colCounts[col]) continue;

                double cx = CellX(col);
                double cy = m_originY + visRow * m_config.rowHeight;
                if (a_mx >= cx && a_mx < cx + m_config.colWidth &&
                    a_my >= cy && a_my < cy + m_config.rowHeight) {
                    m_hoverIndex = LinearIndex(col, dataRow);
                    // Sync cursor to hovered cell
                    m_cursorCol = col;
                    m_cursorRow = dataRow;
                }
            }
        }

        return m_hoverIndex != oldHover;
    }

    bool Grid::HandleClick(float a_mx, float a_my) {
        int cols = static_cast<int>(m_colCounts.size());
        int visRows = VisibleRowCount();

        for (int col = 0; col < cols; ++col) {
            for (int visRow = 0; visRow < visRows; ++visRow) {
                int dataRow = m_scrollOffset + visRow;
                if (dataRow >= m_colCounts[col]) continue;

                double cx = CellX(col);
                double cy = m_originY + visRow * m_config.rowHeight;
                if (a_mx >= cx && a_mx < cx + m_config.colWidth &&
                    a_my >= cy && a_my < cy + m_config.rowHeight) {
                    m_cursorCol = col;
                    m_cursorRow = dataRow;
                    Toggle();
                    return true;
                }
            }
        }
        return false;
    }

    void Grid::ClearHover() {
        m_hoverIndex = -1;
    }

    void Grid::ClearCursor() {
        m_cursorRow = -1;
        m_cursorCol = -1;
    }
}
