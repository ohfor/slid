#include "WelcomeMenu.h"
#include "ScaleformUtil.h"
#include "Settings.h"
#include "TranslationService.h"

namespace WelcomeMenu {

    static Menu* g_activeMenu = nullptr;

    // =========================================================================
    // Menu
    // =========================================================================

    Menu::Menu() {
        depthPriority = 5;

        menuFlags.set(RE::UI_MENU_FLAGS::kPausesGame);
        menuFlags.set(RE::UI_MENU_FLAGS::kUsesMenuContext);
        menuFlags.set(RE::UI_MENU_FLAGS::kModal);
        menuFlags.set(RE::UI_MENU_FLAGS::kUsesCursor);

        inputContext = Context::kMenuMode;

        auto scaleform = RE::BSScaleformManager::GetSingleton();
        if (scaleform) {
            bool loaded = scaleform->LoadMovie(this, uiMovie, FILE_NAME.data());
            if (loaded && uiMovie) {
                logger::info("WelcomeMenu: loaded SWF {}", FILE_NAME);
            } else {
                logger::error("WelcomeMenu: failed to load SWF {}", FILE_NAME);
            }
        }
    }

    void Menu::Register() {
        auto ui = RE::UI::GetSingleton();
        if (ui) {
            ui->Register(MENU_NAME, Create);
            logger::info("WelcomeMenu registered");
        }
    }

    RE::IMenu* Menu::Create() {
        return new Menu();
    }

