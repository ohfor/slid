#pragma once

#include <functional>
#include <string>
#include <vector>

class UIHelper {
public:
    using Callback = std::function<void(int buttonIndex)>;

    // Show a dynamic MessageBox with callback on button press
    static void ShowMessageBox(const std::string& a_body,
                               const std::vector<std::string>& a_buttons,
                               Callback a_callback);

    // Dismantle network confirmation flow
    static void BeginDismantleNetwork(RE::TESObjectREFR* a_masterRef);

private:
    class CallbackAdapter : public RE::IMessageBoxCallback {
    public:
        CallbackAdapter(Callback a_fn) : m_fn(std::move(a_fn)) {}
        void Run(Message a_msg) override {
            if (m_fn) {
                int idx = static_cast<int>(a_msg);
                logger::debug("MessageBox callback: index = {}", idx);
                m_fn(idx);
            }
        }
    private:
        Callback m_fn;
    };
};
