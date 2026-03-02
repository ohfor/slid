#include "ContextMenu.h"
#include "TranslationService.h"

namespace ContextMenu {

    static Menu* g_activeMenu = nullptr;

    // =========================================================================
    // Menu
    // =========================================================================

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
            scaleform->LoadMovie(this, uiMovie, FILE_NAME.data());
        }
    }

    void Menu::Register() {
        auto ui = RE::UI::GetSingleton();
        if (ui) {
            ui->Register(MENU_NAME, Create);
            logger::info("ContextMenu registered");
        }
    }

    RE::IMenu* Menu::Create() {
        return new Menu();
    }

    // Pending data for the next Show — stashed here so ProcessMessage(kShow) can pick it up
    static ContextResolver::ResolvedContext s_pendingContext;
    static Callback s_pendingCallback;

    void Menu::Show(const ContextResolver::ResolvedContext& a_context, Callback a_callback) {
        auto ui = RE::UI::GetSingleton();
        if (ui && !ui->IsMenuOpen(MENU_NAME)) {
            s_pendingContext = a_context;
            s_pendingCallback = std::move(a_callback);

            auto msgQueue = RE::UIMessageQueue::GetSingleton();
            if (msgQueue) {
                msgQueue->AddMessage(MENU_NAME, RE::UI_MESSAGE_TYPE::kShow, nullptr);
            }
        }
    }

    void Menu::Hide() {
        auto ui = RE::UI::GetSingleton();
        if (ui && ui->IsMenuOpen(MENU_NAME)) {
            auto msgQueue = RE::UIMessageQueue::GetSingleton();
            if (msgQueue) {
                msgQueue->AddMessage(MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
            }
        }
    }

    bool Menu::IsOpen() {
        auto ui = RE::UI::GetSingleton();
        return ui && ui->IsMenuOpen(MENU_NAME);
    }

    RE::UI_MESSAGE_RESULTS Menu::ProcessMessage(RE::UIMessage& a_message) {
        using Message = RE::UI_MESSAGE_TYPE;
        switch (*a_message.type) {
            case Message::kShow: {
                g_activeMenu = this;
                m_context = std::move(s_pendingContext);
                m_callback = std::move(s_pendingCallback);
                m_holding = false;
                m_holdRow = -1;
                if (uiMovie) {
                    uiMovie->GetVariable(&m_root, "_root");
                    BuildUI();
                }
                return RE::UI_MESSAGE_RESULTS::kHandled;
            }
            case Message::kHide: {
                g_activeMenu = nullptr;
                return RE::UI_MESSAGE_RESULTS::kHandled;
            }
            case Message::kUpdate: {
                UpdateHold();
                return RE::UI_MESSAGE_RESULTS::kHandled;
            }
            default:
                break;
        }
        return RE::IMenu::ProcessMessage(a_message);
    }

    double Menu::PopupH() const {
        // Title + network (if cyclable) + separator + rows + separator + description
        double h = PAD + TITLE_H;
        if (!m_context.cyclableNetworks.empty()) {
            h += NETWORK_H;
        }
        h += SEP_H + 4.0;  // separator + small gap
        h += static_cast<double>(m_context.actions.size()) * ROW_H;
        h += 4.0 + SEP_H;  // small gap + separator
        h += DESC_PAD + DESC_H;
        h += PAD;
        return h;
    }

    void Menu::BuildUI() {
        if (!uiMovie) return;

        m_cursor = 0;

        RE::GRectF rect = uiMovie->GetVisibleFrameRect();
        double screenW = rect.right - rect.left;
        double screenH = rect.bottom - rect.top;
        if (screenW <= 0) screenW = 1280.0;
        if (screenH <= 0) screenH = 720.0;

        double popupH = PopupH();
        m_popupX = (screenW - POPUP_W) / 2.0;
        m_popupY = (screenH - popupH) / 2.0;

        // Dim background
        ScaleformUtil::DrawFilledRect(uiMovie.get(), "_dim", 1,
            0.0, 0.0, screenW, screenH, 0x000000, ALPHA_DIM);

        DrawBackground();
        DrawTitle();
        DrawNetworkSubtitle();
        DrawActionRows();
        DrawCursorHighlight();
        DrawDescription();
    }

    void Menu::DrawBackground() {
        double popupH = PopupH();

        // Soft outer glow — 5 expanding layers with decreasing opacity
        constexpr struct { double spread; int alpha; } glowLayers[] = {
            {10.0, 6}, {8.0, 10}, {6.0, 14}, {4.0, 20}, {2.0, 28}
        };
        for (int i = 0; i < 5; ++i) {
            double s = glowLayers[i].spread;
            std::string name = "ctx_glow" + std::to_string(i);
            ScaleformUtil::DrawFilledRect(uiMovie.get(), name.c_str(), 5 + i,
                m_popupX - s, m_popupY - s, POPUP_W + s * 2, popupH + s * 2,
                0x000000, glowLayers[i].alpha);
        }

        // Background fill
        ScaleformUtil::DrawFilledRect(uiMovie.get(), "ctx_bg", 10,
            m_popupX, m_popupY, POPUP_W, popupH, COLOR_BG, ALPHA_BG);

        // Border
        ScaleformUtil::DrawBorderRect(uiMovie.get(), "ctx_border", 11,
            m_popupX, m_popupY, POPUP_W, popupH, COLOR_BORDER);

        // Gold accent line at top
        ScaleformUtil::DrawFilledRect(uiMovie.get(), "ctx_accent", 12,
            m_popupX, m_popupY, POPUP_W, 2.0, COLOR_CURSOR, 90);
    }

    void Menu::DrawTitle() {
        double x = m_popupX + PAD;
        double y = m_popupY + PAD;
        ScaleformUtil::CreateLabel(uiMovie.get(), "ctx_title", 20,
            x, y, POPUP_W - PAD * 2, TITLE_H,
            "SLID", 16, COLOR_TITLE);
    }

    void Menu::DrawNetworkSubtitle() {
        if (m_context.cyclableNetworks.empty() && m_context.networkName.empty()) return;

        double x = m_popupX + PAD;
        double y = m_popupY + PAD + TITLE_H;

        std::string text;
        if (m_context.cyclableNetworks.size() > 1) {
            text = "\x3C " + m_context.networkName + " \x3E";  // < Name >
        } else {
            text = m_context.networkName;
        }

        ScaleformUtil::CreateLabel(uiMovie.get(), "ctx_network", 21,
            x, y, POPUP_W - PAD * 2, NETWORK_H,
            text.c_str(), 14, COLOR_NETWORK);

        // Right-aligned "1 / N" counter when multiple networks exist
        if (m_context.cyclableNetworks.size() > 1) {
            auto it = std::find(m_context.cyclableNetworks.begin(),
                                m_context.cyclableNetworks.end(),
                                m_context.networkName);
            size_t idx = (it != m_context.cyclableNetworks.end())
                ? static_cast<size_t>(std::distance(m_context.cyclableNetworks.begin(), it)) + 1
                : 1;
            std::string cycleCount = std::to_string(idx)
                + " / " + std::to_string(m_context.cyclableNetworks.size());

            ScaleformUtil::CreateLabel(uiMovie.get(), "ctx_cycle_hint", 29,
                x, y + 2.0, POPUP_W - PAD * 2, NETWORK_H,
                cycleCount.c_str(), 10, COLOR_CHEVRON);

            // Right-align via TextFormat
            RE::GFxValue hint;
            uiMovie->GetVariable(&hint, "_root.ctx_cycle_hint");
            if (!hint.IsUndefined()) {
                RE::GFxValue tfmt;
                hint.Invoke("getTextFormat", &tfmt, nullptr, 0);
                if (!tfmt.IsUndefined()) {
                    RE::GFxValue alignVal;
                    alignVal.SetString("right");
                    tfmt.SetMember("align", alignVal);
                    RE::GFxValue args[1] = {tfmt};
                    hint.Invoke("setTextFormat", nullptr, args, 1);
                }
            }
        }
    }

    void Menu::RedrawNetworkSubtitle() {
        if (!uiMovie) return;

        std::string text;
        if (m_context.cyclableNetworks.size() > 1) {
            text = "\x3C " + m_context.networkName + " \x3E";
        } else {
            text = m_context.networkName;
        }

        RE::GFxValue field;
        uiMovie->GetVariable(&field, "_root.ctx_network");
        if (field.IsDisplayObject()) {
            RE::GFxValue val;
            val.SetString(text.c_str());
            field.SetMember("text", val);
        }

        // Update cycle counter
        if (m_context.cyclableNetworks.size() > 1) {
            RE::GFxValue hint;
            uiMovie->GetVariable(&hint, "_root.ctx_cycle_hint");
            if (hint.IsDisplayObject()) {
                auto it = std::find(m_context.cyclableNetworks.begin(),
                                    m_context.cyclableNetworks.end(),
                                    m_context.networkName);
                size_t idx = (it != m_context.cyclableNetworks.end())
                    ? static_cast<size_t>(std::distance(m_context.cyclableNetworks.begin(), it)) + 1
                    : 1;
                std::string cycleCount = std::to_string(idx)
                    + " / " + std::to_string(m_context.cyclableNetworks.size());

                RE::GFxValue val;
                val.SetString(cycleCount.c_str());
                hint.SetMember("text", val);

                // Re-apply right alignment after text change
                RE::GFxValue tfmt;
                hint.Invoke("getTextFormat", &tfmt, nullptr, 0);
                if (!tfmt.IsUndefined()) {
                    RE::GFxValue alignVal;
                    alignVal.SetString("right");
                    tfmt.SetMember("align", alignVal);
                    RE::GFxValue args[1] = {tfmt};
                    hint.Invoke("setTextFormat", nullptr, args, 1);
                }
            }
        }
    }

    void Menu::DrawActionRows() {
        double yBase = PAD + TITLE_H;
        if (!m_context.cyclableNetworks.empty() || !m_context.networkName.empty()) {
            yBase += NETWORK_H;
        }
        yBase += SEP_H + 4.0;

        m_rowsY = yBase;

        // Separator before rows
        double sepY = m_popupY + yBase - SEP_H - 4.0;
        ScaleformUtil::DrawFilledRect(uiMovie.get(), "ctx_sep1", 22,
            m_popupX + PAD, sepY, POPUP_W - PAD * 2, SEP_H, COLOR_SEP, 100);

        constexpr double fontSize = 14.0;
        constexpr double textVPad = (ROW_H - fontSize) / 2.0 - 1.0;  // -1 for visual centering

        for (size_t i = 0; i < m_context.actions.size(); ++i) {
            auto& entry = m_context.actions[i];
            double rowY = m_popupY + yBase + static_cast<double>(i) * ROW_H;

            std::string name = "ctx_row" + std::to_string(i);
            std::string translated = T(entry.nameKey);

            uint32_t color = (static_cast<int>(i) == m_cursor) ? COLOR_ROW_SELECTED : COLOR_ROW_NORMAL;
            double textX = m_popupX + PAD + CURSOR_W + CURSOR_PAD;
            double textW = POPUP_W - PAD * 2 - CURSOR_W - CURSOR_PAD;

            ScaleformUtil::CreateLabel(uiMovie.get(), name.c_str(), 30 + static_cast<int>(i),
                textX, rowY + textVPad, textW, ROW_H,
                translated.c_str(), 14, color);
        }

        // Separator after rows
        double sep2Y = m_popupY + yBase + static_cast<double>(m_context.actions.size()) * ROW_H + 4.0;
        ScaleformUtil::DrawFilledRect(uiMovie.get(), "ctx_sep2", 23,
            m_popupX + PAD, sep2Y, POPUP_W - PAD * 2, SEP_H, COLOR_SEP, 100);
    }

    void Menu::RedrawActionRows() {
        if (!uiMovie) return;

        for (size_t i = 0; i < m_context.actions.size(); ++i) {
            std::string path = "_root.ctx_row" + std::to_string(i);
            uint32_t color = (static_cast<int>(i) == m_cursor) ? COLOR_ROW_SELECTED : COLOR_ROW_NORMAL;
            ScaleformUtil::SetTextFieldFormat(uiMovie.get(), path, 14, color);
        }
    }

    static std::string BuildDescriptionText(const ContextResolver::ActionEntry& a_entry) {
        std::string text = T(a_entry.descKey);

        auto holdType = Menu::GetHoldType(a_entry.action);
        if (holdType == HoldType::kHoldReconfigure) {
            text += " " + T("$SLID_CtxHoldToConfigure");
        } else if (holdType == HoldType::kHoldConfirm) {
            text += " " + T("$SLID_CtxHoldToConfirm");
        }

        return text;
    }

    void Menu::DrawDescription() {
        if (m_context.actions.empty()) return;

        auto& entry = m_context.actions[m_cursor];
        std::string text = BuildDescriptionText(entry);

        double descY = m_popupY + m_rowsY +
                        static_cast<double>(m_context.actions.size()) * ROW_H +
                        4.0 + SEP_H + DESC_PAD;

        ScaleformUtil::CreateLabel(uiMovie.get(), "ctx_desc", 50,
            m_popupX + PAD, descY, POPUP_W - PAD * 2, DESC_H,
            text.c_str(), 12, COLOR_DESC);

        // Enable word wrap for long descriptions
        RE::GFxValue tf;
        uiMovie->GetVariable(&tf, "_root.ctx_desc");
        if (!tf.IsUndefined()) {
            RE::GFxValue trueVal;
            trueVal.SetBoolean(true);
            tf.SetMember("wordWrap", trueVal);
            tf.SetMember("multiline", trueVal);
        }
    }

    void Menu::RedrawDescription() {
        if (!uiMovie || m_context.actions.empty()) return;

        auto& entry = m_context.actions[m_cursor];
        std::string text = BuildDescriptionText(entry);

        RE::GFxValue field;
        uiMovie->GetVariable(&field, "_root.ctx_desc");
        if (field.IsDisplayObject()) {
            RE::GFxValue val;
            val.SetString(text.c_str());
            field.SetMember("text", val);
        }
    }

    void Menu::DrawCursorHighlight() {
        if (m_context.actions.empty()) return;

        double rowY = m_popupY + m_rowsY + static_cast<double>(m_cursor) * ROW_H;

        // Background highlight for selected row
        ScaleformUtil::DrawFilledRect(uiMovie.get(), "ctx_cursor_bg", 25,
            m_popupX + PAD - 2.0, rowY, POPUP_W - PAD * 2 + 4.0, ROW_H,
            COLOR_CURSOR_BG, ALPHA_CURSOR);

        // Cursor indicator bar
        ScaleformUtil::DrawFilledRect(uiMovie.get(), "ctx_cursor", 26,
            m_popupX + PAD, rowY + 4.0, CURSOR_W, ROW_H - 8.0,
            COLOR_CURSOR, 100);
    }

    void Menu::UpdateCursorHighlight() {
        // Redraw cursor elements at new position
        // (Can't reposition — DrawFilledRect draws at absolute coords inside a (0,0) MC)
        DrawCursorHighlight();
    }

    int Menu::HitTestRow(float a_mx, float a_my) const {
        double localY = a_my - m_popupY - m_rowsY;
        if (localY < 0) return -1;

        int row = static_cast<int>(localY / ROW_H);
        if (row >= 0 && row < static_cast<int>(m_context.actions.size())) {
            // Check X bounds
            if (a_mx >= m_popupX + PAD && a_mx <= m_popupX + POPUP_W - PAD) {
                return row;
            }
        }
        return -1;
    }

    // --- Hold mechanic ---

    HoldType Menu::GetHoldType(ContextResolver::Action a_action) {
        using Action = ContextResolver::Action;
        switch (a_action) {
            case Action::kWhoosh:       return HoldType::kHoldReconfigure;
            case Action::kRestock:      return HoldType::kHoldReconfigure;
            case Action::kDestroyLink:  return HoldType::kHoldConfirm;
            case Action::kRemove:       return HoldType::kHoldConfirm;
            default:                    return HoldType::kNone;
        }
    }

    HoldConfig Menu::GetHoldConfig(ContextResolver::Action a_action) {
        using Action = ContextResolver::Action;
        switch (a_action) {
            case Action::kWhoosh:
                return {COLOR_HOLD_BLUE, ALPHA_HOLD, HOLD_DEAD_ZONE, HOLD_DURATION};
            case Action::kRestock:
                return {COLOR_HOLD_GREEN, ALPHA_HOLD, HOLD_DEAD_ZONE, HOLD_DURATION};
            case Action::kDestroyLink:
                return {COLOR_HOLD_RED, ALPHA_HOLD, 0.0f, HOLD_DURATION};
            case Action::kRemove:
                return {COLOR_HOLD_YELLOW, ALPHA_HOLD, 0.0f, HOLD_DURATION};
            default:
                return {0, 0, 0.0f, 0.0f};
        }
    }

    void Menu::BeginHold() {
        if (!g_activeMenu || g_activeMenu->m_context.actions.empty()) return;
        auto& m = *g_activeMenu;

        auto action = m.m_context.actions[m.m_cursor].action;
        auto holdType = GetHoldType(action);

        if (holdType == HoldType::kNone) {
            // Instant action — fire immediately
            Confirm();
            return;
        }

        m.m_holding = true;
        m.m_holdRow = m.m_cursor;
        m.m_holdStart = std::chrono::steady_clock::now();
    }

    void Menu::UpdateHold() {
        if (!g_activeMenu || !g_activeMenu->m_holding) return;
        auto& m = *g_activeMenu;

        if (m.m_holdRow < 0 || m.m_holdRow >= static_cast<int>(m.m_context.actions.size())) {
            CancelHold();
            return;
        }

        auto action = m.m_context.actions[m.m_holdRow].action;
        auto holdType = GetHoldType(action);
        auto config = GetHoldConfig(action);

        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - m.m_holdStart).count();

        // Still in dead zone — no visual yet
        if (elapsed < config.deadZone) return;

        float ratio = std::clamp((elapsed - config.deadZone) / config.duration, 0.0f, 1.0f);
        m.DrawHoldProgress(ratio, config.color, config.alpha);

        if (ratio >= 1.0f) {
            m.m_holding = false;
            m.ClearHoldProgress();

            if (holdType == HoldType::kHoldConfirm) {
                // Fire the action (DestroyLink → BeginDismantleNetwork with its own MessageBox)
                Confirm();
            } else if (holdType == HoldType::kHoldReconfigure) {
                // Fire the reconfigure action instead of the normal action
                auto originalAction = m.m_context.actions[m.m_holdRow].action;
                auto configAction = (originalAction == ContextResolver::Action::kRestock)
                    ? ContextResolver::Action::kRestockConfigure
                    : ContextResolver::Action::kWhooshConfigure;
                auto networkName = m.m_context.networkName;
                auto callback = m.m_callback;
                Hide();
                if (callback) {
                    callback(configAction, networkName);
                }
            }
        }
    }

    void Menu::ReleaseHold() {
        if (!g_activeMenu || !g_activeMenu->m_holding) return;
        auto& m = *g_activeMenu;

        auto action = m.m_context.actions[m.m_holdRow].action;
        auto holdType = GetHoldType(action);
        auto config = GetHoldConfig(action);

        float elapsed = std::chrono::duration<float>(
            std::chrono::steady_clock::now() - m.m_holdStart).count();

        m.m_holding = false;
        m.ClearHoldProgress();

        if (holdType == HoldType::kHoldReconfigure) {
            // Whoosh: release in dead zone → execute tap action
            if (elapsed < config.deadZone) {
                Confirm();
            }
            // Release after dead zone but before complete → cancel (no action)
        }
        // kHoldConfirm: release before fill → cancel (no action)
    }

    void Menu::CancelHold() {
        if (!g_activeMenu) return;
        auto& m = *g_activeMenu;
        if (m.m_holding) {
            m.m_holding = false;
            m.ClearHoldProgress();
        }
    }

    void Menu::DrawHoldProgress(float a_ratio, uint32_t a_color, int a_alpha) {
        if (!uiMovie) return;

        // Create/reuse a root-level clip (depth 27: above cursor_bg=25 and cursor=26, below text=30+)
        RE::GFxValue fillClip;
        uiMovie->GetVariable(&fillClip, "_root.ctx_holdFill");
        if (fillClip.IsUndefined()) {
            RE::GFxValue root;
            uiMovie->GetVariable(&root, "_root");
            RE::GFxValue args[2];
            args[0].SetString("ctx_holdFill");
            args[1].SetNumber(27.0);
            root.Invoke("createEmptyMovieClip", &fillClip, args, 2);
        }
        if (fillClip.IsUndefined()) return;

        fillClip.Invoke("clear", nullptr, nullptr, 0);

        double fullW = POPUP_W - PAD * 2 + 4.0;
        double fillW = fullW * static_cast<double>(a_ratio);
        if (fillW < 1.0) return;

        // Draw at absolute position of the held row
        double x = m_popupX + PAD - 2.0;
        double rowY = m_popupY + m_rowsY + static_cast<double>(m_holdRow) * ROW_H;

        RE::GFxValue fillArgs[2];
        fillArgs[0].SetNumber(static_cast<double>(a_color));
        fillArgs[1].SetNumber(static_cast<double>(a_alpha));
        fillClip.Invoke("beginFill", nullptr, fillArgs, 2);

        RE::GFxValue pt[2];
        pt[0].SetNumber(x); pt[1].SetNumber(rowY);
        fillClip.Invoke("moveTo", nullptr, pt, 2);
        pt[0].SetNumber(x + fillW);
        fillClip.Invoke("lineTo", nullptr, pt, 2);
        pt[1].SetNumber(rowY + ROW_H);
        fillClip.Invoke("lineTo", nullptr, pt, 2);
        pt[0].SetNumber(x);
        fillClip.Invoke("lineTo", nullptr, pt, 2);
        pt[1].SetNumber(rowY);
        fillClip.Invoke("lineTo", nullptr, pt, 2);

        fillClip.Invoke("endFill", nullptr, nullptr, 0);
    }

    void Menu::ClearHoldProgress() {
        if (!uiMovie) return;

        RE::GFxValue fillClip;
        uiMovie->GetVariable(&fillClip, "_root.ctx_holdFill");
        if (!fillClip.IsUndefined()) {
            fillClip.Invoke("clear", nullptr, nullptr, 0);
        }
    }

    // --- Input actions ---

    void Menu::CursorUp() {
        if (!g_activeMenu || g_activeMenu->m_context.actions.empty()) return;
        auto& m = *g_activeMenu;
        CancelHold();
        if (m.m_cursor > 0) {
            m.m_cursor--;
            m.RedrawActionRows();
            m.UpdateCursorHighlight();
            m.RedrawDescription();
        }
    }

    void Menu::CursorDown() {
        if (!g_activeMenu || g_activeMenu->m_context.actions.empty()) return;
        auto& m = *g_activeMenu;
        CancelHold();
        int maxIdx = static_cast<int>(m.m_context.actions.size()) - 1;
        if (m.m_cursor < maxIdx) {
            m.m_cursor++;
            m.RedrawActionRows();
            m.UpdateCursorHighlight();
            m.RedrawDescription();
        }
    }

    void Menu::CycleLeft() {
        if (!g_activeMenu) return;
        auto& m = *g_activeMenu;
        CancelHold();
        if (m.m_context.cyclableNetworks.size() <= 1) return;

        // Find current index
        auto& nets = m.m_context.cyclableNetworks;
        auto it = std::find(nets.begin(), nets.end(), m.m_context.networkName);
        size_t idx = (it != nets.end()) ? std::distance(nets.begin(), it) : 0;

        // Cycle backwards (wrap)
        idx = (idx == 0) ? nets.size() - 1 : idx - 1;
        m.m_context.networkName = nets[idx];
        m.RedrawNetworkSubtitle();
    }

    void Menu::CycleRight() {
        if (!g_activeMenu) return;
        auto& m = *g_activeMenu;
        CancelHold();
        if (m.m_context.cyclableNetworks.size() <= 1) return;

        auto& nets = m.m_context.cyclableNetworks;
        auto it = std::find(nets.begin(), nets.end(), m.m_context.networkName);
        size_t idx = (it != nets.end()) ? std::distance(nets.begin(), it) : 0;

        idx = (idx + 1) % nets.size();
        m.m_context.networkName = nets[idx];
        m.RedrawNetworkSubtitle();
    }

    void Menu::Confirm() {
        if (!g_activeMenu || g_activeMenu->m_context.actions.empty()) return;
        auto& m = *g_activeMenu;

        auto action = m.m_context.actions[m.m_cursor].action;
        auto networkName = m.m_context.networkName;
        auto callback = m.m_callback;

        Hide();

        if (callback) {
            callback(action, networkName);
        }
    }

    void Menu::Cancel() {
        Hide();
    }

    void Menu::OnMouseMove() {
        if (!g_activeMenu || !g_activeMenu->uiMovie) return;
        auto& m = *g_activeMenu;

        RE::GFxValue xVal, yVal;
        m.uiMovie->GetVariable(&xVal, "_root._xmouse");
        m.uiMovie->GetVariable(&yVal, "_root._ymouse");
        float mx = static_cast<float>(xVal.GetNumber());
        float my = static_cast<float>(yVal.GetNumber());

        int hit = m.HitTestRow(mx, my);
        if (hit >= 0 && hit != m.m_cursor) {
            CancelHold();
            m.m_cursor = hit;
            m.RedrawActionRows();
            m.UpdateCursorHighlight();
            m.RedrawDescription();
        }
    }

    void Menu::OnMouseClick() {
        // Legacy — kept for any external callers
        OnMouseDown();
    }

    void Menu::OnMouseDown() {
        if (!g_activeMenu || !g_activeMenu->uiMovie) return;
        auto& m = *g_activeMenu;

        RE::GFxValue xVal, yVal;
        m.uiMovie->GetVariable(&xVal, "_root._xmouse");
        m.uiMovie->GetVariable(&yVal, "_root._ymouse");
        float mx = static_cast<float>(xVal.GetNumber());
        float my = static_cast<float>(yVal.GetNumber());

        int hit = m.HitTestRow(mx, my);
        if (hit >= 0) {
            m.m_cursor = hit;
            m.RedrawActionRows();
            m.UpdateCursorHighlight();
            m.RedrawDescription();
            BeginHold();  // BeginHold fires Confirm immediately for kNone actions
            return;
        }

        // Check chevron clicks for network cycling
        if (m.m_context.cyclableNetworks.size() > 1) {
            double netY = m.m_popupY + PAD + TITLE_H;
            if (my >= netY && my <= netY + NETWORK_H) {
                double midX = m.m_popupX + POPUP_W / 2.0;
                if (mx < midX) {
                    CycleLeft();
                } else {
                    CycleRight();
                }
                return;
            }
        }

        // Click outside popup = cancel
        double popupH = m.PopupH();
        if (mx < m.m_popupX || mx > m.m_popupX + POPUP_W ||
            my < m.m_popupY || my > m.m_popupY + popupH) {
            Cancel();
        }
    }

    void Menu::OnMouseUp() {
        if (!g_activeMenu) return;
        ReleaseHold();
    }

    // =========================================================================
    // InputHandler
    // =========================================================================

    InputHandler* InputHandler::GetSingleton() {
        static InputHandler singleton;
        return &singleton;
    }

    void InputHandler::Register() {
        auto* inputMgr = RE::BSInputDeviceManager::GetSingleton();
        if (inputMgr) {
            inputMgr->AddEventSink(GetSingleton());
            logger::info("ContextMenu::InputHandler registered");
        }
    }

    RE::BSEventNotifyControl InputHandler::ProcessEvent(
        RE::InputEvent* const* a_event,
        RE::BSTEventSource<RE::InputEvent*>*)
    {
        if (!a_event || !Menu::IsOpen() || !g_activeMenu) {
            return RE::BSEventNotifyControl::kContinue;
        }

        for (auto* event = *a_event; event; event = event->next) {
            // --- Thumbstick ---
            if (event->eventType == RE::INPUT_EVENT_TYPE::kThumbstick) {
                auto* thumb = static_cast<RE::ThumbstickEvent*>(event);
                if (thumb->IsLeft()) {
                    auto edges = DirectionalInput::ProcessThumbstick(
                        thumb->xValue, thumb->yValue, m_thumbState);

                    if (edges.up)    Menu::CursorUp();
                    if (edges.down)  Menu::CursorDown();
                    if (edges.left)  Menu::CycleLeft();
                    if (edges.right) Menu::CycleRight();
                }
                if (thumb->IsRight()) {
                    // Right thumbstick moves the cursor — update hover
                    Menu::OnMouseMove();
                }
                continue;
            }

            // --- Buttons ---
            if (auto* button = event->AsButtonEvent()) {
                auto device = button->GetDevice();
                auto key = button->GetIDCode();

                // Gamepad
                if (device == RE::INPUT_DEVICE::kGamepad) {
                    if (key == ScaleformUtil::GAMEPAD_A) {
                        if (button->IsDown()) Menu::BeginHold();
                        else if (button->IsUp()) Menu::ReleaseHold();
                    } else if (key == ScaleformUtil::GAMEPAD_B) {
                        if (button->IsDown()) {
                            if (g_activeMenu && g_activeMenu->m_holding) {
                                Menu::CancelHold();
                            } else {
                                Menu::Cancel();
                            }
                        }
                    } else if (button->IsDown()) {
                        if (key == ScaleformUtil::GAMEPAD_DPAD_UP) Menu::CursorUp();
                        else if (key == ScaleformUtil::GAMEPAD_DPAD_DOWN) Menu::CursorDown();
                        else if (key == ScaleformUtil::GAMEPAD_DPAD_LEFT) Menu::CycleLeft();
                        else if (key == ScaleformUtil::GAMEPAD_DPAD_RIGHT) Menu::CycleRight();
                    }
                }

                // Keyboard
                if (device == RE::INPUT_DEVICE::kKeyboard) {
                    using Key = RE::BSKeyboardDevice::Key;
                    if (key == Key::kEnter) {
                        if (button->IsDown()) Menu::BeginHold();
                        else if (button->IsUp()) Menu::ReleaseHold();
                    } else if (key == Key::kEscape) {
                        if (button->IsDown()) {
                            if (g_activeMenu && g_activeMenu->m_holding) {
                                Menu::CancelHold();
                            } else {
                                Menu::Cancel();
                            }
                        }
                    } else if (button->IsDown()) {
                        if (key == Key::kUp || key == Key::kW) Menu::CursorUp();
                        else if (key == Key::kDown || key == Key::kS) Menu::CursorDown();
                        else if (key == Key::kLeft || key == Key::kA) Menu::CycleLeft();
                        else if (key == Key::kRight || key == Key::kD) Menu::CycleRight();
                    }
                }

                // Mouse
                if (device == RE::INPUT_DEVICE::kMouse) {
                    if (key == 0) {  // Left click
                        if (button->IsDown()) Menu::OnMouseDown();
                        else if (button->IsUp()) Menu::OnMouseUp();
                    }
                }

                // D-pad repeat for held buttons
                if (device == RE::INPUT_DEVICE::kGamepad) {
                    bool isDown = button->IsDown();
                    bool isPressed = button->IsPressed();
                    bool isUp = button->IsUp();

                    if (key == ScaleformUtil::GAMEPAD_DPAD_UP) {
                        if (DirectionalInput::ProcessButtonRepeat(-1, isDown, isPressed, isUp, m_repeatV)) {
                            if (!isDown) Menu::CursorUp();  // first press already handled above
                        }
                    } else if (key == ScaleformUtil::GAMEPAD_DPAD_DOWN) {
                        if (DirectionalInput::ProcessButtonRepeat(1, isDown, isPressed, isUp, m_repeatV)) {
                            if (!isDown) Menu::CursorDown();
                        }
                    }
                }
            }

            // --- Mouse move ---
            if (event->eventType == RE::INPUT_EVENT_TYPE::kMouseMove) {
                Menu::OnMouseMove();
            }
        }

        return RE::BSEventNotifyControl::kStop;  // Consume all input while open
    }

}
