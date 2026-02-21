#include "Dropdown.h"
#include "MenuLayout.h"
#include "ScaleformUtil.h"

// --- Static member ---
Dropdown* Dropdown::s_openInstance = nullptr;

// Scaleform clip depths (600-619 range, non-overlapping with ConfirmDialog 500-505)
static constexpr int DEPTH_DIM         = 600;
static constexpr int DEPTH_BG          = 601;
static constexpr int DEPTH_BORDER      = 602;
static constexpr int DEPTH_TITLE       = 603;
static constexpr int DEPTH_SEP         = 604;
static constexpr int DEPTH_ROW_BASE    = 610;  // 610..617
static constexpr int DEPTH_SCROLL_TRACK = 618;
static constexpr int DEPTH_SCROLL_THUMB = 619;

// Popup padding
static constexpr double PAD = 12.0;
static constexpr double TITLE_H = 28.0;
static constexpr double SEP_H = 1.0;

// Colors
static constexpr uint32_t COLOR_BG       = 0x0A0A0A;
static constexpr uint32_t COLOR_BORDER   = 0x666666;
static constexpr uint32_t COLOR_TITLE    = 0xFFFFFF;
static constexpr uint32_t COLOR_DISABLED = 0x555555;
static constexpr uint32_t COLOR_SUBLABEL = 0x777777;
static constexpr uint32_t COLOR_ROW_SEL  = 0x444444;
static constexpr uint32_t COLOR_ROW_NORM = 0x111111;
static constexpr uint32_t COLOR_ROW_HOV  = 0x2A2A2A;
static constexpr int ALPHA_BG            = 95;
static constexpr int ALPHA_DIM           = 50;
static constexpr int ALPHA_ROW_SEL       = 85;
static constexpr int ALPHA_ROW_NORM      = 60;
static constexpr int ALPHA_ROW_HOV       = 75;

// --- Static API ---

bool Dropdown::IsAnyOpen() { return s_openInstance != nullptr; }
Dropdown* Dropdown::GetOpen() { return s_openInstance; }

// --- Value access ---

const std::string& Dropdown::GetSelectedId() const { return m_selectedId; }
const std::string& Dropdown::GetSelectedLabel() const { return m_selectedLabel; }

// --- Closed state ---

void Dropdown::SetValue(const std::string& a_id, const std::string& a_label,
                        const std::string& a_sublabel, uint32_t a_closedColor) {
    m_selectedId = a_id;
    m_selectedLabel = a_label;
    m_selectedSublabel = a_sublabel;
    m_closedColorOverride = a_closedColor;
}

// Closed-state dropdown control colors
static constexpr uint32_t DD_CLOSED_BORDER      = 0x444444;
static constexpr uint32_t DD_CLOSED_BORDER_FOCUS = 0x888888;
static constexpr uint32_t DD_CLOSED_BG          = 0x111111;
static constexpr int      DD_CLOSED_ALPHA       = 70;
static constexpr uint32_t DD_CLOSED_CHEVRON     = 0x666666;
static constexpr uint32_t DD_CLOSED_CHEVRON_FOCUS = 0xAAAAAA;
static constexpr uint32_t DD_CLOSED_UNLINKED    = 0xCC8888;
static constexpr uint32_t DD_CLOSED_LINKED      = 0xDDDDDD;
static constexpr uint32_t DD_CLOSED_SUBLABEL    = 0x777777;
static constexpr double   DD_CHEVRON_SIZE       = 6.0;
static constexpr double   DD_CLOSED_PAD         = 6.0;

