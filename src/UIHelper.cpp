#include "UIHelper.h"
#include "NetworkManager.h"
#include "Feedback.h"
#include "TranslationService.h"

void UIHelper::ShowMessageBox(const std::string& a_body,
                               const std::vector<std::string>& a_buttons,
                               Callback a_callback) {
    // MessageBoxData MUST be created through the game's message-data factory, not
    // hand-allocated. The factory runs the game's native constructor, which sets
    // the internal fields (unk38..unk4F) the MessageBoxMenu relies on to handle
    // button input and dismissal. A raw `new RE::MessageBoxData()` zero-inits
    // those fields, producing a box that renders but accepts no input and never
    // closes — the cause of the SLID freeze where the master-activation and
    // Remove-Link popups locked the UI (reports E & F).
    auto* factoryManager   = RE::MessageDataFactoryManager::GetSingleton();
    auto* interfaceStrings = RE::InterfaceStrings::GetSingleton();
    if (!factoryManager || !interfaceStrings) {
        logger::error("ShowMessageBox: message-data factory unavailable");
        return;
    }

    auto* factory = factoryManager->GetCreator<RE::MessageBoxData>(interfaceStrings->messageBoxData);
    if (!factory) {
        logger::error("ShowMessageBox: MessageBoxData creator not found");
        return;
    }

    auto* msgBoxData = factory->Create();
    if (!msgBoxData) {
        logger::error("ShowMessageBox: MessageBoxData creation failed");
        return;
    }

    msgBoxData->bodyText = a_body;
    for (const auto& btn : a_buttons) {
        msgBoxData->buttonText.push_back(btn.c_str());
    }

    msgBoxData->callback = RE::BSTSmartPointer<RE::IMessageBoxCallback>(
        new CallbackAdapter(std::move(a_callback)));

    msgBoxData->QueueMessage();
}

// --- Dismantle Network Flow ---

void UIHelper::BeginDismantleNetwork(RE::TESObjectREFR* a_masterRef) {
    if (!a_masterRef) {
        RE::DebugNotification(T("$SLID_ErrNoTarget").c_str());
        return;
    }

    auto formID = a_masterRef->GetFormID();
    auto* mgr = NetworkManager::GetSingleton();

    auto networkName = mgr->FindNetworkByMaster(formID);
    if (networkName.empty()) {
        RE::DebugNotification(T("$SLID_ErrNotNetworkMaster").c_str());
        Feedback::OnError();
        return;
    }

    std::string body = TF("$SLID_ConfirmDismantleNetwork", networkName);

    ShowMessageBox(body, {T("$SLID_Yes"), T("$SLID_No")}, [networkName, formID](int idx) {
        if (idx == 0) {
            SKSE::GetTaskInterface()->AddTask([networkName, formID]() {
                auto* mgr = NetworkManager::GetSingleton();
                if (mgr->RemoveNetwork(networkName)) {
                    std::string msg = TF("$SLID_NotifyNetworkDestroyed", networkName);
                    RE::DebugNotification(msg.c_str());
                    logger::info("Dismantled network '{}'", networkName);
                    if (auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(formID)) {
                        Feedback::OnDismantleNetwork(ref);
                    }
                } else {
                    RE::DebugNotification(T("$SLID_ErrDismantleFailed").c_str());
                    Feedback::OnError();
                }
            });
        }
    });
}
