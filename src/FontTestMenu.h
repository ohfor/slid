#pragma once

#include <RE/Skyrim.h>
#include "ButtonBar.h"

namespace FontTestMenu {

    class Menu : public RE::IMenu {
    public:
        static constexpr std::string_view MENU_NAME = "SLID_FontTestMenu"sv;
        static constexpr std::string_view FILE_NAME = "SLIDConfig"sv;

        Menu();
        ~Menu() override = default;

        static void Register();
        static RE::IMenu* Create();
        static void Show();
        static void Hide();
        static bool IsOpen();

        // IMenu
        RE::UI_MESSAGE_RESULTS ProcessMessage(RE::UIMessage& a_message) override;

    private:
        friend class InputHandler;

        void BuildUI();
        void CreateTextField(RE::GFxValue& a_parent, const char* a_name, int a_depth,
                             double a_x, double a_y, double a_w, double a_h);
        void SetTextFormat(RE::GFxValue& a_textField, const char* a_font, int a_size,
                           uint32_t a_color, const char* a_align, bool a_multiline, bool a_wordWrap);
        void UpdateScroll();

        RE::GFxValue m_root;
        RE::GFxValue m_panel;
        RE::GFxValue m_contentClip;  // scrollable content container
        RE::GFxValue m_scrollTrack;
        RE::GFxValue m_scrollThumb;

        int m_scrollOffset = 0;
        int m_maxScroll = 0;
        int m_hoverIndex = -1;

        ButtonBar m_buttonBar;

        // Layout constants
        static constexpr float POPUP_W = 600.0f;
        static constexpr float POPUP_H = 560.0f;
        static constexpr float PADDING = 30.0f;
        static constexpr float HEADER_SIZE = 20.0f;
        static constexpr float TITLE_SIZE = 14.0f;
        static constexpr float SAMPLE_SIZE = 13.0f;
        static constexpr float ROW_HEIGHT = 56.0f;
        static constexpr float CONTENT_TOP = 60.0f;      // below title + separator
        static constexpr float CONTENT_BOTTOM = 80.0f;   // space for close button
        static constexpr float SCROLLBAR_W = 6.0f;
        static constexpr int   VISIBLE_ROWS = 7;
        static constexpr int   TOTAL_LANGUAGES = 13;

        // Colors
        static constexpr std::uint32_t COLOR_BG = 0x1A1A1A;
        static constexpr std::uint32_t COLOR_BORDER = 0x8B7355;
        static constexpr std::uint32_t COLOR_HEADER = 0xD4AF37;
        static constexpr std::uint32_t COLOR_TITLE = 0xD4AF37;
        static constexpr std::uint32_t COLOR_SAMPLE = 0xAAAAAA;
        static constexpr std::uint32_t COLOR_SCROLL_TRACK = 0x333333;
        static constexpr std::uint32_t COLOR_SCROLL_THUMB = 0x8B7355;
    };

    class InputHandler : public RE::BSTEventSink<RE::InputEvent*> {
    public:
        static InputHandler* GetSingleton();
        static void Register();

        RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event,
                                               RE::BSTEventSource<RE::InputEvent*>* a_source) override;
    };

}  // namespace FontTestMenu
