#include "SellOverviewMenu.h"
#include "NetworkManager.h"
#include "SalesProcessor.h"
#include "VendorRegistry.h"
#include "ScaleformUtil.h"
#include "Settings.h"
#include "TranslationService.h"

#include <algorithm>

namespace SellOverview {

    static Menu* g_activeMenu = nullptr;

    // Format game time as "HH:MM (today)" / "HH:MM (1d)" / "HH:MM (2d)" etc.
    static std::string FormatRelativeTime(float a_gameTime) {
        float nowHours = 0.0f;
        if (auto* cal = RE::Calendar::GetSingleton()) {
            nowHours = cal->GetHoursPassed();
        }

        int txDay = static_cast<int>(a_gameTime / 24.0f);
        int nowDay = static_cast<int>(nowHours / 24.0f);
        int dayDiff = nowDay - txDay;

        int hour = static_cast<int>(a_gameTime) % 24;
        int minute = static_cast<int>((a_gameTime - std::floor(a_gameTime)) * 60.0f) % 60;

        char buf[32];
        if (dayDiff <= 0) {
            std::snprintf(buf, sizeof(buf), "%d:%02d (today)", hour, minute);
        } else {
            std::snprintf(buf, sizeof(buf), "%d:%02d (%dd)", hour, minute, dayDiff);
        }
        return buf;
    }

    // Set text alignment on a label created by CreateLabel
    static void SetLabelAlign(RE::GFxMovieView* a_movie, const char* a_name, const char* a_align) {
        RE::GFxValue tf;
        a_movie->CreateObject(&tf, "TextFormat");
        RE::GFxValue alignVal;
        alignVal.SetString(a_align);
        tf.SetMember("align", alignVal);
        RE::GFxValue field;
        std::string path = std::string("_root.") + a_name;
        a_movie->GetVariable(&field, path.c_str());
        if (field.IsDisplayObject()) {
            RE::GFxValue args[] = { tf };
            field.Invoke("setTextFormat", nullptr, args, 1);
        }
    }

    static std::string FormatCountdown(float a_remainingHours) {
        if (a_remainingHours <= 0.0f) {
            return T("$SLID_OnNextRest");
        }
        int hours = static_cast<int>(a_remainingHours);
        int mins = static_cast<int>((a_remainingHours - hours) * 60.0f);
        return std::to_string(hours) + "h " + std::to_string(mins) + "m";
    }

    // Describe what a vendor buys based on their faction's buy list
    static std::string DescribeVendorBuyList(RE::FormID a_factionFormID) {
        if (a_factionFormID == 0) return T("$SLID_AllItems");

        auto* faction = RE::TESForm::LookupByID<RE::TESFaction>(a_factionFormID);
        if (!faction) return T("$SLID_Unknown");

        auto* buyList = faction->vendorData.vendorSellBuyList;
        bool inverted = faction->vendorData.vendorValues.notBuySell;

        if (!buyList) {
            return inverted ? T("$SLID_AllItems") : T("$SLID_Nothing");
        }

        std::vector<std::string> keywords;
        buyList->ForEachForm([&](RE::TESForm& form) {
            auto* kw = form.As<RE::BGSKeyword>();
            if (kw) {
                std::string name = kw->GetFormEditorID();
                // Strip "VendorItem" prefix for readability
                if (name.size() > 10 && name.substr(0, 10) == "VendorItem") {
                    name = name.substr(10);
                }
                keywords.push_back(name);
            }
            return RE::BSContainer::ForEachResult::kContinue;
        });

        if (keywords.empty()) {
            return inverted ? T("$SLID_AllItems") : T("$SLID_Nothing");
        }

        std::string result;
        for (size_t i = 0; i < keywords.size(); i++) {
            if (i > 0) result += ", ";
            result += keywords[i];
        }

        if (inverted) {
            result = "Everything except " + result;
        }

        return result;
    }

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
        menuFlags.set(RE::UI_MENU_FLAGS::kUsesCursor);
        menuFlags.set(RE::UI_MENU_FLAGS::kRequiresUpdate);

        inputContext = Context::kMenuMode;

