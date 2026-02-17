#pragma once

#include <RE/Skyrim.h>

namespace WelcomeMenu {

    class Menu : public RE::IMenu {
    public:
        static constexpr std::string_view MENU_NAME = "SLID_WelcomeMenu"sv;
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
        void CreateHeader();
        void CreateBody();
        void CreateCheckbox();
        void CreateButton();
        void UpdateCheckboxVisual();

        // Helper methods
        void CreateTextField(RE::GFxValue& a_parent, const char* a_name, int a_depth,
                             double a_x, double a_y, double a_w, double a_h);
        void SetTextFormat(RE::GFxValue& a_textField, const char* a_font, int a_size,
                           uint32_t a_color, const char* a_align, bool a_multiline, bool a_wordWrap);

        RE::GFxValue m_root;
        RE::GFxValue m_panel;
        RE::GFxValue m_checkboxMark;
        bool m_dontShowAgain = false;

        // Layout constants
        static constexpr float POPUP_W = 680.0f;
        static constexpr float POPUP_H = 520.0f;
        static constexpr float PADDING = 30.0f;
        static constexpr float HEADER_SIZE = 24.0f;
        static constexpr float SUBHEADER_SIZE = 16.0f;
        static constexpr float BODY_SIZE = 14.0f;
        static constexpr float LINE_HEIGHT = 20.0f;
        static constexpr float SECTION_GAP = 24.0f;
        static constexpr float CHECKBOX_SIZE = 16.0f;
        static constexpr float BUTTON_W = 100.0f;
        static constexpr float BUTTON_H = 32.0f;

        // Colors
        static constexpr std::uint32_t COLOR_BG = 0x1A1A1A;
        static constexpr std::uint32_t COLOR_BORDER = 0x8B7355;
        static constexpr std::uint32_t COLOR_HEADER = 0xD4AF37;
        static constexpr std::uint32_t COLOR_SUBHEADER = 0xC9A227;
        static constexpr std::uint32_t COLOR_BODY = 0xCCCCCC;
        static constexpr std::uint32_t COLOR_CHECKBOX_BG = 0x333333;
        static constexpr std::uint32_t COLOR_CHECKBOX_BORDER = 0x666666;
        static constexpr std::uint32_t COLOR_CHECKBOX_MARK = 0xD4AF37;
        static constexpr std::uint32_t COLOR_BUTTON_BG = 0x3A3A3A;
        static constexpr std::uint32_t COLOR_BUTTON_BORDER = 0x8B7355;
        static constexpr std::uint32_t COLOR_BUTTON_TEXT = 0xFFFFFF;
    };

    class InputHandler : public RE::BSTEventSink<RE::InputEvent*> {
    public:
        static InputHandler* GetSingleton();
        static void Register();

        RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event,
                                               RE::BSTEventSource<RE::InputEvent*>* a_source) override;
    };

    // Call at potential trigger points
    void TryShowWelcome();

}  // namespace WelcomeMenu