void Dropdown::RenderClosed(RE::GFxMovieView* a_movie, RE::GFxValue& a_parentClip,
                            const std::string& a_parentPath,
                            double a_x, double a_y, double a_w, double a_h,
                            bool a_focused) const
{
    if (!a_movie) return;

    // Get or create the dropdown sub-clip inside the parent
    RE::GFxValue ddClip;
    a_parentClip.GetMember("_dd", &ddClip);
    if (ddClip.IsUndefined()) {
        RE::GFxValue args[2];
        args[0].SetString("_dd");
        args[1].SetNumber(25.0);  // depth within parent clip
        a_parentClip.Invoke("createEmptyMovieClip", &ddClip, args, 2);
    }
    if (ddClip.IsUndefined()) return;

    // Position the sub-clip
    RE::GFxValue posX, posY;
    posX.SetNumber(a_x);
    posY.SetNumber(a_y);
    ddClip.SetMember("_x", posX);
    ddClip.SetMember("_y", posY);

    // Draw background + border using Drawing API
    ddClip.Invoke("clear", nullptr, nullptr, 0);

    // Background fill
    {
        RE::GFxValue fillArgs[2];
        fillArgs[0].SetNumber(static_cast<double>(DD_CLOSED_BG));
        fillArgs[1].SetNumber(static_cast<double>(DD_CLOSED_ALPHA));
        ddClip.Invoke("beginFill", nullptr, fillArgs, 2);

        RE::GFxValue pt[2];
        pt[0].SetNumber(0.0); pt[1].SetNumber(0.0);
        ddClip.Invoke("moveTo", nullptr, pt, 2);
        pt[0].SetNumber(a_w);
        ddClip.Invoke("lineTo", nullptr, pt, 2);
        pt[1].SetNumber(a_h);
        ddClip.Invoke("lineTo", nullptr, pt, 2);
        pt[0].SetNumber(0.0);
        ddClip.Invoke("lineTo", nullptr, pt, 2);
        pt[1].SetNumber(0.0);
        ddClip.Invoke("lineTo", nullptr, pt, 2);
        ddClip.Invoke("endFill", nullptr, nullptr, 0);
    }

    // Border
    {
        uint32_t borderColor = a_focused ? DD_CLOSED_BORDER_FOCUS : DD_CLOSED_BORDER;
        RE::GFxValue styleArgs[3];
        styleArgs[0].SetNumber(1.0);
        styleArgs[1].SetNumber(static_cast<double>(borderColor));
        styleArgs[2].SetNumber(100.0);
        ddClip.Invoke("lineStyle", nullptr, styleArgs, 3);

        RE::GFxValue pt[2];
        pt[0].SetNumber(0.0); pt[1].SetNumber(0.0);
        ddClip.Invoke("moveTo", nullptr, pt, 2);
        pt[0].SetNumber(a_w);
        ddClip.Invoke("lineTo", nullptr, pt, 2);
        pt[1].SetNumber(a_h);
        ddClip.Invoke("lineTo", nullptr, pt, 2);
        pt[0].SetNumber(0.0);
        ddClip.Invoke("lineTo", nullptr, pt, 2);
        pt[1].SetNumber(0.0);
        ddClip.Invoke("lineTo", nullptr, pt, 2);
    }

    // Chevron (small down-pointing triangle on the right side)
    {
        uint32_t chevColor = a_focused ? DD_CLOSED_CHEVRON_FOCUS : DD_CLOSED_CHEVRON;

        // Reset line style (no outline on chevron)
        RE::GFxValue noLineArgs[3];
        noLineArgs[0].SetNumber(0.0);
        noLineArgs[1].SetNumber(0.0);
        noLineArgs[2].SetNumber(0.0);
        ddClip.Invoke("lineStyle", nullptr, noLineArgs, 3);

        RE::GFxValue fillArgs[2];
        fillArgs[0].SetNumber(static_cast<double>(chevColor));
        fillArgs[1].SetNumber(100.0);
        ddClip.Invoke("beginFill", nullptr, fillArgs, 2);

        double chevX = a_w - DD_CLOSED_PAD - DD_CHEVRON_SIZE;
        double chevY = (a_h - DD_CHEVRON_SIZE * 0.6) / 2.0;

        RE::GFxValue pt[2];
        pt[0].SetNumber(chevX); pt[1].SetNumber(chevY);
        ddClip.Invoke("moveTo", nullptr, pt, 2);
        pt[0].SetNumber(chevX + DD_CHEVRON_SIZE);
        ddClip.Invoke("lineTo", nullptr, pt, 2);
        pt[0].SetNumber(chevX + DD_CHEVRON_SIZE / 2.0);
        pt[1].SetNumber(chevY + DD_CHEVRON_SIZE * 0.6);
        ddClip.Invoke("lineTo", nullptr, pt, 2);
        pt[0].SetNumber(chevX); pt[1].SetNumber(chevY);
        ddClip.Invoke("lineTo", nullptr, pt, 2);

        ddClip.Invoke("endFill", nullptr, nullptr, 0);
    }

    // Update the existing containerText field with value text
    // (reuse the TextField that FilterPanel already created in the slot clip)
    bool isLinked = !m_selectedId.empty() && m_selectedId != "0";
    uint32_t textColor;
    if (m_closedColorOverride != 0) {
        textColor = m_closedColorOverride;
    } else {
        textColor = isLinked ? DD_CLOSED_LINKED : DD_CLOSED_UNLINKED;
    }

    std::string textPath = a_parentPath + ".containerText";
    ScaleformUtil::SetTextFieldFormat(a_movie, textPath, 14, textColor);

    RE::GFxValue tf;
    a_movie->GetVariable(&tf, textPath.c_str());
    if (!tf.IsUndefined()) {
        // Position text inside the dropdown box with left margin, vertically centered
        double textX = a_x + DD_CLOSED_PAD;
        double textW = a_w - DD_CLOSED_PAD * 2 - DD_CHEVRON_SIZE - 4.0;
        double textY = a_y + (a_h - 18.0) / 2.0;  // 18px ~= 14pt line height

        RE::GFxValue xVal, yVal, wVal;
        xVal.SetNumber(textX);
        yVal.SetNumber(textY);
        wVal.SetNumber(textW);
        tf.SetMember("_x", xVal);
        tf.SetMember("_y", yVal);
        tf.SetMember("_width", wVal);

        std::string displayText = m_selectedLabel;
        if (!m_selectedSublabel.empty() && isLinked) {
            displayText += "  (" + m_selectedSublabel + ")";
        }

        // Truncate with ellipsis if text exceeds available width
        // Arial 14pt averages ~7.5px per character
        int maxChars = static_cast<int>(textW / 7.5);
        if (maxChars > 3 && static_cast<int>(displayText.length()) > maxChars) {
            displayText = displayText.substr(0, maxChars - 3) + "...";
        }

        RE::GFxValue textVal;
        textVal.SetString(displayText.c_str());
        tf.SetMember("text", textVal);
    }
}

