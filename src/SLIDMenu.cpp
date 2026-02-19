#include "SLIDMenu.h"
#include "ActionBar.h"
#include "ActivationHook.h"
#include "CatchAllPanel.h"
#include "ConfigState.h"
#include "ConfirmDialog.h"
#include "ContainerRegistry.h"
#include "Distributor.h"
#include "Dropdown.h"
#include "Feedback.h"
#include "FilterPanel.h"
#include "FilterRegistry.h"
#include "FilterRow.h"
#include "HoldRemove.h"
#include "NetworkManager.h"
#include "OriginPanel.h"
#include "ScaleformUtil.h"
#include "SCIEIntegration.h"
#include "Settings.h"
#include "TranslationService.h"
#include "WhooshConfigMenu.h"

namespace SLIDMenu {

    // Active menu instance (set on open, cleared on close)
    static ConfigMenu* g_activeMenu = nullptr;

    // --- Helpers for contextual defaults dialog ---

    static int CountAllLinkedItems() {
        auto& filterRows = FilterPanel::GetFilterRows();
        auto masterFormID = ConfigState::GetMasterFormID();
        int total = 0;

        auto* registry = ContainerRegistry::GetSingleton();
        for (const auto& row : filterRows) {
            auto countContainer = [&](RE::FormID formID) {
                if (formID == 0 || formID == masterFormID) return;
                total += registry->CountItems(formID);
            };
            countContainer(row.GetData().containerFormID);
            for (const auto& child : row.GetChildren()) {
                countContainer(child.containerFormID);
            }
        }

        // Count catch-all items (only if catch-all is not the master)
        auto catchAllFormID = CatchAllPanel::GetContainerFormID();
        if (catchAllFormID != 0 && catchAllFormID != masterFormID) {
            total += registry->CountItems(catchAllFormID);
        }
        return total;
    }

    static int CountLinkedContainers() {
        auto& filterRows = FilterPanel::GetFilterRows();
        auto masterFormID = ConfigState::GetMasterFormID();
        std::set<RE::FormID> unique;

        for (const auto& row : filterRows) {
            auto fid = row.GetData().containerFormID;
            if (fid != 0 && fid != masterFormID) unique.insert(fid);
            for (const auto& child : row.GetChildren()) {
                if (child.containerFormID != 0 && child.containerFormID != masterFormID)
                    unique.insert(child.containerFormID);
            }
        }

        auto catchAllFormID = CatchAllPanel::GetContainerFormID();
        if (catchAllFormID != 0 && catchAllFormID != masterFormID)
            unique.insert(catchAllFormID);

        return static_cast<int>(unique.size());
    }


    // --- ConfigMenu implementation ---

    void ConfigMenu::Register() {
        auto ui = RE::UI::GetSingleton();
        if (ui) {
            ui->Register(MENU_NAME, Create);
            logger::info("Registered menu: {}", MENU_NAME);
        }
    }

    RE::IMenu* ConfigMenu::Create() {
        return new ConfigMenu();
    }

