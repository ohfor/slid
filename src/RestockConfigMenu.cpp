#include "RestockConfigMenu.h"
#include "ButtonBar.h"
#include "MouseGlow.h"
#include "ScaleformUtil.h"
#include "TranslationService.h"

#include <algorithm>
#include <unordered_set>

namespace RestockConfig {

    // Active menu instance
    static Menu* g_activeMenu = nullptr;

    // Static state
    Menu::Callback Menu::s_callback;
    RestockCategory::RestockConfig Menu::s_initialConfig;

    // --- Registration ---

    void Menu::Register() {
        auto ui = RE::UI::GetSingleton();
        if (ui) {
            ui->Register(MENU_NAME, Create);
            logger::info("Registered menu: {}", MENU_NAME);
        }
    }

    RE::IMenu* Menu::Create() {
        return new Menu();
    }

    Menu::Menu() {
        depthPriority = 5;

        menuFlags.set(RE::UI_MENU_FLAGS::kPausesGame);
        menuFlags.set(RE::UI_MENU_FLAGS::kUsesMenuContext);
        menuFlags.set(RE::UI_MENU_FLAGS::kModal);
        menuFlags.set(RE::UI_MENU_FLAGS::kRequiresUpdate);
        menuFlags.set(RE::UI_MENU_FLAGS::kUsesCursor);

        inputContext = Context::kMenuMode;

        auto scaleform = RE::BSScaleformManager::GetSingleton();
        if (scaleform) {
            bool loaded = scaleform->LoadMovie(this, uiMovie, FILE_NAME.data());
            if (loaded && uiMovie) {
                logger::info("RestockConfigMenu: loaded SWF");
            } else {
                logger::error("RestockConfigMenu: failed to load SWF");
            }
        }

        BuildBrowserRows();
        LoadFromConfig(s_initialConfig);
    }

    void Menu::PostCreate() {
        g_activeMenu = this;

        // Position cursor on first selectable browser row
        m_browserCursor = NextSelectableRow(-1, 1);

        DrawAll();
    }

    RE::UI_MESSAGE_RESULTS Menu::ProcessMessage(RE::UIMessage& a_message) {
        using Message = RE::UI_MESSAGE_TYPE;
        switch (*a_message.type) {
            case Message::kUpdate:
                // Hold-to-confirm animation
                if (m_buttonBar.IsHolding()) {
                    m_buttonBar.UpdateHold();
                    int completed = m_buttonBar.CompletedHold();
                    if (completed == 1) SetDefault();
                    else if (completed == 2) ClearAll();
                }
                // Check delete animation timer
                if (m_deleteRow >= 0) {
                    auto now = std::chrono::steady_clock::now();
                    float elapsed = std::chrono::duration<float>(now - m_deleteStart).count();
                    if (elapsed >= DELETE_ANIM_SECS) {
                        FinishDelete();
                        UpdateAll();
                    } else {
                        // Redraw to keep flash visible
                        DrawPad();
                    }
                }
                return RE::UI_MESSAGE_RESULTS::kHandled;
            case Message::kHide:
                g_activeMenu = nullptr;
                return RE::UI_MESSAGE_RESULTS::kHandled;
            default:
                return RE::IMenu::ProcessMessage(a_message);
        }
    }

    // --- Show / Hide ---

    void Menu::Show(const RestockCategory::RestockConfig& a_initial, Callback a_callback) {
        s_initialConfig = a_initial;
        s_callback = std::move(a_callback);

        auto ui = RE::UI::GetSingleton();
        if (ui && !ui->IsMenuOpen(MENU_NAME)) {
            auto msgQueue = RE::UIMessageQueue::GetSingleton();
            if (msgQueue) {
                msgQueue->AddMessage(MENU_NAME, RE::UI_MESSAGE_TYPE::kShow, nullptr);
                logger::info("Opening RestockConfigMenu");
            }
        }
    }