// --- Public API ---

void Dropdown::Open(RE::GFxMovieView* a_movie, double a_anchorX, double a_anchorY,
                    const Config& a_config, std::vector<Entry> a_entries, Callback a_callback) {
    if (s_openInstance) {
        s_openInstance->Cancel();
    }

    m_movie = a_movie;
    m_anchorX = a_anchorX;
    m_anchorY = a_anchorY;
    m_config = a_config;
    m_entries = std::move(a_entries);
    m_callback = std::move(a_callback);
    m_open = true;
    m_scrollOffset = 0;
    m_hoverIndex = -1;

    s_openInstance = this;

    int entryCount = static_cast<int>(m_entries.size());
    m_visibleCount = std::min(entryCount, m_config.maxVisible);

    // Pre-select
    if (m_config.preSelect >= 0 && m_config.preSelect < entryCount) {
        m_cursorIndex = m_config.preSelect;
    } else {
        m_cursorIndex = FindNextEnabled(-1, 1);
        if (m_cursorIndex < 0) m_cursorIndex = 0;
    }

    // Ensure selected is scrolled into view
    if (m_cursorIndex >= m_visibleCount) {
        m_scrollOffset = m_cursorIndex - m_visibleCount + 1;
    }

    DrawPopup();
    PopulateRows();

    logger::info("Dropdown: opened '{}' with {} entries, selected={}",
                 m_config.title, entryCount, m_cursorIndex);
}

bool Dropdown::IsOpen() const { return m_open; }

void Dropdown::Prev() {
    if (!m_open) return;
    if (m_cursorIndex <= 0) return;
    m_cursorIndex--;
    if (m_cursorIndex < m_scrollOffset) {
        m_scrollOffset = m_cursorIndex;
    }
    PopulateRows();
}

