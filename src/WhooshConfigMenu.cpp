#include "WhooshConfigMenu.h"
#include "FilterRegistry.h"
#include "ScaleformUtil.h"
#include "TranslationService.h"

#include <algorithm>

namespace WhooshConfig {

    // Active menu instance
    static Menu* g_activeMenu = nullptr;

    // Static state
    Menu::Callback Menu::s_callback;
    std::unordered_set<std::string> Menu::s_initialSet;

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
        depthPriority = 5;  // above ConfigMenu

        menuFlags.set(RE::UI_MENU_FLAGS::kPausesGame);
        menuFlags.set(RE::UI_MENU_FLAGS::kUsesMenuContext);
        menuFlags.set(RE::UI_MENU_FLAGS::kModal);
        menuFlags.set(RE::UI_MENU_FLAGS::kRequiresUpdate);
        menuFlags.set(RE::UI_MENU_FLAGS::kUsesCursor);

        inputContext = Context::kMenuMode;

        m_enabledFilters = s_initialSet;

        auto scaleform = RE::BSScaleformManager::GetSingleton();
        if (scaleform) {
            bool loaded = scaleform->LoadMovie(this, uiMovie, FILE_NAME.data());
            if (loaded && uiMovie) {
                logger::info("WhooshConfigMenu: loaded SWF");
            } else {
                logger::error("WhooshConfigMenu: failed to load SWF");
            }
        }
    }

    void Menu::PostCreate() {
        g_activeMenu = this;
        DrawPopup();
        // Start with OK button focused, grid cursor cleared
        m_grid.ClearCursor();
        m_grid.Update();
    }

    RE::UI_MESSAGE_RESULTS Menu::ProcessMessage(RE::UIMessage& a_message) {
        using Message = RE::UI_MESSAGE_TYPE;
        switch (*a_message.type) {
            case Message::kHide:
                if (g_activeMenu) {
                    g_activeMenu->m_grid.Destroy();
                }
                g_activeMenu = nullptr;
                return RE::UI_MESSAGE_RESULTS::kHandled;
            default:
                return RE::IMenu::ProcessMessage(a_message);
        }
    }

    // --- Show / Hide ---

    void Menu::Show(const std::unordered_set<std::string>& a_initialSet, Callback a_callback) {
        s_initialSet = a_initialSet;
        s_callback = std::move(a_callback);

        auto ui = RE::UI::GetSingleton();
        if (ui && !ui->IsMenuOpen(MENU_NAME)) {
            auto msgQueue = RE::UIMessageQueue::GetSingleton();
            if (msgQueue) {
                msgQueue->AddMessage(MENU_NAME, RE::UI_MESSAGE_TYPE::kShow, nullptr);
                logger::info("Opening WhooshConfigMenu");
            }
        }
    }

    void Menu::Hide() {
        auto ui = RE::UI::GetSingleton();
        if (ui && ui->IsMenuOpen(MENU_NAME)) {
            auto msgQueue = RE::UIMessageQueue::GetSingleton();
            if (msgQueue) {
                msgQueue->AddMessage(MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
                logger::info("Closing WhooshConfigMenu");
            }
        }
    }

    bool Menu::IsOpen() {
        auto ui = RE::UI::GetSingleton();
        return ui && ui->IsMenuOpen(MENU_NAME);
    }

    // --- Build grid items from active categories ---

    std::vector<ChecklistGrid::Item> Menu::BuildGridItems() const {
        auto* registry = FilterRegistry::GetSingleton();
        const auto& roots = registry->GetFamilyRoots();
        std::vector<ChecklistGrid::Item> items;

        constexpr int CHILD_INDENT = 12;

        for (const auto& rootID : roots) {
            auto* rootFilter = registry->GetFilter(rootID);
            if (!rootFilter) continue;

            const auto& children = registry->GetChildren(rootID);

            if (children.empty()) {
                // Single-member family — regular checkbox, no group fields
                items.push_back({
                    rootID,
                    std::string(rootFilter->GetDisplayName()),
                    std::string(rootFilter->GetDescription()),
                    m_enabledFilters.count(rootID) > 0
                });
            } else {
                // Multi-member family — root as group header + children
                int rootIdx = static_cast<int>(items.size());
                ChecklistGrid::Item rootItem;
                rootItem.id = rootID;
                rootItem.label = std::string(rootFilter->GetDisplayName());
                rootItem.description = std::string(rootFilter->GetDescription());
                rootItem.isGroupRoot = true;
                // Root tri-state: checked (all), partial (some), unchecked (none)
                bool anyChecked = false;
                bool allChecked = true;
                for (const auto& childID : children) {
                    if (m_enabledFilters.count(childID) > 0) {
                        anyChecked = true;
                    } else {
                        allChecked = false;
                    }
                }
                rootItem.checked = allChecked && anyChecked;
                rootItem.partial = anyChecked && !allChecked;
                items.push_back(std::move(rootItem));

                for (const auto& childID : children) {
                    auto* childFilter = registry->GetFilter(childID);
                    if (!childFilter) continue;

                    int childIdx = static_cast<int>(items.size());
                    items[rootIdx].groupChildren.push_back(childIdx);

                    ChecklistGrid::Item childItem;
                    childItem.id = childID;
                    childItem.label = std::string(childFilter->GetDisplayName());
                    childItem.description = std::string(childFilter->GetDescription());
                    childItem.checked = m_enabledFilters.count(childID) > 0;
                    childItem.groupParent = rootIdx;
                    childItem.indent = CHILD_INDENT;
                    items.push_back(std::move(childItem));
                }
            }
        }

        return items;
    }

    // --- Drawing ---

    void Menu::DrawPopup() {
        constexpr int    BASE_DEPTH = 110;
        constexpr double HEADER_H   = 66.0;
        constexpr double FOOTER_H   = 16.0 + 20.0 + 12.0 + 28.0 + 16.0;  // guideGap + guide + btnGap + btn + bottomPad

        // Dim overlay
        ScaleformUtil::DrawFilledRect(uiMovie.get(), "_dimOverlay", 100, 0.0, 0.0, 1280.0, 720.0, 0x000000, ALPHA_DIM);

        // --- Auto-expand columns to fit grid within height budget ---
        int cols = MIN_COLS;
        double gridH = 0.0;
        ChecklistGrid::Config gridCfg;

        for (; cols <= MAX_COLS; ++cols) {
            gridCfg.columns = cols;
            gridCfg.maxVisibleRows = 0;
            m_grid = ChecklistGrid::Grid(uiMovie.get(), "_wcG", BASE_DEPTH);
            m_grid.SetConfig(gridCfg);
            m_grid.SetItems(BuildGridItems());
            gridH = m_grid.GetComputedHeight();
            if (gridH <= MAX_GRID_H) break;
        }
        cols = std::min(cols, MAX_COLS);

        // Scroll fallback if still too tall at max columns
        double visibleGridH = gridH;
        if (gridH > MAX_GRID_H) {
            int maxVisRows = static_cast<int>(MAX_GRID_H / gridCfg.rowHeight);
            gridCfg.maxVisibleRows = maxVisRows;
            m_grid = ChecklistGrid::Grid(uiMovie.get(), "_wcG", BASE_DEPTH);
            m_grid.SetConfig(gridCfg);
            m_grid.SetItems(BuildGridItems());
            visibleGridH = maxVisRows * gridCfg.rowHeight;
        }

        // Overlay depths above any grid content (3 layers per item + scrollbar margin)
        m_overlayDepth = BASE_DEPTH + 3 * m_grid.GetItemCount() + 10;

        // --- Compute popup dimensions from chosen layout ---
        m_popupW = cols * COL_W + GRID_PAD;
        m_popupH = HEADER_H + visibleGridH + FOOTER_H;

        m_popupX = (1280.0 - m_popupW) / 2.0;
        m_popupY = (720.0 - m_popupH) / 2.0;

        // Background
        ScaleformUtil::DrawFilledRect(uiMovie.get(), "_whooshBg", 101, m_popupX, m_popupY, m_popupW, m_popupH, COLOR_BG, ALPHA_BG);
        ScaleformUtil::DrawBorderRect(uiMovie.get(), "_whooshBorder", 102, m_popupX, m_popupY, m_popupW, m_popupH, COLOR_BORDER);

        // Title
        std::string title = T("$SLID_WhooshCategories");
        ScaleformUtil::CreateLabel(uiMovie.get(), "_whooshTitle", 103, m_popupX + 20.0, m_popupY + 12.0,
                   m_popupW - 40.0, 24.0, title.c_str(), 16, COLOR_TITLE);

        // Subtitle
        std::string subtitle = T("$SLID_WhooshCategoriesSubtitle");
        ScaleformUtil::CreateLabel(uiMovie.get(), "_whooshSubtitle", 104, m_popupX + 20.0, m_popupY + 36.0,
                   m_popupW - 40.0, 18.0, subtitle.c_str(), 11, COLOR_SUBTITLE);

        // Grid area
        m_gridStartX = m_popupX + 24.0;
        m_gridStartY = m_popupY + HEADER_H;
        m_grid.Draw(m_gridStartX, m_gridStartY);

        // Guide text area (positioned dynamically below visible grid)
        m_guideY = m_gridStartY + visibleGridH + 16.0;
        DrawGuideText();

        // Buttons
        m_btnY = m_popupY + m_popupH - 44.0;
        double totalBtnW = BTN_COUNT * BTN_W + (BTN_COUNT - 1) * BTN_GAP;
        m_btnStartX = m_popupX + (m_popupW - totalBtnW) / 2.0;

        DrawButtons();
    }

    void Menu::DrawGuideText() {
        // Guide text shows category description for current selection
        auto* item = m_grid.GetCursorItem();
        std::string guideStr = item ? item->description : "";

        // Remove old
        RE::GFxValue root;
        uiMovie->GetVariable(&root, "_root");
        if (!root.IsUndefined()) {
            RE::GFxValue existing;
            root.GetMember("_wcGuide", &existing);
            if (!existing.IsUndefined()) {
                existing.Invoke("removeMovieClip", nullptr, nullptr, 0);
            }
        }

        if (m_inGrid && !guideStr.empty()) {
            ScaleformUtil::CreateLabel(uiMovie.get(), "_wcGuide", m_overlayDepth, m_popupX + 24.0, m_guideY,
                       m_popupW - 48.0, 18.0, guideStr.c_str(), 11, COLOR_GUIDE);
        }
    }

    void Menu::DrawButtons() {
        std::string btnLabels[BTN_COUNT] = {
            T("$SLID_OK"),
            T("$SLID_WhooshDefault"),
            T("$SLID_WhooshClear"),
            T("$SLID_Cancel")
        };

        for (int i = 0; i < BTN_COUNT; ++i) {
            double bx = m_btnStartX + i * (BTN_W + BTN_GAP);

            bool selected = (!m_inGrid && m_btnIndex == i);
            bool hovered = (m_hoverBtnIndex == i);

            uint32_t bgColor = selected ? COLOR_BTN_SELECT : (hovered ? COLOR_BTN_HOVER : COLOR_BTN_NORMAL);
            int bgAlpha = selected ? ALPHA_BTN_SELECT : (hovered ? ALPHA_BTN_HOVER : ALPHA_BTN_NORMAL);

            std::string bgName = "_wcBtn" + std::to_string(i);
            ScaleformUtil::DrawFilledRect(uiMovie.get(),bgName.c_str(), m_overlayDepth + 10 + i, bx, m_btnY, BTN_W, BTN_H, bgColor, bgAlpha);

            std::string lblName = "_wcBtnLbl" + std::to_string(i);
            ScaleformUtil::CreateLabel(uiMovie.get(),lblName.c_str(), m_overlayDepth + 20 + i, bx, m_btnY + 4.0,
                       BTN_W, BTN_H, btnLabels[i].c_str(), 13, COLOR_BTN_LABEL);

            // Center-align button text
            std::string lblPath = std::string("_root.") + lblName;
            RE::GFxValue tf;
            uiMovie->GetVariable(&tf, lblPath.c_str());
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

    void Menu::UpdateButtons() {
        DrawButtons();
    }

    void Menu::UpdateGuideText() {
        DrawGuideText();
    }

    // --- Navigation ---

    void Menu::NavigateUp() {
        if (!g_activeMenu) return;
        auto& menu = *g_activeMenu;

        if (menu.m_inGrid) {
            if (!menu.m_grid.IsAtTop()) {
                menu.m_grid.NavigateUp();
                menu.m_grid.Update();
            }
        } else {
            // Jump from buttons to grid — restore cursor to bottom of grid
            menu.m_inGrid = true;
            menu.m_grid.NavigateToBottom();
            menu.m_grid.Update();
        }
        menu.UpdateButtons();
        menu.UpdateGuideText();
    }

    void Menu::NavigateDown() {
        if (!g_activeMenu) return;
        auto& menu = *g_activeMenu;

        if (menu.m_inGrid) {
            if (!menu.m_grid.IsAtBottom()) {
                menu.m_grid.NavigateDown();
                menu.m_grid.Update();
            } else {
                // Drop to button bar — clear grid cursor highlight
                menu.m_inGrid = false;
                menu.m_grid.ClearCursor();
                menu.m_grid.Update();
            }
        }
        menu.UpdateButtons();
        menu.UpdateGuideText();
    }

    void Menu::NavigateLeft() {
        if (!g_activeMenu) return;
        auto& menu = *g_activeMenu;

        if (menu.m_inGrid) {
            menu.m_grid.NavigateLeft();
            menu.m_grid.Update();
        } else {
            menu.m_btnIndex = (menu.m_btnIndex > 0) ? menu.m_btnIndex - 1 : BTN_COUNT - 1;
        }
        menu.UpdateButtons();
        menu.UpdateGuideText();
    }

    void Menu::NavigateRight() {
        if (!g_activeMenu) return;
        auto& menu = *g_activeMenu;

        if (menu.m_inGrid) {
            menu.m_grid.NavigateRight();
            menu.m_grid.Update();
        } else {
            menu.m_btnIndex = (menu.m_btnIndex < BTN_COUNT - 1) ? menu.m_btnIndex + 1 : 0;
        }
        menu.UpdateButtons();
        menu.UpdateGuideText();
    }

    void Menu::ToggleCheck() {
        if (!g_activeMenu) return;
        auto& menu = *g_activeMenu;

        if (!menu.m_inGrid) {
            // In button bar — activate the button
            switch (menu.m_btnIndex) {
                case 0: Confirm(); break;
                case 1: SetDefault(); break;
                case 2: ClearAll(); break;
                case 3: Cancel(); break;
            }
            return;
        }

        menu.m_grid.Toggle();
        menu.m_enabledFilters = menu.m_grid.GetCheckedIDs();
        menu.m_grid.Update();
    }

    void Menu::Confirm() {
        if (!g_activeMenu) return;
        auto filters = g_activeMenu->m_enabledFilters;
        auto cb = s_callback;
        Hide();
        if (cb) cb(true, std::move(filters));
    }

    void Menu::Cancel() {
        auto cb = s_callback;
        Hide();
        if (cb) cb(false, {});
    }

    void Menu::SetDefault() {
        if (!g_activeMenu) return;
        g_activeMenu->m_enabledFilters = FilterRegistry::DefaultWhooshFilters();
        g_activeMenu->m_grid.SetCheckedIDs(g_activeMenu->m_enabledFilters);
        g_activeMenu->m_grid.Update();
    }

    void Menu::ClearAll() {
        if (!g_activeMenu) return;
        g_activeMenu->m_enabledFilters.clear();
        g_activeMenu->m_grid.SetAll(false);
        g_activeMenu->m_grid.Update();
    }

    // --- Mouse ---

    std::pair<float, float> Menu::GetMousePos() const {
        if (!uiMovie) return {0.0f, 0.0f};

        RE::GFxValue xVal, yVal;
        uiMovie->GetVariable(&xVal, "_root._xmouse");
        uiMovie->GetVariable(&yVal, "_root._ymouse");

        float mx = xVal.IsNumber() ? static_cast<float>(xVal.GetNumber()) : 0.0f;
        float my = yVal.IsNumber() ? static_cast<float>(yVal.GetNumber()) : 0.0f;
        return {mx, my};
    }

    void Menu::OnMouseMove() {
        if (!g_activeMenu) return;
        auto& menu = *g_activeMenu;

        auto [mx, my] = menu.GetMousePos();
        int oldHoverBtn = menu.m_hoverBtnIndex;

        // Grid hover
        if (menu.m_grid.UpdateHover(mx, my)) {
            menu.m_grid.Update();
            menu.m_inGrid = true;
            menu.UpdateGuideText();
        }

        // Button hover
        menu.m_hoverBtnIndex = -1;
        for (int i = 0; i < BTN_COUNT; ++i) {
            double bx = menu.m_btnStartX + i * (BTN_W + BTN_GAP);
            if (mx >= bx && mx < bx + BTN_W && my >= menu.m_btnY && my < menu.m_btnY + BTN_H) {
                menu.m_hoverBtnIndex = i;
            }
        }

        if (menu.m_hoverBtnIndex != oldHoverBtn) {
            menu.UpdateButtons();
        }
    }

    void Menu::OnMouseDown() {
        if (!g_activeMenu) return;
        auto& menu = *g_activeMenu;

        auto [mx, my] = menu.GetMousePos();

        // Grid click
        if (menu.m_grid.HandleClick(mx, my)) {
            menu.m_enabledFilters = menu.m_grid.GetCheckedIDs();
            menu.m_inGrid = true;
            menu.m_grid.Update();
            return;
        }

        // Button click
        for (int i = 0; i < BTN_COUNT; ++i) {
            double bx = menu.m_btnStartX + i * (BTN_W + BTN_GAP);
            if (mx >= bx && mx < bx + BTN_W && my >= menu.m_btnY && my < menu.m_btnY + BTN_H) {
                switch (i) {
                    case 0: Confirm(); break;
                    case 1: SetDefault(); break;
                    case 2: ClearAll(); break;
                    case 3: Cancel(); break;
                }
                return;
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
            logger::info("WhooshConfig::InputHandler registered");
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

            // Left thumbstick navigation (with repeat on vertical)
            if (event->eventType == RE::INPUT_EVENT_TYPE::kThumbstick) {
                auto* thumbstick = static_cast<RE::ThumbstickEvent*>(event);
                if (!thumbstick->IsLeft()) continue;

                auto edges = DirectionalInput::ProcessThumbstick(
                    thumbstick->xValue, thumbstick->yValue, m_thumbState);

                // Horizontal: edge-only (no repeat)
                if (edges.left)  Menu::NavigateLeft();
                if (edges.right) Menu::NavigateRight();

                // Vertical: with repeat
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
                if (key == 0 && isDown) {  // Left click
                    Menu::OnMouseDown();
                }
                continue;
            }

            // Gamepad
            if (device == RE::INPUT_DEVICE::kGamepad) {
                // Vertical D-pad: repeat
                if (key == ScaleformUtil::GAMEPAD_DPAD_UP || key == ScaleformUtil::GAMEPAD_DPAD_DOWN) {
                    int dir = (key == ScaleformUtil::GAMEPAD_DPAD_UP) ? -1 : 1;
                    if (DirectionalInput::ProcessButtonRepeat(dir, isDown, isPressed, isUp, m_repeatState)) {
                        if (dir == -1) Menu::NavigateUp();
                        else           Menu::NavigateDown();
                    }
                    continue;
                }
                // Horizontal D-pad + action buttons: edge-only
                if (!isDown) continue;
                switch (key) {
                    case ScaleformUtil::GAMEPAD_DPAD_LEFT:  Menu::NavigateLeft(); break;
                    case ScaleformUtil::GAMEPAD_DPAD_RIGHT: Menu::NavigateRight(); break;
                    case ScaleformUtil::GAMEPAD_A:          Menu::ToggleCheck(); break;
                    case ScaleformUtil::GAMEPAD_B:          Menu::Cancel(); break;
                }
                continue;
            }

            // Keyboard
            if (device == RE::INPUT_DEVICE::kKeyboard) {
                using Key = RE::BSKeyboardDevice::Key;
                // Vertical arrows: repeat
                if (key == Key::kUp || key == Key::kDown) {
                    int dir = (key == Key::kUp) ? -1 : 1;
                    if (DirectionalInput::ProcessButtonRepeat(dir, isDown, isPressed, isUp, m_repeatState)) {
                        if (dir == -1) Menu::NavigateUp();
                        else           Menu::NavigateDown();
                    }
                    continue;
                }
                // Horizontal + action keys: edge-only
                if (!isDown) continue;
                switch (key) {
                    case Key::kLeft:     Menu::NavigateLeft(); break;
                    case Key::kRight:    Menu::NavigateRight(); break;
                    case Key::kEnter:    Menu::ToggleCheck(); break;
                    case Key::kSpacebar: Menu::ToggleCheck(); break;
                    case Key::kEscape:   Menu::Cancel(); break;
                }
                continue;
            }
        }

        return RE::BSEventNotifyControl::kContinue;
    }
}