    ConfigMenu::ConfigMenu() {
        depthPriority = 3;

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
                logger::info("Loaded SWF: {}", FILE_NAME);
            } else {
                logger::error("Failed to load SWF: {}", FILE_NAME);
            }
        }
    }

    void ConfigMenu::PostCreate() {
        if (!uiMovie) return;
        g_activeMenu = this;

        // Initialize FilterPanel with movie, network context, and callbacks
        FilterPanel::Callbacks cb{
            []() { ConfigMenu::Hide(); },
            [](const std::string& n) { ConfigMenu::Show(n); },
            []() { InputHandler::ResetRepeat(); },
            [this]() { RecalcPredictions(); },
            [this]() { BuildStagesFromNetwork(); },
            [this]() { RunSort(); },
            [this]() { RunWhoosh(); },
            []() -> RE::FormID { return CatchAllPanel::GetContainerFormID(); },
            [this]() { FilterPanel::SaveOrchestratorFocus(static_cast<int>(m_focus), m_actionIndex); },
            [](RE::FormID id) { ActivationHook::SetBypass(id); }
        };
        FilterPanel::Init(uiMovie.get(), cb);

        // Initialize CatchAllPanel
        CatchAllPanel::Callbacks catchAllCb{
            [this]() {
                ConfigState::CommitToNetwork(ConfigState::GetNetworkName(),
                    FilterPanel::BuildFilterStages(), CatchAllPanel::GetContainerFormID());
            },
            [this]() { RecalcPredictions(); },
            []() { ConfigMenu::Hide(); },
            []() { InputHandler::ResetRepeat(); },
            [this]() { FilterPanel::SaveOrchestratorFocus(static_cast<int>(m_focus), m_actionIndex); FilterPanel::SaveState(); }
        };
        CatchAllPanel::Init(uiMovie.get(), ConfigState::GetMasterFormID(), catchAllCb);

        // Load network data into panels (after both Init calls, before Draw)
        FilterPanel::LoadFromNetwork();

        // Draw panel chrome (background, borders, column headers, bands)
        DrawUI();

        // Draw origin row
        OriginPanel::Draw(uiMovie.get(), ConfigState::GetMasterFormID(),
                          FilterPanel::ROW_X, FilterPanel::ROW_Y,
                          FilterPanel::ROW_W, FilterPanel::ROW_HEIGHT);

        // Draw filter rows, scrollbar, add row, restore state
        FilterPanel::Draw();

        // Draw catch-all row
        CatchAllPanel::Draw();

        // Draw action bar
        DrawActionBar();

        // Restore orchestrator focus state if reopening
        m_focus = static_cast<FocusTarget>(FilterPanel::GetSavedFocusTarget());
        m_actionIndex = FilterPanel::GetSavedActionIndex();
        if (m_focus == FocusTarget::kCatchAllPanel) {
            CatchAllPanel::Select();
        } else if (m_focus == FocusTarget::kFilterPanel) {
            // s_selectedIndex already restored by FilterPanel::RestoreState()
        }
        UpdateActionBar();
        UpdateGuideText();

        logger::info("ConfigMenu ready: {} filter stages, rowsReady",
                     FilterPanel::GetFilterCount());
    }

    // --- DrawUI: panel chrome only ---

    void ConfigMenu::DrawUI() {
        using namespace FilterPanel;

        // Panel background
        ScaleformUtil::DrawFilledRect(uiMovie.get(), "_panelBg", 1, PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 0x000000, 92);
        ScaleformUtil::DrawBorderRect(uiMovie.get(), "_panelBorder", 2, PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 0x555555);

        // Title
        ScaleformUtil::CreateLabel(uiMovie.get(), "_title", 40, PANEL_X + 20.0, PANEL_Y + 12.0, 500.0, 34.0,
                    "Linked Item Distribution", 26, COLOR_TITLE);

        // Network name (right-aligned)
        auto& networkName = ConfigState::GetNetworkName();
        std::string networkNameDisplay = networkName.empty() ? "Network" : networkName;
        ScaleformUtil::CreateLabel(uiMovie.get(), "_networkName", 41, PANEL_X + 20.0, PANEL_Y + 18.0, PANEL_W - 40.0, 26.0,
                    networkNameDisplay.c_str(), 16, COLOR_HEADERS);
        {
            RE::GFxValue tf;
            uiMovie->GetVariable(&tf, "_root._networkName");
            if (!tf.IsUndefined()) {
                RE::GFxValue alignFmt;
                uiMovie->CreateObject(&alignFmt, "TextFormat");
                if (!alignFmt.IsUndefined()) {
                    RE::GFxValue alignVal;
                    alignVal.SetString("right");
                    alignFmt.SetMember("align", alignVal);
                    RE::GFxValue fmtArgs[1];
                    fmtArgs[0] = alignFmt;
                    tf.Invoke("setTextFormat", nullptr, fmtArgs, 1);
                    tf.Invoke("setNewTextFormat", nullptr, fmtArgs, 1);
                }
            }
        }

        // Column headers (inside FILTERS band)
        double headerY = ROW_Y + ROW_HEIGHT + 1.0;  // just inside the FILTERS band
        ScaleformUtil::CreateLabel(uiMovie.get(), "_colNum",       60, ROW_X + COL_NUM_X,       headerY, COL_NUM_W,       18.0, "#",         12, COLOR_HEADERS);
        std::string colFilter = T("$SLID_ColFilter");
        std::string colContainer = T("$SLID_ColContainer");
        ScaleformUtil::CreateLabel(uiMovie.get(), "_colFilter",    61, ROW_X + COL_FILTER_X,    headerY, COL_FILTER_W,    18.0, colFilter.c_str(),    12, COLOR_HEADERS);
        ScaleformUtil::CreateLabel(uiMovie.get(), "_colContainer", 62, ROW_X + COL_CONTAINER_X, headerY, COL_CONTAINER_W, 18.0, colContainer.c_str(), 12, COLOR_HEADERS);
        ScaleformUtil::CreateLabel(uiMovie.get(), "_colItems",     63, ROW_X + COL_ITEMS_X,     headerY, COL_ITEMS_W,     18.0, "Items",     12, COLOR_HEADERS);

        // Guide text
        double guideTextY = CATCHALL_ROW_Y + ROW_HEIGHT + 4.0;
        ScaleformUtil::CreateLabel(uiMovie.get(), "_guideText", 42, ROW_X, guideTextY, ROW_W, 18.0, "", 14, COLOR_HINT);

        // FILTERS band (column headers are overlaid on this band)
        {
            double bandY = ROW_Y + ROW_HEIGHT;
            ScaleformUtil::DrawFilledRect(uiMovie.get(), "_filtersBand", 4, ROW_X, bandY, ROW_W, BAND_H, COLOR_SEP_BAND, 90);
            ScaleformUtil::DrawLine(uiMovie.get(), "_filtersBandTop", 45, ROW_X, bandY, ROW_X + ROW_W, bandY, 0x555555);
            ScaleformUtil::DrawLine(uiMovie.get(), "_filtersBandBot", 46, ROW_X, bandY + BAND_H, ROW_X + ROW_W, bandY + BAND_H, 0x555555);
        }

        // CATCH-ALL band
        {
            double bandY = CATCHALL_BAND_Y;
            ScaleformUtil::DrawFilledRect(uiMovie.get(), "_sepBand", 7, ROW_X, bandY, ROW_W, BAND_H, COLOR_SEP_BAND, 90);
            ScaleformUtil::DrawLine(uiMovie.get(), "_sepLineTop", 48, ROW_X, bandY, ROW_X + ROW_W, bandY, 0x555555);
            ScaleformUtil::DrawLine(uiMovie.get(), "_sepLineBot", 49, ROW_X, bandY + BAND_H, ROW_X + ROW_W, bandY + BAND_H, 0x555555);

            ScaleformUtil::CreateLabel(uiMovie.get(), "_sepLabel", 50, ROW_X, bandY + 1.0, ROW_W, BAND_H, "CATCH-ALL", 12, 0x888888);
            RE::GFxValue tf;
            uiMovie->GetVariable(&tf, "_root._sepLabel");
            if (!tf.IsUndefined()) {
                RE::GFxValue alignFmt;
                uiMovie->CreateObject(&alignFmt, "TextFormat");
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
        }

        // Tagline
        std::string credits = T("$SLID_Credits");
        ScaleformUtil::CreateLabel(uiMovie.get(), "_credits", 43, PANEL_X + 20.0, PANEL_BOTTOM - 16.0, PANEL_W - 40.0, 14.0,
                    credits.c_str(), 10, COLOR_CREDITS);
        {
            RE::GFxValue tf;
            uiMovie->GetVariable(&tf, "_root._credits");
            if (!tf.IsUndefined()) {
                RE::GFxValue alignFmt;
                uiMovie->CreateObject(&alignFmt, "TextFormat");
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
        }
    }

    // --- Action bar ---

    void ConfigMenu::DrawActionBar() {
        double barY = FilterPanel::PANEL_BOTTOM - 44.0;
        ActionBar::Draw(uiMovie.get(), FilterPanel::PANEL_X, FilterPanel::PANEL_W, barY,
                        InActionBar(), m_actionIndex);
    }

    void ConfigMenu::UpdateActionBar() {
        ActionBar::Update(InActionBar(), m_actionIndex,
                          m_hoverActionBar, m_hoverActionIndex);
    }

    void ConfigMenu::UpdateGuideText() {
        if (!uiMovie) return;
        std::string text;
        switch (m_focus) {
            case FocusTarget::kActionBar:
                text = ActionBar::GetGuideText(m_actionIndex);
                break;
            case FocusTarget::kCatchAllPanel:
                text = CatchAllPanel::GetGuideText();
                break;
            case FocusTarget::kFilterPanel:
                text = FilterPanel::GetGuideText();
                break;
        }
        RE::GFxValue tf;
        uiMovie->GetVariable(&tf, "_root._guideText");
        if (!tf.IsUndefined()) {
            RE::GFxValue textVal;
            textVal.SetString(text.c_str());
            tf.SetMember("text", textVal);
        }
    }

    // --- Pipeline operations ---

    void ConfigMenu::BuildStagesFromNetwork() {
        auto data = ConfigState::BuildFromNetwork();

        if (!data.hasNetwork) {
            CatchAllPanel::SetCatchAll(data.catchAll.containerName, data.catchAll.containerFormID,
                                       data.catchAll.location, data.catchAll.count);
            FilterPanel::BuildDefaultsAndCommit();
            return;
        }

        // Convert to FilterRow::Data
        std::vector<FilterRow::Data> stages;
        stages.reserve(data.stages.size());
        for (auto& s : data.stages) {
            FilterRow::Data d;
            d.filterID = std::move(s.filterID);
            d.name = std::move(s.name);
            d.containerName = std::move(s.containerName);
            d.location = std::move(s.location);
            d.containerFormID = s.containerFormID;
            d.count = s.count;
            d.predictedCount = -1;
            stages.push_back(std::move(d));
        }

        FilterPanel::LoadStages(std::move(stages));
        CatchAllPanel::SetCatchAll(data.catchAll.containerName, data.catchAll.containerFormID,
                                   data.catchAll.location, data.catchAll.count);
    }

    void ConfigMenu::RecalcPredictions() {
        auto filters = FilterPanel::BuildFilterStages();
        auto catchAllFormID = CatchAllPanel::GetContainerFormID();
        auto masterFormID = ConfigState::GetMasterFormID();

        auto prediction = Distributor::PredictDistribution(
            masterFormID, filters, catchAllFormID);

        FilterPanel::SetPredictions(prediction.filterCounts, prediction.contestedCounts,
                                    prediction.contestedByMaps, prediction.originCount);
        bool catchAllIsMaster = (catchAllFormID == 0 || catchAllFormID == masterFormID);

        // Refresh catch-all base count from live container data (may have changed
        // due to item transfers like GatherFamilyToMaster)
        RE::FormID countFormID = catchAllIsMaster ? masterFormID : catchAllFormID;
        if (countFormID != 0) {
            int liveCount = ContainerRegistry::GetSingleton()->CountItems(countFormID);
            CatchAllPanel::SetCount(liveCount, false);
        }

        CatchAllPanel::SetPrediction(
            catchAllIsMaster ? prediction.originCount : prediction.catchAllCount,
            catchAllIsMaster);

        OriginPanel::UpdateCount(uiMovie.get(),
            FilterPanel::GetCurrentOriginCount(),
            FilterPanel::GetPredictedOriginCount());

        UpdateGuideText();
    }

    void ConfigMenu::RunSort() {
        auto networkName = ConfigState::GetNetworkName();
        auto& filterRows = FilterPanel::GetFilterRows();

        std::vector<int> oldFilterCounts;
        for (const auto& row : filterRows) oldFilterCounts.push_back(row.GetData().count);
        int oldCatchAllCount = ContainerRegistry::GetSingleton()->CountItems(
            CatchAllPanel::GetContainerFormID());

        int oldOriginCount = FilterPanel::GetCurrentOriginCount();

        auto result = Distributor::Distribute(networkName);
        logger::info("Sort complete: {} items moved", result.totalItems);

        BuildStagesFromNetwork();

        // Determine which rows changed counts
        std::set<int> flashIndices;
        auto& newRows = FilterPanel::GetFilterRows();
        int filterCount = static_cast<int>(newRows.size());
        for (int i = 0; i < filterCount; i++) {
            int oldCount = (i < static_cast<int>(oldFilterCounts.size())) ? oldFilterCounts[i] : 0;
            if (newRows[i].GetData().count != oldCount) flashIndices.insert(i);
        }
        // Check catch-all
        int newCatchAllCount = ContainerRegistry::GetSingleton()->CountItems(
            CatchAllPanel::GetContainerFormID());
        if (newCatchAllCount != oldCatchAllCount) flashIndices.insert(filterCount);

        int newOriginCount = FilterPanel::GetCurrentOriginCount();
        if (newOriginCount != oldOriginCount) flashIndices.insert(-1);

        // Remove catch-all flash from the set before passing to FilterPanel
        bool catchAllFlash = flashIndices.count(filterCount) > 0;
        flashIndices.erase(filterCount);
        FilterPanel::RefreshAfterSort(flashIndices);
        if (catchAllFlash) {
            // newCatchAllCount was already computed above — flash the updated count
            CatchAllPanel::SetCount(newCatchAllCount, true);
        }
        OriginPanel::SetCount(uiMovie.get(), newOriginCount, flashIndices.count(-1) > 0);

        if (result.totalItems > 0) {
            std::string msg = TF("$SLID_NotifySorted", std::to_string(result.totalItems));
            RE::DebugNotification(msg.c_str());
        } else {
            RE::DebugNotification(T("$SLID_NothingToSort").c_str());
        }
    }

    void ConfigMenu::RunSweep() {
        auto networkName = ConfigState::GetNetworkName();

        // Snapshot old counts for flash detection
        auto& filterRows = FilterPanel::GetFilterRows();
        std::vector<int> oldFilterCounts;
        for (const auto& row : filterRows) oldFilterCounts.push_back(row.GetData().count);
        int oldCatchAllCount = ContainerRegistry::GetSingleton()->CountItems(
            CatchAllPanel::GetContainerFormID());
        int oldOriginCount = FilterPanel::GetCurrentOriginCount();

        auto moved = Distributor::GatherToMaster(networkName);
        logger::info("Sweep complete: {} items gathered to master", moved);

        BuildStagesFromNetwork();
        RecalcPredictions();

        // Flash changed rows
        std::set<int> flashIndices;
        auto& newRows = FilterPanel::GetFilterRows();
        int filterCount = static_cast<int>(newRows.size());
        for (int i = 0; i < filterCount; i++) {
            int oldCount = (i < static_cast<int>(oldFilterCounts.size())) ? oldFilterCounts[i] : 0;
            if (newRows[i].GetData().count != oldCount) flashIndices.insert(i);
        }
        int newCatchAllCount = ContainerRegistry::GetSingleton()->CountItems(
            CatchAllPanel::GetContainerFormID());
        if (newCatchAllCount != oldCatchAllCount) flashIndices.insert(filterCount);

        int newOriginCount = FilterPanel::GetCurrentOriginCount();
        if (newOriginCount != oldOriginCount) flashIndices.insert(-1);

        bool catchAllFlash = flashIndices.count(filterCount) > 0;
        flashIndices.erase(filterCount);
        FilterPanel::RefreshAfterSort(flashIndices);
        if (catchAllFlash) {
            CatchAllPanel::SetCount(newCatchAllCount, true);
        }
        OriginPanel::SetCount(uiMovie.get(), newOriginCount, flashIndices.count(-1) > 0);

        if (moved > 0) {
            std::string msg = TF("$SLID_NotifySwept", std::to_string(moved));
            RE::DebugNotification(msg.c_str());
        } else {
            RE::DebugNotification(T("$SLID_NothingToSweep").c_str());
        }
    }

    void ConfigMenu::RunWhoosh() {
        auto networkName = ConfigState::GetNetworkName();
        auto* mgr = NetworkManager::GetSingleton();
        auto* net = mgr->FindNetwork(networkName);
        if (!net) return;

        if (!net->whooshConfigured) {
            auto defaultFilters = FilterRegistry::DefaultWhooshFilters();
            WhooshConfig::Menu::Show(defaultFilters, [this](bool confirmed, std::unordered_set<std::string> filters) {
                if (!confirmed) return;
                auto nm = ConfigState::GetNetworkName();
                auto* nmgr = NetworkManager::GetSingleton();
                nmgr->SetWhooshConfig(nm, filters);

                auto moved = Distributor::Whoosh(nm);
                if (moved > 0) {
                    Feedback::OnWhoosh();
                    std::string msg = TF("$SLID_NotifyWhooshed", std::to_string(moved));
                    RE::DebugNotification(msg.c_str());
                } else {
                    RE::DebugNotification(T("$SLID_NothingToWhoosh").c_str());
                }

                BuildStagesFromNetwork();
                RecalcPredictions();
            });
            return;
        }

        auto moved = Distributor::Whoosh(networkName);
        if (moved > 0) {
            Feedback::OnWhoosh();
            std::string msg = TF("$SLID_NotifyWhooshed", std::to_string(moved));
            RE::DebugNotification(msg.c_str());
        } else {
            RE::DebugNotification(T("$SLID_NothingToWhoosh").c_str());
        }

        BuildStagesFromNetwork();
        RecalcPredictions();
    }

    // RunCancel removed — close is now immediate (no dirty tracking)

    void ConfigMenu::HapticBrief() {}
    void ConfigMenu::HapticMedium() {}

    // --- Menu lifecycle ---

    RE::UI_MESSAGE_RESULTS ConfigMenu::ProcessMessage(RE::UIMessage& a_message) {
        using Message = RE::UI_MESSAGE_TYPE;

        switch (a_message.type.get()) {
            case Message::kHide:
                logger::debug("ConfigMenu: kHide");
                FilterPanel::Destroy();
                CatchAllPanel::Destroy();
                OriginPanel::Destroy();
                ActionBar::Destroy();
                g_activeMenu = nullptr;
                return RE::UI_MESSAGE_RESULTS::kHandled;

            case Message::kShow:
                logger::debug("ConfigMenu: kShow");
                return RE::UI_MESSAGE_RESULTS::kHandled;

            case Message::kUpdate: {
                bool predictionsRecalculated = FilterPanel::Update();
                ActionBar::UpdateFlash();
                CatchAllPanel::Update();
                OriginPanel::Update(uiMovie.get());
                if (predictionsRecalculated) {
                    OriginPanel::UpdateCount(uiMovie.get(),
                        FilterPanel::GetCurrentOriginCount(),
                        FilterPanel::GetPredictedOriginCount());
                }
                return RE::UI_MESSAGE_RESULTS::kHandled;
            }

            default:
                return RE::IMenu::ProcessMessage(a_message);
        }
    }

    void ConfigMenu::Show(const std::string& a_networkName) {
        if (!a_networkName.empty()) {
            auto* net = NetworkManager::GetSingleton()->FindNetwork(a_networkName);
            // Set context before opening (statics survive across menu instances)
            ConfigState::SetContext(a_networkName, net ? net->masterFormID : 0);
        }

        // Request SCIE containers for picker (async — response cached for session)
        if (Settings::bSCIEIncludeContainers && SCIEIntegration::IsInstalled()) {
            SCIEIntegration::RequestContainers();
        }

        auto ui = RE::UI::GetSingleton();
        if (ui && !ui->IsMenuOpen(MENU_NAME)) {
            auto msgQueue = RE::UIMessageQueue::GetSingleton();
            if (msgQueue) {
                msgQueue->AddMessage(MENU_NAME, RE::UI_MESSAGE_TYPE::kShow, nullptr);
                logger::info("Opening menu: {} for network '{}'", MENU_NAME, a_networkName);
            }
        }
    }

    void ConfigMenu::Hide() {
        auto ui = RE::UI::GetSingleton();
        if (ui && ui->IsMenuOpen(MENU_NAME)) {
            auto msgQueue = RE::UIMessageQueue::GetSingleton();
            if (msgQueue) {
                msgQueue->AddMessage(MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
                logger::info("Closing menu: {}", MENU_NAME);
            }
        }
        // Clear SCIE container cache (request fresh data next time)
        SCIEIntegration::ClearCache();
    }

    void ConfigMenu::RequestClose() {
        Hide();
    }

    bool ConfigMenu::IsOpen() {
        auto ui = RE::UI::GetSingleton();
        return ui && ui->IsMenuOpen(MENU_NAME);
    }

    // --- ContainerCloseListener ---

    ContainerCloseListener* ContainerCloseListener::GetSingleton() {
        static ContainerCloseListener instance;
        return &instance;
    }

    void ContainerCloseListener::Register() {
        auto ui = RE::UI::GetSingleton();
        if (ui) {
            ui->AddEventSink<RE::MenuOpenCloseEvent>(GetSingleton());
            logger::info("ContainerCloseListener registered");
        }
    }

    RE::BSEventNotifyControl ContainerCloseListener::ProcessEvent(
        const RE::MenuOpenCloseEvent* a_event,
        [[maybe_unused]] RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_source)
    {
        if (!a_event) return RE::BSEventNotifyControl::kContinue;

        if (!a_event->opening) {
            if (a_event->menuName == RE::ContainerMenu::MENU_NAME) {
                FilterPanel::OnContainerClosed();
            }

        }

        return RE::BSEventNotifyControl::kContinue;
    }

    // --- InputHandler: thin focus router ---

    InputHandler* InputHandler::GetSingleton() {
        static InputHandler instance;
        return &instance;
    }

    void InputHandler::Register() {
        auto inputManager = RE::BSInputDeviceManager::GetSingleton();
        if (inputManager) {
            inputManager->AddEventSink(GetSingleton());
            logger::info("Registered input handler");
        }
    }

    void InputHandler::ResetRepeat() {
        auto* handler = GetSingleton();
        DirectionalInput::Reset(handler->m_thumbState, handler->m_repeatState);
    }

    void InputHandler::HandleMouseEvent(RE::InputEvent* event) {
        if (!g_activeMenu) return;
        auto& menu = *g_activeMenu;

        constexpr uint32_t MOUSE_LEFT_BUTTON  = 0;
        constexpr uint32_t MOUSE_RIGHT_BUTTON = 1;
        constexpr uint32_t MOUSE_WHEEL_UP     = 8;
        constexpr uint32_t MOUSE_WHEEL_DOWN   = 9;

        if (event->eventType == RE::INPUT_EVENT_TYPE::kMouseMove) {
            auto [mx, my] = FilterPanel::GetMousePos();
            FilterPanel::OnMouseMove();
            CatchAllPanel::UpdateHover(mx, my);

            // Orchestrator-level hover: ConfirmDialog > ActionBar
            if (ConfirmDialog::IsOpen()) {
                int btnHit = ConfirmDialog::HitTest(mx, my);
                ConfirmDialog::UpdateHover(btnHit);
            } else {
                int hitBtn = ActionBar::HitTest(mx, my);
                bool newHoverAB = (hitBtn >= 0);
                if (newHoverAB != menu.m_hoverActionBar || hitBtn != menu.m_hoverActionIndex) {
                    menu.m_hoverActionBar = newHoverAB;
                    menu.m_hoverActionIndex = hitBtn;
                    menu.UpdateActionBar();
                }
            }

            menu.UpdateGuideText();
            return;
        }

        auto* button = event->AsButtonEvent();
        if (!button) return;
        auto key = button->GetIDCode();

        if ((key == MOUSE_WHEEL_UP || key == MOUSE_WHEEL_DOWN) && button->IsDown()) {
            if (ConfirmDialog::IsOpen() || HoldRemove::IsHolding() || ActionBar::IsDefaultsHolding()) return;
            int dir = (key == MOUSE_WHEEL_UP) ? -1 : 1;
            if (Dropdown::IsAnyOpen()) {
                auto* dd = Dropdown::GetOpen();
                if (dir == -1) dd->Prev(); else dd->Next();
            } else {
                FilterPanel::OnScrollWheel(dir);
            }
            return;
        }
        if (key == MOUSE_LEFT_BUTTON) {
            if (button->IsDown()) {
                auto [mx, my] = FilterPanel::GetMousePos();

                // ConfirmDialog takes top priority
                if (ConfirmDialog::IsOpen()) {
                    int btnHit = ConfirmDialog::HitTest(mx, my);
                    if (btnHit >= 0) ConfirmDialog::Close(btnHit);
                    else ConfirmDialog::Cancel();
                    return;
                }

                // Cancel active holds on any click outside them
                if (ActionBar::IsDefaultsHolding()) { ActionBar::CancelDefaultsHold(); return; }

                // ActionBar hit test (before panel dispatch)
                {
                    int hitBtn = ActionBar::HitTest(mx, my);
                    if (hitBtn >= 0) {
                        CatchAllPanel::Deselect();
                        menu.m_focus = FocusTarget::kActionBar;
                        menu.m_actionIndex = hitBtn;
                        menu.UpdateActionBar();
                        ActivateButton(menu, hitBtn);
                        return;
                    }
                }

                // Dropdown takes priority over all panel clicks
                if (Dropdown::IsAnyOpen()) {
                    Dropdown::GetOpen()->OnMouseClick(mx, my);
                    return;
                }

                // Check CatchAllPanel
                int catchAllIdx = -1;
                auto catchAllZone = CatchAllPanel::HitTest(mx, my, catchAllIdx);
                if (catchAllZone == CatchAllPanel::HitZone::kChestIcon) {
                    menu.m_focus = FocusTarget::kCatchAllPanel;
                    CatchAllPanel::Select();
                    CatchAllPanel::OpenLinkedContainer();
                    menu.UpdateActionBar();
                    menu.UpdateGuideText();
                    return;
                }
                if (catchAllZone == CatchAllPanel::HitZone::kRow) {
                    menu.m_focus = FocusTarget::kCatchAllPanel;
                    CatchAllPanel::Select();
                    FilterPanel::ClearHover();
                    menu.UpdateActionBar();
                    menu.UpdateGuideText();
                    return;
                }

                // FilterPanel handles its own rows
                auto result = FilterPanel::OnMouseDown();
                if (result.signal == FilterPanel::MouseSignal::kFocusToPanel) {
                    CatchAllPanel::Deselect();
                    menu.m_focus = FocusTarget::kFilterPanel;
                    menu.UpdateActionBar();
                    menu.UpdateGuideText();
                }
            } else if (button->IsUp()) {
                FilterPanel::OnMouseUp();
            }
            return;
        }
        if (key == MOUSE_RIGHT_BUTTON && button->IsDown()) {
            if (ConfirmDialog::IsOpen()) { ConfirmDialog::Cancel(); return; }
            if (ActionBar::IsDefaultsHolding()) { ActionBar::CancelDefaultsHold(); return; }
            if (Dropdown::IsAnyOpen()) {
                Dropdown::GetOpen()->Cancel();
            } else {
                FilterPanel::OnRightClick();
            }
            return;
        }
    }

    // --- Input parsing helpers ---

    InputHandler::ParsedInput InputHandler::ParseButton(uint32_t a_key, RE::INPUT_DEVICE a_device) {
        using Key = RE::BSKeyboardDevice::Key;
        using namespace ScaleformUtil;
        ParsedInput p;
        if (a_device == RE::INPUT_DEVICE::kGamepad) {
            p.up      = (a_key == GAMEPAD_DPAD_UP);
            p.down    = (a_key == GAMEPAD_DPAD_DOWN);
            p.left    = (a_key == GAMEPAD_DPAD_LEFT);
            p.right   = (a_key == GAMEPAD_DPAD_RIGHT);
            p.confirm = (a_key == GAMEPAD_A);
            p.cancel  = (a_key == GAMEPAD_B);
            p.action  = (a_key == GAMEPAD_X);
            p.liftToggle = (a_key == 0x0040);  // L3
        } else if (a_device == RE::INPUT_DEVICE::kKeyboard) {
            p.up      = (a_key == Key::kUp);
            p.down    = (a_key == Key::kDown);
            p.left    = (a_key == Key::kLeft);
            p.right   = (a_key == Key::kRight);
            p.confirm = (a_key == Key::kEnter);
            p.cancel  = (a_key == Key::kEscape);
            p.action  = (a_key == Key::kSpacebar);
            p.tab     = (a_key == Key::kTab);
        }
        return p;
    }

    void InputHandler::ActivateButton(ConfigMenu& a_menu, int a_index) {
        switch (a_index) {
            case ActionBar::BTN_WHOOSH:
                ActionBar::StartWhooshHold(a_index);
                break;
            case ActionBar::BTN_SORT:
                ActionBar::FlashButton(a_index);
                a_menu.RunSort();
                break;
            case ActionBar::BTN_SWEEP:
                ActionBar::FlashButton(a_index);
                a_menu.RunSweep();
                break;
            case ActionBar::BTN_DEFAULTS:
                ActionBar::StartDefaultsHold();
                break;
            case ActionBar::BTN_CLOSE:
                ActionBar::FlashButton(a_index);
                ConfigMenu::Hide();
                break;
        }
    }

    void InputHandler::NavigateVertical(ConfigMenu& a_menu, int a_dir) {
        if (a_dir == -1) {
            // Moving up
            switch (a_menu.m_focus) {
                case FocusTarget::kActionBar:
                    a_menu.m_focus = FocusTarget::kCatchAllPanel;
                    CatchAllPanel::Select();
                    break;
                case FocusTarget::kCatchAllPanel:
                    CatchAllPanel::Deselect();
                    a_menu.m_focus = FocusTarget::kFilterPanel;
                    FilterPanel::SelectLast();
                    break;
                case FocusTarget::kFilterPanel:
                    FilterPanel::SelectPrev();
                    break;
            }
        } else {
            // Moving down
            switch (a_menu.m_focus) {
                case FocusTarget::kFilterPanel: {
                    auto signal = FilterPanel::SelectNext();
                    if (signal == FilterPanel::FocusSignal::kToActionBar) {
                        FilterPanel::ClearSelection();
                        a_menu.m_focus = FocusTarget::kCatchAllPanel;
                        CatchAllPanel::Select();
                    }
                    break;
                }
                case FocusTarget::kCatchAllPanel:
                    CatchAllPanel::Deselect();
                    a_menu.m_focus = FocusTarget::kActionBar;
                    break;
                case FocusTarget::kActionBar:
                    break;  // already at bottom
            }
        }
        a_menu.UpdateActionBar();
        a_menu.UpdateGuideText();
    }

    RE::BSEventNotifyControl InputHandler::ProcessEvent(
        RE::InputEvent* const* a_event,
        [[maybe_unused]] RE::BSTEventSource<RE::InputEvent*>* a_source)
    {
        if (!a_event || !ConfigMenu::IsOpen() || !g_activeMenu) {
            return RE::BSEventNotifyControl::kContinue;
        }

        if (WhooshConfig::Menu::IsOpen()) {
            return RE::BSEventNotifyControl::kContinue;
        }

        auto& menu = *g_activeMenu;

        // Mouse pre-pass
        for (auto* event = *a_event; event; event = event->next) {
            if (event->GetDevice() == RE::INPUT_DEVICE::kMouse) {
                HandleMouseEvent(event);
            }
        }

        // Clear hover on gamepad/keyboard input
        for (auto* event = *a_event; event; event = event->next) {
            auto device = event->GetDevice();
            if (device == RE::INPUT_DEVICE::kGamepad || device == RE::INPUT_DEVICE::kKeyboard) {
                auto* button = event->AsButtonEvent();
                if ((button && button->IsDown()) || event->eventType == RE::INPUT_EVENT_TYPE::kThumbstick) {
                    FilterPanel::ClearHover();
                    CatchAllPanel::ClearHover();
                    menu.m_hoverActionBar = false;
                    menu.m_hoverActionIndex = -1;
                    break;
                }
            }
        }

        // --- Modal priority chain ---

        // Confirm dialog
        if (ConfirmDialog::IsOpen()) {
            for (auto* event = *a_event; event; event = event->next) {
                if (event->eventType == RE::INPUT_EVENT_TYPE::kThumbstick) {
                    auto* ts = static_cast<RE::ThumbstickEvent*>(event);
                    if (!ts->IsLeft()) continue;
                    auto edges = DirectionalInput::ProcessThumbstick(ts->xValue, ts->yValue, m_thumbState);
                    if (edges.left) ConfirmDialog::NavigateLeft();
                    if (edges.right) ConfirmDialog::NavigateRight();
                    continue;
                }
                auto* button = event->AsButtonEvent();
                if (!button || !button->IsDown()) continue;
                auto p = ParseButton(button->GetIDCode(), event->GetDevice());
                if (p.confirm)     ConfirmDialog::Confirm();
                else if (p.cancel) ConfirmDialog::Cancel();
                else if (p.left)   ConfirmDialog::NavigateLeft();
                else if (p.right)  ConfirmDialog::NavigateRight();
            }
            return RE::BSEventNotifyControl::kContinue;
        }

        // Hold-remove tracking
        if (HoldRemove::IsHolding()) {
            for (auto* event = *a_event; event; event = event->next) {
                auto* button = event->AsButtonEvent();
                if (!button) continue;
                auto p = ParseButton(button->GetIDCode(), event->GetDevice());
                if (p.action) {
                    if (button->IsUp()) {
                        HoldRemove::Cancel();
                        FilterPanel::SelectChest();
                    }
                } else if (button->IsDown()) {
                    HoldRemove::Cancel();
                }
            }
            return RE::BSEventNotifyControl::kContinue;
        }

        // Defaults hold tracking
        if (ActionBar::IsDefaultsHolding()) {
            for (auto* event = *a_event; event; event = event->next) {
                auto* button = event->AsButtonEvent();
                if (!button) continue;
                auto p = ParseButton(button->GetIDCode(), event->GetDevice());
                if (p.confirm) {
                    if (button->IsPressed()) {
                        ActionBar::UpdateDefaultsHold();
                        if (!ActionBar::IsDefaultsHolding()) {
                            ResetRepeat();
                            auto doReset = []() {
                                auto masterFormID = ConfigState::GetMasterFormID();
                                auto display = ContainerRegistry::GetSingleton()->Resolve(masterFormID);
                                CatchAllPanel::SetCatchAll(display.name.empty() ? "Master" : display.name, masterFormID, display.location, 0);
                                FilterPanel::BuildDefaultsAndCommit();
                                if (g_activeMenu) {
                                    g_activeMenu->m_focus = FocusTarget::kActionBar;
                                    g_activeMenu->m_actionIndex = FilterPanel::DEFAULTS_ACTION_INDEX;
                                    g_activeMenu->UpdateActionBar();
                                }
                            };
                            int totalItems = CountAllLinkedItems();
                            if (totalItems == 0) {
                                // No items — simple Yes/No
                                ConfirmDialog::Show(menu.uiMovie.get(),
                                    {.title = "Reset to defaults?", .buttons = {"Yes", "No"}, .popupW = 240.0, .defaultIndex = 1},
                                    [doReset](int idx) {
                                        if (idx == 0) {
                                            logger::info("Defaults: confirmed (no items)");
                                            doReset();
                                        }
                                        InputHandler::ResetRepeat();
                                    });
                            } else {
                                // Items present — contextual 3-option dialog
                                int containerCount = CountLinkedContainers();
                                std::string title = "Reset to defaults? " + std::to_string(totalItems) +
                                    " items across " + std::to_string(containerCount) + " containers.";
                                auto networkName = ConfigState::GetNetworkName();
                                ConfirmDialog::Show(menu.uiMovie.get(),
                                    {.title = title,
                                     .buttons = {"Pull to master", "Leave items", "Cancel"},
                                     .popupW = 400.0,
                                     .defaultIndex = 2},
                                    [networkName, doReset](int idx) {
                                        if (idx == 0) {
                                            // Pull items to master, then reset
                                            logger::info("Defaults: pulling items to master before reset");
                                            Distributor::GatherToMaster(networkName);
                                            doReset();
                                        } else if (idx == 1) {
                                            // Leave items, just reset
                                            logger::info("Defaults: confirmed (leaving items)");
                                            doReset();
                                        }
                                        // idx == 2: Cancel — no-op
                                        InputHandler::ResetRepeat();
                                    });
                            }
                        }
                    } else if (button->IsUp()) {
                        ActionBar::CancelDefaultsHold();
                    }
                } else if (button->IsDown()) {
                    ActionBar::CancelDefaultsHold();
                }
            }
            return RE::BSEventNotifyControl::kContinue;
        }

        // Whoosh hold tracking
        if (ActionBar::IsWhooshHolding()) {
            for (auto* event = *a_event; event; event = event->next) {
                auto* button = event->AsButtonEvent();
                if (!button) continue;
                auto p = ParseButton(button->GetIDCode(), event->GetDevice());
                if (p.confirm) {
                    if (button->IsPressed()) {
                        ActionBar::UpdateWhooshHold();
                        if (!ActionBar::IsWhooshHolding()) {
                            auto* mgr = NetworkManager::GetSingleton();
                            auto* net = mgr->FindNetwork(ConfigState::GetNetworkName());
                            auto currentFilters = (net && net->whooshConfigured) ? net->whooshFilters : FilterRegistry::DefaultWhooshFilters();
                            WhooshConfig::Menu::Show(currentFilters, [](bool confirmed, std::unordered_set<std::string> filters) {
                                if (!confirmed) return;
                                NetworkManager::GetSingleton()->SetWhooshConfig(ConfigState::GetNetworkName(), filters);
                                logger::info("Whoosh: reconfigured via hold gesture");
                            });
                        }
                    } else if (button->IsUp()) {
                        bool wasPastDeadZone = ActionBar::IsWhooshPastDeadZone();
                        ActionBar::ReleaseWhooshHold();
                        if (!wasPastDeadZone) {
                            ActionBar::FlashButton(menu.m_actionIndex);
                            menu.RunWhoosh();
                        }
                    }
                } else if (button->IsDown()) {
                    ActionBar::CancelWhooshHold();
                }
            }
            return RE::BSEventNotifyControl::kContinue;
        }

        // Dropdown mode (replaces FilterDialogue + CatchAllPanel picker routing)
        if (Dropdown::IsAnyOpen()) {
            auto* dd = Dropdown::GetOpen();
            for (auto* event = *a_event; event; event = event->next) {
                if (event->eventType == RE::INPUT_EVENT_TYPE::kThumbstick) {
                    auto* ts = static_cast<RE::ThumbstickEvent*>(event);
                    if (!ts->IsLeft()) continue;
                    DirectionalInput::ProcessThumbstick(ts->xValue, ts->yValue, m_thumbState);
                    int vertDir = m_thumbState.up ? -1 : (m_thumbState.down ? 1 : 0);
                    if (DirectionalInput::ProcessRepeat(vertDir, m_repeatState)) {
                        if (vertDir == -1) dd->Prev(); else dd->Next();
                    }
                    continue;
                }
                auto* button = event->AsButtonEvent();
                if (!button) continue;
                auto p = ParseButton(button->GetIDCode(), event->GetDevice());
                if (p.up || p.down) {
                    int dir = p.up ? -1 : 1;
                    if (DirectionalInput::ProcessButtonRepeat(dir, button->IsDown(), button->IsPressed(), button->IsUp(), m_repeatState)) {
                        if (dir == -1) dd->Prev(); else dd->Next();
                    }
                    continue;
                }
                if (!button->IsDown()) continue;
                if (p.confirm)     dd->Confirm();
                else if (p.cancel) dd->Cancel();
            }
            return RE::BSEventNotifyControl::kContinue;
        }

        // --- Normal mode ---
        for (auto* event = *a_event; event; event = event->next) {
            bool subFocused = (menu.m_focus == FocusTarget::kFilterPanel && FilterPanel::IsSubFocused());

            // Thumbstick
            if (event->eventType == RE::INPUT_EVENT_TYPE::kThumbstick) {
                auto* ts = static_cast<RE::ThumbstickEvent*>(event);
                if (!ts->IsLeft()) continue;
                auto edges = DirectionalInput::ProcessThumbstick(ts->xValue, ts->yValue, m_thumbState);

                // Horizontal
                if ((edges.left || edges.right) && !m_thumbState.down && !m_thumbState.up) {
                    if (menu.InActionBar()) {
                        if (edges.left)  menu.m_actionIndex = std::max(0, menu.m_actionIndex - 1);
                        if (edges.right) menu.m_actionIndex = std::min(ActionBar::BTN_COUNT - 1, menu.m_actionIndex + 1);
                        menu.UpdateActionBar();
                        menu.UpdateGuideText();
                    } else if (menu.m_focus == FocusTarget::kFilterPanel) {
                        if (edges.right && !subFocused) {
                            FilterPanel::EnterRow();
                            menu.UpdateGuideText();
                        } else if (edges.left && subFocused) {
                            FilterPanel::ExitRow();
                            FilterPanel::CollapseRow();
                            menu.UpdateGuideText();
                        } else if (edges.left && !subFocused) {
                            FilterPanel::CollapseRow();
                            menu.UpdateGuideText();
                        }
                    }
                }

                // Vertical with repeat (sub-focus persists across row changes)
                int vertDir = m_thumbState.up ? -1 : (m_thumbState.down ? 1 : 0);
                if (DirectionalInput::ProcessRepeat(vertDir, m_repeatState)) {
                    NavigateVertical(menu, vertDir);
                }
                continue;
            }

            auto* button = event->AsButtonEvent();
            if (!button) continue;
            auto p = ParseButton(button->GetIDCode(), event->GetDevice());

            // L3 / liftToggle: always lift/drop
            if (p.liftToggle && button->IsDown()) {
                if (menu.m_focus == FocusTarget::kFilterPanel) {
                    FilterPanel::ExitRow();
                    FilterPanel::ToggleLift();
                    menu.UpdateGuideText();
                }
                continue;
            }

            // Tab: sub-focus navigation
            if (p.tab && button->IsDown()) {
                if (subFocused) {
                    FilterPanel::TabToNextChild();
                    menu.UpdateGuideText();
                }
                continue;
            }

            // Left/Right (no repeat)
            if ((p.left || p.right) && button->IsDown()) {
                if (menu.InActionBar()) {
                    if (p.left)  menu.m_actionIndex = std::max(0, menu.m_actionIndex - 1);
                    if (p.right) menu.m_actionIndex = std::min(ActionBar::BTN_COUNT - 1, menu.m_actionIndex + 1);
                    menu.UpdateActionBar();
                    menu.UpdateGuideText();
                } else if (menu.m_focus == FocusTarget::kFilterPanel) {
                    if (p.right && !subFocused) {
                        FilterPanel::EnterRow();
                        menu.UpdateGuideText();
                    } else if (p.left && subFocused) {
                        FilterPanel::ExitRow();
                        FilterPanel::CollapseRow();
                        menu.UpdateGuideText();
                    } else if (p.left && !subFocused) {
                        FilterPanel::CollapseRow();
                        menu.UpdateGuideText();
                    }
                }
                continue;
            }

            // Up/Down with repeat (sub-focus persists across row changes)
            if (p.up || p.down) {
                int dir = p.up ? -1 : 1;
                if (DirectionalInput::ProcessButtonRepeat(dir, button->IsDown(), button->IsPressed(), button->IsUp(), m_repeatState)) {
                    NavigateVertical(menu, dir);
                }
                continue;
            }

            // A/Enter: action bar activate, sub-focus activate, lift/drop/hold-A, or catch-all activate
            if (p.confirm) {
                if (menu.InActionBar()) {
                    if (button->IsDown()) ActivateButton(menu, menu.m_actionIndex);
                } else if (menu.m_focus == FocusTarget::kCatchAllPanel) {
                    if (button->IsDown()) {
                        if (CatchAllPanel::HasLinkedContainer()) {
                            CatchAllPanel::StartHoldA();
                        } else {
                            CatchAllPanel::Activate();  // open picker
                        }
                    } else if (button->IsUp()) {
                        if (CatchAllPanel::IsHoldingA()) {
                            CatchAllPanel::CancelHoldA();
                            CatchAllPanel::Activate();  // short press = open picker
                        }
                    }
                } else if (subFocused) {
                    if (button->IsDown()) FilterPanel::ActivateSubFocus();
                } else {
                    if (button->IsDown()) {
                        if (FilterPanel::SelectedRowNeedsHold()) FilterPanel::StartHoldA();
                        else FilterPanel::ToggleLift();
                        menu.UpdateGuideText();
                    } else if (button->IsUp()) {
                        if (FilterPanel::IsHoldingA()) {
                            FilterPanel::CancelHoldA();
                            FilterPanel::ToggleLift();
                            menu.UpdateGuideText();
                        }
                    }
                }
                continue;
            }

            // X/Space: edit/remove on filter rows, or chest icon on catch-all
            if (p.action && !menu.InActionBar()) {
                if (menu.m_focus == FocusTarget::kCatchAllPanel) {
                    if (button->IsDown()) {
                        CatchAllPanel::Activate();  // open picker
                    }
                } else if (FilterPanel::SelectedRowIsFilter()) {
                    if (button->IsDown()) FilterPanel::StartHoldRemove();
                    else if (button->IsUp() && HoldRemove::IsHolding()) {
                        HoldRemove::Cancel();
                        FilterPanel::SelectChest();
                    }
                } else if (button->IsDown()) {
                    FilterPanel::SelectChest();
                }
                continue;
            }

            // B/Escape: close menu or exit sub-focus
            if (p.cancel && button->IsDown()) {
                if (subFocused) {
                    FilterPanel::ExitRow();
                    menu.UpdateGuideText();
                } else {
                    ConfigMenu::Hide();
                }
            }
        }

        return RE::BSEventNotifyControl::kContinue;
    }
}