void Dropdown::Next() {
    if (!m_open) return;
    int count = static_cast<int>(m_entries.size());
    if (m_cursorIndex >= count - 1) return;
    m_cursorIndex++;
    if (m_cursorIndex >= m_scrollOffset + m_visibleCount) {
        m_scrollOffset = m_cursorIndex - m_visibleCount + 1;
    }
    PopulateRows();
}

void Dropdown::Confirm() {
    if (!m_open) return;
    int entryCount = static_cast<int>(m_entries.size());
    if (m_cursorIndex < 0 || m_cursorIndex >= entryCount) return;
    if (!m_entries[m_cursorIndex].enabled) return;

    m_open = false;
    if (s_openInstance == this) s_openInstance = nullptr;

    auto id = m_entries[m_cursorIndex].id;
    auto label = m_entries[m_cursorIndex].label;
    auto idx = m_cursorIndex;
    DestroyPopupVisuals();

    m_selectedId = id;
    m_selectedLabel = label;

    auto cb = std::move(m_callback);
    m_callback = nullptr;
    m_movie = nullptr;
    m_entries.clear();

    if (cb) cb(true, idx, id);
}

void Dropdown::Cancel() {
    if (!m_open) return;
    m_open = false;
    if (s_openInstance == this) s_openInstance = nullptr;

    DestroyPopupVisuals();

    auto cb = std::move(m_callback);
    m_callback = nullptr;
    m_movie = nullptr;
    m_entries.clear();

    if (cb) cb(false, -1, "");
}

// --- Mouse ---

void Dropdown::OnMouseClick(float a_mx, float a_my) {
    if (!m_open) return;

    // Check row hits
    int entryCount = static_cast<int>(m_entries.size());
    int visibleRows = std::min(entryCount - m_scrollOffset, m_visibleCount);
    double rowW = m_popupW - PAD * 2 - 8.0;
    double rowX = m_popupX + PAD;

    for (int i = 0; i < visibleRows; i++) {
        double rowY = m_rowAreaY + i * m_config.rowHeight;
        if (a_mx >= rowX && a_mx <= rowX + rowW &&
            a_my >= rowY && a_my <= rowY + m_config.rowHeight) {
            int dataIdx = m_scrollOffset + i;
            if (dataIdx >= 0 && dataIdx < entryCount && m_entries[dataIdx].enabled) {
                m_cursorIndex = dataIdx;
                Confirm();
            }
            return;
        }
    }

    // Click outside popup = cancel (dim overlay catch)
    Cancel();
}

void Dropdown::OnScrollWheel(int a_direction) {
    if (!m_open) return;
    if (a_direction < 0) Prev();
    else Next();
}

void Dropdown::UpdateHover(float a_mx, float a_my) {
    if (!m_open) return;

    int oldHover = m_hoverIndex;
    m_hoverIndex = -1;

    int entryCount = static_cast<int>(m_entries.size());
    int visibleRows = std::min(entryCount - m_scrollOffset, m_visibleCount);
    double rowW = m_popupW - PAD * 2 - 8.0;
    double rowX = m_popupX + PAD;

    for (int i = 0; i < visibleRows; i++) {
        double rowY = m_rowAreaY + i * m_config.rowHeight;
        if (a_mx >= rowX && a_mx <= rowX + rowW &&
            a_my >= rowY && a_my <= rowY + m_config.rowHeight) {
            m_hoverIndex = m_scrollOffset + i;
            break;
        }
    }

    if (m_hoverIndex != oldHover) {
        PopulateRows();
    }
}

void Dropdown::ClearHover() {
    if (m_hoverIndex < 0) return;
    m_hoverIndex = -1;
    if (m_open) PopulateRows();
}

void Dropdown::Destroy() {
    if (m_open) {
        m_open = false;
        if (s_openInstance == this) s_openInstance = nullptr;
        DestroyPopupVisuals();
    }
    m_callback = nullptr;
    m_movie = nullptr;
    m_entries.clear();
}

// --- Internal helpers ---

int Dropdown::FindNextEnabled(int a_from, int a_dir) const {
    int count = static_cast<int>(m_entries.size());
    if (count == 0) return -1;
    int idx = a_from + a_dir;
    while (idx >= 0 && idx < count) {
        if (m_entries[idx].enabled) return idx;
        idx += a_dir;
    }
    return -1;
}