        auto scaleform = RE::BSScaleformManager::GetSingleton();
        if (scaleform) {
            bool loaded = scaleform->LoadMovie(this, uiMovie, FILE_NAME.data());
            if (loaded && uiMovie) {
                logger::info("SellOverviewMenu: loaded SWF {}", FILE_NAME);
            } else {
                logger::error("SellOverviewMenu: failed to load SWF {}", FILE_NAME);
            }
        }
    }

    void Menu::PostCreate() {
        if (!uiMovie) return;
        g_activeMenu = this;
        m_logScrollOffset = 0;
        m_selectedRow = 0;
        m_hoverButton = -1;
        m_usingCursor = false;
        m_focusZone = FocusZone::kTransactionLog;
        m_vendorCursorIdx = -1;
        m_highlightVendorName.clear();
        m_vendorFlashFrames = 0;
        m_lastLogSize = NetworkManager::GetSingleton()->GetTransactionLog().size();
        m_menuOpenTime = std::chrono::steady_clock::now();
        if (auto* cal = RE::Calendar::GetSingleton()) {
            m_gameHoursAtOpen = cal->GetHoursPassed();
            m_timeScale = cal->GetTimescale();
        }
        BuildRuns();
        DrawPopup();
        logger::info("SellOverviewMenu: ready");
    }

    RE::UI_MESSAGE_RESULTS Menu::ProcessMessage(RE::UIMessage& a_message) {
        using Message = RE::UI_MESSAGE_TYPE;
        switch (*a_message.type) {
            case Message::kHide:
                g_activeMenu = nullptr;
                break;
            case Message::kUpdate:
                if (m_hasVendorSchedule) {
                    UpdateVendorTimers();
                }
                if (m_vendorFlashFrames > 0) {
                    m_vendorFlashFrames--;
                    // Redraw every frame for smooth white→gold fade
                    UpdateLogRows();
                }

                // Live sale processing is handled in UpdateVendorTimers
                // when a timer first crosses zero
                break;
            default:
                break;
        }
        return RE::IMenu::ProcessMessage(a_message);
    }

    void Menu::Show() {
        auto ui = RE::UI::GetSingleton();
        if (!ui || ui->IsMenuOpen(MENU_NAME)) return;
        auto* queue = RE::UIMessageQueue::GetSingleton();
        if (queue) {
            queue->AddMessage(MENU_NAME, RE::UI_MESSAGE_TYPE::kShow, nullptr);
        }
    }

    void Menu::Hide() {
        auto ui = RE::UI::GetSingleton();
        if (ui && ui->IsMenuOpen(MENU_NAME)) {
            auto* queue = RE::UIMessageQueue::GetSingleton();
            if (queue) {
                queue->AddMessage(MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
            }
        }
    }

    bool Menu::IsOpen() {
        auto ui = RE::UI::GetSingleton();
        return ui && ui->IsMenuOpen(MENU_NAME);
    }

    // --- Vendor entry building ---

    void Menu::BuildVendorEntries() {
        m_vendorEntries.clear();

        float currentHours = 0.0f;
        if (auto* calendar = RE::Calendar::GetSingleton()) {
            currentHours = calendar->GetHoursPassed();
        }

        auto* mgr = NetworkManager::GetSingleton();
        const auto& sellState = mgr->GetSellState();

        // General vendor (if sell container exists and timer started or any sales)
        if (sellState.formID != 0 && (sellState.timerStarted || sellState.totalItemsSold > 0)) {
            float remaining = Settings::fSellIntervalHours - (currentHours - sellState.lastSellTime);
            m_vendorEntries.push_back({T("$SLID_GeneralVendor"), T("$SLID_DefaultVendor"), remaining, 0, true, false});
        }

        // Registered vendors
        auto* vendorReg = VendorRegistry::GetSingleton();
        const auto& vendors = vendorReg->GetVendors();
        for (const auto& v : vendors) {
            if (!v.active) continue;
            float remaining = Settings::fVendorIntervalHours - (currentHours - v.lastVisitTime);
            m_vendorEntries.push_back({v.vendorName, v.storeName, remaining, v.factionFormID, false, v.invested});
        }

        // Sort by remaining ascending (elapsed timers first)
        std::sort(m_vendorEntries.begin(), m_vendorEntries.end(), [](const auto& a, const auto& b) {
            return a.remainingHours < b.remainingHours;
        });

        // Cap to MAX_VENDOR_LINES
        if (m_vendorEntries.size() > static_cast<size_t>(MAX_VENDOR_LINES)) {
            m_vendorEntries.resize(MAX_VENDOR_LINES);
        }
    }

    // --- Run building ---

    void Menu::BuildRuns() {
        m_runs.clear();
        auto* mgr = NetworkManager::GetSingleton();
        const auto& log = mgr->GetTransactionLog();

        for (size_t i = 0; i < log.size(); ) {
            TransactionRun run;
            run.vendorName = log[i].vendorName;
            run.vendorAssortment = log[i].vendorAssortment;
            run.gameTime = log[i].gameTime;
            run.totalItems = 0;
            run.totalGold = 0;

            while (i < log.size() && log[i].gameTime == run.gameTime &&
                   log[i].vendorName == run.vendorName) {
                run.items.push_back(&log[i]);
                run.totalItems += log[i].quantity;
                run.totalGold += log[i].goldEarned;
                i++;
            }
            m_runs.push_back(std::move(run));
        }

        BuildVisibleRows();
    }

    void Menu::BuildVisibleRows() {
        m_visibleRows.clear();
        for (int r = 0; r < static_cast<int>(m_runs.size()); r++) {
            VisibleRow header;
            header.type = RowType::kRunHeader;
            header.runIndex = r;
            header.itemIndex = -1;
            m_visibleRows.push_back(header);

            if (m_runs[r].expanded) {
                for (int it = 0; it < static_cast<int>(m_runs[r].items.size()); it++) {
                    VisibleRow detail;
                    detail.type = RowType::kDetailItem;
                    detail.runIndex = r;
                    detail.itemIndex = it;
                    m_visibleRows.push_back(detail);
                }
            }
        }
    }

    int Menu::TotalFlattenedRows() const {
        return static_cast<int>(m_visibleRows.size());
    }

    void Menu::EnsureCursorVisible() {
        if (m_selectedRow < m_logScrollOffset) {
            m_logScrollOffset = m_selectedRow;
            return;
        }

        // Check if selected row fits within visible pixel area
        double availH = BTN_ZONE_TOP - (STATS_Y + STATS_H + 4.0 + m_vendorSchedH + LOG_HEADING_H + 6.0 + LOG_HEADER_H);
        double usedH = 0.0;
        for (int r = m_logScrollOffset; r <= m_selectedRow && r < static_cast<int>(m_visibleRows.size()); r++) {
            double rh = (m_visibleRows[r].type == RowType::kRunHeader) ? LOG_ROW_H : DETAIL_ROW_H;
            usedH += rh;
        }
        while (usedH > availH && m_logScrollOffset < m_selectedRow) {
            double rh = (m_visibleRows[m_logScrollOffset].type == RowType::kRunHeader) ? LOG_ROW_H : DETAIL_ROW_H;
            usedH -= rh;
            m_logScrollOffset++;
        }
    }

    int Menu::HitTestLogRow(float a_mx, float a_my) const {
        double logX = m_popupX + 20.0;
        double logRight = logX + POPUP_W - 40.0;
        double maxY = m_popupY + BTN_ZONE_TOP;

        if (a_mx < logX || a_mx > logRight) return -1;

        double curY = m_logAreaY;
        for (int flatIdx = m_logScrollOffset; flatIdx < static_cast<int>(m_visibleRows.size()); flatIdx++) {
            double rh = (m_visibleRows[flatIdx].type == RowType::kRunHeader) ? LOG_ROW_H : DETAIL_ROW_H;
            if (curY + rh > maxY) break;

            if (a_my >= curY && a_my < curY + rh) {
                // Only return run headers for click-to-expand
                if (m_visibleRows[flatIdx].type == RowType::kRunHeader) {
                    return flatIdx;
                }
                return -1;
            }
            curY += rh;
        }
        return -1;
    }

    int Menu::HitTestVendorRow(float a_mx, float a_my) const {
        if (!m_hasVendorSchedule || m_vendorScheduleCount <= 0) return -1;

        double logX = m_popupX + 20.0;
        double logRight = logX + POPUP_W - 40.0;

        if (a_mx < logX || a_mx > logRight) return -1;

        for (int i = 0; i < m_vendorScheduleCount; ++i) {
            double rowY = m_vendorRowsY + i * VENDOR_LINE_H;
            if (a_my >= rowY && a_my < rowY + VENDOR_LINE_H) {
                return i;
            }
        }
        return -1;
    }

    // --- Input statics ---

    void Menu::ScrollUp() {
        if (!g_activeMenu) return;
        if (g_activeMenu->m_logScrollOffset > 0) {
            g_activeMenu->m_logScrollOffset--;
            g_activeMenu->UpdateLogRows();
            g_activeMenu->UpdateScrollbar();
        }
    }

    void Menu::ScrollDown() {
        if (!g_activeMenu) return;
        int total = g_activeMenu->TotalFlattenedRows();
        if (g_activeMenu->m_logScrollOffset < total - 1) {
            g_activeMenu->m_logScrollOffset++;
            g_activeMenu->UpdateLogRows();
            g_activeMenu->UpdateScrollbar();
        }
    }

    void Menu::CursorUp() {
        if (!g_activeMenu) return;
        auto& menu = *g_activeMenu;
        bool wasUsingCursor = menu.m_usingCursor;
        menu.m_usingCursor = true;

        if (menu.m_focusZone == FocusZone::kVendorSchedule) {
            // Move up within vendor schedule
            if (menu.m_vendorCursorIdx > 0) {
                menu.m_vendorCursorIdx--;
                menu.m_highlightVendorName = menu.m_vendorEntries[menu.m_vendorCursorIdx].name;
                menu.m_vendorFlashFrames = 12;
                menu.RedrawVendorSchedule();
                menu.DrawVendorInfo();
                menu.UpdateLogRows();
            }
            return;
        }

        // In transaction log zone
        // Find previous run header
        for (int r = menu.m_selectedRow - 1; r >= 0; r--) {
            if (menu.m_visibleRows[r].type == RowType::kRunHeader) {
                menu.m_selectedRow = r;
                menu.EnsureCursorVisible();
                menu.UpdateLogRows();
                menu.UpdateScrollbar();
                return;
            }
        }

        // At top of log — transition to vendor schedule if available
        if (menu.m_hasVendorSchedule && menu.m_vendorScheduleCount > 0) {
            menu.m_focusZone = FocusZone::kVendorSchedule;
            menu.m_vendorCursorIdx = menu.m_vendorScheduleCount - 1;
            menu.m_highlightVendorName = menu.m_vendorEntries[menu.m_vendorCursorIdx].name;
            menu.m_vendorFlashFrames = 12;
            menu.RedrawVendorSchedule();
            menu.DrawVendorInfo();
            menu.UpdateLogRows();
            return;
        }

        // First activation — redraw to show cursor even if we didn't move
        if (!wasUsingCursor) {
            menu.UpdateLogRows();
        }
    }

    void Menu::CursorDown() {
        if (!g_activeMenu) return;
        auto& menu = *g_activeMenu;
        bool wasUsingCursor = menu.m_usingCursor;
        menu.m_usingCursor = true;

        if (menu.m_focusZone == FocusZone::kVendorSchedule) {
            // Move down within vendor schedule
            if (menu.m_vendorCursorIdx < menu.m_vendorScheduleCount - 1) {
                menu.m_vendorCursorIdx++;
                menu.m_highlightVendorName = menu.m_vendorEntries[menu.m_vendorCursorIdx].name;
                menu.m_vendorFlashFrames = 12;
                menu.RedrawVendorSchedule();
                menu.DrawVendorInfo();
                menu.UpdateLogRows();
            } else {
                // Transition to transaction log
                menu.m_focusZone = FocusZone::kTransactionLog;
                menu.m_vendorCursorIdx = -1;
                menu.m_highlightVendorName.clear();
                menu.m_vendorFlashFrames = 0;
                menu.RedrawVendorSchedule();
                menu.DrawVendorInfo();
                // Select first run header in log
                menu.m_selectedRow = 0;
                for (int r = 0; r < menu.TotalFlattenedRows(); r++) {
                    if (menu.m_visibleRows[r].type == RowType::kRunHeader) {
                        menu.m_selectedRow = r;
                        break;
                    }
                }
                menu.EnsureCursorVisible();
                menu.UpdateLogRows();
                menu.UpdateScrollbar();
            }
            return;
        }

        // In transaction log zone
        int total = menu.TotalFlattenedRows();

        // Find next run header
        for (int r = menu.m_selectedRow + 1; r < total; r++) {
            if (menu.m_visibleRows[r].type == RowType::kRunHeader) {
                menu.m_selectedRow = r;
                menu.EnsureCursorVisible();
                menu.UpdateLogRows();
                menu.UpdateScrollbar();
                return;
            }
        }

        // First activation — redraw to show cursor even if we didn't move
        if (!wasUsingCursor) {
            menu.UpdateLogRows();
        }
    }

    void Menu::ActivateRow() {
        if (!g_activeMenu) return;
        auto& menu = *g_activeMenu;

        // No-op in vendor schedule zone
        if (menu.m_focusZone == FocusZone::kVendorSchedule) return;

        int idx = menu.m_selectedRow;
        if (idx < 0 || idx >= static_cast<int>(menu.m_visibleRows.size())) return;

        const auto& row = menu.m_visibleRows[idx];
        if (row.type == RowType::kRunHeader) {
            menu.m_runs[row.runIndex].expanded = !menu.m_runs[row.runIndex].expanded;
            menu.BuildVisibleRows();

            // Clamp cursor and scroll
            int total = menu.TotalFlattenedRows();
            if (menu.m_selectedRow >= total) {
                menu.m_selectedRow = std::max(0, total - 1);
            }
            if (menu.m_logScrollOffset >= total) {
                menu.m_logScrollOffset = std::max(0, total - 1);
            }

            menu.UpdateLogRows();
            menu.UpdateScrollbar();
        }
    }

    void Menu::Close() {
        Hide();
    }

    // --- Drawing ---

    void Menu::DrawPopup() {
        m_popupX = (1280.0 - POPUP_W) / 2.0;
        m_popupY = (720.0 - POPUP_H) / 2.0;

        // Dim overlay
        ScaleformUtil::DrawFilledRect(uiMovie.get(), "_dimOverlay", 0, 0.0, 0.0, 1280.0, 720.0, 0x000000, ALPHA_DIM);

        // Background
        ScaleformUtil::DrawFilledRect(uiMovie.get(), "_popupBg", 1, m_popupX, m_popupY, POPUP_W, POPUP_H, COLOR_BG, ALPHA_BG);
        ScaleformUtil::DrawBorderRect(uiMovie.get(), "_popupBorder", 2, m_popupX, m_popupY, POPUP_W, POPUP_H, COLOR_BORDER);

        // Title
        std::string title = T("$SLID_SellOverviewTitle");
        ScaleformUtil::CreateLabel(uiMovie.get(), "_title", 10, m_popupX + 20.0, m_popupY + 14.0, 400.0, 30.0,
                    title.c_str(), 22, COLOR_TITLE);

        DrawStats();
        DrawVendorSchedule();
        DrawVendorInfo();
        DrawLogHeading();
        DrawLogHeader();
        DrawLogRows();
        DrawScrollbar();
        DrawCloseButton();
    }

    void Menu::DrawStats() {
        auto* mgr = NetworkManager::GetSingleton();
        const auto& state = mgr->GetSellState();

        double statsX = m_popupX + 20.0;
        double statsY = m_popupY + STATS_Y;

        // Row 1: Total Items Sold | Total Gold Earned
        std::string totalItemsSoldLabel = T("$SLID_TotalItemsSold");
        ScaleformUtil::CreateLabel(uiMovie.get(), "_statLabel1", 20, statsX, statsY, 150.0, 20.0,
                    totalItemsSoldLabel.c_str(), 13, COLOR_STAT_LABEL);
        std::string itemsSold = std::to_string(state.totalItemsSold);
        ScaleformUtil::CreateLabel(uiMovie.get(), "_statVal1", 21, statsX + 150.0, statsY, 100.0, 20.0,
                    itemsSold.c_str(), 13, COLOR_STAT_VALUE);

        std::string totalGoldLabel = T("$SLID_TotalGoldEarned");
        ScaleformUtil::CreateLabel(uiMovie.get(), "_statLabel2", 22, statsX + 280.0, statsY, 150.0, 20.0,
                    totalGoldLabel.c_str(), 13, COLOR_STAT_LABEL);
        std::string goldEarned = std::to_string(state.totalGoldEarned);
        ScaleformUtil::CreateLabel(uiMovie.get(), "_statVal2", 23, statsX + 430.0, statsY, 100.0, 20.0,
                    goldEarned.c_str(), 13, COLOR_STAT_VALUE);

        // Row 2: Items Pending | Wholesale Contracts
        double row2Y = statsY + 24.0;

        // Count items in sell container
        int pendingItems = 0;
        if (state.formID != 0) {
            auto* sellRef = RE::TESForm::LookupByID<RE::TESObjectREFR>(state.formID);
            if (sellRef) {
                auto inv = sellRef->GetInventory();
                for (auto& [item, data] : inv) {
                    if (!item || data.first <= 0 || IsPhantomItem(item)) continue;
                    if (!item->IsGold()) {
                        pendingItems += data.first;
                    }
                }
            }
        }

        std::string itemsPendingLabel = T("$SLID_ItemsPending");
        ScaleformUtil::CreateLabel(uiMovie.get(), "_statLabel3", 24, statsX, row2Y, 150.0, 20.0,
                    itemsPendingLabel.c_str(), 13, COLOR_STAT_LABEL);
        std::string pendingStr = std::to_string(pendingItems);
        ScaleformUtil::CreateLabel(uiMovie.get(), "_statVal3", 25, statsX + 150.0, row2Y, 100.0, 20.0,
                    pendingStr.c_str(), 13, COLOR_STAT_VALUE);

        std::string wholesaleLabel = T("$SLID_WholesaleContracts");
        ScaleformUtil::CreateLabel(uiMovie.get(), "_statLabel4", 26, statsX + 280.0, row2Y, 180.0, 20.0,
                    wholesaleLabel.c_str(), 13, COLOR_STAT_LABEL);

        auto* vendorReg = VendorRegistry::GetSingleton();
        std::string contractCount = std::to_string(vendorReg->GetActiveCount());
        ScaleformUtil::CreateLabel(uiMovie.get(), "_statVal4", 27, statsX + 430.0, row2Y, 100.0, 20.0,
                    contractCount.c_str(), 13, COLOR_STAT_VALUE);
    }

    void Menu::DrawVendorSchedule() {
        BuildVendorEntries();
        m_hasVendorSchedule = !m_vendorEntries.empty();
        m_vendorScheduleCount = static_cast<int>(m_vendorEntries.size());

        if (!m_hasVendorSchedule) {
            m_vendorSchedH = 0.0;
            for (int i = 0; i < MAX_VENDOR_LINES; ++i) {
                m_cachedTimerTexts[i].clear();
            }
            return;
        }

        // Compute dynamic section height
        m_vendorSchedH = 24.0 + m_vendorScheduleCount * VENDOR_LINE_H + VENDOR_INFO_H + 6.0;

        double baseX = m_popupX + 20.0;
        double baseY = m_popupY + VENDOR_SCHED_Y;
        double contentW = POPUP_W - 40.0;

        // "Upcoming Visits" heading
        std::string upcomingHeading = T("$SLID_UpcomingVisits");
        ScaleformUtil::CreateLabel(uiMovie.get(), "_vendSchedHeading", 40,
                    baseX, baseY, 300.0, 20.0, upcomingHeading.c_str(), 13, COLOR_HEADING);

        // Separator line
        double lineY = baseY + 20.0;
        ScaleformUtil::DrawLine(uiMovie.get(), "_vendSchedSep", 41,
                    baseX, lineY, baseX + contentW, lineY, COLOR_BORDER);

        m_vendorRowsY = lineY + 4.0;  // store for hit testing
        double rowY = m_vendorRowsY;

        for (int i = 0; i < m_vendorScheduleCount; ++i) {
            const auto& entry = m_vendorEntries[i];
            std::string cursorKey = "_vendCursor" + std::to_string(i);
            std::string nameKey = "_vendName" + std::to_string(i);
            std::string storeKey = "_vendStore" + std::to_string(i);
            std::string timerKey = "_vendTimer" + std::to_string(i);

            // Cursor highlight
            bool isSelected = (m_focusZone == FocusZone::kVendorSchedule && m_vendorCursorIdx == i);
            if (isSelected) {
                ScaleformUtil::DrawFilledRect(uiMovie.get(), cursorKey.c_str(), 42 + i,
                            baseX - 4.0, rowY, contentW + 8.0, VENDOR_LINE_H, COLOR_CURSOR_BG, ALPHA_CURSOR);
            } else {
                ScaleformUtil::DrawFilledRect(uiMovie.get(), cursorKey.c_str(), 42 + i,
                            0.0, 0.0, 1.0, 1.0, 0x000000, 0);
            }

            // Vendor name (left) — gold if selected
            uint32_t nameColor = isSelected ? COLOR_VENDOR_SELECTED : COLOR_VENDOR_NAME;
            ScaleformUtil::CreateLabel(uiMovie.get(), nameKey.c_str(), 46 + i,
                        baseX + 8.0, rowY, 160.0, VENDOR_LINE_H,
                        entry.name.c_str(), 12, nameColor);

            // Store name (gray, after vendor name)
            ScaleformUtil::CreateLabel(uiMovie.get(), storeKey.c_str(), 46 + MAX_VENDOR_LINES + i,
                        baseX + 170.0, rowY, 220.0, VENDOR_LINE_H,
                        entry.store.c_str(), 11, COLOR_VENDOR_STORE);

            // Timer (right-aligned)
            std::string timerText = FormatCountdown(entry.remainingHours);
            m_cachedTimerTexts[i] = timerText;
            ScaleformUtil::CreateLabel(uiMovie.get(), timerKey.c_str(), 46 + MAX_VENDOR_LINES * 2 + i,
                        baseX + contentW - 120.0, rowY, 120.0, VENDOR_LINE_H,
                        timerText.c_str(), 12, COLOR_VENDOR_TIMER);
            SetLabelAlign(uiMovie.get(), timerKey.c_str(), "right");

            rowY += VENDOR_LINE_H;
        }

        // Clear unused vendor slots
        for (int i = m_vendorScheduleCount; i < MAX_VENDOR_LINES; ++i) {
            std::string cursorKey = "_vendCursor" + std::to_string(i);
            std::string nameKey = "_vendName" + std::to_string(i);
            std::string storeKey = "_vendStore" + std::to_string(i);
            std::string timerKey = "_vendTimer" + std::to_string(i);

            ScaleformUtil::DrawFilledRect(uiMovie.get(), cursorKey.c_str(), 42 + i,
                        0.0, 0.0, 1.0, 1.0, 0x000000, 0);
            ScaleformUtil::CreateLabel(uiMovie.get(), nameKey.c_str(), 46 + i,
                        0.0, 0.0, 1.0, 1.0, "", 10, 0x000000);
            ScaleformUtil::CreateLabel(uiMovie.get(), storeKey.c_str(), 46 + MAX_VENDOR_LINES + i,
                        0.0, 0.0, 1.0, 1.0, "", 10, 0x000000);
            ScaleformUtil::CreateLabel(uiMovie.get(), timerKey.c_str(), 46 + MAX_VENDOR_LINES * 2 + i,
                        0.0, 0.0, 1.0, 1.0, "", 10, 0x000000);
            m_cachedTimerTexts[i].clear();
        }
    }

    void Menu::RedrawVendorSchedule() {
        if (!uiMovie || !m_hasVendorSchedule) return;

        double baseX = m_popupX + 20.0;
        double contentW = POPUP_W - 40.0;
        double rowY = m_vendorRowsY;

        for (int i = 0; i < m_vendorScheduleCount; ++i) {
            const auto& entry = m_vendorEntries[i];
            std::string cursorKey = "_vendCursor" + std::to_string(i);
            std::string nameKey = "_vendName" + std::to_string(i);

            bool isSelected = (m_focusZone == FocusZone::kVendorSchedule && m_vendorCursorIdx == i);

            // Cursor highlight
            if (isSelected) {
                ScaleformUtil::DrawFilledRect(uiMovie.get(), cursorKey.c_str(), 42 + i,
                            baseX - 4.0, rowY, contentW + 8.0, VENDOR_LINE_H, COLOR_CURSOR_BG, ALPHA_CURSOR);
            } else {
                ScaleformUtil::DrawFilledRect(uiMovie.get(), cursorKey.c_str(), 42 + i,
                            0.0, 0.0, 1.0, 1.0, 0x000000, 0);
            }

            // Vendor name color
            uint32_t nameColor = isSelected ? COLOR_VENDOR_SELECTED : COLOR_VENDOR_NAME;
            ScaleformUtil::CreateLabel(uiMovie.get(), nameKey.c_str(), 46 + i,
                        baseX + 8.0, rowY, 160.0, VENDOR_LINE_H,
                        entry.name.c_str(), 12, nameColor);

            rowY += VENDOR_LINE_H;
        }
    }

    void Menu::DrawVendorInfo() {
        // Info line below vendor rows — depth 60 (safe gap before log rows at 70+)
        double baseX = m_popupX + 20.0;

        if (m_focusZone != FocusZone::kVendorSchedule ||
            m_vendorCursorIdx < 0 ||
            m_vendorCursorIdx >= static_cast<int>(m_vendorEntries.size())) {
            // Clear info line
            ScaleformUtil::CreateLabel(uiMovie.get(), "_vendInfo", 60,
                        0.0, 0.0, 1.0, 1.0, "", 10, 0x000000);
            return;
        }

        const auto& entry = m_vendorEntries[m_vendorCursorIdx];

        // Build info text
        std::string buyDesc = DescribeVendorBuyList(entry.factionFormID);
        float rate = entry.isGeneral ? (Settings::fSellPricePercent * 100.0f)
                                     : (Settings::fVendorPricePercent * 100.0f);
        if (entry.invested) rate *= 1.05f;
        float interval = entry.isGeneral ? Settings::fSellIntervalHours
                                         : Settings::fVendorIntervalHours;

        char buf[256];
        if (entry.invested) {
            std::snprintf(buf, sizeof(buf), "Buys: %s  |  %.1f%% base value (invested)  |  %.0fh cycle",
                          buyDesc.c_str(), rate, interval);
        } else {
            std::snprintf(buf, sizeof(buf), "Buys: %s  |  %.0f%% base value  |  %.0fh cycle",
                          buyDesc.c_str(), rate, interval);
        }

        double infoY = m_vendorRowsY + m_vendorScheduleCount * VENDOR_LINE_H + 2.0;
        ScaleformUtil::CreateLabel(uiMovie.get(), "_vendInfo", 60,
                    baseX + 8.0, infoY, POPUP_W - 56.0, VENDOR_INFO_H,
                    buf, 10, COLOR_VENDOR_INFO);
    }

    void Menu::UpdateVendorTimers() {
        if (!uiMovie || !m_hasVendorSchedule) return;

        // Game time is frozen while menu is open — simulate elapsed time
        // using real wall-clock time * timescale
        auto elapsed = std::chrono::steady_clock::now() - m_menuOpenTime;
        float realSeconds = std::chrono::duration<float>(elapsed).count();
        float virtualGameHours = m_gameHoursAtOpen + (realSeconds * m_timeScale / 3600.0f);

        // Update remaining hours in existing entries using virtual time
        auto* mgr = NetworkManager::GetSingleton();
        const auto& sellState = mgr->GetSellState();
        auto* vendorReg = VendorRegistry::GetSingleton();

        for (auto& entry : m_vendorEntries) {
            if (entry.isGeneral) {
                entry.remainingHours = Settings::fSellIntervalHours - (virtualGameHours - sellState.lastSellTime);
            } else {
                const auto& vendors = vendorReg->GetVendors();
                for (const auto& v : vendors) {
                    if (v.vendorName == entry.name && v.active) {
                        entry.remainingHours = Settings::fVendorIntervalHours - (virtualGameHours - v.lastVisitTime);
                        break;
                    }
                }
            }
        }

        bool anyJustExpired = false;
        std::string onNextRest = T("$SLID_OnNextRest");
        for (int i = 0; i < m_vendorScheduleCount && i < MAX_VENDOR_LINES; ++i) {
            // Detect timer crossing zero (was positive in cache, now <= 0)
            if (m_vendorEntries[i].remainingHours <= 0.0f &&
                !m_cachedTimerTexts[i].empty() && m_cachedTimerTexts[i] != onNextRest) {
                anyJustExpired = true;
            }

            std::string timerText = FormatCountdown(m_vendorEntries[i].remainingHours);
            if (timerText != m_cachedTimerTexts[i]) {
                m_cachedTimerTexts[i] = timerText;
                std::string timerKey = "_vendTimer" + std::to_string(i);
                std::string path = "_root." + timerKey;
                RE::GFxValue field;
                uiMovie->GetVariable(&field, path.c_str());
                if (field.IsDisplayObject()) {
                    RE::GFxValue textVal;
                    textVal.SetString(timerText.c_str());
                    field.SetMember("text", textVal);
                    // Re-apply right alignment (setting text clears TextFormat)
                    SetLabelAlign(uiMovie.get(), timerKey.c_str(), "right");
                }
            }
        }

        // Immediately process sale when a timer crosses zero
        if (anyJustExpired) {
            // Advance calendar to match simulated time so sales engine sees elapsed timer
            float gameHoursElapsed = virtualGameHours - m_gameHoursAtOpen;
            if (auto* cal = RE::Calendar::GetSingleton()) {
                auto* daysPassed = cal->gameDaysPassed;
                if (daysPassed) {
                    daysPassed->value += gameHoursElapsed / 24.0f;
                }
                auto* gameHour = cal->gameHour;
                if (gameHour) {
                    gameHour->value += gameHoursElapsed;
                    while (gameHour->value >= 24.0f) {
                        gameHour->value -= 24.0f;
                    }
                }
            }
            // Reset baseline so we don't double-advance
            m_menuOpenTime = std::chrono::steady_clock::now();
            m_gameHoursAtOpen = virtualGameHours;

            SalesProcessor::TryProcessSales();

            // Rebuild UI with new transactions
            m_lastLogSize = NetworkManager::GetSingleton()->GetTransactionLog().size();
            BuildRuns();
            BuildVendorEntries();
            DrawPopup();
        }
    }

    void Menu::DrawLogHeading() {
        double logX = m_popupX + 20.0;
        double baseY = m_popupY + STATS_Y + STATS_H + 4.0 + m_vendorSchedH;

        // "Recent Transactions" heading (above the line)
        std::string recentHeading = T("$SLID_RecentTransactions");
        ScaleformUtil::CreateLabel(uiMovie.get(), "_logHeading", 31, logX, baseY, 300.0, LOG_HEADING_H,
                    recentHeading.c_str(), 13, COLOR_HEADING);

        // Separator line (below heading)
        double lineY = baseY + LOG_HEADING_H + 2.0;
        ScaleformUtil::DrawLine(uiMovie.get(), "_logSep", 30, logX, lineY,
                    logX + POPUP_W - 40.0, lineY, COLOR_BORDER);
    }

    void Menu::DrawLogHeader() {
        double logX = m_popupX + 20.0;
        double headerY = m_popupY + STATS_Y + STATS_H + 4.0 + m_vendorSchedH + LOG_HEADING_H + 6.0;  // after heading + line

        // Column headers — Vendor is indented to align with vendor name (after [+] icon zone)
        std::string colVendor = T("$SLID_ColVendor");
        std::string colItem = T("$SLID_ColItem");
        std::string colQty = T("$SLID_ColQty");
        std::string colPrice = T("$SLID_ColPrice");
        std::string colTotal = T("$SLID_ColTotal");
        std::string colTime = T("$SLID_ColTime");
        ScaleformUtil::CreateLabel(uiMovie.get(), "_logHdrVendor", 32, logX + LOG_COL_VENDOR_X, headerY, LOG_COL_VENDOR_W, LOG_HEADER_H,
                    colVendor.c_str(), 11, COLOR_HEADER);
        ScaleformUtil::CreateLabel(uiMovie.get(), "_logHdrItem", 33, logX + LOG_COL_ITEM_X, headerY, LOG_COL_ITEM_W, LOG_HEADER_H,
                    colItem.c_str(), 11, COLOR_HEADER);
        ScaleformUtil::CreateLabel(uiMovie.get(), "_logHdrQty", 34, logX + LOG_COL_QTY_X, headerY, LOG_COL_QTY_W, LOG_HEADER_H,
                    colQty.c_str(), 11, COLOR_HEADER);
        SetLabelAlign(uiMovie.get(), "_logHdrQty", "right");
        ScaleformUtil::CreateLabel(uiMovie.get(), "_logHdrPrice", 35, logX + LOG_COL_PRICE_X, headerY, LOG_COL_PRICE_W, LOG_HEADER_H,
                    colPrice.c_str(), 11, COLOR_HEADER);
        SetLabelAlign(uiMovie.get(), "_logHdrPrice", "right");
        ScaleformUtil::CreateLabel(uiMovie.get(), "_logHdrTotal", 36, logX + LOG_COL_TOTAL_X, headerY, LOG_COL_TOTAL_W, LOG_HEADER_H,
                    colTotal.c_str(), 11, COLOR_HEADER);
        SetLabelAlign(uiMovie.get(), "_logHdrTotal", "right");
        ScaleformUtil::CreateLabel(uiMovie.get(), "_logHdrTime", 37, logX + LOG_COL_TIME_X, headerY, LOG_COL_TIME_W, LOG_HEADER_H,
                    colTime.c_str(), 11, COLOR_HEADER);

        m_logAreaY = headerY + LOG_HEADER_H;
        m_logAreaH = (m_popupY + BTN_ZONE_TOP) - m_logAreaY;
    }

    void Menu::DrawLogRows() {
        double logX = m_popupX + 20.0;
        double maxY = m_popupY + BTN_ZONE_TOP;
        constexpr int MAX_SLOTS = 20;  // max scaleform elements to allocate

        // Determine highlight color for vendor-matched rows
        // Smooth fade from white (flash start) to gold (settled)
        bool hasHighlight = !m_highlightVendorName.empty();
        uint32_t highlightColor = COLOR_LOG_HIGHLIGHT;
        if (m_vendorFlashFrames > 0) {
            float t = static_cast<float>(m_vendorFlashFrames) / 12.0f;  // 1.0=white, 0.0=gold
            auto lerp = [](uint8_t a, uint8_t b, float t) -> uint8_t {
                return static_cast<uint8_t>(a + (b - a) * t);
            };
            uint8_t r = lerp((COLOR_LOG_HIGHLIGHT >> 16) & 0xFF, (COLOR_LOG_FLASH >> 16) & 0xFF, t);
            uint8_t g = lerp((COLOR_LOG_HIGHLIGHT >> 8) & 0xFF, (COLOR_LOG_FLASH >> 8) & 0xFF, t);
            uint8_t b = lerp(COLOR_LOG_HIGHLIGHT & 0xFF, COLOR_LOG_FLASH & 0xFF, t);
            highlightColor = (r << 16) | (g << 8) | b;
        }

        if (m_visibleRows.empty()) {
            std::string noSales = T("$SLID_NoSalesYet");
            ScaleformUtil::CreateLabel(uiMovie.get(), "_logEmpty", 70, logX, m_logAreaY + 40.0,
                        POPUP_W - 40.0, 24.0, noSales.c_str(), 14, COLOR_EMPTY);
            // Clear all row slots
            for (int slot = 0; slot < MAX_SLOTS; slot++) {
                std::string prefix = "_logRow" + std::to_string(slot);
                int baseDepth = 70 + slot * 10;
                ScaleformUtil::DrawFilledRect(uiMovie.get(), (prefix + "Cur").c_str(), baseDepth,
                            0.0, 0.0, 1.0, 1.0, 0x000000, 0);
            }
            return;
        }

        // Clear the "No sales yet" label in case it was showing
        ScaleformUtil::CreateLabel(uiMovie.get(), "_logEmpty", 70,
                    0.0, 0.0, 1.0, 1.0, "", 10, 0x000000);

        double curY = m_logAreaY;
        int slot = 0;

        for (int flatIdx = m_logScrollOffset;
             flatIdx < static_cast<int>(m_visibleRows.size()) && slot < MAX_SLOTS;
             flatIdx++, slot++) {

            const auto& vrow = m_visibleRows[flatIdx];
            double rh = (vrow.type == RowType::kRunHeader) ? LOG_ROW_H : DETAIL_ROW_H;

            if (curY + rh > maxY) break;

            std::string prefix = "_logRow" + std::to_string(slot);
            int baseDepth = 70 + slot * 10;

            // Check if this row's vendor matches the highlighted vendor
            const auto& run = m_runs[vrow.runIndex];
            bool vendorMatch = hasHighlight && run.vendorName == m_highlightVendorName;

            // Cursor highlight (only on run headers in transaction log zone)
            std::string cursorName = prefix + "Cur";
            bool showCursor = m_usingCursor && m_focusZone == FocusZone::kTransactionLog &&
                              flatIdx == m_selectedRow && vrow.type == RowType::kRunHeader;
            if (showCursor) {
                ScaleformUtil::DrawFilledRect(uiMovie.get(), cursorName.c_str(), baseDepth,
                            logX - 4.0, curY, POPUP_W - 32.0, rh, COLOR_CURSOR_BG, ALPHA_CURSOR);
            } else {
                ScaleformUtil::DrawFilledRect(uiMovie.get(), cursorName.c_str(), baseDepth,
                            logX - 4.0, curY, POPUP_W - 32.0, rh, 0x000000, 0);
            }

            if (vrow.type == RowType::kRunHeader) {
                uint32_t headerColor = vendorMatch ? highlightColor
                                     : (run.expanded ? COLOR_RUN_EXPANDED : COLOR_RUN_HEADER);
                std::string symbol = run.expanded ? "-" : "+";

                // [+] / [-] icon square — vertically aligned to text baseline
                double iconX = logX;
                double iconY = curY + 4.0;  // align with text top
                ScaleformUtil::DrawFilledRect(uiMovie.get(), (prefix + "IconBg").c_str(), baseDepth + 1,
                            iconX, iconY, EXPAND_ICON_SIZE, EXPAND_ICON_SIZE, COLOR_EXPAND_BG, 90);
                ScaleformUtil::DrawBorderRect(uiMovie.get(), (prefix + "IconBrd").c_str(), baseDepth + 2,
                            iconX, iconY, EXPAND_ICON_SIZE, EXPAND_ICON_SIZE, COLOR_EXPAND_BORDER);
                ScaleformUtil::CreateLabel(uiMovie.get(), (prefix + "IconSym").c_str(), baseDepth + 3,
                            iconX, iconY - 2.0, EXPAND_ICON_SIZE, EXPAND_ICON_SIZE + 2.0,
                            symbol.c_str(), 10, COLOR_EXPAND_SYMBOL);
                SetLabelAlign(uiMovie.get(), (prefix + "IconSym").c_str(), "center");

                // Vendor name
                ScaleformUtil::CreateLabel(uiMovie.get(), (prefix + "Vendor").c_str(), baseDepth + 4,
                            logX + LOG_COL_VENDOR_X, curY, LOG_COL_VENDOR_W, rh,
                            run.vendorName.c_str(), 11, headerColor);

                // Assortment
                uint32_t assortColor = vendorMatch ? highlightColor : COLOR_RUN_DETAIL;
                ScaleformUtil::CreateLabel(uiMovie.get(), (prefix + "Item").c_str(), baseDepth + 5,
                            logX + LOG_COL_ITEM_X, curY, LOG_COL_ITEM_W, rh,
                            run.vendorAssortment.c_str(), 11, assortColor);

                // Qty (right-aligned)
                std::string itemsText = std::to_string(run.totalItems);
                ScaleformUtil::CreateLabel(uiMovie.get(), (prefix + "Qty").c_str(), baseDepth + 6,
                            logX + LOG_COL_QTY_X, curY, LOG_COL_QTY_W, rh,
                            itemsText.c_str(), 11, headerColor);
                SetLabelAlign(uiMovie.get(), (prefix + "Qty").c_str(), "right");

                // Total gold (right-aligned)
                std::string totalText = std::to_string(run.totalGold) + "g";
                ScaleformUtil::CreateLabel(uiMovie.get(), (prefix + "Total").c_str(), baseDepth + 7,
                            logX + LOG_COL_TOTAL_X, curY, LOG_COL_TOTAL_W, rh,
                            totalText.c_str(), 11, headerColor);
                SetLabelAlign(uiMovie.get(), (prefix + "Total").c_str(), "right");

                // Time — relative day
                std::string timeStr = FormatRelativeTime(run.gameTime);
                ScaleformUtil::CreateLabel(uiMovie.get(), (prefix + "Time").c_str(), baseDepth + 8,
                            logX + LOG_COL_TIME_X, curY, LOG_COL_TIME_W, rh,
                            timeStr.c_str(), 11, headerColor);

                // Clear Price
                ScaleformUtil::CreateLabel(uiMovie.get(), (prefix + "Price").c_str(), baseDepth + 9,
                            logX, curY, 1.0, 1.0, "", 10, 0x000000);

            } else {
                // Detail row — compact, smaller font
                const auto* tx = run.items[vrow.itemIndex];
                uint32_t detailColor = vendorMatch ? highlightColor : COLOR_RUN_DETAIL;

                // Clear icon + vendor
                ScaleformUtil::DrawFilledRect(uiMovie.get(), (prefix + "IconBg").c_str(), baseDepth + 1,
                            0.0, 0.0, 1.0, 1.0, 0x000000, 0);
                ScaleformUtil::DrawBorderRect(uiMovie.get(), (prefix + "IconBrd").c_str(), baseDepth + 2,
                            0.0, 0.0, 1.0, 1.0, 0x000000);
                ScaleformUtil::CreateLabel(uiMovie.get(), (prefix + "IconSym").c_str(), baseDepth + 3,
                            logX, curY, 1.0, 1.0, "", 10, 0x000000);
                ScaleformUtil::CreateLabel(uiMovie.get(), (prefix + "Vendor").c_str(), baseDepth + 4,
                            logX, curY, 1.0, 1.0, "", 10, 0x000000);

                // Item
                ScaleformUtil::CreateLabel(uiMovie.get(), (prefix + "Item").c_str(), baseDepth + 5,
                            logX + LOG_COL_ITEM_X, curY, LOG_COL_ITEM_W, rh,
                            tx->itemName.c_str(), 10, detailColor);

                // Qty (right-aligned)
                std::string qtyText = std::to_string(tx->quantity);
                ScaleformUtil::CreateLabel(uiMovie.get(), (prefix + "Qty").c_str(), baseDepth + 6,
                            logX + LOG_COL_QTY_X, curY, LOG_COL_QTY_W, rh,
                            qtyText.c_str(), 10, detailColor);
                SetLabelAlign(uiMovie.get(), (prefix + "Qty").c_str(), "right");

                // Price (per unit, right-aligned) — 2 decimal places
                char priceBuf[32];
                std::snprintf(priceBuf, sizeof(priceBuf), "%.2fg", tx->pricePerUnit);
                std::string priceText = priceBuf;
                ScaleformUtil::CreateLabel(uiMovie.get(), (prefix + "Price").c_str(), baseDepth + 7,
                            logX + LOG_COL_PRICE_X, curY, LOG_COL_PRICE_W, rh,
                            priceText.c_str(), 10, detailColor);
                SetLabelAlign(uiMovie.get(), (prefix + "Price").c_str(), "right");

                // Total (right-aligned)
                std::string totalText = std::to_string(tx->goldEarned) + "g";
                ScaleformUtil::CreateLabel(uiMovie.get(), (prefix + "Total").c_str(), baseDepth + 8,
                            logX + LOG_COL_TOTAL_X, curY, LOG_COL_TOTAL_W, rh,
                            totalText.c_str(), 10, detailColor);
                SetLabelAlign(uiMovie.get(), (prefix + "Total").c_str(), "right");

                // Time: empty
                ScaleformUtil::CreateLabel(uiMovie.get(), (prefix + "Time").c_str(), baseDepth + 9,
                            logX, curY, 1.0, 1.0, "", 10, 0x000000);
            }

            curY += rh;
        }

        // Clear remaining slots
        for (; slot < MAX_SLOTS; slot++) {
            std::string prefix = "_logRow" + std::to_string(slot);
            int baseDepth = 70 + slot * 10;
            ScaleformUtil::DrawFilledRect(uiMovie.get(), (prefix + "Cur").c_str(), baseDepth,
                        0.0, 0.0, 1.0, 1.0, 0x000000, 0);
            ScaleformUtil::DrawFilledRect(uiMovie.get(), (prefix + "IconBg").c_str(), baseDepth + 1,
                        0.0, 0.0, 1.0, 1.0, 0x000000, 0);
            ScaleformUtil::DrawBorderRect(uiMovie.get(), (prefix + "IconBrd").c_str(), baseDepth + 2,
                        0.0, 0.0, 1.0, 1.0, 0x000000);
            ScaleformUtil::CreateLabel(uiMovie.get(), (prefix + "IconSym").c_str(), baseDepth + 3,
                        logX, 0.0, 1.0, 1.0, "", 10, 0x000000);
            ScaleformUtil::CreateLabel(uiMovie.get(), (prefix + "Vendor").c_str(), baseDepth + 4,
                        logX, 0.0, 1.0, 1.0, "", 10, 0x000000);
            ScaleformUtil::CreateLabel(uiMovie.get(), (prefix + "Item").c_str(), baseDepth + 5,
                        logX, 0.0, 1.0, 1.0, "", 10, 0x000000);
            ScaleformUtil::CreateLabel(uiMovie.get(), (prefix + "Qty").c_str(), baseDepth + 6,
                        logX, 0.0, 1.0, 1.0, "", 10, 0x000000);
            ScaleformUtil::CreateLabel(uiMovie.get(), (prefix + "Price").c_str(), baseDepth + 7,
                        logX, 0.0, 1.0, 1.0, "", 10, 0x000000);
            ScaleformUtil::CreateLabel(uiMovie.get(), (prefix + "Total").c_str(), baseDepth + 8,
                        logX, 0.0, 1.0, 1.0, "", 10, 0x000000);
            ScaleformUtil::CreateLabel(uiMovie.get(), (prefix + "Time").c_str(), baseDepth + 9,
                        logX, 0.0, 1.0, 1.0, "", 10, 0x000000);
        }
    }

    void Menu::UpdateLogRows() {
        if (!uiMovie) return;
        // Full redraw of log rows (recreates labels at same depths)
        DrawLogRows();
    }

    void Menu::DrawScrollbar() {
        int total = TotalFlattenedRows();
        int maxScroll = std::max(0, total - 1);
        if (maxScroll <= 0 || m_logScrollOffset == 0) {
            // Check if all content fits — compute total pixel height
            double totalH = 0.0;
            for (const auto& vr : m_visibleRows) {
                totalH += (vr.type == RowType::kRunHeader) ? LOG_ROW_H : DETAIL_ROW_H;
            }
            if (totalH <= m_logAreaH) {
                ScaleformUtil::DrawFilledRect(uiMovie.get(), "_scrollTrack", 290,
                            0.0, 0.0, 1.0, 1.0, 0x000000, 0);
                ScaleformUtil::DrawFilledRect(uiMovie.get(), "_scrollThumb", 291,
                            0.0, 0.0, 1.0, 1.0, 0x000000, 0);
                return;
            }
        }

        double trackX = m_popupX + POPUP_W - 20.0 - SCROLLBAR_W + SCROLLBAR_RIGHT_PAD;
        double trackY = m_logAreaY;
        double trackH = m_logAreaH;

        ScaleformUtil::DrawFilledRect(uiMovie.get(), "_scrollTrack", 290,
                    trackX, trackY, SCROLLBAR_W, trackH, COLOR_SCROLLBAR_TRACK, ALPHA_TRACK);

        // Thumb — proportional to scroll position
        double thumbRatio = 0.3;  // reasonable default visual size
        double thumbH = std::max(SCROLLBAR_MIN_THUMB, trackH * thumbRatio);
        double scrollRatio = (maxScroll > 0) ? static_cast<double>(m_logScrollOffset) / maxScroll : 0.0;
        double thumbY = trackY + scrollRatio * (trackH - thumbH);

        ScaleformUtil::DrawFilledRect(uiMovie.get(), "_scrollThumb", 291,
                    trackX, thumbY, SCROLLBAR_W, thumbH, COLOR_SCROLLBAR_THUMB, 100);
    }

    void Menu::UpdateScrollbar() {
        if (!uiMovie) return;
        DrawScrollbar();
    }

    void Menu::DrawCursorHighlight() {
        // Handled inline in DrawLogRows
    }

    void Menu::DrawCloseButton() {
        m_btnX = m_popupX + (POPUP_W - BTN_W) / 2.0;
        m_btnY = m_popupY + POPUP_H - 44.0;

        ScaleformUtil::DrawFilledRect(uiMovie.get(), "_btnCloseBg", 300, m_btnX, m_btnY, BTN_W, BTN_H,
                    COLOR_BTN_SELECT, ALPHA_BTN_SELECT);
        std::string closeLabel = T("$SLID_Close");
        ScaleformUtil::CreateLabel(uiMovie.get(), "_btnCloseLabel", 301, m_btnX, m_btnY + 4.0, BTN_W, BTN_H,
                    closeLabel.c_str(), 14, COLOR_BTN_LABEL);
        SetLabelAlign(uiMovie.get(), "_btnCloseLabel", "center");
    }

    void Menu::UpdateCloseButton() {
        if (!uiMovie) return;
        uint32_t color = (m_hoverButton == 0) ? COLOR_BTN_HOVER : COLOR_BTN_SELECT;
        int alpha = (m_hoverButton == 0) ? ALPHA_BTN_HOVER : ALPHA_BTN_SELECT;
        ScaleformUtil::DrawFilledRect(uiMovie.get(), "_btnCloseBg", 300, m_btnX, m_btnY, BTN_W, BTN_H, color, alpha);
    }

    std::pair<float, float> Menu::GetMousePos() const {
        float mx = 0.0f, my = 0.0f;
        if (uiMovie) {
            RE::GFxValue xVal, yVal;
            uiMovie->GetVariable(&xVal, "_root._xmouse");
            uiMovie->GetVariable(&yVal, "_root._ymouse");
            if (xVal.IsNumber()) mx = static_cast<float>(xVal.GetNumber());
            if (yVal.IsNumber()) my = static_cast<float>(yVal.GetNumber());
        }
        return {mx, my};
    }

    void Menu::OnMouseMove() {
        if (!g_activeMenu) return;
        auto& menu = *g_activeMenu;

        auto [mx, my] = menu.GetMousePos();
        int oldHover = menu.m_hoverButton;

        // Hit test close button
        if (mx >= menu.m_btnX && mx <= menu.m_btnX + BTN_W &&
            my >= menu.m_btnY && my <= menu.m_btnY + BTN_H) {
            menu.m_hoverButton = 0;
        } else {
            menu.m_hoverButton = -1;
        }

        if (menu.m_hoverButton != oldHover) {
            menu.UpdateCloseButton();
        }
    }

    void Menu::OnMouseDown() {
        if (!g_activeMenu) return;
        auto& menu = *g_activeMenu;

        // Close button
        if (menu.m_hoverButton == 0) {
            Close();
            return;
        }

        auto [mx, my] = menu.GetMousePos();

        // Hit test vendor rows
        int hitVendor = menu.HitTestVendorRow(mx, my);
        if (hitVendor >= 0) {
            menu.m_usingCursor = false;
            bool wasSelected = (menu.m_focusZone == FocusZone::kVendorSchedule &&
                               menu.m_vendorCursorIdx == hitVendor);
            if (wasSelected) {
                // Deselect
                menu.m_focusZone = FocusZone::kTransactionLog;
                menu.m_vendorCursorIdx = -1;
                menu.m_highlightVendorName.clear();
                menu.m_vendorFlashFrames = 0;
            } else {
                menu.m_focusZone = FocusZone::kVendorSchedule;
                menu.m_vendorCursorIdx = hitVendor;
                menu.m_highlightVendorName = menu.m_vendorEntries[hitVendor].name;
                menu.m_vendorFlashFrames = 12;
            }
            menu.RedrawVendorSchedule();
            menu.DrawVendorInfo();
            menu.UpdateLogRows();
            return;
        }

        // Hit test log rows
        int hitRow = menu.HitTestLogRow(mx, my);
        if (hitRow >= 0 && hitRow < static_cast<int>(menu.m_visibleRows.size())) {
            // If we were in vendor zone, switch to log
            if (menu.m_focusZone == FocusZone::kVendorSchedule) {
                menu.m_focusZone = FocusZone::kTransactionLog;
                menu.m_vendorCursorIdx = -1;
                menu.m_highlightVendorName.clear();
                menu.m_vendorFlashFrames = 0;
                menu.RedrawVendorSchedule();
                menu.DrawVendorInfo();
            }

            menu.m_selectedRow = hitRow;
            menu.m_usingCursor = false;

            const auto& vrow = menu.m_visibleRows[hitRow];
            if (vrow.type == RowType::kRunHeader) {
                menu.m_runs[vrow.runIndex].expanded = !menu.m_runs[vrow.runIndex].expanded;
                menu.BuildVisibleRows();

                // Clamp
                int total = menu.TotalFlattenedRows();
                if (menu.m_selectedRow >= total) {
                    menu.m_selectedRow = std::max(0, total - 1);
                }
                if (menu.m_logScrollOffset >= total) {
                    menu.m_logScrollOffset = std::max(0, total - 1);
                }

                menu.UpdateLogRows();
                menu.UpdateScrollbar();
            }
        }
    }

    // --- InputHandler ---

    InputHandler* InputHandler::GetSingleton() {
        static InputHandler singleton;
        return &singleton;
    }

    void InputHandler::Register() {
        auto* input = RE::BSInputDeviceManager::GetSingleton();
        if (input) {
            input->AddEventSink(GetSingleton());
            logger::info("SellOverview::InputHandler registered");
        }
    }

    RE::BSEventNotifyControl InputHandler::ProcessEvent(
        RE::InputEvent* const* a_event,
        RE::BSTEventSource<RE::InputEvent*>*) {

        if (!Menu::IsOpen() || !a_event) {
            return RE::BSEventNotifyControl::kContinue;
        }

        for (auto* event = *a_event; event; event = event->next) {
            if (event->eventType == RE::INPUT_EVENT_TYPE::kButton) {
                auto* button = static_cast<RE::ButtonEvent*>(event);
                if (!button->IsDown()) continue;

                auto device = event->device.get();
                auto key = button->GetIDCode();

                // Gamepad
                if (device == RE::INPUT_DEVICE::kGamepad) {
                    if (key == ScaleformUtil::GAMEPAD_B) {
                        Menu::Close();
                    } else if (key == ScaleformUtil::GAMEPAD_DPAD_UP) {
                        Menu::CursorUp();
                    } else if (key == ScaleformUtil::GAMEPAD_DPAD_DOWN) {
                        Menu::CursorDown();
                    } else if (key == ScaleformUtil::GAMEPAD_A) {
                        Menu::ActivateRow();
                    }
                }

                // Keyboard
                if (device == RE::INPUT_DEVICE::kKeyboard) {
                    if (key == 0x01) {  // Escape
                        Menu::Close();
                    } else if (key == 0xC8) {  // Up arrow
                        Menu::CursorUp();
                    } else if (key == 0xD0) {  // Down arrow
                        Menu::CursorDown();
                    } else if (key == 0x1C) {  // Enter
                        Menu::ActivateRow();
                    }
                }

                // Mouse
                if (device == RE::INPUT_DEVICE::kMouse) {
                    if (key == 0) {  // Left click
                        Menu::OnMouseDown();
                    } else if (key == 1) {  // Right click
                        Menu::Close();
                    } else if (key == 8) {  // Scroll wheel up
                        Menu::ScrollUp();
                    } else if (key == 9) {  // Scroll wheel down
                        Menu::ScrollDown();
                    }
                }
            }

            // Thumbstick (left stick up/down for cursor navigation)
            if (event->eventType == RE::INPUT_EVENT_TYPE::kThumbstick) {
                auto* ts = static_cast<RE::ThumbstickEvent*>(event);
                if (ts->IsLeft()) {
                    constexpr float DEADZONE = 0.5f;
                    bool nowUp = ts->yValue > DEADZONE;
                    bool nowDown = ts->yValue < -DEADZONE;

                    if (nowUp && !m_thumbUp) {
                        Menu::CursorUp();
                    } else if (nowDown && !m_thumbDown) {
                        Menu::CursorDown();
                    }
                    m_thumbUp = nowUp;
                    m_thumbDown = nowDown;
                }
            }

            // Mouse move
            if (event->eventType == RE::INPUT_EVENT_TYPE::kMouseMove) {
                Menu::OnMouseMove();
            }
        }

        return RE::BSEventNotifyControl::kContinue;
    }
}
