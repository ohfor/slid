#include "TagInputMenu.h"
#include "Feedback.h"
#include "NetworkManager.h"
#include "ScaleformUtil.h"
#include "TranslationService.h"

namespace TagInputMenu {

    // Statics
    RE::FormID  Menu::s_pendingFormID = 0;
    std::string Menu::s_defaultName;
    bool        Menu::s_isRename = false;
    std::string    Menu::s_titleOverride;
    Menu::CommitCallback Menu::s_commitCallback;
    std::string Menu::s_currentText;
    int         Menu::s_selStart = 0;
    int         Menu::s_selEnd = 0;
    bool        Menu::s_allSelected = false;
    bool        InputHandler::s_shiftHeld = false;

    static Menu* g_activeMenu = nullptr;

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
        depthPriority = 5;  // above ConfigMenu (3)

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
                logger::info("TagInputMenu: loaded SWF {}", FILE_NAME);
            } else {
                logger::error("TagInputMenu: failed to load SWF {}", FILE_NAME);
            }
        }
    }

    Menu::~Menu() {
        g_activeMenu = nullptr;
        // Restore textEntryCount only if we successfully incremented it
        if (m_ownedTextInput) {
            auto controlMap = RE::ControlMap::GetSingleton();
            if (controlMap) {
                controlMap->AllowTextInput(false);
            }
            m_ownedTextInput = false;
        }
    }

    void Menu::PostCreate() {
        if (!uiMovie) return;

        g_activeMenu = this;

        // Initialize text buffer from default name
        s_currentText = s_defaultName;
        s_selStart = 0;
        s_selEnd = static_cast<int>(s_currentText.length());
        s_allSelected = !s_currentText.empty();

        m_cursorVisible = true;
        m_cursorBlinkTime = std::chrono::steady_clock::now();

        DrawPopup();

        // Enable text input — tells the engine to generate CharEvents from WM_CHAR.
        // textEntryCount == -1 means "locked on" (AllowTextInput(true) is a no-op but
        // AllowTextInput(false) still decrements — corrupting the counter). Only call
        // if we can actually increment, and track ownership so we decrement exactly once.
        auto controlMap = RE::ControlMap::GetSingleton();
        if (controlMap && controlMap->textEntryCount != -1) {
            controlMap->AllowTextInput(true);
            m_ownedTextInput = true;
        }

        logger::info("TagInputMenu ready: formID={:08X}, default='{}', rename={}",
                     s_pendingFormID, s_defaultName, s_isRename);
    }

    RE::UI_MESSAGE_RESULTS Menu::ProcessMessage(RE::UIMessage& a_message) {
        using Message = RE::UI_MESSAGE_TYPE;

        switch (a_message.type.get()) {
            case Message::kHide:
                logger::debug("TagInputMenu: kHide");
                // AllowTextInput cleanup is in the destructor — guaranteed to run
                // even if kHide is not delivered before the menu object is destroyed.
                return RE::UI_MESSAGE_RESULTS::kHandled;

            case Message::kShow:
                return RE::UI_MESSAGE_RESULTS::kHandled;

            case Message::kUpdate: {
                // Cursor blink
                auto now = std::chrono::steady_clock::now();
                float elapsed = std::chrono::duration<float>(now - m_cursorBlinkTime).count();
                if (elapsed >= CURSOR_BLINK_INTERVAL) {
                    m_cursorBlinkTime = now;
                    m_cursorVisible = !m_cursorVisible;

                    if (uiMovie && !s_allSelected) {
                        RE::GFxValue vis;
                        vis.SetBoolean(m_cursorVisible);
                        uiMovie->SetVariable("_root._cursor._visible", vis);
                    }
                }
                return RE::UI_MESSAGE_RESULTS::kHandled;
            }

            default:
                return RE::IMenu::ProcessMessage(a_message);
        }
    }

    // --- Open/Close ---

    void Menu::Show(RE::FormID a_formID, const std::string& a_defaultName, bool a_isRename) {
        auto ui = RE::UI::GetSingleton();
        if (!ui) return;

        if (ui->IsMenuOpen(MENU_NAME)) {
            logger::warn("TagInputMenu::Show: already open");
            return;
        }

        s_pendingFormID = a_formID;
        s_defaultName = a_defaultName;
        s_isRename = a_isRename;
        s_titleOverride.clear();
        s_commitCallback = nullptr;

        auto msgQueue = RE::UIMessageQueue::GetSingleton();
        if (msgQueue) {
            msgQueue->AddMessage(MENU_NAME, RE::UI_MESSAGE_TYPE::kShow, nullptr);
            logger::info("TagInputMenu: opening for {:08X} (default='{}', rename={})",
                         a_formID, a_defaultName, a_isRename);
        }
    }

    void Menu::ShowWithCallback(const std::string& a_title, const std::string& a_defaultName,
                                CommitCallback a_callback) {
        auto ui = RE::UI::GetSingleton();
        if (!ui) return;

        if (ui->IsMenuOpen(MENU_NAME)) {
            logger::warn("TagInputMenu::ShowWithCallback: already open");
            return;
        }

        s_pendingFormID = 0;
        s_defaultName = a_defaultName;
        s_isRename = false;
        s_titleOverride = a_title;
        s_commitCallback = std::move(a_callback);

        auto msgQueue = RE::UIMessageQueue::GetSingleton();
        if (msgQueue) {
            msgQueue->AddMessage(MENU_NAME, RE::UI_MESSAGE_TYPE::kShow, nullptr);
            logger::info("TagInputMenu: opening with callback (title='{}', default='{}')",
                         a_title, a_defaultName);
        }
    }

    void Menu::Hide() {
        // Restore text input before closing — only if we own the increment
        if (g_activeMenu && g_activeMenu->m_ownedTextInput) {
            auto controlMap = RE::ControlMap::GetSingleton();
            if (controlMap) {
                controlMap->AllowTextInput(false);
            }
            g_activeMenu->m_ownedTextInput = false;
        }

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

    // --- Selection helpers ---

    int Menu::SelMin() { return (std::min)(s_selStart, s_selEnd); }
    int Menu::SelMax() { return (std::max)(s_selStart, s_selEnd); }
    bool Menu::HasSelection() { return s_selStart != s_selEnd; }

    void Menu::DeleteSelection() {
        if (!HasSelection()) return;
        int lo = SelMin();
        int hi = SelMax();
        s_currentText.erase(lo, hi - lo);
        s_selStart = lo;
        s_selEnd = lo;
        s_allSelected = false;
    }

    void Menu::ClearSelection() {
        s_selStart = s_selEnd;
        s_allSelected = false;
    }

    // --- Cursor measurement ---

    double Menu::MeasureTextWidth(int a_charCount) {
        if (!uiMovie) return 0.0;
        if (a_charCount <= 0) return 0.0;

        // Test textWidth on first call
        if (!m_textWidthTested) {
            m_textWidthTested = true;
            // Set measurement field to a known string and check
            std::string testStr = s_currentText.substr(0, (std::min)(a_charCount, static_cast<int>(s_currentText.length())));
            RE::GFxValue tv;
            tv.SetString(testStr.c_str());
            uiMovie->SetVariable("_root._measure.text", tv);

            RE::GFxValue widthVal;
            uiMovie->GetVariable(&widthVal, "_root._measure.textWidth");
            if (!widthVal.IsNumber() || widthVal.GetNumber() <= 0.0) {
                logger::warn("TagInputMenu: textWidth unavailable, using fallback (~{:.1f}px/char)", FALLBACK_CHAR_WIDTH);
                m_textWidthWorks = false;
            } else {
                m_textWidthWorks = true;
            }
        }

        if (!m_textWidthWorks) {
            return static_cast<double>(a_charCount) * FALLBACK_CHAR_WIDTH;
        }

        std::string sub = s_currentText.substr(0, a_charCount);
        RE::GFxValue tv;
        tv.SetString(sub.c_str());
        uiMovie->SetVariable("_root._measure.text", tv);

        RE::GFxValue widthVal;
        uiMovie->GetVariable(&widthVal, "_root._measure.textWidth");
        if (widthVal.IsNumber()) {
            return widthVal.GetNumber();
        }
        return static_cast<double>(a_charCount) * FALLBACK_CHAR_WIDTH;
    }

    void Menu::UpdateCursorPosition() {
        if (!uiMovie) return;

        // Position cursor at s_selEnd
        double textW = MeasureTextWidth(s_selEnd);
        // TextField has ~2px gutter
        double cursorX = m_inputFieldX + 4.0 + textW + 2.0;
        double cursorY = m_inputFieldY + 4.0;
        double cursorH = INPUT_H - 8.0;

        // Draw cursor line via _cursor clip
        RE::GFxValue cursor;
        uiMovie->GetVariable(&cursor, "_root._cursor");
        if (!cursor.IsUndefined()) {
            cursor.Invoke("clear", nullptr, nullptr, 0);

            RE::GFxValue fillArgs[2];
            fillArgs[0].SetNumber(static_cast<double>(COLOR_CURSOR));
            fillArgs[1].SetNumber(100.0);
            cursor.Invoke("beginFill", nullptr, fillArgs, 2);

            RE::GFxValue pt[2];
            pt[0].SetNumber(cursorX); pt[1].SetNumber(cursorY);
            cursor.Invoke("moveTo", nullptr, pt, 2);
            pt[0].SetNumber(cursorX + CURSOR_WIDTH);
            cursor.Invoke("lineTo", nullptr, pt, 2);
            pt[1].SetNumber(cursorY + cursorH);
            cursor.Invoke("lineTo", nullptr, pt, 2);
            pt[0].SetNumber(cursorX);
            cursor.Invoke("lineTo", nullptr, pt, 2);
            pt[1].SetNumber(cursorY);
            cursor.Invoke("lineTo", nullptr, pt, 2);

            cursor.Invoke("endFill", nullptr, nullptr, 0);
        }

        // Hide cursor when there's a selection; show when no selection
        RE::GFxValue vis;
        vis.SetBoolean(!HasSelection() && !s_allSelected);
        uiMovie->SetVariable("_root._cursor._visible", vis);
    }

    void Menu::UpdateSelectionHighlight() {
        if (!uiMovie) return;

        RE::GFxValue highlight;
        uiMovie->GetVariable(&highlight, "_root._selectHighlight");
        if (highlight.IsUndefined()) return;

        if (!HasSelection() && !s_allSelected) {
            RE::GFxValue vis;
            vis.SetBoolean(false);
            highlight.SetMember("_visible", vis);
            return;
        }

        int lo = SelMin();
        int hi = SelMax();

        double startW = MeasureTextWidth(lo);
        double endW = MeasureTextWidth(hi);
        double selX = m_inputFieldX + 4.0 + startW + 2.0;
        double selW = endW - startW;
        double selY = m_inputFieldY + 3.0;
        double selH = INPUT_H - 6.0;

        highlight.Invoke("clear", nullptr, nullptr, 0);

        RE::GFxValue fillArgs[2];
        fillArgs[0].SetNumber(static_cast<double>(COLOR_SELECT_BG));
        fillArgs[1].SetNumber(static_cast<double>(ALPHA_SELECT));
        highlight.Invoke("beginFill", nullptr, fillArgs, 2);

        RE::GFxValue pt[2];
        pt[0].SetNumber(selX); pt[1].SetNumber(selY);
        highlight.Invoke("moveTo", nullptr, pt, 2);
        pt[0].SetNumber(selX + selW);
        highlight.Invoke("lineTo", nullptr, pt, 2);
        pt[1].SetNumber(selY + selH);
        highlight.Invoke("lineTo", nullptr, pt, 2);
        pt[0].SetNumber(selX);
        highlight.Invoke("lineTo", nullptr, pt, 2);
        pt[1].SetNumber(selY);
        highlight.Invoke("lineTo", nullptr, pt, 2);

        highlight.Invoke("endFill", nullptr, nullptr, 0);

        RE::GFxValue vis;
        vis.SetBoolean(true);
        highlight.SetMember("_visible", vis);
    }

    void Menu::ResetCursorBlink() {
        if (!g_activeMenu) return;
        g_activeMenu->m_cursorVisible = true;
        g_activeMenu->m_cursorBlinkTime = std::chrono::steady_clock::now();

        if (g_activeMenu->uiMovie) {
            RE::GFxValue vis;
            vis.SetBoolean(!HasSelection() && !s_allSelected);
            g_activeMenu->uiMovie->SetVariable("_root._cursor._visible", vis);
        }
    }

    int Menu::XToCharPos(double a_screenX) {
        double clickX = a_screenX - (m_inputFieldX + 4.0 + 2.0);  // relative to text start
        if (clickX <= 0.0) return 0;

        int len = static_cast<int>(s_currentText.length());
        for (int i = 1; i <= len; i++) {
            double w = MeasureTextWidth(i);
            if (w >= clickX) {
                // Check if closer to i-1 or i
                double prevW = (i > 1) ? MeasureTextWidth(i - 1) : 0.0;
                double midpoint = (prevW + w) / 2.0;
                return (clickX < midpoint) ? (i - 1) : i;
            }
        }
        return len;
    }

    // --- Drawing ---

    void Menu::CreateInputField(const char* a_name, int a_depth, double a_x, double a_y,
                                double a_w, double a_h, const char* a_defaultText,
                                int a_size, uint32_t a_color) {
        ScaleformUtil::CreateLabel(uiMovie.get(), a_name, a_depth, a_x, a_y, a_w, a_h, a_defaultText, a_size, a_color);
    }

    void Menu::DrawPopup() {
        double popupX = (1280.0 - POPUP_W) / 2.0;
        double popupY = (720.0 - POPUP_H) / 2.0;

        // Dim overlay
        ScaleformUtil::DrawFilledRect(uiMovie.get(), "_dim", 1, 0.0, 0.0, 1280.0, 720.0, 0x000000, ALPHA_DIM);

        // Popup background + border
        ScaleformUtil::DrawFilledRect(uiMovie.get(), "_bg", 2, popupX, popupY, POPUP_W, POPUP_H, COLOR_BG, ALPHA_BG);
        ScaleformUtil::DrawBorderRect(uiMovie.get(), "_border", 3, popupX, popupY, POPUP_W, POPUP_H, COLOR_BORDER);

        // Title (size 18, at +10)
        std::string title = !s_titleOverride.empty() ? s_titleOverride
                          : s_isRename ? T("$SLID_RenameContainer") : T("$SLID_NameContainer");
        ScaleformUtil::CreateLabel(uiMovie.get(), "_title", 10, popupX + 20.0, popupY + 10.0,
                    POPUP_W - 40.0, 24.0, title.c_str(), 18, COLOR_TITLE);

        // Input field background + border (at +40)
        double inputX = popupX + 20.0;
        double inputY = popupY + 40.0;
        double inputW = POPUP_W - 40.0;
        ScaleformUtil::DrawFilledRect(uiMovie.get(), "_inputBg", 4, inputX, inputY, inputW, INPUT_H, COLOR_INPUT_BG, ALPHA_INPUT);
        ScaleformUtil::DrawBorderRect(uiMovie.get(), "_inputBorder", 5, inputX, inputY, inputW, INPUT_H, COLOR_INPUT_BORDER);

        // Cache input field geometry for hit testing
        m_inputFieldX = inputX;
        m_inputFieldY = inputY;
        m_inputFieldW = inputW;

        // Selection highlight (depth 19, behind text, in front of input bg)
        {
            RE::GFxValue root;
            uiMovie->GetVariable(&root, "_root");
            if (!root.IsUndefined()) {
                RE::GFxValue clip;
                RE::GFxValue args[2];
                args[0].SetString("_selectHighlight");
                args[1].SetNumber(19.0);
                root.Invoke("createEmptyMovieClip", &clip, args, 2);
                // Initially hidden; UpdateSelectionHighlight will draw if needed
            }
        }

        // Input text field (display-only, depth 20)
        CreateInputField("_input", 20, inputX + 4.0, inputY + 2.0,
                         inputW - 8.0, INPUT_H - 4.0,
                         s_currentText.c_str(), 16, COLOR_INPUT_TEXT);

        // Hidden measurement field (offscreen, for textWidth)
        {
            RE::GFxValue root;
            uiMovie->GetVariable(&root, "_root");
            if (!root.IsUndefined()) {
                RE::GFxValue tfArgs[6];
                tfArgs[0].SetString("_measure");
                tfArgs[1].SetNumber(15.0);
                tfArgs[2].SetNumber(0.0);
                tfArgs[3].SetNumber(-500.0);  // offscreen
                tfArgs[4].SetNumber(800.0);
                tfArgs[5].SetNumber(30.0);
                root.Invoke("createTextField", nullptr, tfArgs, 6);

                RE::GFxValue tf;
                uiMovie->GetVariable(&tf, "_root._measure");
                if (!tf.IsUndefined()) {
                    RE::GFxValue fmt;
                    uiMovie->CreateObject(&fmt, "TextFormat");
                    if (!fmt.IsUndefined()) {
                        RE::GFxValue fontVal, sizeVal;
                        fontVal.SetString("Arial");
                        fmt.SetMember("font", fontVal);
                        sizeVal.SetNumber(16.0);
                        fmt.SetMember("size", sizeVal);

                        RE::GFxValue fmtArgs[1];
                        fmtArgs[0] = fmt;
                        tf.Invoke("setTextFormat", nullptr, fmtArgs, 1);
                        tf.Invoke("setNewTextFormat", nullptr, fmtArgs, 1);
                    }

                    RE::GFxValue embedVal;
                    embedVal.SetBoolean(true);
                    tf.SetMember("embedFonts", embedVal);

                    RE::GFxValue autoSize;
                    autoSize.SetString("left");
                    tf.SetMember("autoSize", autoSize);
                }
            }
        }

        // Cursor clip (depth 25, above text)
        {
            RE::GFxValue root;
            uiMovie->GetVariable(&root, "_root");
            if (!root.IsUndefined()) {
                RE::GFxValue clip;
                RE::GFxValue args[2];
                args[0].SetString("_cursor");
                args[1].SetNumber(25.0);
                root.Invoke("createEmptyMovieClip", &clip, args, 2);
            }
        }

        // Draw action buttons (at +78)
        DrawButtons();

        // Hint text (at +110, size 10)
        std::string hint = T("$SLID_TagInputHint");
        ScaleformUtil::CreateLabel(uiMovie.get(), "_hint", 11, popupX + 20.0, popupY + 110.0,
                    POPUP_W - 40.0, 16.0,
                    hint.c_str(), 10, COLOR_HINT);

        // Center-align hint
        {
            RE::GFxValue tf;
            uiMovie->GetVariable(&tf, "_root._hint");
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

        // Initial selection/cursor state
        if (s_allSelected) {
            UpdateSelectionHighlight();
        }
        UpdateCursorPosition();
    }

    // --- Buttons ---

    void Menu::DrawButtons() {
        if (!uiMovie) return;

        double popupX = (1280.0 - POPUP_W) / 2.0;
        double popupY = (720.0 - POPUP_H) / 2.0;

        double totalW = BTN_ACCEPT_W + BTN_GAP + BTN_CANCEL_W;
        double startX = popupX + (POPUP_W - totalW) / 2.0;
        double btnY = popupY + 78.0;

        m_btnAcceptX = startX;
        m_btnCancelX = startX + BTN_ACCEPT_W + BTN_GAP;
        m_btnY = btnY;

        RE::GFxValue root;
        uiMovie->GetVariable(&root, "_root");
        if (root.IsUndefined()) return;

        // Accept button
        {
            RE::GFxValue clip;
            RE::GFxValue args[2];
            args[0].SetString("_btnAccept");
            args[1].SetNumber(30.0);
            root.Invoke("createEmptyMovieClip", &clip, args, 2);
            if (!clip.IsUndefined()) {
                RE::GFxValue posX, posY;
                posX.SetNumber(m_btnAcceptX);
                posY.SetNumber(btnY);
                clip.SetMember("_x", posX);
                clip.SetMember("_y", posY);

                // Background (selected by default)
                RE::GFxValue bgClip;
                RE::GFxValue bgArgs[2];
                bgArgs[0].SetString("_bg"); bgArgs[1].SetNumber(1.0);
                clip.Invoke("createEmptyMovieClip", &bgClip, bgArgs, 2);
                if (!bgClip.IsUndefined()) {
                    RE::GFxValue fillArgs[2];
                    fillArgs[0].SetNumber(static_cast<double>(COLOR_BTN_SELECT));
                    fillArgs[1].SetNumber(static_cast<double>(ALPHA_BTN_SELECT));
                    bgClip.Invoke("beginFill", nullptr, fillArgs, 2);
                    RE::GFxValue pt[2];
                    pt[0].SetNumber(0.0); pt[1].SetNumber(0.0);
                    bgClip.Invoke("moveTo", nullptr, pt, 2);
                    pt[0].SetNumber(BTN_ACCEPT_W);
                    bgClip.Invoke("lineTo", nullptr, pt, 2);
                    pt[1].SetNumber(BTN_H);
                    bgClip.Invoke("lineTo", nullptr, pt, 2);
                    pt[0].SetNumber(0.0);
                    bgClip.Invoke("lineTo", nullptr, pt, 2);
                    pt[1].SetNumber(0.0);
                    bgClip.Invoke("lineTo", nullptr, pt, 2);
                    bgClip.Invoke("endFill", nullptr, nullptr, 0);
                }

                // Label
                RE::GFxValue tfArgs[6];
                tfArgs[0].SetString("_label"); tfArgs[1].SetNumber(10.0);
                tfArgs[2].SetNumber(0.0); tfArgs[3].SetNumber(4.0);
                tfArgs[4].SetNumber(BTN_ACCEPT_W); tfArgs[5].SetNumber(BTN_H - 4.0);
                clip.Invoke("createTextField", nullptr, tfArgs, 6);

                RE::GFxValue tf;
                uiMovie->GetVariable(&tf, "_root._btnAccept._label");
                if (!tf.IsUndefined()) {
                    RE::GFxValue fmt;
                    uiMovie->CreateObject(&fmt, "TextFormat");
                    if (!fmt.IsUndefined()) {
                        RE::GFxValue fontVal, sizeVal, colorVal, alignVal;
                        fontVal.SetString("Arial");
                        fmt.SetMember("font", fontVal);
                        sizeVal.SetNumber(13.0);
                        fmt.SetMember("size", sizeVal);
                        colorVal.SetNumber(static_cast<double>(COLOR_BTN_LABEL));
                        fmt.SetMember("color", colorVal);
                        alignVal.SetString("center");
                        fmt.SetMember("align", alignVal);
                        RE::GFxValue fmtArgs[1];
                        fmtArgs[0] = fmt;
                        tf.Invoke("setTextFormat", nullptr, fmtArgs, 1);
                        tf.Invoke("setNewTextFormat", nullptr, fmtArgs, 1);
                    }
                    RE::GFxValue embedVal;
                    embedVal.SetBoolean(true);
                    tf.SetMember("embedFonts", embedVal);

                    RE::GFxValue selVal;
                    selVal.SetBoolean(false);
                    tf.SetMember("selectable", selVal);
                }
                std::string acceptLabel = T("$SLID_Accept");
                RE::GFxValue textVal;
                textVal.SetString(acceptLabel.c_str());
                uiMovie->SetVariable("_root._btnAccept._label.text", textVal);
            }
        }

        // Cancel button
        {
            RE::GFxValue clip;
            RE::GFxValue args[2];
            args[0].SetString("_btnCancel");
            args[1].SetNumber(31.0);
            root.Invoke("createEmptyMovieClip", &clip, args, 2);
            if (!clip.IsUndefined()) {
                RE::GFxValue posX, posY;
                posX.SetNumber(m_btnCancelX);
                posY.SetNumber(btnY);
                clip.SetMember("_x", posX);
                clip.SetMember("_y", posY);

                // Background (normal)
                RE::GFxValue bgClip;
                RE::GFxValue bgArgs[2];
                bgArgs[0].SetString("_bg"); bgArgs[1].SetNumber(1.0);
                clip.Invoke("createEmptyMovieClip", &bgClip, bgArgs, 2);
                if (!bgClip.IsUndefined()) {
                    RE::GFxValue fillArgs[2];
                    fillArgs[0].SetNumber(static_cast<double>(COLOR_BTN_NORMAL));
                    fillArgs[1].SetNumber(static_cast<double>(ALPHA_BTN_NORMAL));
                    bgClip.Invoke("beginFill", nullptr, fillArgs, 2);
                    RE::GFxValue pt[2];
                    pt[0].SetNumber(0.0); pt[1].SetNumber(0.0);
                    bgClip.Invoke("moveTo", nullptr, pt, 2);
                    pt[0].SetNumber(BTN_CANCEL_W);
                    bgClip.Invoke("lineTo", nullptr, pt, 2);
                    pt[1].SetNumber(BTN_H);
                    bgClip.Invoke("lineTo", nullptr, pt, 2);
                    pt[0].SetNumber(0.0);
                    bgClip.Invoke("lineTo", nullptr, pt, 2);
                    pt[1].SetNumber(0.0);
                    bgClip.Invoke("lineTo", nullptr, pt, 2);
                    bgClip.Invoke("endFill", nullptr, nullptr, 0);
                }

                // Label
                RE::GFxValue tfArgs[6];
                tfArgs[0].SetString("_label"); tfArgs[1].SetNumber(10.0);
                tfArgs[2].SetNumber(0.0); tfArgs[3].SetNumber(4.0);
                tfArgs[4].SetNumber(BTN_CANCEL_W); tfArgs[5].SetNumber(BTN_H - 4.0);
                clip.Invoke("createTextField", nullptr, tfArgs, 6);

                RE::GFxValue tf;
                uiMovie->GetVariable(&tf, "_root._btnCancel._label");
                if (!tf.IsUndefined()) {
                    RE::GFxValue fmt;
                    uiMovie->CreateObject(&fmt, "TextFormat");
                    if (!fmt.IsUndefined()) {
                        RE::GFxValue fontVal, sizeVal, colorVal, alignVal;
                        fontVal.SetString("Arial");
                        fmt.SetMember("font", fontVal);
                        sizeVal.SetNumber(13.0);
                        fmt.SetMember("size", sizeVal);
                        colorVal.SetNumber(static_cast<double>(COLOR_BTN_LABEL));
                        fmt.SetMember("color", colorVal);
                        alignVal.SetString("center");
                        fmt.SetMember("align", alignVal);
                        RE::GFxValue fmtArgs[1];
                        fmtArgs[0] = fmt;
                        tf.Invoke("setTextFormat", nullptr, fmtArgs, 1);
                        tf.Invoke("setNewTextFormat", nullptr, fmtArgs, 1);
                    }
                    RE::GFxValue embedVal;
                    embedVal.SetBoolean(true);
                    tf.SetMember("embedFonts", embedVal);

                    RE::GFxValue selVal;
                    selVal.SetBoolean(false);
                    tf.SetMember("selectable", selVal);
                }
                std::string cancelLabel = T("$SLID_Cancel");
                RE::GFxValue textVal;
                textVal.SetString(cancelLabel.c_str());
                uiMovie->SetVariable("_root._btnCancel._label.text", textVal);
            }
        }
    }

    void Menu::UpdateButtonVisual(int a_btnIndex, uint32_t a_color, int a_alpha) {
        if (!g_activeMenu || !g_activeMenu->uiMovie) return;

        const char* clipPath = (a_btnIndex == 0) ? "_root._btnAccept._bg" : "_root._btnCancel._bg";
        double btnW = (a_btnIndex == 0) ? BTN_ACCEPT_W : BTN_CANCEL_W;

        RE::GFxValue bgClip;
        g_activeMenu->uiMovie->GetVariable(&bgClip, clipPath);
        if (bgClip.IsUndefined()) return;

        bgClip.Invoke("clear", nullptr, nullptr, 0);

        RE::GFxValue fillArgs[2];
        fillArgs[0].SetNumber(static_cast<double>(a_color));
        fillArgs[1].SetNumber(static_cast<double>(a_alpha));
        bgClip.Invoke("beginFill", nullptr, fillArgs, 2);

        RE::GFxValue pt[2];
        pt[0].SetNumber(0.0); pt[1].SetNumber(0.0);
        bgClip.Invoke("moveTo", nullptr, pt, 2);
        pt[0].SetNumber(btnW);
        bgClip.Invoke("lineTo", nullptr, pt, 2);
        pt[1].SetNumber(BTN_H);
        bgClip.Invoke("lineTo", nullptr, pt, 2);
        pt[0].SetNumber(0.0);
        bgClip.Invoke("lineTo", nullptr, pt, 2);
        pt[1].SetNumber(0.0);
        bgClip.Invoke("lineTo", nullptr, pt, 2);

        bgClip.Invoke("endFill", nullptr, nullptr, 0);
    }

    void Menu::UpdateButtonHover() {
        if (!g_activeMenu || !g_activeMenu->uiMovie) return;

        auto [mx, my] = g_activeMenu->GetMousePos();
        int newHover = -1;

        if (g_activeMenu->HitTestButton(mx, my, 0)) {
            newHover = 0;
        } else if (g_activeMenu->HitTestButton(mx, my, 1)) {
            newHover = 1;
        }

        if (newHover == g_activeMenu->m_hoverButton) return;

        // Restore old hover
        if (g_activeMenu->m_hoverButton == 0) {
            UpdateButtonVisual(0, COLOR_BTN_SELECT, ALPHA_BTN_SELECT);  // Accept stays selected
        } else if (g_activeMenu->m_hoverButton == 1) {
            UpdateButtonVisual(1, COLOR_BTN_NORMAL, ALPHA_BTN_NORMAL);
        }

        // Apply new hover
        if (newHover >= 0) {
            UpdateButtonVisual(newHover, COLOR_BTN_HOVER, ALPHA_BTN_HOVER);
        }

        g_activeMenu->m_hoverButton = newHover;
    }

    // --- Mouse support ---

    std::pair<float, float> Menu::GetMousePos() const {
        if (!uiMovie) return {0.0f, 0.0f};

        RE::GFxValue xVal, yVal;
        uiMovie->GetVariable(&xVal, "_root._xmouse");
        uiMovie->GetVariable(&yVal, "_root._ymouse");

        float mx = xVal.IsNumber() ? static_cast<float>(xVal.GetNumber()) : 0.0f;
        float my = yVal.IsNumber() ? static_cast<float>(yVal.GetNumber()) : 0.0f;
        return {mx, my};
    }

    bool Menu::HitTestButton(float a_mx, float a_my, int a_btnIndex) const {
        double bx, bw;
        if (a_btnIndex == 0) {
            bx = m_btnAcceptX;
            bw = BTN_ACCEPT_W;
        } else {
            bx = m_btnCancelX;
            bw = BTN_CANCEL_W;
        }
        return a_mx >= bx && a_mx <= bx + bw &&
               a_my >= m_btnY && a_my <= m_btnY + BTN_H;
    }

    bool Menu::HitTestInputField(float a_mx, float a_my) const {
        return a_mx >= m_inputFieldX && a_mx <= m_inputFieldX + m_inputFieldW &&
               a_my >= m_inputFieldY && a_my <= m_inputFieldY + INPUT_H;
    }

    void Menu::UpdateTextField() {
        if (!g_activeMenu || !g_activeMenu->uiMovie) return;

        RE::GFxValue textVal;
        textVal.SetString(s_currentText.c_str());
        g_activeMenu->uiMovie->SetVariable("_root._input.text", textVal);

        g_activeMenu->UpdateCursorPosition();
        g_activeMenu->UpdateSelectionHighlight();
        ResetCursorBlink();
    }

    // --- Actions ---

    void Menu::Confirm() {
        if (!g_activeMenu || !g_activeMenu->uiMovie) return;

        std::string name = s_currentText;

        // Trim
        auto start = name.find_first_not_of(" \t");
        auto end = name.find_last_not_of(" \t");
        if (start == std::string::npos) {
            name.clear();
        } else {
            name = name.substr(start, end - start + 1);
        }

        if (name.empty()) {
            logger::debug("TagInputMenu::Confirm: empty name, ignoring");
            return;
        }

        g_activeMenu->CommitTag(name);
        Hide();
    }

    void Menu::Cancel() {
        logger::info("TagInputMenu: cancelled");
        Hide();
    }

    void Menu::HandleChar(std::uint32_t a_charCode) {
        if (!g_activeMenu || !g_activeMenu->uiMovie) return;
        if (a_charCode < 32 || a_charCode == 127) return;

        // If all selected or has selection, delete selection first
        if (HasSelection() || s_allSelected) {
            DeleteSelection();
        }

        if (static_cast<int>(s_currentText.length()) >= MAX_CHARS) return;

        s_currentText.insert(s_currentText.begin() + s_selEnd, static_cast<char>(a_charCode));
        s_selEnd++;
        s_selStart = s_selEnd;
        s_allSelected = false;

        UpdateTextField();
    }

    void Menu::HandleBackspace() {
        if (!g_activeMenu || !g_activeMenu->uiMovie) return;

        if (HasSelection() || s_allSelected) {
            DeleteSelection();
            UpdateTextField();
            return;
        }

        if (s_selEnd <= 0) return;

        s_currentText.erase(s_selEnd - 1, 1);
        s_selEnd--;
        s_selStart = s_selEnd;

        UpdateTextField();
    }

    void Menu::HandleDelete() {
        if (!g_activeMenu || !g_activeMenu->uiMovie) return;

        if (HasSelection() || s_allSelected) {
            DeleteSelection();
            UpdateTextField();
            return;
        }

        if (s_selEnd >= static_cast<int>(s_currentText.length())) return;

        s_currentText.erase(s_selEnd, 1);

        UpdateTextField();
    }

    void Menu::HandleArrowLeft(bool a_shift) {
        if (!g_activeMenu) return;

        if (a_shift) {
            // Extend selection left
            if (s_allSelected) {
                // Shift+Left from allSelected: anchor at end, move cursor to end-1
                s_selStart = static_cast<int>(s_currentText.length());
                s_selEnd = (std::max)(0, s_selStart - 1);
                s_allSelected = false;
            } else if (s_selEnd > 0) {
                s_selEnd--;
            }
        } else {
            if (s_allSelected) {
                s_selStart = 0;
                s_selEnd = 0;
                s_allSelected = false;
            } else if (HasSelection()) {
                // Collapse to left edge
                int lo = SelMin();
                s_selStart = lo;
                s_selEnd = lo;
            } else if (s_selEnd > 0) {
                s_selEnd--;
                s_selStart = s_selEnd;
            }
        }

        UpdateTextField();
    }

    void Menu::HandleArrowRight(bool a_shift) {
        if (!g_activeMenu) return;

        int len = static_cast<int>(s_currentText.length());

        if (a_shift) {
            if (s_allSelected) {
                s_selStart = 0;
                s_selEnd = (std::min)(len, 1);
                s_allSelected = false;
            } else if (s_selEnd < len) {
                s_selEnd++;
            }
        } else {
            if (s_allSelected) {
                s_selStart = len;
                s_selEnd = len;
                s_allSelected = false;
            } else if (HasSelection()) {
                int hi = SelMax();
                s_selStart = hi;
                s_selEnd = hi;
            } else if (s_selEnd < len) {
                s_selEnd++;
                s_selStart = s_selEnd;
            }
        }

        UpdateTextField();
    }

    void Menu::HandleHome(bool a_shift) {
        if (!g_activeMenu) return;

        if (a_shift) {
            if (s_allSelected) {
                // Shift+Home: anchor at end, cursor at 0
                s_selStart = static_cast<int>(s_currentText.length());
                s_allSelected = false;
            }
            s_selEnd = 0;
        } else {
            s_selStart = 0;
            s_selEnd = 0;
            s_allSelected = false;
        }

        UpdateTextField();
    }

    void Menu::HandleEnd(bool a_shift) {
        if (!g_activeMenu) return;

        int len = static_cast<int>(s_currentText.length());

        if (a_shift) {
            if (s_allSelected) {
                s_selStart = 0;
                s_allSelected = false;
            }
            s_selEnd = len;
        } else {
            s_selStart = len;
            s_selEnd = len;
            s_allSelected = false;
        }

        UpdateTextField();
    }

    void Menu::HandleSelectAll() {
        if (!g_activeMenu) return;
        if (s_currentText.empty()) return;

        s_selStart = 0;
        s_selEnd = static_cast<int>(s_currentText.length());
        s_allSelected = true;

        UpdateTextField();
    }

    // --- Mouse actions ---

    void Menu::OnMouseDown() {
        if (!g_activeMenu || !g_activeMenu->uiMovie) return;

        auto [mx, my] = g_activeMenu->GetMousePos();

        // Check buttons first
        if (g_activeMenu->HitTestButton(mx, my, 0)) {
            Confirm();
            return;
        }
        if (g_activeMenu->HitTestButton(mx, my, 1)) {
            Cancel();
            return;
        }

        // Check input field
        if (g_activeMenu->HitTestInputField(mx, my)) {
            // Double-click detection
            auto now = std::chrono::steady_clock::now();
            float timeSinceLast = std::chrono::duration<float>(now - g_activeMenu->m_lastClickTime).count();
            g_activeMenu->m_lastClickTime = now;

            if (timeSinceLast < DOUBLE_CLICK_TIME) {
                OnDoubleClick();
                return;
            }

            // Single click: position cursor
            int pos = g_activeMenu->XToCharPos(static_cast<double>(mx));
            s_selStart = pos;
            s_selEnd = pos;
            s_allSelected = false;
            g_activeMenu->m_mouseDown = true;

            UpdateTextField();
        }
    }

    void Menu::OnMouseUp() {
        if (!g_activeMenu) return;
        g_activeMenu->m_mouseDown = false;
    }

    void Menu::OnMouseMove() {
        if (!g_activeMenu || !g_activeMenu->uiMovie) return;

        // Update button hover
        UpdateButtonHover();

        // Drag selection in text field
        if (g_activeMenu->m_mouseDown) {
            auto [mx, my] = g_activeMenu->GetMousePos();
            int pos = g_activeMenu->XToCharPos(static_cast<double>(mx));
            if (pos != s_selEnd) {
                s_selEnd = pos;
                s_allSelected = false;
                UpdateTextField();
            }
        }
    }

    void Menu::OnDoubleClick() {
        // Select all text
        HandleSelectAll();
    }

    void Menu::CommitTag(const std::string& a_name) {
        // Custom callback mode (e.g., network naming)
        if (s_commitCallback) {
            auto cb = std::move(s_commitCallback);
            s_commitCallback = nullptr;
            cb(a_name);
            return;
        }

        // Default mode: tag a container
        auto* mgr = NetworkManager::GetSingleton();
        mgr->TagContainer(s_pendingFormID, a_name);

        std::string msg = s_isRename
            ? TF("$SLID_NotifyRenamed", a_name)
            : TF("$SLID_NotifyTagged", a_name);
        RE::DebugNotification(msg.c_str());

        auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(s_pendingFormID);
        if (ref) {
            Feedback::OnTagContainer(ref);
        }

        logger::info("TagInputMenu: {} {:08X} as '{}'",
                     s_isRename ? "renamed" : "tagged", s_pendingFormID, a_name);
    }

    // --- InputHandler ---

    InputHandler* InputHandler::GetSingleton() {
        static InputHandler instance;
        return &instance;
    }

    void InputHandler::Register() {
        auto inputManager = RE::BSInputDeviceManager::GetSingleton();
        if (inputManager) {
            inputManager->AddEventSink(GetSingleton());
            logger::info("TagInputMenu: registered input handler");
        }
    }

    RE::BSEventNotifyControl InputHandler::ProcessEvent(
        RE::InputEvent* const* a_event,
        [[maybe_unused]] RE::BSTEventSource<RE::InputEvent*>* a_source)
    {
        if (!a_event || !Menu::IsOpen()) {
            return RE::BSEventNotifyControl::kContinue;
        }

        using namespace ScaleformUtil;
        constexpr uint32_t MOUSE_LEFT_BUTTON  = 0;

        for (auto* event = *a_event; event; event = event->next) {
            // Software keyboard: intercept CharEvents
            if (event->GetEventType() == RE::INPUT_EVENT_TYPE::kChar) {
                auto* charEvent = static_cast<RE::CharEvent*>(event);
                Menu::HandleChar(charEvent->keycode);
                continue;
            }

            // Mouse move events
            if (event->eventType == RE::INPUT_EVENT_TYPE::kMouseMove) {
                Menu::OnMouseMove();
                continue;
            }

            auto* button = event->AsButtonEvent();
            if (!button) continue;

            auto key = button->GetIDCode();
            auto device = event->GetDevice();

            // Mouse events
            if (device == RE::INPUT_DEVICE::kMouse) {
                if (key == MOUSE_LEFT_BUTTON) {
                    if (button->IsDown()) {
                        Menu::OnMouseDown();
                    } else if (button->IsUp()) {
                        Menu::OnMouseUp();
                    }
                }
                // Ignore scroll wheel and other mouse buttons
                continue;
            }

            // Track shift key state
            if (device == RE::INPUT_DEVICE::kKeyboard) {
                if (key == RE::BSKeyboardDevice::Key::kLeftShift ||
                    key == RE::BSKeyboardDevice::Key::kRightShift)
                {
                    s_shiftHeld = button->IsDown() || button->IsPressed();
                    if (button->IsUp()) s_shiftHeld = false;
                    continue;
                }
            }

            if (!button->IsDown()) continue;

            // Gamepad
            if (device == RE::INPUT_DEVICE::kGamepad) {
                if (key == GAMEPAD_A) Menu::Confirm();
                else if (key == GAMEPAD_B) Menu::Cancel();
                else if (key == GAMEPAD_DPAD_LEFT) Menu::HandleArrowLeft(false);
                else if (key == GAMEPAD_DPAD_RIGHT) Menu::HandleArrowRight(false);
                continue;
            }

            // Keyboard
            if (device == RE::INPUT_DEVICE::kKeyboard) {
                switch (key) {
                    case RE::BSKeyboardDevice::Key::kEnter:
                        Menu::Confirm();
                        break;
                    case RE::BSKeyboardDevice::Key::kEscape:
                        Menu::Cancel();
                        break;
                    case RE::BSKeyboardDevice::Key::kBackspace:
                        Menu::HandleBackspace();
                        break;
                    case RE::BSKeyboardDevice::Key::kDelete:
                        Menu::HandleDelete();
                        break;
                    case RE::BSKeyboardDevice::Key::kLeft:
                        Menu::HandleArrowLeft(s_shiftHeld);
                        break;
                    case RE::BSKeyboardDevice::Key::kRight:
                        Menu::HandleArrowRight(s_shiftHeld);
                        break;
                    case RE::BSKeyboardDevice::Key::kHome:
                        Menu::HandleHome(s_shiftHeld);
                        break;
                    case RE::BSKeyboardDevice::Key::kEnd:
                        Menu::HandleEnd(s_shiftHeld);
                        break;
                    case RE::BSKeyboardDevice::Key::kA:
                        // Ctrl+A = select all. But with AllowTextInput(true),
                        // 'a' arrives as CharEvent. kA ButtonEvent only fires
                        // when a modifier is held and the char is suppressed.
                        // So if we get kA as a ButtonEvent, treat as Ctrl+A.
                        Menu::HandleSelectAll();
                        break;
                    default:
                        break;
                }
            }
        }

        return RE::BSEventNotifyControl::kContinue;
    }
}