void Dropdown::DrawPopup() {
    if (!m_movie) return;

    RE::GFxValue root;
    m_movie->GetVariable(&root, "_root");
    if (root.IsUndefined()) return;

    int entryCount = static_cast<int>(m_entries.size());
    int visibleRows = std::min(entryCount, m_config.maxVisible);
    m_visibleCount = visibleRows;

    bool hasTitle = !m_config.title.empty();
    double titleBlock = hasTitle ? (TITLE_H + SEP_H + 4.0) : 0.0;
    double contentH = visibleRows * m_config.rowHeight;
    double neededH = PAD + titleBlock + contentH + PAD;

    // Positioning: try below anchor first, then above
    double anchorBottom = m_anchorY + MenuLayout::ROW_HEIGHT;
    double spaceBelow = 720.0 - anchorBottom;
    double spaceAbove = m_anchorY;

    double popupY;
    if (neededH <= spaceBelow) {
        popupY = anchorBottom;
    } else if (neededH <= spaceAbove) {
        popupY = m_anchorY - neededH;
    } else {
        popupY = anchorBottom;
        neededH = std::min(neededH, 720.0 - popupY);
    }

    // Clamp to viewport
    popupY = std::max(0.0, std::min(popupY, 720.0 - neededH));
    double popupX = std::max(0.0, std::min(m_anchorX, 1280.0 - m_config.width));

    m_popupX = popupX;
    m_popupY = popupY;
    m_popupW = m_config.width;
    m_popupH = neededH;

    // Dim overlay
    ScaleformUtil::DrawFilledRect(m_movie, "_ddDim", DEPTH_DIM,
        0.0, 0.0, 1280.0, 720.0, 0x000000, ALPHA_DIM);

    // Background
    ScaleformUtil::DrawFilledRect(m_movie, "_ddBg", DEPTH_BG,
        popupX, popupY, m_config.width, neededH, COLOR_BG, ALPHA_BG);

    // Border
    ScaleformUtil::DrawBorderRect(m_movie, "_ddBorder", DEPTH_BORDER,
        popupX, popupY, m_config.width, neededH, COLOR_BORDER);

    double currentY = popupY + PAD;

    // Title
    if (hasTitle) {
        ScaleformUtil::CreateLabel(m_movie, "_ddTitle", DEPTH_TITLE,
            popupX + PAD, currentY, m_config.width - PAD * 2, TITLE_H,
            m_config.title.c_str(), 16, COLOR_TITLE);
        currentY += TITLE_H;

        // Separator
        ScaleformUtil::DrawLine(m_movie, "_ddSep", DEPTH_SEP,
            popupX + PAD, currentY,
            popupX + m_config.width - PAD, currentY,
            0x444444);
        currentY += SEP_H + 4.0;
    }

    m_rowAreaY = currentY;

    // Row clips
    for (int i = 0; i < MAX_ROW_SLOTS; i++) {
        m_rowClips[i] = RE::GFxValue();
    }

    int slotsToCreate = std::min(visibleRows, MAX_ROW_SLOTS);
    for (int i = 0; i < slotsToCreate; i++) {
        std::string rowName = "_ddRow" + std::to_string(i);

        RE::GFxValue args[2];
        args[0].SetString(rowName.c_str());
        args[1].SetNumber(static_cast<double>(DEPTH_ROW_BASE + i));
        root.Invoke("createEmptyMovieClip", &m_rowClips[i], args, 2);

        if (m_rowClips[i].IsUndefined()) continue;

        RE::GFxValue posXVal, posYVal;
        posXVal.SetNumber(popupX + PAD);
        posYVal.SetNumber(currentY + i * m_config.rowHeight);
        m_rowClips[i].SetMember("_x", posXVal);
        m_rowClips[i].SetMember("_y", posYVal);

        // Background child clip
        RE::GFxValue bgClip;
        RE::GFxValue bgArgs[2];
        bgArgs[0].SetString("_bg"); bgArgs[1].SetNumber(1.0);
        m_rowClips[i].Invoke("createEmptyMovieClip", &bgClip, bgArgs, 2);

        // Text field
        double textW = m_config.width - PAD * 2 - 20.0;
        RE::GFxValue tfArgs[6];
        tfArgs[0].SetString("_text"); tfArgs[1].SetNumber(10.0);
        tfArgs[2].SetNumber(8.0); tfArgs[3].SetNumber(4.0);
        tfArgs[4].SetNumber(textW); tfArgs[5].SetNumber(m_config.rowHeight - 4.0);
        m_rowClips[i].Invoke("createTextField", nullptr, tfArgs, 6);

        std::string textPath = "_root." + rowName + "._text";
        ScaleformUtil::SetTextFieldFormat(m_movie, textPath, 14, MenuLayout::COLOR_PICKER_NAME);
    }

    // Scrollbar track + thumb
    m_scrollThumbClip = RE::GFxValue();
    m_scrollTrackX = 0.0;
    if (entryCount > m_visibleCount) {
        double trackX = popupX + m_config.width - PAD - 6.0;
        double trackY = m_rowAreaY;
        double trackH = visibleRows * m_config.rowHeight;
        m_scrollTrackX = trackX;

        ScaleformUtil::DrawFilledRect(m_movie, "_ddScrollTrack", DEPTH_SCROLL_TRACK,
            trackX, trackY, 4.0, trackH, 0x333333, 80);

        // Create thumb clip with explicit position — UpdateScrollbar redraws at local (0,0)
        RE::GFxValue thumbClip;
        RE::GFxValue thumbArgs[2];
        thumbArgs[0].SetString("_ddScrollThumb");
        thumbArgs[1].SetNumber(static_cast<double>(DEPTH_SCROLL_THUMB));
        root.Invoke("createEmptyMovieClip", &thumbClip, thumbArgs, 2);
        if (!thumbClip.IsUndefined()) {
            RE::GFxValue xVal;
            xVal.SetNumber(trackX);
            thumbClip.SetMember("_x", xVal);
            m_scrollThumbClip = thumbClip;
        }
    }
}