    void Menu::Show() {
        auto ui = RE::UI::GetSingleton();
        if (ui && !ui->IsMenuOpen(MENU_NAME)) {
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
        switch (a_message.type.get()) {
            case RE::UI_MESSAGE_TYPE::kShow: {
                g_activeMenu = this;
                if (uiMovie) {
                    uiMovie->GetVariable(&m_root, "_root");
                    BuildUI();
                }
                return RE::UI_MESSAGE_RESULTS::kHandled;
            }
            case RE::UI_MESSAGE_TYPE::kHide: {
                if (m_dontShowAgain) {
                    Settings::bShownWelcomeTutorial = true;
                    Settings::Save();
                }
                g_activeMenu = nullptr;
                return RE::UI_MESSAGE_RESULTS::kHandled;
            }
            default:
                return RE::IMenu::ProcessMessage(a_message);
        }
    }

    void Menu::BuildUI() {
        if (!uiMovie) return;

        // Screen dimensions
        RE::GRectF rect = uiMovie->GetVisibleFrameRect();
        float screenW = rect.right - rect.left;
        float screenH = rect.bottom - rect.top;
        if (screenW <= 0) screenW = 1280.0f;
        if (screenH <= 0) screenH = 720.0f;

        float panelX = (screenW - POPUP_W) / 2.0f;
        float panelY = (screenH - POPUP_H) / 2.0f;

        // Background (full screen dim)
        ScaleformUtil::DrawFilledRect(uiMovie.get(), "_dim", 1, 0.0, 0.0,
            static_cast<double>(screenW), static_cast<double>(screenH), 0x000000, 50);

        // Create panel clip
        RE::GFxValue panelArgs[2];
        panelArgs[0].SetString("welcomePanel");
        panelArgs[1].SetNumber(100);
        m_root.Invoke("createEmptyMovieClip", &m_panel, panelArgs, 2);

        RE::GFxValue posX, posY;
        posX.SetNumber(static_cast<double>(panelX));
        posY.SetNumber(static_cast<double>(panelY));
        m_panel.SetMember("_x", posX);
        m_panel.SetMember("_y", posY);

        // Panel background using Drawing API on the panel clip
        {
            RE::GFxValue fillArgs[2];
            fillArgs[0].SetNumber(static_cast<double>(COLOR_BG));
            fillArgs[1].SetNumber(95.0);
            m_panel.Invoke("beginFill", nullptr, fillArgs, 2);

            RE::GFxValue pt[2];
            pt[0].SetNumber(0.0); pt[1].SetNumber(0.0);
            m_panel.Invoke("moveTo", nullptr, pt, 2);
            pt[0].SetNumber(static_cast<double>(POPUP_W));
            m_panel.Invoke("lineTo", nullptr, pt, 2);
            pt[1].SetNumber(static_cast<double>(POPUP_H));
            m_panel.Invoke("lineTo", nullptr, pt, 2);
            pt[0].SetNumber(0.0);
            m_panel.Invoke("lineTo", nullptr, pt, 2);
            pt[1].SetNumber(0.0);
            m_panel.Invoke("lineTo", nullptr, pt, 2);

            m_panel.Invoke("endFill", nullptr, nullptr, 0);
        }

        // Border
        {
            RE::GFxValue styleArgs[3];
            styleArgs[0].SetNumber(2.0);
            styleArgs[1].SetNumber(static_cast<double>(COLOR_BORDER));
            styleArgs[2].SetNumber(100.0);
            m_panel.Invoke("lineStyle", nullptr, styleArgs, 3);

            RE::GFxValue pt[2];
            pt[0].SetNumber(0.0); pt[1].SetNumber(0.0);
            m_panel.Invoke("moveTo", nullptr, pt, 2);
            pt[0].SetNumber(static_cast<double>(POPUP_W));
            m_panel.Invoke("lineTo", nullptr, pt, 2);
            pt[1].SetNumber(static_cast<double>(POPUP_H));
            m_panel.Invoke("lineTo", nullptr, pt, 2);
            pt[0].SetNumber(0.0);
            m_panel.Invoke("lineTo", nullptr, pt, 2);
            pt[1].SetNumber(0.0);
            m_panel.Invoke("lineTo", nullptr, pt, 2);
        }

        // Inner accent line
        {
            RE::GFxValue styleArgs[3];
            styleArgs[0].SetNumber(1.0);
            styleArgs[1].SetNumber(static_cast<double>(COLOR_BORDER));
            styleArgs[2].SetNumber(30.0);
            m_panel.Invoke("lineStyle", nullptr, styleArgs, 3);

            RE::GFxValue pt[2];
            pt[0].SetNumber(4.0); pt[1].SetNumber(4.0);
            m_panel.Invoke("moveTo", nullptr, pt, 2);
            pt[0].SetNumber(static_cast<double>(POPUP_W - 4.0));
            m_panel.Invoke("lineTo", nullptr, pt, 2);
            pt[1].SetNumber(static_cast<double>(POPUP_H - 4.0));
            m_panel.Invoke("lineTo", nullptr, pt, 2);
            pt[0].SetNumber(4.0);
            m_panel.Invoke("lineTo", nullptr, pt, 2);
            pt[1].SetNumber(4.0);
            m_panel.Invoke("lineTo", nullptr, pt, 2);
        }

        CreateHeader();
        CreateBody();
        CreateCheckbox();
        CreateButton();
    }

    void Menu::CreateTextField(RE::GFxValue& a_parent, const char* a_name, int a_depth,
                               double a_x, double a_y, double a_w, double a_h) {
        RE::GFxValue tfArgs[6];
        tfArgs[0].SetString(a_name);
        tfArgs[1].SetNumber(static_cast<double>(a_depth));
        tfArgs[2].SetNumber(a_x);
        tfArgs[3].SetNumber(a_y);
        tfArgs[4].SetNumber(a_w);
        tfArgs[5].SetNumber(a_h);
        a_parent.Invoke("createTextField", nullptr, tfArgs, 6);
    }

    void Menu::SetTextFormat(RE::GFxValue& a_textField, const char* a_font, int a_size,
                             uint32_t a_color, const char* a_align, bool a_multiline, bool a_wordWrap) {
        if (!uiMovie) return;

        RE::GFxValue fmt;
        uiMovie->CreateObject(&fmt, "TextFormat");
        if (fmt.IsUndefined()) return;

        RE::GFxValue fontVal, sizeVal, colorVal, alignVal;
        fontVal.SetString(a_font);
        fmt.SetMember("font", fontVal);
        sizeVal.SetNumber(static_cast<double>(a_size));
        fmt.SetMember("size", sizeVal);
        colorVal.SetNumber(static_cast<double>(a_color));
        fmt.SetMember("color", colorVal);
        alignVal.SetString(a_align);
        fmt.SetMember("align", alignVal);

        RE::GFxValue fmtArgs[1];
        fmtArgs[0] = fmt;
        a_textField.Invoke("setTextFormat", nullptr, fmtArgs, 1);
        a_textField.Invoke("setNewTextFormat", nullptr, fmtArgs, 1);

        RE::GFxValue embedVal;
        embedVal.SetBoolean(true);
        a_textField.SetMember("embedFonts", embedVal);

        if (a_multiline) {
            RE::GFxValue mlVal;
            mlVal.SetBoolean(true);
            a_textField.SetMember("multiline", mlVal);
        }

        if (a_wordWrap) {
            RE::GFxValue wwVal;
            wwVal.SetBoolean(true);
            a_textField.SetMember("wordWrap", wwVal);
        }

        RE::GFxValue selVal;
        selVal.SetBoolean(false);
        a_textField.SetMember("selectable", selVal);
    }

    void Menu::CreateHeader() {
        float y = PADDING;

        // Main title
        CreateTextField(m_panel, "titleText", 10, PADDING, y, POPUP_W - (PADDING * 2), HEADER_SIZE + 4);

        RE::GFxValue titleField;
        m_panel.GetMember("titleText", &titleField);
        if (!titleField.IsUndefined()) {
            SetTextFormat(titleField, "Arial", static_cast<int>(HEADER_SIZE), COLOR_HEADER, "center", false, false);

            RE::GFxValue textVal;
            std::string title = T("$SLID_WelcomeTitle");
            textVal.SetString(title.c_str());
            titleField.SetMember("htmlText", textVal);
        }
    }

    void Menu::CreateBody() {
        float y = PADDING + HEADER_SIZE + 20.0f;
        float textW = POPUP_W - (PADDING * 2);
        float textX = PADDING;
        int depth = 20;

        // Section 1: Your Link
        {
            std::string name = "sub1";
            CreateTextField(m_panel, name.c_str(), depth++, textX, y, textW, SUBHEADER_SIZE + 4);
            RE::GFxValue field;
            m_panel.GetMember(name.c_str(), &field);
            if (!field.IsUndefined()) {
                SetTextFormat(field, "Arial", static_cast<int>(SUBHEADER_SIZE), COLOR_SUBHEADER, "left", false, false);
                RE::GFxValue textVal;
                std::string text = T("$SLID_WelcomeYourLink");
                textVal.SetString(text.c_str());
                field.SetMember("text", textVal);
            }
            y += SUBHEADER_SIZE + 8.0f;

            name = "body1";
            CreateTextField(m_panel, name.c_str(), depth++, textX, y, textW, LINE_HEIGHT * 3);
            m_panel.GetMember(name.c_str(), &field);
            if (!field.IsUndefined()) {
                SetTextFormat(field, "Arial", static_cast<int>(BODY_SIZE), COLOR_BODY, "left", true, true);
                RE::GFxValue textVal;
                std::string text = T("$SLID_WelcomeYourLinkBody");
                textVal.SetString(text.c_str());
                field.SetMember("text", textVal);
            }
            y += LINE_HEIGHT * 3 + SECTION_GAP;
        }

        // Section 2: Sorting
        {
            std::string name = "sub2";
            CreateTextField(m_panel, name.c_str(), depth++, textX, y, textW, SUBHEADER_SIZE + 4);
            RE::GFxValue field;
            m_panel.GetMember(name.c_str(), &field);
            if (!field.IsUndefined()) {
                SetTextFormat(field, "Arial", static_cast<int>(SUBHEADER_SIZE), COLOR_SUBHEADER, "left", false, false);
                RE::GFxValue textVal;
                std::string text = T("$SLID_WelcomeSorting");
                textVal.SetString(text.c_str());
                field.SetMember("text", textVal);
            }
            y += SUBHEADER_SIZE + 8.0f;

            name = "body2";
            CreateTextField(m_panel, name.c_str(), depth++, textX, y, textW, LINE_HEIGHT * 3);
            m_panel.GetMember(name.c_str(), &field);
            if (!field.IsUndefined()) {
                SetTextFormat(field, "Arial", static_cast<int>(BODY_SIZE), COLOR_BODY, "left", true, true);
                RE::GFxValue textVal;
                std::string text = T("$SLID_WelcomeSortingBody");
                textVal.SetString(text.c_str());
                field.SetMember("text", textVal);
            }
            y += LINE_HEIGHT * 4 + SECTION_GAP;
        }

        // Section 3: Selling
        {
            std::string name = "sub3";
            CreateTextField(m_panel, name.c_str(), depth++, textX, y, textW, SUBHEADER_SIZE + 4);
            RE::GFxValue field;
            m_panel.GetMember(name.c_str(), &field);
            if (!field.IsUndefined()) {
                SetTextFormat(field, "Arial", static_cast<int>(SUBHEADER_SIZE), COLOR_SUBHEADER, "left", false, false);
                RE::GFxValue textVal;
                std::string text = T("$SLID_WelcomeSelling");
                textVal.SetString(text.c_str());
                field.SetMember("text", textVal);
            }
            y += SUBHEADER_SIZE + 8.0f;

            name = "body3";
            CreateTextField(m_panel, name.c_str(), depth++, textX, y, textW, LINE_HEIGHT * 4);
            m_panel.GetMember(name.c_str(), &field);
            if (!field.IsUndefined()) {
                SetTextFormat(field, "Arial", static_cast<int>(BODY_SIZE), COLOR_BODY, "left", true, true);
                RE::GFxValue textVal;
                std::string text = T("$SLID_WelcomeSellingBody");
                textVal.SetString(text.c_str());
                field.SetMember("text", textVal);
            }
            y += LINE_HEIGHT * 4 + SECTION_GAP;
        }

        // Closing line
        {
            CreateTextField(m_panel, "closing", depth++, textX, y, textW, BODY_SIZE + 4);
            RE::GFxValue field;
            m_panel.GetMember("closing", &field);
            if (!field.IsUndefined()) {
                SetTextFormat(field, "Arial", static_cast<int>(BODY_SIZE), 0x999999, "left", false, false);
                RE::GFxValue textVal;
                std::string text = T("$SLID_WelcomeClosing");
                textVal.SetString(text.c_str());
                field.SetMember("text", textVal);
            }
        }
    }

    void Menu::CreateCheckbox() {
        // Align checkbox vertically centered with the OK button
        float buttonY = POPUP_H - PADDING - BUTTON_H;
        float checkboxY = buttonY + (BUTTON_H - CHECKBOX_SIZE) / 2.0f;
        float checkboxX = PADDING;

        // Checkbox box background clip
        RE::GFxValue boxArgs[2];
        boxArgs[0].SetString("checkboxBox");
        boxArgs[1].SetNumber(201);
        RE::GFxValue checkboxBox;
        m_panel.Invoke("createEmptyMovieClip", &checkboxBox, boxArgs, 2);

        RE::GFxValue posX, posY;
        posX.SetNumber(static_cast<double>(checkboxX));
        posY.SetNumber(static_cast<double>(checkboxY));
        checkboxBox.SetMember("_x", posX);
        checkboxBox.SetMember("_y", posY);

        // Draw checkbox background
        {
            RE::GFxValue fillArgs[2];
            fillArgs[0].SetNumber(static_cast<double>(COLOR_CHECKBOX_BG));
            fillArgs[1].SetNumber(100.0);
            checkboxBox.Invoke("beginFill", nullptr, fillArgs, 2);

            RE::GFxValue pt[2];
            pt[0].SetNumber(0.0); pt[1].SetNumber(0.0);
            checkboxBox.Invoke("moveTo", nullptr, pt, 2);
            pt[0].SetNumber(static_cast<double>(CHECKBOX_SIZE));
            checkboxBox.Invoke("lineTo", nullptr, pt, 2);
            pt[1].SetNumber(static_cast<double>(CHECKBOX_SIZE));
            checkboxBox.Invoke("lineTo", nullptr, pt, 2);
            pt[0].SetNumber(0.0);
            checkboxBox.Invoke("lineTo", nullptr, pt, 2);
            pt[1].SetNumber(0.0);
            checkboxBox.Invoke("lineTo", nullptr, pt, 2);
            checkboxBox.Invoke("endFill", nullptr, nullptr, 0);

            // Border
            RE::GFxValue styleArgs[3];
            styleArgs[0].SetNumber(1.0);
            styleArgs[1].SetNumber(static_cast<double>(COLOR_CHECKBOX_BORDER));
            styleArgs[2].SetNumber(100.0);
            checkboxBox.Invoke("lineStyle", nullptr, styleArgs, 3);

            pt[0].SetNumber(0.0); pt[1].SetNumber(0.0);
            checkboxBox.Invoke("moveTo", nullptr, pt, 2);
            pt[0].SetNumber(static_cast<double>(CHECKBOX_SIZE));
            checkboxBox.Invoke("lineTo", nullptr, pt, 2);
            pt[1].SetNumber(static_cast<double>(CHECKBOX_SIZE));
            checkboxBox.Invoke("lineTo", nullptr, pt, 2);
            pt[0].SetNumber(0.0);
            checkboxBox.Invoke("lineTo", nullptr, pt, 2);
            pt[1].SetNumber(0.0);
            checkboxBox.Invoke("lineTo", nullptr, pt, 2);
        }

        // Checkmark (initially hidden)
        RE::GFxValue markArgs[2];
        markArgs[0].SetString("checkboxMark");
        markArgs[1].SetNumber(202);
        m_panel.Invoke("createEmptyMovieClip", &m_checkboxMark, markArgs, 2);

        posX.SetNumber(static_cast<double>(checkboxX));
        posY.SetNumber(static_cast<double>(checkboxY));
        m_checkboxMark.SetMember("_x", posX);
        m_checkboxMark.SetMember("_y", posY);

        // Draw checkmark shape using lineStyle
        RE::GFxValue styleArgs[3];
        styleArgs[0].SetNumber(2.0);
        styleArgs[1].SetNumber(static_cast<double>(COLOR_CHECKBOX_MARK));
        styleArgs[2].SetNumber(100.0);
        m_checkboxMark.Invoke("lineStyle", nullptr, styleArgs, 3);

        RE::GFxValue point[2];
        point[0].SetNumber(3.0);
        point[1].SetNumber(8.0);
        m_checkboxMark.Invoke("moveTo", nullptr, point, 2);
        point[0].SetNumber(6.0);
        point[1].SetNumber(12.0);
        m_checkboxMark.Invoke("lineTo", nullptr, point, 2);
        point[0].SetNumber(13.0);
        point[1].SetNumber(4.0);
        m_checkboxMark.Invoke("lineTo", nullptr, point, 2);

        UpdateCheckboxVisual();

        // Label
        CreateTextField(m_panel, "checkboxLabel", 203,
            checkboxX + CHECKBOX_SIZE + 8.0f, checkboxY - 2.0f, 250.0f, BODY_SIZE + 4);
        RE::GFxValue labelField;
        m_panel.GetMember("checkboxLabel", &labelField);
        if (!labelField.IsUndefined()) {
            SetTextFormat(labelField, "Arial", static_cast<int>(BODY_SIZE), COLOR_BODY, "left", false, false);
            RE::GFxValue textVal;
            std::string text = T("$SLID_WelcomeCheckbox");
            textVal.SetString(text.c_str());
            labelField.SetMember("text", textVal);
        }
    }

    void Menu::CreateButton() {
        float buttonX = (POPUP_W - BUTTON_W) / 2.0f;
        float buttonY = POPUP_H - PADDING - BUTTON_H;

        RE::GFxValue btnArgs[2];
        btnArgs[0].SetString("okButton");
        btnArgs[1].SetNumber(300);
        RE::GFxValue buttonClip;
        m_panel.Invoke("createEmptyMovieClip", &buttonClip, btnArgs, 2);

        RE::GFxValue posX, posY;
        posX.SetNumber(static_cast<double>(buttonX));
        posY.SetNumber(static_cast<double>(buttonY));
        buttonClip.SetMember("_x", posX);
        buttonClip.SetMember("_y", posY);

        // Button background
        {
            RE::GFxValue fillArgs[2];
            fillArgs[0].SetNumber(static_cast<double>(COLOR_BUTTON_BG));
            fillArgs[1].SetNumber(100.0);
            buttonClip.Invoke("beginFill", nullptr, fillArgs, 2);

            RE::GFxValue pt[2];
            pt[0].SetNumber(0.0); pt[1].SetNumber(0.0);
            buttonClip.Invoke("moveTo", nullptr, pt, 2);
            pt[0].SetNumber(static_cast<double>(BUTTON_W));
            buttonClip.Invoke("lineTo", nullptr, pt, 2);
            pt[1].SetNumber(static_cast<double>(BUTTON_H));
            buttonClip.Invoke("lineTo", nullptr, pt, 2);
            pt[0].SetNumber(0.0);
            buttonClip.Invoke("lineTo", nullptr, pt, 2);
            pt[1].SetNumber(0.0);
            buttonClip.Invoke("lineTo", nullptr, pt, 2);
            buttonClip.Invoke("endFill", nullptr, nullptr, 0);

            // Border
            RE::GFxValue styleArgs[3];
            styleArgs[0].SetNumber(2.0);
            styleArgs[1].SetNumber(static_cast<double>(COLOR_BUTTON_BORDER));
            styleArgs[2].SetNumber(100.0);
            buttonClip.Invoke("lineStyle", nullptr, styleArgs, 3);

            pt[0].SetNumber(0.0); pt[1].SetNumber(0.0);
            buttonClip.Invoke("moveTo", nullptr, pt, 2);
            pt[0].SetNumber(static_cast<double>(BUTTON_W));
            buttonClip.Invoke("lineTo", nullptr, pt, 2);
            pt[1].SetNumber(static_cast<double>(BUTTON_H));
            buttonClip.Invoke("lineTo", nullptr, pt, 2);
            pt[0].SetNumber(0.0);
            buttonClip.Invoke("lineTo", nullptr, pt, 2);
            pt[1].SetNumber(0.0);
            buttonClip.Invoke("lineTo", nullptr, pt, 2);
        }

        // Button text
        CreateTextField(buttonClip, "okText", 10, 0, (BUTTON_H - BODY_SIZE) / 2.0f - 2.0f, BUTTON_W, BODY_SIZE + 4);
        RE::GFxValue buttonText;
        buttonClip.GetMember("okText", &buttonText);
        if (!buttonText.IsUndefined()) {
            SetTextFormat(buttonText, "Arial", static_cast<int>(BODY_SIZE), COLOR_BUTTON_TEXT, "center", false, false);
            RE::GFxValue textVal;
            std::string text = T("$SLID_OK");
            textVal.SetString(text.c_str());
            buttonText.SetMember("text", textVal);
        }
    }

    void Menu::UpdateCheckboxVisual() {
        RE::GFxValue visible;
        visible.SetBoolean(m_dontShowAgain);
        m_checkboxMark.SetMember("_visible", visible);
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
            logger::info("WelcomeMenu::InputHandler registered");
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
            if (auto* button = event->AsButtonEvent(); button && button->IsDown()) {
                auto device = button->GetDevice();
                auto key = button->GetIDCode();

                bool isConfirm = false;
                bool isToggle = false;

                if (device == RE::INPUT_DEVICE::kKeyboard) {
                    isConfirm = (key == RE::BSKeyboardDevice::Key::kEnter ||
                                 key == RE::BSKeyboardDevice::Key::kEscape);
                    isToggle = (key == RE::BSKeyboardDevice::Key::kSpacebar);
                } else if (device == RE::INPUT_DEVICE::kGamepad) {
                    isConfirm = (key == ScaleformUtil::GAMEPAD_A || key == ScaleformUtil::GAMEPAD_B);
                    isToggle = (key == ScaleformUtil::GAMEPAD_X);
                }

                if (isToggle) {
                    g_activeMenu->m_dontShowAgain = !g_activeMenu->m_dontShowAgain;
                    g_activeMenu->UpdateCheckboxVisual();
                    return RE::BSEventNotifyControl::kStop;
                }

                if (isConfirm) {
                    Menu::Hide();
                    return RE::BSEventNotifyControl::kStop;
                }
            }

            // Mouse click handling
            if (auto* mouseButton = event->AsButtonEvent();
                mouseButton && mouseButton->IsDown() &&
                mouseButton->GetDevice() == RE::INPUT_DEVICE::kMouse)
            {
                auto key = mouseButton->GetIDCode();
                if (key == 0) {  // Left click
                    // Get mouse position
                    float mouseX = 0.0f, mouseY = 0.0f;
                    if (g_activeMenu->uiMovie) {
                        RE::GFxValue xVal, yVal;
                        g_activeMenu->uiMovie->GetVariable(&xVal, "_root._xmouse");
                        g_activeMenu->uiMovie->GetVariable(&yVal, "_root._ymouse");
                        mouseX = static_cast<float>(xVal.GetNumber());
                        mouseY = static_cast<float>(yVal.GetNumber());
                    }

                    // Get panel position
                    RE::GFxValue panelX, panelY;
                    g_activeMenu->m_panel.GetMember("_x", &panelX);
                    g_activeMenu->m_panel.GetMember("_y", &panelY);
                    float pX = static_cast<float>(panelX.GetNumber());
                    float pY = static_cast<float>(panelY.GetNumber());

                    // Check checkbox hit (aligned with button)
                    float buttonY = pY + Menu::POPUP_H - Menu::PADDING - Menu::BUTTON_H;
                    float checkboxX = pX + Menu::PADDING;
                    float checkboxY = buttonY + (Menu::BUTTON_H - Menu::CHECKBOX_SIZE) / 2.0f;
                    float checkboxHitW = Menu::CHECKBOX_SIZE + 200.0f;
                    float checkboxHitH = Menu::CHECKBOX_SIZE + 4.0f;

                    if (mouseX >= checkboxX && mouseX <= checkboxX + checkboxHitW &&
                        mouseY >= checkboxY && mouseY <= checkboxY + checkboxHitH)
                    {
                        g_activeMenu->m_dontShowAgain = !g_activeMenu->m_dontShowAgain;
                        g_activeMenu->UpdateCheckboxVisual();
                        return RE::BSEventNotifyControl::kStop;
                    }

                    // Check OK button hit (buttonY already computed above)
                    float buttonX = pX + (Menu::POPUP_W - Menu::BUTTON_W) / 2.0f;

                    if (mouseX >= buttonX && mouseX <= buttonX + Menu::BUTTON_W &&
                        mouseY >= buttonY && mouseY <= buttonY + Menu::BUTTON_H)
                    {
                        Menu::Hide();
                        return RE::BSEventNotifyControl::kStop;
                    }
                }
            }
        }

        return RE::BSEventNotifyControl::kStop;  // Consume all input while open
    }

    // =========================================================================
    // Trigger
    // =========================================================================

    void TryShowWelcome() {
        if (Settings::bShownWelcomeTutorial) {
            return;
        }

        // Small delay to let other UI settle
        SKSE::GetTaskInterface()->AddTask([]() {
            Menu::Show();
        });
    }

}  // namespace WelcomeMenu
