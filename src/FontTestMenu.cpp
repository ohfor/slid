#include "FontTestMenu.h"
#include "ScaleformUtil.h"
#include "TranslationService.h"

namespace FontTestMenu {

    static Menu* g_activeMenu = nullptr;

    // Font names — must match embedded font names in SLIDConfig.swf
    static constexpr const char* FONT_INTL = "Noto Sans";                // Latin, Cyrillic, Greek
    static constexpr const char* FONT_CJK  = "Noto Sans CJK SC Regular"; // CJK + Korean

    // Hardcoded language data — not translated, intentionally static
    struct LanguageEntry {
        const char* name;
        const char* sample;
        bool        cjk;  // true → use FONT_CJK, false → use FONT_INTL
    };

    // Language data initialized at first access (non-constexpr due to UTF-8 literals)
    static const LanguageEntry* GetLanguages() {
        static const LanguageEntry entries[] = {
            {"English",              "The quick brown fox jumps over the lazy dog.",                                                     false},
            {"French",               "Portez ce vieux whisky au juge blond qui fume.",                                                  false},
            {"German",               "Franz jagt im komplett verwahrlosten Taxi quer durch Bayern.",                                        false},
            {"Italian",              "Ma la volpe, col suo balzo, ha raggiunto il quieto Fido.",                                         false},
            {"Spanish",              "El veloz murci" "\xC3\xA9" "lago hind" "\xC3\xBA" " com" "\xC3\xAD" "a feliz cardillo y kiwi.",   false},
            {"Russian",              "\xD0\xA1\xD1\x8A" "\xD0\xB5\xD1\x88\xD1\x8C" " " "\xD0\xB5\xD1\x89\xD1\x91" " " "\xD1\x8D\xD1\x82\xD0\xB8\xD1\x85" " " "\xD0\xBC\xD1\x8F\xD0\xB3\xD0\xBA\xD0\xB8\xD1\x85" " " "\xD1\x84\xD1\x80\xD0\xB0\xD0\xBD\xD1\x86\xD1\x83\xD0\xB7\xD1\x81\xD0\xBA\xD0\xB8\xD1\x85" " " "\xD0\xB1\xD1\x83\xD0\xBB\xD0\xBE\xD0\xBA" ", " "\xD0\xB4\xD0\xB0" " " "\xD0\xB2\xD1\x8B\xD0\xBF\xD0\xB5\xD0\xB9" " " "\xD1\x87\xD0\xB0\xD1\x8E" ".", false},
            {"Polish",               "Za" "\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87" " g" "\xC4\x99\xC5\x9B" "l" "\xC4\x85" " ja" "\xC5\xBA\xC5\x84" ".",                                     false},
            {"Czech",                "P" "\xC5\x99\xC3\xAD" "li" "\xC5\xA1" " " "\xC5\xBE" "lu" "\xC5\xA5" "ou" "\xC4\x8D" "k" "\xC3\xBD" " k" "\xC5\xAF\xC5\x88" " " "\xC3\xBA" "p" "\xC4\x9B" "l " "\xC4\x8F\xC3\xA1" "belsk" "\xC3\xA9" " " "\xC3\xB3" "dy.", false},
            {"Turkish",              "Pijamal" "\xC4\xB1" " hasta ya" "\xC4\x9F\xC4\xB1" "z " "\xC5\x9F" "of" "\xC3\xB6" "re " "\xC3\xA7" "abucak g" "\xC3\xBC" "vendi.", false},
            {"Japanese",             "\xE3\x81\x84\xE3\x82\x8D\xE3\x81\xAF\xE3\x81\xAB\xE3\x81\xBB\xE3\x81\xB8\xE3\x81\xA8" " " "\xE3\x81\xA1\xE3\x82\x8A\xE3\x81\xAC\xE3\x82\x8B\xE3\x82\x92" " " "\xE3\x82\x8F\xE3\x81\x8B\xE3\x82\x88\xE3\x81\x9F\xE3\x82\x8C\xE3\x81\x9D" " " "\xE3\x81\xA4\xE3\x81\xAD\xE3\x81\xAA\xE3\x82\x89\xE3\x82\x80", true},
            {"Korean",               "\xED\x82\xA4\xEC\x8A\xA4\xEC\x9D\x98" " " "\xEA\xB3\xA0\xEC\x9C\xA0\xEC\xA1\xB0\xEA\xB1\xB4\xEC\x9D\x80" " " "\xEC\x9E\x85\xEC\x88\xA0\xEB\x81\xBC\xEB\xA6\xAC" " " "\xEB\xA7\x8C\xEB\x82\x98\xEC\x95\xBC" " " "\xED\x95\xA9\xEB\x8B\x88\xEB\x8B\xA4" ".", true},
            {"Simplified Chinese",   "\xE5\xA4\xA9\xE5\x9C\xB0\xE7\x8E\x84\xE9\xBB\x84" "\xEF\xBC\x8C" "\xE5\xAE\x87\xE5\xAE\x99\xE6\xB4\xAA\xE8\x8D\x92" "\xE3\x80\x82" "\xE6\x97\xA5\xE6\x9C\x88\xE7\x9B\x88\xE6\x98\x83" "\xEF\xBC\x8C" "\xE8\xBE\xB0\xE5\xAE\xBF\xE5\x88\x97\xE5\xBC\xA0" "\xE3\x80\x82", true},
            {"Traditional Chinese",  "\xE5\xA4\xA9\xE5\x9C\xB0\xE7\x8E\x84\xE9\xBB\x83" "\xEF\xBC\x8C" "\xE5\xAE\x87\xE5\xAE\x99\xE6\xB4\xAA\xE8\x8D\x92" "\xE3\x80\x82" "\xE6\x97\xA5\xE6\x9C\x88\xE7\x9B\x88\xE6\x98\x83" "\xEF\xBC\x8C" "\xE8\xBE\xB0\xE5\xAE\xBF\xE5\x88\x97\xE5\xBC\xB5" "\xE3\x80\x82", true},
        };
        return entries;
    }
    static constexpr int kLanguageCount = 13;

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
                logger::info("FontTestMenu: loaded SWF {}", FILE_NAME);
            } else {
                logger::error("FontTestMenu: failed to load SWF {}", FILE_NAME);
            }
        }
    }

    void Menu::Register() {
        auto ui = RE::UI::GetSingleton();
        if (ui) {
            ui->Register(MENU_NAME, Create);
            logger::info("FontTestMenu registered");
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
                g_activeMenu = nullptr;
                return RE::UI_MESSAGE_RESULTS::kHandled;
            }
            default:
                return RE::IMenu::ProcessMessage(a_message);
        }
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

        // Background dim
        ScaleformUtil::DrawFilledRect(uiMovie.get(), "_dim", 1, 0.0, 0.0,
            static_cast<double>(screenW), static_cast<double>(screenH), 0x000000, 50);

        // Create panel clip
        RE::GFxValue panelArgs[2];
        panelArgs[0].SetString("fontTestPanel");
        panelArgs[1].SetNumber(100);
        m_root.Invoke("createEmptyMovieClip", &m_panel, panelArgs, 2);

        RE::GFxValue posX, posY;
        posX.SetNumber(static_cast<double>(panelX));
        posY.SetNumber(static_cast<double>(panelY));
        m_panel.SetMember("_x", posX);
        m_panel.SetMember("_y", posY);

        // Panel background
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

        // Title
        float titleY = PADDING;
        CreateTextField(m_panel, "titleText", 10, PADDING,
            titleY - static_cast<float>(ScaleformUtil::TextYCorrection(static_cast<int>(HEADER_SIZE))),
            POPUP_W - (PADDING * 2), HEADER_SIZE + 10);
        RE::GFxValue titleField;
        m_panel.GetMember("titleText", &titleField);
        if (!titleField.IsUndefined()) {
            SetTextFormat(titleField, FONT_INTL, static_cast<int>(HEADER_SIZE),
                          COLOR_HEADER, "center", false, false);
            RE::GFxValue textVal;
            std::string title = T("$SLID_FontTestTitle");
            textVal.SetString(title.c_str());
            titleField.SetMember("htmlText", textVal);
        }

        // Separator line
        float sepY = titleY + HEADER_SIZE + 12.0f;
        {
            RE::GFxValue lineClipArgs[2];
            lineClipArgs[0].SetString("separator");
            lineClipArgs[1].SetNumber(11);
            RE::GFxValue lineClip;
            m_panel.Invoke("createEmptyMovieClip", &lineClip, lineClipArgs, 2);

            RE::GFxValue styleArgs[3];
            styleArgs[0].SetNumber(1.0);
            styleArgs[1].SetNumber(static_cast<double>(COLOR_BORDER));
            styleArgs[2].SetNumber(50.0);
            lineClip.Invoke("lineStyle", nullptr, styleArgs, 3);

            RE::GFxValue pt[2];
            pt[0].SetNumber(static_cast<double>(PADDING)); pt[1].SetNumber(static_cast<double>(sepY));
            lineClip.Invoke("moveTo", nullptr, pt, 2);
            pt[0].SetNumber(static_cast<double>(POPUP_W - PADDING));
            lineClip.Invoke("lineTo", nullptr, pt, 2);
        }

        // Scrollable content area
        float contentY = sepY + 8.0f;
        float contentH = POPUP_H - contentY - CONTENT_BOTTOM;
        float contentW = POPUP_W - (PADDING * 2) - SCROLLBAR_W - 8.0f;

        // Create content clip with scroll masking via _scrollRect-style manual clipping
        // AVM1 doesn't have scrollRect, so we use a mask clip
        RE::GFxValue maskClipArgs[2];
        maskClipArgs[0].SetString("contentMask");
        maskClipArgs[1].SetNumber(50);
        RE::GFxValue maskClip;
        m_panel.Invoke("createEmptyMovieClip", &maskClip, maskClipArgs, 2);
        {
            RE::GFxValue fillArgs[2];
            fillArgs[0].SetNumber(0xFF00FF);
            fillArgs[1].SetNumber(100.0);
            maskClip.Invoke("beginFill", nullptr, fillArgs, 2);

            RE::GFxValue pt[2];
            pt[0].SetNumber(static_cast<double>(PADDING));
            pt[1].SetNumber(static_cast<double>(contentY));
            maskClip.Invoke("moveTo", nullptr, pt, 2);
            pt[0].SetNumber(static_cast<double>(POPUP_W - PADDING));
            maskClip.Invoke("lineTo", nullptr, pt, 2);
            pt[1].SetNumber(static_cast<double>(contentY + contentH));
            maskClip.Invoke("lineTo", nullptr, pt, 2);
            pt[0].SetNumber(static_cast<double>(PADDING));
            maskClip.Invoke("lineTo", nullptr, pt, 2);
            pt[1].SetNumber(static_cast<double>(contentY));
            maskClip.Invoke("lineTo", nullptr, pt, 2);
            maskClip.Invoke("endFill", nullptr, nullptr, 0);
        }

        RE::GFxValue contentArgs[2];
        contentArgs[0].SetString("contentClip");
        contentArgs[1].SetNumber(51);
        m_panel.Invoke("createEmptyMovieClip", &m_contentClip, contentArgs, 2);

        // Set mask
        RE::GFxValue maskRef;
        maskRef = maskClip;
        m_contentClip.Invoke("setMask", nullptr, &maskRef, 1);

        // Place content clip at content origin
        RE::GFxValue contentX;
        contentX.SetNumber(static_cast<double>(PADDING));
        m_contentClip.SetMember("_x", contentX);
        RE::GFxValue contentYVal;
        contentYVal.SetNumber(static_cast<double>(contentY));
        m_contentClip.SetMember("_y", contentYVal);

        // Create language rows inside content clip
        int depth = 100;
        for (int i = 0; i < TOTAL_LANGUAGES; ++i) {
            float rowY = static_cast<float>(i) * ROW_HEIGHT;
            std::string nameField = "lang_name_" + std::to_string(i);
            std::string sampleField = "lang_sample_" + std::to_string(i);

            // Language name (gold)
            CreateTextField(m_contentClip, nameField.c_str(), depth++,
                0.0, rowY - static_cast<float>(ScaleformUtil::TextYCorrection(static_cast<int>(TITLE_SIZE))),
                contentW, TITLE_SIZE + 8);
            RE::GFxValue nameText;
            m_contentClip.GetMember(nameField.c_str(), &nameText);
            if (!nameText.IsUndefined()) {
                SetTextFormat(nameText, FONT_INTL, static_cast<int>(TITLE_SIZE),
                              COLOR_TITLE, "left", false, false);
                RE::GFxValue val;
                val.SetString(GetLanguages()[i].name);
                nameText.SetMember("text", val);
            }

            // Sample text (grey)
            CreateTextField(m_contentClip, sampleField.c_str(), depth++,
                0.0, rowY + TITLE_SIZE + 8.0f - static_cast<float>(ScaleformUtil::TextYCorrection(static_cast<int>(SAMPLE_SIZE))),
                contentW, SAMPLE_SIZE + 8);
            RE::GFxValue sampleText;
            m_contentClip.GetMember(sampleField.c_str(), &sampleText);
            if (!sampleText.IsUndefined()) {
                const char* sampleFont = GetLanguages()[i].cjk ? FONT_CJK : FONT_INTL;
                SetTextFormat(sampleText, sampleFont, static_cast<int>(SAMPLE_SIZE),
                              COLOR_SAMPLE, "left", true, true);
                RE::GFxValue val;
                val.SetString(GetLanguages()[i].sample);
                sampleText.SetMember("text", val);
            }
        }

        // Scroll state
        float totalContentH = static_cast<float>(TOTAL_LANGUAGES) * ROW_HEIGHT;
        m_maxScroll = (totalContentH > contentH)
            ? static_cast<int>((totalContentH - contentH) / ROW_HEIGHT) + 1
            : 0;
        m_scrollOffset = 0;

        // Scrollbar track
        if (m_maxScroll > 0) {
            float scrollX = POPUP_W - PADDING - SCROLLBAR_W;
            RE::GFxValue trackArgs[2];
            trackArgs[0].SetString("scrollTrack");
            trackArgs[1].SetNumber(200);
            m_panel.Invoke("createEmptyMovieClip", &m_scrollTrack, trackArgs, 2);
            {
                RE::GFxValue fillArgs[2];
                fillArgs[0].SetNumber(static_cast<double>(COLOR_SCROLL_TRACK));
                fillArgs[1].SetNumber(60.0);
                m_scrollTrack.Invoke("beginFill", nullptr, fillArgs, 2);

                RE::GFxValue pt[2];
                pt[0].SetNumber(static_cast<double>(scrollX));
                pt[1].SetNumber(static_cast<double>(contentY));
                m_scrollTrack.Invoke("moveTo", nullptr, pt, 2);
                pt[0].SetNumber(static_cast<double>(scrollX + SCROLLBAR_W));
                m_scrollTrack.Invoke("lineTo", nullptr, pt, 2);
                pt[1].SetNumber(static_cast<double>(contentY + contentH));
                m_scrollTrack.Invoke("lineTo", nullptr, pt, 2);
                pt[0].SetNumber(static_cast<double>(scrollX));
                m_scrollTrack.Invoke("lineTo", nullptr, pt, 2);
                pt[1].SetNumber(static_cast<double>(contentY));
                m_scrollTrack.Invoke("lineTo", nullptr, pt, 2);
                m_scrollTrack.Invoke("endFill", nullptr, nullptr, 0);
            }

            // Scrollbar thumb (drawn in UpdateScroll)
            RE::GFxValue thumbArgs[2];
            thumbArgs[0].SetString("scrollThumb");
            thumbArgs[1].SetNumber(201);
            m_panel.Invoke("createEmptyMovieClip", &m_scrollThumb, thumbArgs, 2);
        }

        // Close button via ButtonBar
        {
            std::string okLabel = T("$SLID_OK");
            std::vector<ButtonDef> defs = {{okLabel, 100.0}};
            double btnCenterX = static_cast<double>(panelX) + POPUP_W / 2.0;
            double btnY = static_cast<double>(panelY) + POPUP_H - PADDING - ButtonColors::HEIGHT;
            m_buttonBar.Init(uiMovie.get(), "_ftBtn", 300, defs, btnCenterX, btnY);
            m_buttonBar.Draw(0, m_hoverIndex);
        }

        UpdateScroll();
    }

    void Menu::UpdateScroll() {
        if (!uiMovie || m_contentClip.IsUndefined()) return;

        // Move content clip vertically based on scroll offset
        float offsetY = -static_cast<float>(m_scrollOffset) * ROW_HEIGHT;

        // Get the base contentY from layout
        float sepY = PADDING + HEADER_SIZE + 12.0f + 8.0f;
        RE::GFxValue yVal;
        yVal.SetNumber(static_cast<double>(sepY + offsetY));
        m_contentClip.SetMember("_y", yVal);

        // Update scrollbar thumb
        if (m_maxScroll > 0 && !m_scrollThumb.IsUndefined()) {
            // Clear previous drawing
            m_scrollThumb.Invoke("clear", nullptr, nullptr, 0);

            float contentY = sepY;
            float contentH = POPUP_H - contentY - CONTENT_BOTTOM;
            float scrollX = POPUP_W - PADDING - SCROLLBAR_W;
            float thumbH = std::max(20.0f, contentH / static_cast<float>(m_maxScroll + VISIBLE_ROWS) * VISIBLE_ROWS);
            float trackRange = contentH - thumbH;
            float thumbY = contentY + (m_maxScroll > 0
                ? (static_cast<float>(m_scrollOffset) / static_cast<float>(m_maxScroll)) * trackRange
                : 0.0f);

            RE::GFxValue fillArgs[2];
            fillArgs[0].SetNumber(static_cast<double>(COLOR_SCROLL_THUMB));
            fillArgs[1].SetNumber(80.0);
            m_scrollThumb.Invoke("beginFill", nullptr, fillArgs, 2);

            RE::GFxValue pt[2];
            pt[0].SetNumber(static_cast<double>(scrollX));
            pt[1].SetNumber(static_cast<double>(thumbY));
            m_scrollThumb.Invoke("moveTo", nullptr, pt, 2);
            pt[0].SetNumber(static_cast<double>(scrollX + SCROLLBAR_W));
            m_scrollThumb.Invoke("lineTo", nullptr, pt, 2);
            pt[1].SetNumber(static_cast<double>(thumbY + thumbH));
            m_scrollThumb.Invoke("lineTo", nullptr, pt, 2);
            pt[0].SetNumber(static_cast<double>(scrollX));
            m_scrollThumb.Invoke("lineTo", nullptr, pt, 2);
            pt[1].SetNumber(static_cast<double>(thumbY));
            m_scrollThumb.Invoke("lineTo", nullptr, pt, 2);
            m_scrollThumb.Invoke("endFill", nullptr, nullptr, 0);
        }
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
            logger::info("FontTestMenu::InputHandler registered");
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
            // Mouse move → update hover
            if (event->GetEventType() == RE::INPUT_EVENT_TYPE::kMouseMove) {
                if (g_activeMenu && g_activeMenu->uiMovie) {
                    RE::GFxValue xVal, yVal;
                    g_activeMenu->uiMovie->GetVariable(&xVal, "_root._xmouse");
                    g_activeMenu->uiMovie->GetVariable(&yVal, "_root._ymouse");
                    float mx = static_cast<float>(xVal.GetNumber());
                    float my = static_cast<float>(yVal.GetNumber());
                    int hit = g_activeMenu->m_buttonBar.HitTest(mx, my);
                    if (hit != g_activeMenu->m_hoverIndex) {
                        g_activeMenu->m_hoverIndex = hit;
                        g_activeMenu->m_buttonBar.Draw(0, hit);
                    }
                }
                continue;
            }

            if (auto* button = event->AsButtonEvent(); button && button->IsDown()) {
                auto device = button->GetDevice();
                auto key = button->GetIDCode();

                bool isClose = false;
                bool isScrollUp = false;
                bool isScrollDown = false;

                if (device == RE::INPUT_DEVICE::kKeyboard) {
                    isClose = (key == RE::BSKeyboardDevice::Key::kEnter ||
                               key == RE::BSKeyboardDevice::Key::kEscape);
                    isScrollUp = (key == RE::BSKeyboardDevice::Key::kUp);
                    isScrollDown = (key == RE::BSKeyboardDevice::Key::kDown);
                } else if (device == RE::INPUT_DEVICE::kGamepad) {
                    isClose = (key == ScaleformUtil::GAMEPAD_A || key == ScaleformUtil::GAMEPAD_B);
                    isScrollUp = (key == ScaleformUtil::GAMEPAD_DPAD_UP);
                    isScrollDown = (key == ScaleformUtil::GAMEPAD_DPAD_DOWN);
                } else if (device == RE::INPUT_DEVICE::kMouse) {
                    // Mouse wheel: IDCode 8 = wheel up, 9 = wheel down
                    if (key == 8) isScrollUp = true;
                    if (key == 9) isScrollDown = true;

                    // Left click on close button
                    if (key == 0) {
                        float mouseX = 0.0f, mouseY = 0.0f;
                        if (g_activeMenu->uiMovie) {
                            RE::GFxValue xVal, yVal;
                            g_activeMenu->uiMovie->GetVariable(&xVal, "_root._xmouse");
                            g_activeMenu->uiMovie->GetVariable(&yVal, "_root._ymouse");
                            mouseX = static_cast<float>(xVal.GetNumber());
                            mouseY = static_cast<float>(yVal.GetNumber());
                        }
                        if (g_activeMenu->m_buttonBar.HitTest(mouseX, mouseY) == 0) {
                            Menu::Hide();
                            return RE::BSEventNotifyControl::kStop;
                        }
                    }
                }

                if (isClose) {
                    Menu::Hide();
                    return RE::BSEventNotifyControl::kStop;
                }

                if (isScrollUp && g_activeMenu->m_scrollOffset > 0) {
                    g_activeMenu->m_scrollOffset--;
                    g_activeMenu->UpdateScroll();
                    return RE::BSEventNotifyControl::kStop;
                }

                if (isScrollDown && g_activeMenu->m_scrollOffset < g_activeMenu->m_maxScroll) {
                    g_activeMenu->m_scrollOffset++;
                    g_activeMenu->UpdateScroll();
                    return RE::BSEventNotifyControl::kStop;
                }
            }
        }

        return RE::BSEventNotifyControl::kStop;  // Consume all input while open
    }

}  // namespace FontTestMenu