void Dropdown::PopulateRows() {
    if (!m_movie) return;

    int entryCount = static_cast<int>(m_entries.size());
    int visibleRows = std::min(entryCount - m_scrollOffset, m_visibleCount);

    for (int i = 0; i < MAX_ROW_SLOTS; i++) {
        if (m_rowClips[i].IsUndefined()) continue;

        int dataIdx = m_scrollOffset + i;
        if (i < visibleRows && dataIdx < entryCount) {
            RE::GFxValue vis;
            vis.SetBoolean(true);
            m_rowClips[i].SetMember("_visible", vis);

            auto& entry = m_entries[dataIdx];

            // Detect sub-group header entries (injected by BuildPickerList)
            bool isHeader = (entry.id.empty() && !entry.subGroup.empty());

            std::string rowName = "_ddRow" + std::to_string(i);
            std::string textPath = "_root." + rowName + "._text";

            if (isHeader) {
                // Header row: smaller font, dimmer color, no prefix/sublabel, flat background
                RE::GFxValue textVal;
                textVal.SetString(entry.label.c_str());
                m_movie->SetVariable((textPath + ".text").c_str(), textVal);

                static constexpr uint32_t COLOR_HEADER = 0x999999;
                ScaleformUtil::SetTextFieldFormat(m_movie, textPath, 12, COLOR_HEADER);

                DrawRowBackground(i, COLOR_ROW_NORM, ALPHA_ROW_NORM);
            } else {
                // Build display text — mark committed selection with "> " prefix
                bool isCommitted = (!m_selectedId.empty() && entry.id == m_selectedId);
                std::string displayText = isCommitted ? "> " + entry.label : entry.label;
                int labelLen = static_cast<int>(displayText.length());
                if (!entry.sublabel.empty()) {
                    displayText += " (" + entry.sublabel + ")";
                }

                RE::GFxValue textVal;
                textVal.SetString(displayText.c_str());
                m_movie->SetVariable((textPath + ".text").c_str(), textVal);

                // Text color
                uint32_t textColor = entry.enabled ? entry.color : COLOR_DISABLED;
                ScaleformUtil::SetTextFieldFormat(m_movie, textPath, 14, textColor);

                // Dim sublabel
                if (!entry.sublabel.empty() && entry.enabled) {
                    RE::GFxValue tf;
                    m_movie->GetVariable(&tf, textPath.c_str());
                    if (!tf.IsUndefined()) {
                        RE::GFxValue dimFmt;
                        m_movie->CreateObject(&dimFmt, "TextFormat");
                        if (!dimFmt.IsUndefined()) {
                            RE::GFxValue colorVal;
                            colorVal.SetNumber(static_cast<double>(COLOR_SUBLABEL));
                            dimFmt.SetMember("color", colorVal);

                            RE::GFxValue fmtArgs[3];
                            fmtArgs[0].SetNumber(static_cast<double>(labelLen));
                            fmtArgs[1].SetNumber(static_cast<double>(displayText.length()));
                            fmtArgs[2] = dimFmt;
                            tf.Invoke("setTextFormat", nullptr, fmtArgs, 3);
                        }
                    }
                }

                // Row background
                bool isSelected = (dataIdx == m_cursorIndex);
                bool isHovered = (dataIdx == m_hoverIndex && dataIdx != m_cursorIndex);
                uint32_t bgColor = isSelected ? COLOR_ROW_SEL :
                                  (isHovered ? COLOR_ROW_HOV : COLOR_ROW_NORM);
                int bgAlpha = isSelected ? ALPHA_ROW_SEL :
                             (isHovered ? ALPHA_ROW_HOV : ALPHA_ROW_NORM);
                DrawRowBackground(i, bgColor, bgAlpha);
            }

            // Group separator — thin line between rows with different groups
            {
                RE::GFxValue sepClip;
                m_rowClips[i].GetMember("_groupSep", &sepClip);
                if (sepClip.IsUndefined()) {
                    RE::GFxValue sepArgs[2];
                    sepArgs[0].SetString("_groupSep");
                    sepArgs[1].SetNumber(2.0);
                    m_rowClips[i].Invoke("createEmptyMovieClip", &sepClip, sepArgs, 2);
                }
                if (!sepClip.IsUndefined()) {
                    sepClip.Invoke("clear", nullptr, nullptr, 0);
                    int prevIdx = dataIdx - 1;
                    if (prevIdx >= 0 && m_entries[prevIdx].group != entry.group) {
                        double lineW = m_popupW - PAD * 2 - 8.0;
                        RE::GFxValue styleArgs[3];
                        styleArgs[0].SetNumber(1.0);
                        styleArgs[1].SetNumber(static_cast<double>(0x444444));
                        styleArgs[2].SetNumber(60.0);
                        sepClip.Invoke("lineStyle", nullptr, styleArgs, 3);
                        RE::GFxValue pt[2];
                        pt[0].SetNumber(0.0); pt[1].SetNumber(0.0);
                        sepClip.Invoke("moveTo", nullptr, pt, 2);
                        pt[0].SetNumber(lineW);
                        sepClip.Invoke("lineTo", nullptr, pt, 2);
                    }
                }
            }
        } else {
            RE::GFxValue vis;
            vis.SetBoolean(false);
            m_rowClips[i].SetMember("_visible", vis);
        }
    }

    UpdateScrollbar();
}

