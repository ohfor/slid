#include "UIHelper.h"
#include "NetworkManager.h"
#include "Feedback.h"
#include "TranslationService.h"

// MessageBoxData::~MessageBoxData is declared override in CommonLibSSE-NG but never defined,
// because the game provides it at runtime. We need it at link time since we allocate
// MessageBoxData on the heap. The destructor just needs to run member destructors
// (BSString, BSTArray, BSTSmartPointer) which all have proper RAII semantics.
namespace RE {
    MessageBoxData::~MessageBoxData() = default;
}

void UIHelper::ShowMessageBox(const std::string& a_body,
                               const std::vector<std::string>& a_buttons,
                               Callback a_callback) {
    auto* msgBoxData = new RE::MessageBoxData();
    msgBoxData->bodyText = a_body;

    for (const auto& btn : a_buttons) {
        msgBoxData->buttonText.push_back(btn.c_str());
    }

    auto callback = RE::BSTSmartPointer<RE::IMessageBoxCallback>(
        new CallbackAdapter(std::move(a_callback)));
    msgBoxData->callback = callback;
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
