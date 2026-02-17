#pragma once

#include "DirectionalInput.h"
#include "ScaleformUtil.h"

namespace SLIDMenu {
    // Menu name used for registration and lookup
    constexpr std::string_view MENU_NAME = "SLIDConfigMenu";

    // SWF filename (without extension) - must exist in Data/Interface/
    constexpr std::string_view FILE_NAME = "SLIDConfig";

    // Input device tracking
    enum class LastDevice {
        kKeyboard,
        kGamepad,
        kMouse
    };

    // Focus state (three focusable actors)
    enum class FocusTarget { kFilterPanel, kCatchAllPanel, kActionBar };

    class ConfigMenu : public RE::IMenu {
    public:
        static void Register();
        static RE::IMenu* Create();

        ConfigMenu();
        ~ConfigMenu() override = default;

        // RE::IMenu overrides
        void PostCreate() override;
        RE::UI_MESSAGE_RESULTS ProcessMessage(RE::UIMessage& a_message) override;

        // Open/close helpers
        static void Show(const std::string& a_networkName);
        static void Hide();
        static void RequestClose();
        static bool IsOpen();

        // Haptic feedback (stubs)
        static void HapticBrief();
        static void HapticMedium();

    private:
        friend class InputHandler;

        // Panel chrome
        void DrawUI();

        // Action bar orchestration
        void DrawActionBar();
        void UpdateActionBar();

        // Guide text — writes correct text for current focus state
        void UpdateGuideText();

        // Pipeline operations
        void RunSort();
        void RunSweep();
        void RunWhoosh();
        void RecalcPredictions();
        void BuildStagesFromNetwork();

        FocusTarget m_focus = FocusTarget::kActionBar;
        int m_actionIndex = 1;  // 0=Whoosh, 1=Sort, 2=Sweep, 3=Defaults, 4=Close
        LastDevice m_lastDevice = LastDevice::kGamepad;

        // Mouse hover state for action bar (owned by orchestrator)
        bool m_hoverActionBar = false;
        int  m_hoverActionIndex = -1;

        // Convenience
        bool InActionBar() const { return m_focus == FocusTarget::kActionBar; }
    };

    // Input handler — thin focus router
    class InputHandler : public RE::BSTEventSink<RE::InputEvent*> {
    public:
        static InputHandler* GetSingleton();
        static void Register();

        RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event,
                                               RE::BSTEventSource<RE::InputEvent*>* a_source) override;

        static void ResetRepeat();

    private:
        void HandleMouseEvent(RE::InputEvent* event);

        // Shared helpers for input dispatch
        struct ParsedInput {
            bool up = false, down = false, left = false, right = false;
            bool confirm = false, cancel = false, action = false;
            bool tab = false;
            bool liftToggle = false;  // L3 (gamepad 0x0040)
        };
        static ParsedInput ParseButton(uint32_t a_key, RE::INPUT_DEVICE a_device);
        static void ActivateButton(ConfigMenu& a_menu, int a_index);
        static void NavigateVertical(ConfigMenu& a_menu, int a_dir);

        InputHandler() = default;
        InputHandler(const InputHandler&) = delete;
        InputHandler& operator=(const InputHandler&) = delete;

        DirectionalInput::ThumbstickState m_thumbState;
        DirectionalInput::RepeatState m_repeatState;
    };

    // Listens for ContainerMenu close to re-open config menu
    class ContainerCloseListener : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
    public:
        static ContainerCloseListener* GetSingleton();
        static void Register();

        RE::BSEventNotifyControl ProcessEvent(
            const RE::MenuOpenCloseEvent* a_event,
            RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_source) override;

    private:
        ContainerCloseListener() = default;
        ContainerCloseListener(const ContainerCloseListener&) = delete;
        ContainerCloseListener& operator=(const ContainerCloseListener&) = delete;
    };
}