void Dropdown::DrawRowBackground(int a_visIndex, uint32_t a_color, int a_alpha) {
    if (a_visIndex < 0 || a_visIndex >= MAX_ROW_SLOTS || m_rowClips[a_visIndex].IsUndefined()) return;

    RE::GFxValue bgClip;
    m_rowClips[a_visIndex].GetMember("_bg", &bgClip);
    if (bgClip.IsUndefined()) return;

    bgClip.Invoke("clear", nullptr, nullptr, 0);

    double rowW = m_popupW - PAD * 2 - 8.0;
    RE::GFxValue fillArgs[2];
    fillArgs[0].SetNumber(static_cast<double>(a_color));
    fillArgs[1].SetNumber(static_cast<double>(a_alpha));
    bgClip.Invoke("beginFill", nullptr, fillArgs, 2);

    RE::GFxValue pt[2];
    pt[0].SetNumber(0.0); pt[1].SetNumber(0.0);
    bgClip.Invoke("moveTo", nullptr, pt, 2);
    pt[0].SetNumber(rowW);
    bgClip.Invoke("lineTo", nullptr, pt, 2);
    pt[1].SetNumber(m_config.rowHeight);
    bgClip.Invoke("lineTo", nullptr, pt, 2);
    pt[0].SetNumber(0.0);
    bgClip.Invoke("lineTo", nullptr, pt, 2);
    pt[1].SetNumber(0.0);
    bgClip.Invoke("lineTo", nullptr, pt, 2);
    bgClip.Invoke("endFill", nullptr, nullptr, 0);
}