    void Menu::Hide() {
        auto ui = RE::UI::GetSingleton();
        if (ui && ui->IsMenuOpen(MENU_NAME)) {
            auto msgQueue = RE::UIMessageQueue::GetSingleton();
            if (msgQueue) {
                msgQueue->AddMessage(MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
                logger::info("Closing RestockConfigMenu");
            }
        }
    }

    bool Menu::IsOpen() {
        auto ui = RE::UI::GetSingleton();
        return ui && ui->IsMenuOpen(MENU_NAME);
    }

    // --- Data model ---

    void Menu::BuildBrowserRows() {
        m_browserRows.clear();
        const auto& allCats = RestockCategory::GetAllCategories();

        for (const auto& cat : allCats) {
            if (!cat.parentID.empty()) continue;  // handled with parent

            auto children = RestockCategory::GetChildren(cat.id);

            if (children.empty()) {
                // Standalone root — selectable leaf
                BrowserRow row;
                row.id = cat.id;
                row.label = T(cat.displayNameKey);
                row.isHeader = false;
                m_browserRows.push_back(std::move(row));
                logger::debug("RestockBrowser: leaf '{}' label='{}'", cat.id, m_browserRows.back().label);
            } else {
                // Family root — header (non-selectable)
                BrowserRow header;
                header.id = cat.id;
                header.label = T(cat.displayNameKey);
                header.isHeader = true;
                header.expanded = true;
                m_browserRows.push_back(std::move(header));
                logger::debug("RestockBrowser: header '{}' label='{}'", cat.id, m_browserRows.back().label);

                // Children
                for (const auto& childID : children) {
                    const RestockCategory::CategoryDef* childDef = nullptr;
                    for (const auto& c : allCats) {
                        if (c.id == childID) { childDef = &c; break; }
                    }
                    if (!childDef) continue;

                    BrowserRow child;
                    child.id = childID;
                    child.label = T(childDef->displayNameKey);
                    child.isHeader = false;
                    child.parentID = cat.id;
                    m_browserRows.push_back(std::move(child));
                    logger::debug("RestockBrowser:   child '{}' label='{}'", childID, m_browserRows.back().label);
                }
            }
        }
        logger::info("RestockBrowser: {} rows built ({} headers, {} leaves)",
            m_browserRows.size(),
            std::count_if(m_browserRows.begin(), m_browserRows.end(), [](const auto& r) { return r.isHeader; }),
            std::count_if(m_browserRows.begin(), m_browserRows.end(), [](const auto& r) { return !r.isHeader; }));
    }

    void Menu::SyncBrowserOnPadState() {
        std::unordered_set<std::string> padIDs;
        for (const auto& row : m_padRows) {
            padIDs.insert(row.id);
        }
        for (auto& row : m_browserRows) {
            row.onPad = padIDs.count(row.id) > 0;
        }
    }

    void Menu::LoadFromConfig(const RestockCategory::RestockConfig& a_config) {
        m_padRows.clear();
        if (!a_config.configured) {
            logger::debug("RestockPad: config not configured, pad empty");
            SyncBrowserOnPadState();
            return;
        }

        logger::info("RestockPad: loading {} itemQuantities into pad", a_config.itemQuantities.size());

        // Populate pad from itemQuantities, maintaining category order
        for (const auto& brow : m_browserRows) {
            if (brow.isHeader) continue;
            auto it = a_config.itemQuantities.find(brow.id);
            if (it != a_config.itemQuantities.end()) {
                PadRow pr;
                pr.id = brow.id;
                pr.label = QualifiedLabel(brow);
                pr.quantity = it->second;
                m_padRows.push_back(std::move(pr));
                logger::debug("RestockPad: added '{}' label='{}' qty={}", brow.id, brow.label, it->second);
            }
        }

        // Log any itemQuantities entries that didn't match a browser leaf
        for (const auto& [id, qty] : a_config.itemQuantities) {
            bool found = false;
            for (const auto& pr : m_padRows) {
                if (pr.id == id) { found = true; break; }
            }
            if (!found) {
                logger::warn("RestockPad: itemQuantities entry '{}' qty={} has no matching browser leaf (family root?)", id, qty);
            }
        }

        logger::info("RestockPad: {} items on pad", m_padRows.size());
        SyncBrowserOnPadState();
    }

    RestockCategory::RestockConfig Menu::AssembleConfig() const {
        RestockCategory::RestockConfig config;
        config.configured = true;
        for (const auto& row : m_padRows) {
            if (row.quantity > 0) {
                config.itemQuantities[row.id] = row.quantity;
            }
        }
        return config;
    }

    // --- Navigation helpers ---

    bool Menu::IsBrowserRowVisible(int a_index) const {
        if (a_index < 0 || a_index >= static_cast<int>(m_browserRows.size())) return false;
        const auto& row = m_browserRows[a_index];
        if (!row.parentID.empty()) {
            // Child — visible only if parent is expanded
            for (const auto& br : m_browserRows) {
                if (br.id == row.parentID) {
                    return br.expanded;
                }
            }
        }
        return true;
    }

    std::string Menu::QualifiedLabel(const BrowserRow& a_row) const {
        if (a_row.parentID.empty()) return a_row.label;
        for (const auto& br : m_browserRows) {
            if (br.id == a_row.parentID) {
                return br.label + ": " + a_row.label;
            }
        }
        return a_row.label;
    }

    int Menu::NextSelectableRow(int a_from, int a_dir) const {
        int total = static_cast<int>(m_browserRows.size());
        int idx = a_from + a_dir;
        while (idx >= 0 && idx < total) {
            if (IsBrowserRowVisible(idx)) {
                return idx;
            }
            idx += a_dir;
        }
        return a_from;  // no valid row found — stay put
    }

    int Menu::BrowserVisibleRows() const {
        return static_cast<int>(PANEL_H / ROW_H);
    }

    int Menu::PadVisibleRows() const {
        return static_cast<int>(PANEL_H / ROW_H);
    }

    int Menu::BrowserMaxScroll() const {
        // Count visible rows
        int count = 0;
        for (int i = 0; i < static_cast<int>(m_browserRows.size()); ++i) {
            if (IsBrowserRowVisible(i)) count++;
        }
        return std::max(0, count - BrowserVisibleRows());
    }

    int Menu::PadMaxScroll() const {
        return std::max(0, static_cast<int>(m_padRows.size()) - PadVisibleRows());
    }

    // --- Pad helpers ---

    void Menu::AddToPad(const std::string& a_id, const std::string& a_label) {
        // Don't add duplicates
        for (const auto& row : m_padRows) {
            if (row.id == a_id) {
                logger::debug("RestockPad: '{}' already on pad, skipping", a_id);
                return;
            }
        }
        PadRow pr;
        pr.id = a_id;
        pr.label = a_label;
        pr.quantity = 1;
        m_padRows.push_back(std::move(pr));
        logger::info("RestockPad: added '{}' label='{}' — pad now has {} items", a_id, a_label, m_padRows.size());
        SyncBrowserOnPadState();
    }

    void Menu::RemoveFromPad(int a_index) {
        if (a_index < 0 || a_index >= static_cast<int>(m_padRows.size())) return;
        logger::info("RestockPad: removing [{}] '{}' — pad had {} items", a_index, m_padRows[a_index].id, m_padRows.size());
        m_padRows.erase(m_padRows.begin() + a_index);
        if (m_padCursor >= static_cast<int>(m_padRows.size()) && m_padCursor > 0) {
            m_padCursor--;
        }
        if (m_padScroll > PadMaxScroll()) {
            m_padScroll = PadMaxScroll();
        }
        SyncBrowserOnPadState();
    }

    // --- Panel-specific navigation ---

    void Menu::BrowserUp() {
        int next = NextSelectableRow(m_browserCursor, -1);
        if (next != m_browserCursor) {
            m_browserCursor = next;
            // Scroll to keep cursor in view — count visible rows up to cursor
            int visIdx = 0;
            for (int i = 0; i <= m_browserCursor; ++i) {
                if (IsBrowserRowVisible(i)) visIdx++;
            }
            if (visIdx - 1 < m_browserScroll) {
                m_browserScroll = visIdx - 1;
            }
        }
    }

    void Menu::BrowserDown() {
        int next = NextSelectableRow(m_browserCursor, 1);
        if (next != m_browserCursor) {
            m_browserCursor = next;
            int visIdx = 0;
            for (int i = 0; i <= m_browserCursor; ++i) {
                if (IsBrowserRowVisible(i)) visIdx++;
            }
            if (visIdx > m_browserScroll + BrowserVisibleRows()) {
                m_browserScroll = visIdx - BrowserVisibleRows();
            }
        }
    }

    void Menu::BrowserActivate() {
        if (m_browserCursor < 0 || m_browserCursor >= static_cast<int>(m_browserRows.size())) return;
        auto& row = m_browserRows[m_browserCursor];
        if (row.isHeader) {
            // Toggle expand/collapse
            row.expanded = !row.expanded;
            if (m_browserScroll > BrowserMaxScroll()) {
                m_browserScroll = BrowserMaxScroll();
            }
            return;
        }
        if (row.onPad) return;  // already on pad
        AddToPad(row.id, QualifiedLabel(row));
    }

    void Menu::PadUp() {
        if (m_padCursor > 0) {
            m_padCursor--;
            if (m_padCursor < m_padScroll) m_padScroll = m_padCursor;
        }
    }

    void Menu::PadDown() {
        if (m_padCursor < static_cast<int>(m_padRows.size()) - 1) {
            m_padCursor++;
            if (m_padCursor >= m_padScroll + PadVisibleRows()) {
                m_padScroll = m_padCursor - PadVisibleRows() + 1;
            }
        }
    }

    void Menu::PadAdjustQty(int a_delta) {
        if (m_padCursor < 0 || m_padCursor >= static_cast<int>(m_padRows.size())) return;
        auto& row = m_padRows[m_padCursor];
        int newVal = static_cast<int>(row.quantity) + a_delta;
        row.quantity = static_cast<uint16_t>(std::clamp(newVal, 0, 999));
    }

    void Menu::StartDelete(int a_index) {
        if (a_index < 0 || a_index >= static_cast<int>(m_padRows.size())) return;
        if (m_deleteRow >= 0) return;  // already animating
        m_deleteRow = a_index;
        m_deleteStart = std::chrono::steady_clock::now();
        m_adjustMode = false;  // exit adjust mode if active
        logger::debug("RestockPad: delete animation started for row {} '{}'", a_index, m_padRows[a_index].id);
    }

    void Menu::FinishDelete() {
        if (m_deleteRow < 0) return;
        logger::debug("RestockPad: delete animation finished for row {}", m_deleteRow);
        RemoveFromPad(m_deleteRow);
        m_deleteRow = -1;
    }

    // --- Drawing ---

    void Menu::RemoveClip(const std::string& a_name) {
        if (!uiMovie) return;
        RE::GFxValue root;
        uiMovie->GetVariable(&root, "_root");
        if (root.IsUndefined()) return;
        RE::GFxValue existing;
        root.GetMember(a_name.c_str(), &existing);
        if (!existing.IsUndefined()) {
            existing.Invoke("removeMovieClip", nullptr, nullptr, 0);
            existing.Invoke("removeTextField", nullptr, nullptr, 0);
        }
    }

    std::pair<float, float> Menu::GetMousePos() const {
        if (!uiMovie) return {0.0f, 0.0f};
        RE::GFxValue xVal, yVal;
        uiMovie->GetVariable(&xVal, "_root._xmouse");
        uiMovie->GetVariable(&yVal, "_root._ymouse");
        float mx = xVal.IsNumber() ? static_cast<float>(xVal.GetNumber()) : 0.0f;
        float my = yVal.IsNumber() ? static_cast<float>(yVal.GetNumber()) : 0.0f;
        return {mx, my};
    }

    void Menu::DrawAll() {
        DrawChrome();
        DrawBrowser();
        DrawPad();
        DrawGuideText();
        DrawButtons();
    }

    void Menu::UpdateAll() {
        DrawBrowser();
        DrawPad();
        DrawGuideText();
        DrawButtons();
    }

    void Menu::DrawChrome() {
        m_popupX = (1280.0 - POPUP_W) / 2.0;
        m_popupY = (720.0 - POPUP_H) / 2.0;

        // Dim overlay
        ScaleformUtil::DrawFilledRect(uiMovie.get(), "_dimOverlay", 90, 0.0, 0.0, 1280.0, 720.0, 0x000000, ALPHA_DIM);

        // Soft outer glow — 5 expanding layers with decreasing opacity
        constexpr struct { double spread; int alpha; } glowLayers[] = {
            {10.0, 6}, {8.0, 10}, {6.0, 14}, {4.0, 20}, {2.0, 28}
        };
        for (int i = 0; i < 5; ++i) {
            double s = glowLayers[i].spread;
            std::string name = "_rsGlow" + std::to_string(i);
            ScaleformUtil::DrawFilledRect(uiMovie.get(), name.c_str(), 95 + i,
                m_popupX - s, m_popupY - s, POPUP_W + s * 2, POPUP_H + s * 2,
                0x000000, glowLayers[i].alpha);
        }

        // Background
        ScaleformUtil::DrawFilledRect(uiMovie.get(), "_rsBg", 101, m_popupX, m_popupY, POPUP_W, POPUP_H, COLOR_BG, ALPHA_BG);
        ScaleformUtil::DrawBorderRect(uiMovie.get(), "_rsBorder", 102, m_popupX, m_popupY, POPUP_W, POPUP_H, COLOR_BORDER);

        // Gold accent line at top
        ScaleformUtil::DrawFilledRect(uiMovie.get(), "_rsAccent", 103,
            m_popupX, m_popupY, POPUP_W, 2.0, COLOR_ACCENT, 90);

        // Title (centered)
        std::string title = T("$SLID_RestockTitle");
        ScaleformUtil::CreateLabel(uiMovie.get(), "_rsTitle", 104, m_popupX + 20.0, m_popupY + 12.0,
                   POPUP_W - 40.0, 24.0, title.c_str(), 16, COLOR_TITLE);
        {
            RE::GFxValue tf;
            uiMovie->GetVariable(&tf, "_root._rsTitle");
            if (!tf.IsUndefined()) {
                RE::GFxValue fmt;
                uiMovie->CreateObject(&fmt, "TextFormat");
                if (!fmt.IsUndefined()) {
                    RE::GFxValue av; av.SetString("center"); fmt.SetMember("align", av);
                    RE::GFxValue fa[1] = {fmt};
                    tf.Invoke("setTextFormat", nullptr, fa, 1);
                    tf.Invoke("setNewTextFormat", nullptr, fa, 1);
                }
            }
        }

        // Panel positions
        m_browserX = m_popupX + SIDE_PAD;
        m_browserY = m_popupY + HEADER_H;
        m_padX = m_browserX + BROWSER_W + PANEL_GAP;
        m_padY = m_browserY;

        // Panel backgrounds
        ScaleformUtil::DrawFilledRect(uiMovie.get(), "_rsBrowserBg", 106,
            m_browserX, m_browserY, BROWSER_W, PANEL_H, COLOR_PANEL_BG, ALPHA_PANEL_BG);
        ScaleformUtil::DrawFilledRect(uiMovie.get(), "_rsPadBg", 107,
            m_padX, m_padY, PAD_W, PANEL_H, COLOR_PANEL_BG, ALPHA_PANEL_BG);

        // Divider between panels
        double divX = m_browserX + BROWSER_W + PANEL_GAP / 2.0;
        ScaleformUtil::DrawLine(uiMovie.get(), "_rsDivider", 108,
            divX, m_browserY + 4.0, divX, m_browserY + PANEL_H - 4.0, COLOR_DIVIDER);

        // Mouse-following radial glow (depth 115: above chrome/headers, below content at 120)
        MouseGlow::Create(uiMovie.get(), "_mouseGlow", 115, m_popupX, m_popupY, POPUP_W, POPUP_H);

        // Column headers
        ScaleformUtil::CreateLabel(uiMovie.get(), "_rsBrowserHdr", 109,
            m_browserX + 6.0, m_browserY - 16.0, BROWSER_W, 16.0,
            T("$SLID_RestockOptionsHdr").c_str(), 10, COLOR_PANEL_HEADER);
        ScaleformUtil::CreateLabel(uiMovie.get(), "_rsPadHdr", 110,
            m_padX + 6.0, m_padY - 16.0, PAD_W, 16.0,
            T("$SLID_RestockPadHdr").c_str(), 10, COLOR_PANEL_HEADER);

        // Guide / button geometry
        m_guideY = m_browserY + PANEL_H + 12.0;
        m_btnY = m_popupY + POPUP_H - 48.0;

        // Init button bar
        m_buttonBar.Init(uiMovie.get(), "_rsBtn", m_baseDepth + 910,
            {
                {T("$SLID_OK"),             100.0},
                {T("$SLID_RestockDefault"), 100.0, "", true, ButtonColors::HOLD_BLUE, 80, 1.0f},
                {T("$SLID_RestockClear"),   100.0, "", true, ButtonColors::HOLD_RED, 80, 1.0f},
                {T("$SLID_Cancel"),         100.0}
            },
            m_popupX + POPUP_W / 2.0, m_btnY);
    }

    void Menu::DrawBrowser() {
        int visRows = BrowserVisibleRows();
        int depthBase = m_baseDepth;

        // Clear old browser rows
        for (int i = 0; i < visRows + 2; ++i) {
            RemoveClip("_rsB_HL" + std::to_string(i));
            RemoveClip("_rsB_Lbl" + std::to_string(i));
        }

        // Build list of visible browser rows in order
        struct VisRow { int dataIdx; };
        std::vector<VisRow> visList;
        for (int i = 0; i < static_cast<int>(m_browserRows.size()); ++i) {
            if (IsBrowserRowVisible(i)) {
                visList.push_back({i});
            }
        }

        int startVis = m_browserScroll;
        int endVis = std::min(startVis + visRows, static_cast<int>(visList.size()));

        for (int vi = startVis; vi < endVis; ++vi) {
            int slot = vi - startVis;
            int dataIdx = visList[vi].dataIdx;
            const auto& row = m_browserRows[dataIdx];

            double y = m_browserY + slot * ROW_H;
            double x = m_browserX;
            double indent = row.parentID.empty() ? 0.0 : 12.0;

            bool isCursor = (m_focus == FocusTarget::kBrowser && dataIdx == m_browserCursor);
            bool isHover = (m_browserHover == dataIdx);

            // Highlight
            std::string hlName = "_rsB_HL" + std::to_string(slot);
            if (isCursor || isHover) {
                ScaleformUtil::DrawFilledRect(uiMovie.get(), hlName.c_str(), depthBase + slot,
                    x, y, BROWSER_W, ROW_H,
                    isCursor ? COLOR_CURSOR : COLOR_HOVER,
                    isCursor ? ALPHA_CURSOR : ALPHA_HOVER);
            } else {
                RemoveClip(hlName);
            }

            // Label
            uint32_t color;
            int fontSize;
            if (row.isHeader) {
                color = COLOR_HEADER_ROW;
                fontSize = 12;
            } else if (row.onPad) {
                color = COLOR_ITEM_DIM;
                fontSize = 11;
            } else {
                color = COLOR_ITEM;
                fontSize = 11;
            }

            std::string lblName = "_rsB_Lbl" + std::to_string(slot);
            std::string label = row.label;

            // Header prefix
            if (row.isHeader) {
                label = (row.expanded ? "- " : "+ ") + label;
            }

            // Truncate
            if (label.length() > 36) {
                label = label.substr(0, 34) + "..";
            }

            ScaleformUtil::CreateLabel(uiMovie.get(), lblName.c_str(), depthBase + visRows + slot,
                x + 6.0 + indent, y + 2.0, BROWSER_W - 12.0 - indent, ROW_H,
                label.c_str(), fontSize, color);
        }

        // Browser scrollbar
        int totalVis = static_cast<int>(visList.size());
        if (totalVis > visRows) {
            DrawScrollbar("_rsBSB", depthBase + 2 * visRows,
                m_browserX + BROWSER_W - 6.0, m_browserY,
                PANEL_H, totalVis, visRows, m_browserScroll);
        } else {
            RemoveClip("_rsBSBTrack");
            RemoveClip("_rsBSBThumb");
        }
    }

    void Menu::DrawPad() {
        int visRows = PadVisibleRows();
        int depthBase = m_baseDepth + 400;

        // Clear old pad rows
        for (int i = 0; i < visRows + 2; ++i) {
            RemoveClip("_rsP_HL" + std::to_string(i));
            RemoveClip("_rsP_Minus" + std::to_string(i));
            RemoveClip("_rsP_MinusHL" + std::to_string(i));
            RemoveClip("_rsP_Qty" + std::to_string(i));
            RemoveClip("_rsP_Plus" + std::to_string(i));
            RemoveClip("_rsP_PlusHL" + std::to_string(i));
            RemoveClip("_rsP_Lbl" + std::to_string(i));
            RemoveClip("_rsP_Trash" + std::to_string(i));
            RemoveClip("_rsP_TrashHL" + std::to_string(i));
        }

        // Empty message
        RemoveClip("_rsPadEmpty");
        if (m_padRows.empty()) {
            std::string emptyMsg = T("$SLID_RestockPadEmpty");
            ScaleformUtil::CreateLabel(uiMovie.get(), "_rsPadEmpty", depthBase,
                m_padX + 10.0, m_padY + PANEL_H / 2.0 - 10.0, PAD_W - 20.0, 20.0,
                emptyMsg.c_str(), 11, COLOR_PAD_EMPTY);
            // Center-align
            std::string path = "_root._rsPadEmpty";
            RE::GFxValue tf;
            uiMovie->GetVariable(&tf, path.c_str());
            if (!tf.IsUndefined()) {
                RE::GFxValue fmt;
                uiMovie->CreateObject(&fmt, "TextFormat");
                if (!fmt.IsUndefined()) {
                    RE::GFxValue alignVal;
                    alignVal.SetString("center");
                    fmt.SetMember("align", alignVal);
                    RE::GFxValue fmtArgs[1] = {fmt};
                    tf.Invoke("setTextFormat", nullptr, fmtArgs, 1);
                    tf.Invoke("setNewTextFormat", nullptr, fmtArgs, 1);
                }
            }

            RemoveClip("_rsPSBTrack");
            RemoveClip("_rsPSBThumb");
            return;
        }

        int startRow = m_padScroll;
        int endRow = std::min(startRow + visRows, static_cast<int>(m_padRows.size()));

        for (int r = startRow; r < endRow; ++r) {
            int slot = r - startRow;
            const auto& row = m_padRows[r];

            double y = m_padY + slot * ROW_H;
            bool isCursor = (m_focus == FocusTarget::kPad && r == m_padCursor);
            bool isHover = (m_padHover == r);
            bool isDeleting = (m_deleteRow == r);
            bool isAdjusting = (m_adjustMode && isCursor);

            // Highlight — delete flash overrides adjust overrides cursor/hover
            std::string hlName = "_rsP_HL" + std::to_string(slot);
            if (isDeleting) {
                ScaleformUtil::DrawFilledRect(uiMovie.get(), hlName.c_str(), depthBase + slot,
                    m_padX, y, PAD_W, ROW_H, COLOR_DELETE_FLASH, 80);
            } else if (isAdjusting) {
                ScaleformUtil::DrawFilledRect(uiMovie.get(), hlName.c_str(), depthBase + slot,
                    m_padX, y, PAD_W, ROW_H, COLOR_ADJUST, ALPHA_ADJUST);
            } else if (isCursor || isHover) {
                ScaleformUtil::DrawFilledRect(uiMovie.get(), hlName.c_str(), depthBase + slot,
                    m_padX, y, PAD_W, ROW_H,
                    isCursor ? COLOR_CURSOR : COLOR_HOVER,
                    isCursor ? ALPHA_CURSOR : ALPHA_HOVER);
            } else {
                RemoveClip(hlName);
            }

            double cx = m_padX + 6.0;
            int labelDepth = depthBase + visRows + slot * 500;

            // [-] button
            bool minusHovered = (m_hoverQtyRow == r && !m_hoverQtyIsPlus);
            std::string minusName = "_rsP_Minus" + std::to_string(slot);
            ScaleformUtil::CreateLabel(uiMovie.get(), minusName.c_str(), labelDepth,
                cx, y + 2.0, QTY_BTN_W, ROW_H, "-", 11,
                minusHovered ? COLOR_QTY_BTN_HOVER : COLOR_QTY_BTN);
            // Center-align minus
            {
                std::string p = "_root." + minusName;
                RE::GFxValue tf;
                uiMovie->GetVariable(&tf, p.c_str());
                if (!tf.IsUndefined()) {
                    RE::GFxValue fmt;
                    uiMovie->CreateObject(&fmt, "TextFormat");
                    if (!fmt.IsUndefined()) {
                        RE::GFxValue av; av.SetString("center"); fmt.SetMember("align", av);
                        RE::GFxValue fa[1] = {fmt};
                        tf.Invoke("setTextFormat", nullptr, fa, 1);
                        tf.Invoke("setNewTextFormat", nullptr, fa, 1);
                    }
                }
            }
            // [-] hover highlight
            std::string minusHL = "_rsP_MinusHL" + std::to_string(slot);
            if (minusHovered) {
                ScaleformUtil::DrawBorderRect(uiMovie.get(), minusHL.c_str(), labelDepth - 1,
                    cx + 1.0, y + 3.0, QTY_BTN_W - 2.0, ROW_H - 4.0, COLOR_QTY_BTN_HOVER);
            } else {
                RemoveClip(minusHL);
            }
            cx += QTY_BTN_W;

            // Quantity number — gold in adjust mode
            uint32_t qtyColor = isAdjusting ? COLOR_QTY_ADJUST : COLOR_QTY;
            std::string qtyName = "_rsP_Qty" + std::to_string(slot);
            std::string qtyStr = std::to_string(row.quantity);
            ScaleformUtil::CreateLabel(uiMovie.get(), qtyName.c_str(), labelDepth + 100,
                cx, y + 2.0, QTY_NUM_W, ROW_H, qtyStr.c_str(), 11, qtyColor);
            // Center-align qty
            {
                std::string p = "_root." + qtyName;
                RE::GFxValue tf;
                uiMovie->GetVariable(&tf, p.c_str());
                if (!tf.IsUndefined()) {
                    RE::GFxValue fmt;
                    uiMovie->CreateObject(&fmt, "TextFormat");
                    if (!fmt.IsUndefined()) {
                        RE::GFxValue av; av.SetString("center"); fmt.SetMember("align", av);
                        RE::GFxValue fa[1] = {fmt};
                        tf.Invoke("setTextFormat", nullptr, fa, 1);
                        tf.Invoke("setNewTextFormat", nullptr, fa, 1);
                    }
                }
            }
            cx += QTY_NUM_W;

            // [+] button
            bool plusHovered = (m_hoverQtyRow == r && m_hoverQtyIsPlus);
            std::string plusName = "_rsP_Plus" + std::to_string(slot);
            ScaleformUtil::CreateLabel(uiMovie.get(), plusName.c_str(), labelDepth + 200,
                cx, y + 2.0, QTY_BTN_W, ROW_H, "+", 11,
                plusHovered ? COLOR_QTY_BTN_HOVER : COLOR_QTY_BTN);
            {
                std::string p = "_root." + plusName;
                RE::GFxValue tf;
                uiMovie->GetVariable(&tf, p.c_str());
                if (!tf.IsUndefined()) {
                    RE::GFxValue fmt;
                    uiMovie->CreateObject(&fmt, "TextFormat");
                    if (!fmt.IsUndefined()) {
                        RE::GFxValue av; av.SetString("center"); fmt.SetMember("align", av);
                        RE::GFxValue fa[1] = {fmt};
                        tf.Invoke("setTextFormat", nullptr, fa, 1);
                        tf.Invoke("setNewTextFormat", nullptr, fa, 1);
                    }
                }
            }
            // [+] hover highlight
            std::string plusHL = "_rsP_PlusHL" + std::to_string(slot);
            if (plusHovered) {
                ScaleformUtil::DrawBorderRect(uiMovie.get(), plusHL.c_str(), labelDepth + 199,
                    cx + 1.0, y + 3.0, QTY_BTN_W - 2.0, ROW_H - 4.0, COLOR_QTY_BTN_HOVER);
            } else {
                RemoveClip(plusHL);
            }
            cx += QTY_BTN_W + 4.0;

            // Label — leave room for trash icon on right
            std::string lblName = "_rsP_Lbl" + std::to_string(slot);
            std::string label = row.label;
            double labelW = PAD_W - (cx - m_padX) - TRASH_W - 10.0;
            if (label.length() > 34) {
                label = label.substr(0, 32) + "..";
            }
            ScaleformUtil::CreateLabel(uiMovie.get(), lblName.c_str(), labelDepth + 300,
                cx, y + 2.0, labelW, ROW_H, label.c_str(), 11, COLOR_ITEM);

            // Trash icon "x" on right side of row
            bool trashHovered = (m_hoverTrashRow == r);
            double trashX = m_padX + PAD_W - TRASH_W - 6.0;
            std::string trashName = "_rsP_Trash" + std::to_string(slot);
            ScaleformUtil::CreateLabel(uiMovie.get(), trashName.c_str(), labelDepth + 400,
                trashX, y + 2.0, TRASH_W, ROW_H, "x", 10,
                trashHovered ? COLOR_TRASH_HOVER : COLOR_TRASH);
            // Center-align trash
            {
                std::string p = "_root." + trashName;
                RE::GFxValue tf;
                uiMovie->GetVariable(&tf, p.c_str());
                if (!tf.IsUndefined()) {
                    RE::GFxValue fmt;
                    uiMovie->CreateObject(&fmt, "TextFormat");
                    if (!fmt.IsUndefined()) {
                        RE::GFxValue av; av.SetString("center"); fmt.SetMember("align", av);
                        RE::GFxValue fa[1] = {fmt};
                        tf.Invoke("setTextFormat", nullptr, fa, 1);
                        tf.Invoke("setNewTextFormat", nullptr, fa, 1);
                    }
                }
            }
        }

        // Pad scrollbar
        int totalPad = static_cast<int>(m_padRows.size());
        if (totalPad > visRows) {
            DrawScrollbar("_rsPSB", depthBase + 4 * visRows,
                m_padX + PAD_W - 6.0, m_padY,
                PANEL_H, totalPad, visRows, m_padScroll);
        } else {
            RemoveClip("_rsPSBTrack");
            RemoveClip("_rsPSBThumb");
        }
    }

    void Menu::DrawScrollbar(const char* a_prefix, int a_depth, double a_x, double a_y,
                             double a_h, int a_totalRows, int a_visibleRows, int a_scrollOffset) {
        double trackW = 4.0;
        int maxOff = std::max(0, a_totalRows - a_visibleRows);

        std::string trackName = std::string(a_prefix) + "Track";
        ScaleformUtil::DrawFilledRect(uiMovie.get(), trackName.c_str(), a_depth,
            a_x, a_y, trackW, a_h, COLOR_SCROLLTRACK, 50);

        double thumbRatio = static_cast<double>(a_visibleRows) / static_cast<double>(a_totalRows);
        double thumbH = std::max(20.0, a_h * thumbRatio);
        double scrollRange = a_h - thumbH;
        double thumbY = a_y + (maxOff > 0 ? scrollRange * a_scrollOffset / maxOff : 0.0);

        std::string thumbName = std::string(a_prefix) + "Thumb";
        ScaleformUtil::DrawFilledRect(uiMovie.get(), thumbName.c_str(), a_depth + 1,
            a_x, thumbY, trackW, thumbH, COLOR_SCROLLTHUMB, 80);
    }

    // --- Hold-to-confirm (delegated to ButtonBar) ---

    void Menu::CancelButtonHold() {
        if (g_activeMenu) g_activeMenu->m_buttonBar.CancelHold();
    }

    void Menu::SetGamepadMode(bool a_gamepad) {
        if (!g_activeMenu) return;
        if (g_activeMenu->m_gamepad == a_gamepad) return;
        g_activeMenu->m_gamepad = a_gamepad;
        g_activeMenu->DrawGuideText();
    }

    void Menu::DrawGuideText() {
        RemoveClip("_rsGuide");

        // Helper: truncate name to fit within guide line
        auto truncName = [](const std::string& name, size_t maxLen) -> std::string {
            if (name.length() <= maxLen) return name;
            return name.substr(0, maxLen - 2) + "..";
        };

        std::string guide;
        bool gp = m_gamepad;

        switch (m_focus) {
            case FocusTarget::kBrowser: {
                // Get current browser row info
                std::string name;
                bool onPad = false;
                bool isHeader = false;
                if (m_browserCursor >= 0 && m_browserCursor < static_cast<int>(m_browserRows.size())) {
                    const auto& row = m_browserRows[m_browserCursor];
                    isHeader = row.isHeader;
                    if (!row.isHeader) {
                        name = row.label;
                        onPad = row.onPad;
                    }
                }

                if (isHeader) {
                    // State 3: on a header row
                    if (gp) {
                        guide = "(A) Expand/Collapse | (LB) Pad | D-pad: Navigate";
                    } else {
                        guide = "Enter: Expand/Collapse | Right/Tab: Pad | Up/Down: Navigate";
                    }
                } else if (name.empty()) {
                    // No selectable item under cursor (shouldn't happen, but guard)
                    guide = gp ? "(LB) Pad" : "Tab: Pad";
                } else if (onPad) {
                    // State 2: item already on pad
                    std::string tn = truncName(name, 40);
                    guide = "'" + tn + "' already on pad";
                    guide += gp ? " | (LB) Pad" : " | Right/Tab: Pad";
                } else {
                    // State 1: item selectable
                    std::string tn = truncName(name, 30);
                    if (gp) {
                        guide = "(A) Add '" + tn + "' | (LB) Pad | D-pad: Navigate";
                    } else {
                        guide = "Enter: Add '" + tn + "' | Right/Tab: Pad | Up/Down: Navigate";
                    }
                }
                break;
            }
            case FocusTarget::kPad: {
                if (m_padRows.empty()) {
                    // State 8: pad empty
                    if (gp) {
                        guide = "(LB) Browse categories | (A) to add items";
                    } else {
                        guide = "Left to browse categories | Enter to add items";
                    }
                    break;
                }

                // Get current pad item name
                std::string name;
                if (m_padCursor >= 0 && m_padCursor < static_cast<int>(m_padRows.size())) {
                    name = m_padRows[m_padCursor].label;
                }

                if (m_deleteRow >= 0 && m_deleteRow < static_cast<int>(m_padRows.size())) {
                    // State 6: delete animation
                    std::string tn = truncName(m_padRows[m_deleteRow].label, 40);
                    guide = "Removing '" + tn + "'...";
                } else if (m_adjustMode) {
                    // State 5: adjust mode
                    std::string tn = truncName(name, 25);
                    if (gp) {
                        guide = "D-pad L/R: Adjust '" + tn + "' qty | (A) Done | (X) Remove";
                    } else {
                        guide = "L/R: Adjust '" + tn + "' qty | Enter: Done | Del: Remove";
                    }
                } else {
                    // State 4: normal pad
                    if (gp) {
                        guide = "(A) Edit qty | (X) Remove | (LB) Browser";
                    } else {
                        guide = "Enter: Edit qty | Del: Remove | Left/Tab: Browser";
                    }
                }
                break;
            }
            case FocusTarget::kButtonBar: {
                // State 7: button bar
                if (gp) {
                    guide = "D-pad L/R: Select | (A) Activate";
                } else {
                    guide = "L/R: Select | Enter: Activate";
                }
                break;
            }
        }

        if (!guide.empty()) {
            int depthGuide = m_baseDepth + 900;
            ScaleformUtil::CreateLabel(uiMovie.get(), "_rsGuide", depthGuide,
                m_popupX + 24.0, m_guideY, POPUP_W - 48.0, 18.0,
                guide.c_str(), 11, COLOR_GUIDE);
        }
    }

    void Menu::DrawButtons() {
        m_buttonBar.Draw(
            (m_focus == FocusTarget::kButtonBar) ? m_btnIndex : -1,
            m_hoverBtnIndex);
    }

    // --- Input actions ---

    void Menu::NavigateUp() {
        if (!g_activeMenu) return;
        auto& m = *g_activeMenu;
        if (m.m_deleteRow >= 0) return;  // locked during delete animation

        m.m_buttonBar.CancelHold();
        // Exit adjust mode on vertical navigation
        m.m_adjustMode = false;

        switch (m.m_focus) {
            case FocusTarget::kBrowser:
                m.BrowserUp();
                break;
            case FocusTarget::kPad:
                m.PadUp();
                break;
            case FocusTarget::kButtonBar:
                // Return to last panel
                m.m_focus = FocusTarget::kPad;
                if (m.m_padRows.empty()) m.m_focus = FocusTarget::kBrowser;
                break;
        }
        m.UpdateAll();
    }

    void Menu::NavigateDown() {
        if (!g_activeMenu) return;
        auto& m = *g_activeMenu;
        if (m.m_deleteRow >= 0) return;  // locked during delete animation

        m.m_buttonBar.CancelHold();
        // Exit adjust mode on vertical navigation
        m.m_adjustMode = false;

        switch (m.m_focus) {
            case FocusTarget::kBrowser: {
                int next = m.NextSelectableRow(m.m_browserCursor, 1);
                if (next == m.m_browserCursor) {
                    // At bottom — drop to button bar
                    m.m_focus = FocusTarget::kButtonBar;
                } else {
                    m.BrowserDown();
                }
                break;
            }
            case FocusTarget::kPad:
                if (m.m_padCursor >= static_cast<int>(m.m_padRows.size()) - 1) {
                    m.m_focus = FocusTarget::kButtonBar;
                } else {
                    m.PadDown();
                }
                break;
            case FocusTarget::kButtonBar:
                break;
        }
        m.UpdateAll();
    }

    void Menu::NavigateLeft() {
        if (!g_activeMenu) return;
        auto& m = *g_activeMenu;
        if (m.m_deleteRow >= 0) return;  // locked during delete animation

        m.m_buttonBar.CancelHold();
        // In adjust mode, L/R adjusts quantity
        if (m.m_adjustMode && m.m_focus == FocusTarget::kPad) {
            m.PadAdjustQty(-1);
            m.UpdateAll();
            return;
        }

        switch (m.m_focus) {
            case FocusTarget::kBrowser:
                break;  // already on left panel
            case FocusTarget::kPad:
                // Switch to browser
                m.m_focus = FocusTarget::kBrowser;
                m.m_adjustMode = false;
                break;
            case FocusTarget::kButtonBar:
                m.m_btnIndex = (m.m_btnIndex > 0) ? m.m_btnIndex - 1 : m.m_buttonBar.Count() - 1;
                break;
        }
        m.UpdateAll();
    }

    void Menu::NavigateRight() {
        if (!g_activeMenu) return;
        auto& m = *g_activeMenu;
        if (m.m_deleteRow >= 0) return;  // locked during delete animation

        m.m_buttonBar.CancelHold();
        // In adjust mode, L/R adjusts quantity
        if (m.m_adjustMode && m.m_focus == FocusTarget::kPad) {
            m.PadAdjustQty(1);
            m.UpdateAll();
            return;
        }

        switch (m.m_focus) {
            case FocusTarget::kBrowser:
                // Switch to pad (if it has items)
                if (!m.m_padRows.empty()) {
                    m.m_focus = FocusTarget::kPad;
                }
                break;
            case FocusTarget::kPad:
                break;  // already on right panel
            case FocusTarget::kButtonBar:
                m.m_btnIndex = (m.m_btnIndex < m.m_buttonBar.Count() - 1) ? m.m_btnIndex + 1 : 0;
                break;
        }
        m.UpdateAll();
    }

    void Menu::Activate() {
        if (!g_activeMenu) return;
        auto& m = *g_activeMenu;
        if (m.m_deleteRow >= 0) return;  // locked during delete animation

        switch (m.m_focus) {
            case FocusTarget::kBrowser:
                m.BrowserActivate();
                break;
            case FocusTarget::kPad:
                if (!m.m_padRows.empty()) {
                    // Toggle adjust mode
                    m.m_adjustMode = !m.m_adjustMode;
                    logger::debug("RestockPad: adjust mode {} on row {}", m.m_adjustMode ? "ON" : "OFF", m.m_padCursor);
                }
                break;
            case FocusTarget::kButtonBar:
                switch (m.m_btnIndex) {
                    case 0: Confirm(); return;
                    case 1:  // Default — hold-to-confirm
                    case 2:  // Clear — hold-to-confirm
                        if (!m.m_buttonBar.IsHolding()) m.m_buttonBar.StartHold(m.m_btnIndex);
                        return;
                    case 3: Cancel(); return;
                }
                break;
        }
        m.UpdateAll();
    }

    void Menu::RemoveItem() {
        if (!g_activeMenu) return;
        auto& m = *g_activeMenu;
        if (m.m_deleteRow >= 0) return;  // already animating

        if (m.m_focus == FocusTarget::kPad && !m.m_padRows.empty()) {
            m.StartDelete(m.m_padCursor);
            m.UpdateAll();
        }
    }

    void Menu::SwitchPanel() {
        if (!g_activeMenu) return;
        auto& m = *g_activeMenu;
        if (m.m_deleteRow >= 0) return;

        m.m_adjustMode = false;

        switch (m.m_focus) {
            case FocusTarget::kBrowser:
                if (!m.m_padRows.empty()) {
                    m.m_focus = FocusTarget::kPad;
                }
                break;
            case FocusTarget::kPad:
                m.m_focus = FocusTarget::kBrowser;
                break;
            case FocusTarget::kButtonBar:
                m.m_focus = FocusTarget::kBrowser;
                break;
        }
        m.UpdateAll();
    }

    void Menu::Confirm() {
        if (!g_activeMenu) return;
        auto config = g_activeMenu->AssembleConfig();
        auto cb = s_callback;
        Hide();
        if (cb) cb(true, std::move(config));
    }

    void Menu::Cancel() {
        auto cb = s_callback;
        Hide();
        if (cb) cb(false, {});
    }

    void Menu::SetDefault() {
        if (!g_activeMenu) return;
        auto& m = *g_activeMenu;
        m.m_adjustMode = false;
        m.m_deleteRow = -1;
        m.LoadFromConfig(RestockCategory::DefaultConfig());
        m.m_padCursor = 0;
        m.m_padScroll = 0;
        m.UpdateAll();
    }

    void Menu::ClearAll() {
        if (!g_activeMenu) return;
        auto& m = *g_activeMenu;
        m.m_adjustMode = false;
        m.m_deleteRow = -1;
        m.m_padRows.clear();
        m.m_padCursor = 0;
        m.m_padScroll = 0;
        m.SyncBrowserOnPadState();
        m.UpdateAll();
    }

    void Menu::AdjustQty(int a_delta) {
        if (!g_activeMenu) return;
        auto& m = *g_activeMenu;
        if (m.m_focus == FocusTarget::kPad && m.m_adjustMode && !m.m_padRows.empty()) {
            m.PadAdjustQty(a_delta);
            m.UpdateAll();
        }
    }

    bool Menu::IsAdjusting() {
        return g_activeMenu &&
               g_activeMenu->m_focus == FocusTarget::kPad &&
               g_activeMenu->m_adjustMode &&
               !g_activeMenu->m_padRows.empty();
    }

    // --- Mouse ---

    void Menu::OnMouseMove() {
        if (!g_activeMenu) return;
        auto& m = *g_activeMenu;

        auto [mx, my] = m.GetMousePos();
        MouseGlow::SetPosition(m.uiMovie.get(), "_mouseGlow", static_cast<double>(mx), static_cast<double>(my));

        // Browser hover
        int oldBrowserHover = m.m_browserHover;
        m.m_browserHover = -1;
        if (mx >= m.m_browserX && mx < m.m_browserX + BROWSER_W &&
            my >= m.m_browserY && my < m.m_browserY + PANEL_H) {

            // Build visible list
            std::vector<int> visList;
            for (int i = 0; i < static_cast<int>(m.m_browserRows.size()); ++i) {
                if (m.IsBrowserRowVisible(i)) visList.push_back(i);
            }

            int visRow = static_cast<int>((my - m.m_browserY) / ROW_H) + m.m_browserScroll;
            if (visRow >= 0 && visRow < static_cast<int>(visList.size())) {
                int dataIdx = visList[visRow];
                if (!m.m_browserRows[dataIdx].isHeader) {
                    m.m_browserHover = dataIdx;
                }
            }
        }

        // Pad hover
        int oldPadHover = m.m_padHover;
        int oldQtyRow = m.m_hoverQtyRow;
        bool oldQtyIsPlus = m.m_hoverQtyIsPlus;
        int oldTrashRow = m.m_hoverTrashRow;
        m.m_padHover = -1;
        m.m_hoverQtyRow = -1;
        m.m_hoverTrashRow = -1;
        if (mx >= m.m_padX && mx < m.m_padX + PAD_W &&
            my >= m.m_padY && my < m.m_padY + PANEL_H) {
            int row = static_cast<int>((my - m.m_padY) / ROW_H) + m.m_padScroll;
            if (row >= 0 && row < static_cast<int>(m.m_padRows.size())) {
                m.m_padHover = row;

                // Check if hovering over [-] or [+]
                double cx = m.m_padX + 6.0;
                if (mx >= cx && mx < cx + QTY_BTN_W) {
                    m.m_hoverQtyRow = row;
                    m.m_hoverQtyIsPlus = false;
                } else if (mx >= cx + QTY_BTN_W + QTY_NUM_W && mx < cx + QTY_BTN_W + QTY_NUM_W + QTY_BTN_W) {
                    m.m_hoverQtyRow = row;
                    m.m_hoverQtyIsPlus = true;
                }

                // Check if hovering over trash icon
                double trashX = m.m_padX + PAD_W - TRASH_W - 6.0;
                if (mx >= trashX && mx < trashX + TRASH_W) {
                    m.m_hoverTrashRow = row;
                }
            }
        }

        // Button hover
        int oldBtnHover = m.m_hoverBtnIndex;
        m.m_hoverBtnIndex = m.m_buttonBar.HitTest(mx, my);

        if (m.m_browserHover != oldBrowserHover || m.m_padHover != oldPadHover ||
            m.m_hoverBtnIndex != oldBtnHover ||
            m.m_hoverQtyRow != oldQtyRow || m.m_hoverQtyIsPlus != oldQtyIsPlus ||
            m.m_hoverTrashRow != oldTrashRow) {
            m.UpdateAll();
        }
    }

    void Menu::OnMouseDown() {
        if (!g_activeMenu) return;
        auto& m = *g_activeMenu;

        auto [mx, my] = m.GetMousePos();

        // Browser click
        if (mx >= m.m_browserX && mx < m.m_browserX + BROWSER_W &&
            my >= m.m_browserY && my < m.m_browserY + PANEL_H) {
            m.m_focus = FocusTarget::kBrowser;

            std::vector<int> visList;
            for (int i = 0; i < static_cast<int>(m.m_browserRows.size()); ++i) {
                if (m.IsBrowserRowVisible(i)) visList.push_back(i);
            }

            int visRow = static_cast<int>((my - m.m_browserY) / ROW_H) + m.m_browserScroll;
            if (visRow >= 0 && visRow < static_cast<int>(visList.size())) {
                int dataIdx = visList[visRow];
                auto& row = m.m_browserRows[dataIdx];
                if (row.isHeader) {
                    // Toggle expand/collapse
                    row.expanded = !row.expanded;
                    // Adjust scroll if needed
                    if (m.m_browserScroll > m.BrowserMaxScroll()) {
                        m.m_browserScroll = m.BrowserMaxScroll();
                    }
                } else {
                    m.m_browserCursor = dataIdx;
                    if (!row.onPad) {
                        m.AddToPad(row.id, m.QualifiedLabel(row));
                    }
                }
            }
            m.UpdateAll();
            return;
        }

        // Pad click
        if (mx >= m.m_padX && mx < m.m_padX + PAD_W &&
            my >= m.m_padY && my < m.m_padY + PANEL_H) {
            m.m_focus = FocusTarget::kPad;

            int row = static_cast<int>((my - m.m_padY) / ROW_H) + m.m_padScroll;
            if (row >= 0 && row < static_cast<int>(m.m_padRows.size())) {
                m.m_padCursor = row;

                // Check if click is on trash icon
                double trashX = m.m_padX + PAD_W - TRASH_W - 6.0;
                if (mx >= trashX && mx < trashX + TRASH_W && m.m_deleteRow < 0) {
                    m.StartDelete(row);
                    m.UpdateAll();
                    return;
                }

                // Check if click is on [-] or [+]
                double cx = m.m_padX + 6.0;
                if (mx >= cx && mx < cx + QTY_BTN_W) {
                    m.PadAdjustQty(-1);
                } else if (mx >= cx + QTY_BTN_W + QTY_NUM_W && mx < cx + QTY_BTN_W + QTY_NUM_W + QTY_BTN_W) {
                    m.PadAdjustQty(1);
                }
            }
            m.UpdateAll();
            return;
        }

        // Button click
        int hitBtn = m.m_buttonBar.HitTest(mx, my);
        if (hitBtn >= 0) {
            switch (hitBtn) {
                case 0: Confirm(); return;
                case 1:  // Default — hold-to-confirm
                case 2:  // Clear — hold-to-confirm
                    if (!m.m_buttonBar.IsHolding()) m.m_buttonBar.StartHold(hitBtn);
                    return;
                case 3: Cancel(); return;
            }
        }
    }

    void Menu::OnMouseUp() {
        // Nothing to do — repeat state is managed in InputHandler
    }

    void Menu::OnMouseHeld() {
        if (!g_activeMenu) return;
        auto& m = *g_activeMenu;

        // Only repeat on +/- buttons
        if (m.m_hoverQtyRow < 0) return;

        m.m_padCursor = m.m_hoverQtyRow;
        m.m_focus = FocusTarget::kPad;
        m.PadAdjustQty(m.m_hoverQtyIsPlus ? 1 : -1);
        m.UpdateAll();
    }

    void Menu::OnMouseRightClick() {
        if (!g_activeMenu) return;
        auto& m = *g_activeMenu;
        if (m.m_deleteRow >= 0) return;  // already animating

        auto [mx, my] = m.GetMousePos();

        // Right-click on pad = delete with animation
        if (mx >= m.m_padX && mx < m.m_padX + PAD_W &&
            my >= m.m_padY && my < m.m_padY + PANEL_H) {
            int row = static_cast<int>((my - m.m_padY) / ROW_H) + m.m_padScroll;
            if (row >= 0 && row < static_cast<int>(m.m_padRows.size())) {
                m.m_padCursor = row;
                m.StartDelete(row);
                m.UpdateAll();
            }
        }
    }

    void Menu::OnMouseScroll(int a_direction) {
        if (!g_activeMenu) return;
        auto& m = *g_activeMenu;

        auto [mx, my] = m.GetMousePos();

        // Scroll in browser
        if (mx >= m.m_browserX && mx < m.m_browserX + BROWSER_W &&
            my >= m.m_browserY && my < m.m_browserY + PANEL_H) {
            int maxOff = m.BrowserMaxScroll();
            int newOff = std::clamp(m.m_browserScroll + a_direction, 0, maxOff);
            if (newOff != m.m_browserScroll) {
                m.m_browserScroll = newOff;
                m.UpdateAll();
            }
            return;
        }

        // Scroll in pad (or adjust qty if on item)
        if (mx >= m.m_padX && mx < m.m_padX + PAD_W &&
            my >= m.m_padY && my < m.m_padY + PANEL_H) {
            int row = static_cast<int>((my - m.m_padY) / ROW_H) + m.m_padScroll;
            if (row >= 0 && row < static_cast<int>(m.m_padRows.size())) {
                // Scroll on pad item adjusts quantity
                m.m_padCursor = row;
                m.m_focus = FocusTarget::kPad;
                m.PadAdjustQty(a_direction > 0 ? 1 : -1);
                m.UpdateAll();
                return;
            }

            int maxOff = m.PadMaxScroll();
            int newOff = std::clamp(m.m_padScroll + a_direction, 0, maxOff);
            if (newOff != m.m_padScroll) {
                m.m_padScroll = newOff;
                m.UpdateAll();
            }
        }
    }

    // --- InputHandler ---

    InputHandler* InputHandler::GetSingleton() {
        static InputHandler singleton;
        return &singleton;
    }

    void InputHandler::Register() {
        auto input = RE::BSInputDeviceManager::GetSingleton();
        if (input) {
            input->AddEventSink(GetSingleton());
            logger::info("RestockConfig::InputHandler registered");
        }
    }

    RE::BSEventNotifyControl InputHandler::ProcessEvent(
        RE::InputEvent* const* a_event,
        RE::BSTEventSource<RE::InputEvent*>*) {

        if (!a_event || !Menu::IsOpen()) {
            return RE::BSEventNotifyControl::kContinue;
        }

        for (auto* event = *a_event; event; event = event->next) {
            // Mouse move
            if (event->eventType == RE::INPUT_EVENT_TYPE::kMouseMove) {
                Menu::OnMouseMove();
                continue;
            }

            // Left thumbstick
            if (event->eventType == RE::INPUT_EVENT_TYPE::kThumbstick) {
                auto* thumbstick = static_cast<RE::ThumbstickEvent*>(event);
                if (!thumbstick->IsLeft()) continue;

                Menu::SetGamepadMode(true);
                auto edges = DirectionalInput::ProcessThumbstick(
                    thumbstick->xValue, thumbstick->yValue, m_thumbState);

                // In adjust mode, L/R repeats for qty adjustment
                if (Menu::IsAdjusting()) {
                    int horizDir = m_thumbState.left ? -1 : (m_thumbState.right ? 1 : 0);
                    if (DirectionalInput::ProcessRepeat(horizDir, m_adjustRepeat)) {
                        if (horizDir == -1) Menu::NavigateLeft();
                        else                Menu::NavigateRight();
                    }
                } else {
                    if (edges.left)  Menu::NavigateLeft();
                    if (edges.right) Menu::NavigateRight();
                }

                int vertDir = m_thumbState.up ? -1 : (m_thumbState.down ? 1 : 0);
                if (DirectionalInput::ProcessRepeat(vertDir, m_repeatState)) {
                    if (vertDir == -1) Menu::NavigateUp();
                    else               Menu::NavigateDown();
                }
                continue;
            }

            auto* button = event->AsButtonEvent();
            if (!button) continue;

            auto device = button->GetDevice();
            auto key = button->GetIDCode();
            bool isDown = button->IsDown();
            bool isPressed = button->IsPressed();
            bool isUp = button->IsUp();

            // Mouse button
            if (device == RE::INPUT_DEVICE::kMouse) {
                constexpr uint32_t MOUSE_WHEEL_UP   = 8;
                constexpr uint32_t MOUSE_WHEEL_DOWN = 9;
                if (key == 0) {
                    if (isDown) {
                        Menu::OnMouseDown();
                        // Start repeat timer — first repeat fires after initialDelay
                        m_mouseQtyRepeat.direction = 1;
                        m_mouseQtyRepeat.active = false;
                        m_mouseQtyRepeat.lastTime = std::chrono::steady_clock::now();
                    } else if (isPressed && m_mouseQtyRepeat.direction != 0) {
                        // Held — repeat on +/- buttons after initial delay
                        auto now = std::chrono::steady_clock::now();
                        float elapsed = std::chrono::duration<float>(now - m_mouseQtyRepeat.lastTime).count();
                        float delay = m_mouseQtyRepeat.active ? m_mouseQtyRepeat.interval : m_mouseQtyRepeat.initialDelay;
                        if (elapsed >= delay) {
                            m_mouseQtyRepeat.active = true;
                            m_mouseQtyRepeat.lastTime = now;
                            Menu::OnMouseHeld();
                        }
                    } else if (isUp) {
                        m_mouseQtyRepeat.direction = 0;
                        m_mouseQtyRepeat.active = false;
                        Menu::CancelButtonHold();
                    }
                } else if (key == 1 && isDown) {
                    Menu::OnMouseRightClick();
                } else if ((key == MOUSE_WHEEL_UP || key == MOUSE_WHEEL_DOWN) && isDown) {
                    int dir = (key == MOUSE_WHEEL_UP) ? -1 : 1;
                    Menu::OnMouseScroll(dir);
                }
                continue;
            }

            // Gamepad
            if (device == RE::INPUT_DEVICE::kGamepad) {
                Menu::SetGamepadMode(true);
                if (key == ScaleformUtil::GAMEPAD_DPAD_UP || key == ScaleformUtil::GAMEPAD_DPAD_DOWN) {
                    int dir = (key == ScaleformUtil::GAMEPAD_DPAD_UP) ? -1 : 1;
                    if (DirectionalInput::ProcessButtonRepeat(dir, isDown, isPressed, isUp, m_repeatState)) {
                        if (dir == -1) Menu::NavigateUp();
                        else           Menu::NavigateDown();
                    }
                    continue;
                }
                // D-pad L/R: switch panes normally, adjust qty in adjust mode (with repeat)
                if (key == ScaleformUtil::GAMEPAD_DPAD_LEFT || key == ScaleformUtil::GAMEPAD_DPAD_RIGHT) {
                    int dir = (key == ScaleformUtil::GAMEPAD_DPAD_LEFT) ? -1 : 1;
                    if (DirectionalInput::ProcessButtonRepeat(dir, isDown, isPressed, isUp, m_adjustRepeat)) {
                        if (dir == -1) Menu::NavigateLeft();
                        else           Menu::NavigateRight();
                    }
                    continue;
                }
                // Gamepad A: hold support for Default/Clear buttons
                if (key == ScaleformUtil::GAMEPAD_A) {
                    if (isDown) Menu::Activate();
                    else if (isUp) Menu::CancelButtonHold();
                    continue;
                }
                if (!isDown) continue;
                switch (key) {
                    case ScaleformUtil::GAMEPAD_B:          Menu::Cancel(); break;
                    case ScaleformUtil::GAMEPAD_X:          Menu::RemoveItem(); break;
                    case ScaleformUtil::GAMEPAD_LB:         Menu::SwitchPanel(); break;
                }
                continue;
            }

            // Keyboard
            if (device == RE::INPUT_DEVICE::kKeyboard) {
                Menu::SetGamepadMode(false);
                using Key = RE::BSKeyboardDevice::Key;
                if (key == Key::kUp || key == Key::kDown) {
                    int dir = (key == Key::kUp) ? -1 : 1;
                    if (DirectionalInput::ProcessButtonRepeat(dir, isDown, isPressed, isUp, m_repeatState)) {
                        if (dir == -1) Menu::NavigateUp();
                        else           Menu::NavigateDown();
                    }
                    continue;
                }
                // L/R arrows: switch panes normally, adjust qty in adjust mode (with repeat)
                if (key == Key::kLeft || key == Key::kRight) {
                    int dir = (key == Key::kLeft) ? -1 : 1;
                    if (DirectionalInput::ProcessButtonRepeat(dir, isDown, isPressed, isUp, m_adjustRepeat)) {
                        if (dir == -1) Menu::NavigateLeft();
                        else           Menu::NavigateRight();
                    }
                    continue;
                }
                // +/- keys for qty adjust in adjust mode (with repeat)
                if (key == Key::kEquals || key == Key::kMinus) {
                    int dir = (key == Key::kEquals) ? 1 : -1;
                    if (DirectionalInput::ProcessButtonRepeat(dir, isDown, isPressed, isUp, m_adjustRepeat)) {
                        if (Menu::IsAdjusting()) {
                            Menu::AdjustQty(dir);
                        }
                    }
                    continue;
                }
                // Enter/Space: hold support for Default/Clear buttons
                if (key == Key::kEnter || key == Key::kSpacebar) {
                    if (isDown) Menu::Activate();
                    else if (isUp) Menu::CancelButtonHold();
                    continue;
                }
                if (!isDown) continue;
                switch (key) {
                    case Key::kEscape:     Menu::Cancel(); break;
                    case Key::kDelete:     Menu::RemoveItem(); break;
                    case Key::kBackspace:  Menu::RemoveItem(); break;
                    case Key::kTab:        Menu::SwitchPanel(); break;
                }
                continue;
            }
        }

        return RE::BSEventNotifyControl::kContinue;
    }
}