void Dropdown::UpdateScrollbar() {
    if (m_scrollThumbClip.IsUndefined()) return;
    int entryCount = static_cast<int>(m_entries.size());
    if (entryCount <= m_visibleCount) {
        RE::GFxValue vis;
        vis.SetBoolean(false);
        m_scrollThumbClip.SetMember("_visible", vis);
        return;
    }

    RE::GFxValue vis;
    vis.SetBoolean(true);
    m_scrollThumbClip.SetMember("_visible", vis);

    double trackH = m_visibleCount * m_config.rowHeight;
    double thumbH = std::max(12.0, trackH * m_visibleCount / entryCount);
    int maxOffset = entryCount - m_visibleCount;
    double ratio = (maxOffset > 0) ? static_cast<double>(m_scrollOffset) / maxOffset : 0.0;
    double thumbY = m_rowAreaY + ratio * (trackH - thumbH);

    RE::GFxValue posY;
    posY.SetNumber(thumbY);
    m_scrollThumbClip.SetMember("_y", posY);

    // Redraw thumb shape at new size
    m_scrollThumbClip.Invoke("clear", nullptr, nullptr, 0);
    RE::GFxValue fillArgs[2];
    fillArgs[0].SetNumber(static_cast<double>(0x555555));
    fillArgs[1].SetNumber(60.0);
    m_scrollThumbClip.Invoke("beginFill", nullptr, fillArgs, 2);
    RE::GFxValue pt[2];
    pt[0].SetNumber(0.0); pt[1].SetNumber(0.0);
    m_scrollThumbClip.Invoke("moveTo", nullptr, pt, 2);
    pt[0].SetNumber(4.0);
    m_scrollThumbClip.Invoke("lineTo", nullptr, pt, 2);
    pt[1].SetNumber(thumbH);
    m_scrollThumbClip.Invoke("lineTo", nullptr, pt, 2);
    pt[0].SetNumber(0.0);
    m_scrollThumbClip.Invoke("lineTo", nullptr, pt, 2);
    pt[1].SetNumber(0.0);
    m_scrollThumbClip.Invoke("lineTo", nullptr, pt, 2);
    m_scrollThumbClip.Invoke("endFill", nullptr, nullptr, 0);
}

void Dropdown::DestroyPopupVisuals() {
    if (!m_movie) return;

    auto removeClip = [this](const char* name) {
        if (!m_movie) return;
        RE::GFxValue clip;
        m_movie->GetVariable(&clip, (std::string("_root.") + name).c_str());
        if (!clip.IsUndefined()) {
            clip.Invoke("removeMovieClip", nullptr, nullptr, 0);
        }
    };

    auto removeTextField = [this](const char* name) {
        if (!m_movie) return;
        RE::GFxValue tf;
        m_movie->GetVariable(&tf, (std::string("_root.") + name).c_str());
        if (!tf.IsUndefined()) {
            tf.Invoke("removeTextField", nullptr, nullptr, 0);
        }
    };

    removeClip("_ddDim");
    removeClip("_ddBg");
    removeClip("_ddBorder");
    removeTextField("_ddTitle");
    removeClip("_ddSep");
    removeClip("_ddScrollTrack");
    removeClip("_ddScrollThumb");

    for (int i = 0; i < MAX_ROW_SLOTS; i++) {
        std::string name = "_ddRow" + std::to_string(i);
        removeClip(name.c_str());
        m_rowClips[i] = RE::GFxValue();
    }
    m_scrollThumbClip = RE::GFxValue();
}
