#include "ConsoleCommands.h"
#include "NetworkManager.h"
#include "VendorRegistry.h"
#include "ActivationHook.h"
#include "Distributor.h"
#include "UIHelper.h"
#include "Feedback.h"
#include "SLIDMenu.h"
#include "SummonChest.h"
#include "TagInputMenu.h"
#include "Settings.h"
#include "FilterRegistry.h"
#include "TranslationService.h"
#include "Version.h"
#include "WelcomeMenu.h"

#include <random>

// TESTopicInfoEvent is forward-declared in CommonLibSSE-NG but never defined.
// Layout from powerof3/CommonLibSSE (dev branch).
namespace RE {
    namespace REFREventCallbacks {
        class IEventCallback;
    }

    struct TESTopicInfoEvent {
        BSTSmartPointer<REFREventCallbacks::IEventCallback> callback;         // 00
        NiPointer<TESObjectREFR>                            speakerRef;       // 08
        FormID                                              topicInfoFormID;  // 10
        std::uint32_t                                       type;             // 14 (0=begin, 1=end)
        std::uint16_t                                       stage;            // 18
    };
    static_assert(sizeof(TESTopicInfoEvent) == 0x20);
}

namespace ConsoleCommands {

    namespace {
        float RandomJitter() {
            static std::mt19937 rng{std::random_device{}()};
            static std::uniform_real_distribution<float> dist(-6.0f, 6.0f);
            return dist(rng);
        }

        constexpr auto kPluginName = "SLID.esp"sv;
        constexpr RE::FormID kSpellIDs[] = {0x801, 0x803, 0x805, 0x807, 0x809, 0x80B, 0x816, 0x818};

        // --- Crosshair capture at spell-cast time ---
        //
        // Powers are self-cast lesser powers. Papyrus OnEffectStart fires on the VM thread,
        // potentially frames after the player pressed the button. By that time the crosshair
        // may have shifted (NPC walked in front, player turned, etc).
        //
        // We listen for TESSpellCastEvent (main thread, same frame as input) and snapshot
        // the crosshair target. Native functions consume the snapshot instead of reading
        // the live crosshair.

        std::atomic<RE::FormID> g_capturedTarget{0};
        std::unordered_set<RE::FormID> g_slidSpellIDs;

        class SpellCastListener : public RE::BSTEventSink<RE::TESSpellCastEvent> {
        public:
            static SpellCastListener* GetSingleton() {
                static SpellCastListener singleton;
                return &singleton;
            }

            RE::BSEventNotifyControl ProcessEvent(
                const RE::TESSpellCastEvent* a_event,
                RE::BSTEventSource<RE::TESSpellCastEvent>*) override {

                if (!a_event) return RE::BSEventNotifyControl::kContinue;

                // Only snapshot for SLID spells
                if (!g_slidSpellIDs.count(a_event->spell)) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                // Only care about player casts
                if (!a_event->object || a_event->object->GetFormID() != 0x14) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                // Snapshot crosshair target (main thread — safe to read CrosshairPickData)
                auto* crosshair = RE::CrosshairPickData::GetSingleton();
                if (crosshair) {
                    auto target = crosshair->target.get();
                    if (target) {
                        g_capturedTarget.store(target->GetFormID());
                        logger::debug("SpellCast: captured target {:08X} ({})",
                                      target->GetFormID(), target->GetName());
                    } else {
                        g_capturedTarget.store(0);
                    }
                } else {
                    g_capturedTarget.store(0);
                }

                return RE::BSEventNotifyControl::kContinue;
            }

        private:
            SpellCastListener() = default;
        };

        // Forward declarations — called from TopicInfoListener, defined below
        void OnVendorDialogueAccept();
        void OnVendorDialogueCancel();

        // --- TopicInfo event sink (vendor dialogue accept detection) ---
        //
        // TESTopicInfoEvent is forward-declared in CommonLibSSE-NG but never defined.
        // We define it here based on the common event pattern (speaker + info FormID).
        // The sink detects when our SLID_VendorAccept INFO is selected and calls
        // the accept logic directly in C++, bypassing Papyrus entirely.

        RE::FormID g_vendorAcceptInfoID = 0;  // resolved at registration time
        RE::FormID g_vendorCancelInfoID = 0;  // resolved at registration time

        class TopicInfoListener : public RE::BSTEventSink<RE::TESTopicInfoEvent> {
        public:
            static TopicInfoListener* GetSingleton() {
                static TopicInfoListener singleton;
                return &singleton;
            }

            RE::BSEventNotifyControl ProcessEvent(
                const RE::TESTopicInfoEvent* a_event,
                RE::BSTEventSource<RE::TESTopicInfoEvent>*) override {

                if (!a_event) return RE::BSEventNotifyControl::kContinue;
                if (a_event->type != 0) return RE::BSEventNotifyControl::kContinue;

                if (a_event->topicInfoFormID == g_vendorAcceptInfoID && g_vendorAcceptInfoID != 0) {
                    logger::info("TopicInfoEvent: matched SLID_VendorAccept ({:08X})", g_vendorAcceptInfoID);
                    OnVendorDialogueAccept();
                } else if (a_event->topicInfoFormID == g_vendorCancelInfoID && g_vendorCancelInfoID != 0) {
                    logger::info("TopicInfoEvent: matched SLID_VendorCancel ({:08X})", g_vendorCancelInfoID);
                    OnVendorDialogueCancel();
                }

                return RE::BSEventNotifyControl::kContinue;
            }

        private:
            TopicInfoListener() = default;
        };

        /// Consume the crosshair target that was captured at spell-cast time.
        /// Returns nullptr if nothing was captured (player wasn't aiming at anything).
        RE::TESObjectREFR* GetCapturedTarget() {
            auto formID = g_capturedTarget.exchange(0);
            if (formID == 0) return nullptr;
            return RE::TESForm::LookupByID<RE::TESObjectREFR>(formID);
        }

        // --- Helper: naming flow for new network creation ---
        void ShowNetworkNamingFlow(RE::FormID formID, const std::string& baseName) {
            SKSE::GetTaskInterface()->AddTask([formID, baseName]() {
                TagInputMenu::Menu::ShowWithCallback(T("$SLID_DlgNameLink"), baseName,
                    [formID](const std::string& chosenName) {
                        auto* mgr2 = NetworkManager::GetSingleton();

                        // Deduplicate the chosen name
                        auto existingNames = mgr2->GetNetworkNames();
                        std::string finalName = chosenName;
                        int suffix = 2;
                        while (true) {
                            bool taken = false;
                            for (const auto& n : existingNames) {
                                if (n == finalName) { taken = true; break; }
                            }
                            if (!taken) break;
                            finalName = chosenName + " " + std::to_string(suffix++);
                        }

                        if (mgr2->CreateNetwork(finalName, formID)) {
                            auto* ref2 = RE::TESForm::LookupByID<RE::TESObjectREFR>(formID);
                            if (ref2) {
                                Feedback::OnSetMaster(ref2);
                            }

                            std::string msg = TF("$SLID_NotifyNetworkCreated", finalName);
                            RE::DebugNotification(msg.c_str());

                            logger::info("SetMasterAuto: created network '{}' with master {:08X}",
                                         finalName, formID);

                            // Show welcome tutorial (first time only), then open config menu
                            WelcomeMenu::TryShowWelcome();
                            auto networkName = finalName;
                            SKSE::GetTaskInterface()->AddTask([networkName]() {
                                SLIDMenu::ConfigMenu::Show(networkName);
                            });
                        }
                    });
            });
        }

        // --- Native functions ---

        RE::BSFixedString SetMasterAuto(RE::StaticFunctionTag*) {
            auto* ref = GetCapturedTarget();
            if (!ref) {
                logger::error("SetMasterAuto: no crosshair target");
                RE::DebugNotification(T("$SLID_ErrNoTarget").c_str());
                Feedback::OnError();
                return "";
            }

            if (ref->As<RE::Actor>()) {
                logger::warn("SetMasterAuto: target {:08X} is an actor, not a container", ref->GetFormID());
                RE::DebugNotification(T("$SLID_ErrNoTarget").c_str());
                Feedback::OnError();
                return "";
            }

            if (!ref->GetContainer()) {
                logger::error("SetMasterAuto: target {:08X} ({}) is not a container",
                             ref->GetFormID(), ref->GetName());
                RE::DebugNotification(T("$SLID_ErrNoTarget").c_str());
                Feedback::OnError();
                return "";
            }

            auto* mgr = NetworkManager::GetSingleton();

            // Cannot designate a sell container as master
            if (mgr->GetSellContainerFormID() == ref->GetFormID()) {
                logger::warn("SetMasterAuto: target {:08X} is already the sell container", ref->GetFormID());
                RE::DebugNotification(T("$SLID_ErrSellAsMaster").c_str());
                Feedback::OnError();
                return "";
            }

            // Check if already a master — open config instead of error
            auto existingNet = mgr->FindNetworkByMaster(ref->GetFormID());
            if (!existingNet.empty()) {
                logger::info("SetMasterAuto: container {:08X} is already master of '{}', opening config",
                            ref->GetFormID(), existingNet);
                auto networkName = existingNet;
                SKSE::GetTaskInterface()->AddTask([networkName]() {
                    SLIDMenu::ConfigMenu::Show(networkName);
                });
                return RE::BSFixedString(existingNet.c_str());
            }

            // Get cell name as default suggestion
            auto* cell = ref->GetParentCell();
            std::string baseName;
            if (cell && cell->GetFullName() && cell->GetFullName()[0] != '\0') {
                baseName = cell->GetFullName();
            } else {
                baseName = "Storage";
            }

            // Show naming popup — network is created when user confirms
            ShowNetworkNamingFlow(ref->GetFormID(), baseName);

            return RE::BSFixedString(baseName.c_str());
        }

        void BeginTagContainer(RE::StaticFunctionTag*) {
            auto* ref = GetCapturedTarget();
            if (!ref || ref->As<RE::Actor>()) {
                RE::DebugNotification(T("$SLID_ErrNoTarget").c_str());
                Feedback::OnError();
                return;
            }

            if (!ref->GetContainer()) {
                RE::DebugNotification(T("$SLID_ErrNotContainer").c_str());
                Feedback::OnError();
                return;
            }

            auto formID = ref->GetFormID();
            auto* mgr = NetworkManager::GetSingleton();

            // Check if already a master
            auto masterNet = mgr->FindNetworkByMaster(formID);
            if (!masterNet.empty()) {
                RE::DebugNotification(T("$SLID_ErrCannotTagMaster").c_str());
                Feedback::OnError();
                return;
            }

            // Determine default name and whether this is a rename
            bool alreadyTagged = mgr->IsTagged(formID);
            std::string defaultName;

            if (alreadyTagged) {
                defaultName = mgr->GetTagName(formID);
            } else {
                defaultName = T("$SLID_Container");
                if (auto* base = ref->GetBaseObject()) {
                    if (base->GetName() && base->GetName()[0] != '\0') {
                        defaultName = base->GetName();
                    }
                }
            }

            // Open tag-input popup on SKSE task thread
            SKSE::GetTaskInterface()->AddTask([formID, defaultName, alreadyTagged]() {
                TagInputMenu::Menu::Show(formID, defaultName, alreadyTagged);
            });
        }

        void BeginDeregister(RE::StaticFunctionTag*) {
            auto* ref = GetCapturedTarget();
            if (!ref || ref->As<RE::Actor>()) {
                RE::DebugNotification(T("$SLID_ErrNoTarget").c_str());
                Feedback::OnError();
                return;
            }

            auto formID = ref->GetFormID();
            auto* mgr = NetworkManager::GetSingleton();

            // Check if target is a master — dismantle the network
            auto networkName = mgr->FindNetworkByMaster(formID);
            if (!networkName.empty()) {
                UIHelper::BeginDismantleNetwork(ref);
                return;
            }

            // Check if target is tagged — untag + clear filter references
            if (mgr->IsTagged(formID)) {
                auto tagName = mgr->GetTagName(formID);
                mgr->UntagContainer(formID);
                mgr->ClearContainerReferences(formID);
                std::string msg = TF("$SLID_NotifyDeregistered", tagName);
                RE::DebugNotification(msg.c_str());
                Feedback::OnUntagContainer(ref);
                return;
            }

            // Neither master nor tagged
            RE::DebugNotification(T("$SLID_ErrNotMasterOrTagged").c_str());
            Feedback::OnError();
        }

        void BeginDetect(RE::StaticFunctionTag*) {
            auto* mgr = NetworkManager::GetSingleton();
            auto* dataHandler = RE::TESDataHandler::GetSingleton();
            if (!dataHandler) return;

            // Shader FormIDs from ESP
            constexpr RE::FormID kLocalShaderWhite  = 0x810;
            constexpr RE::FormID kLocalShaderBlue   = 0x811;
            constexpr RE::FormID kLocalShaderOrange = 0x815;

            auto* shaderWhite  = dataHandler->LookupForm<RE::TESEffectShader>(kLocalShaderWhite, kPluginName);
            auto* shaderBlue   = dataHandler->LookupForm<RE::TESEffectShader>(kLocalShaderBlue, kPluginName);
            auto* shaderOrange = dataHandler->LookupForm<RE::TESEffectShader>(kLocalShaderOrange, kPluginName);

            if (!shaderWhite || !shaderBlue) {
                logger::error("BeginDetect: shaders not found in ESP");
                RE::DebugNotification(T("$SLID_ErrDetectShaders").c_str());
                Feedback::OnError();
                return;
            }

            // Get sell container FormID (handled separately with orange shader)
            auto sellFormID = mgr->GetSellContainerFormID();

            // Collect all master FormIDs
            std::set<RE::FormID> masters;
            const auto& networks = mgr->GetNetworks();
            for (const auto& net : networks) {
                if (net.masterFormID != 0) {
                    masters.insert(net.masterFormID);
                }
            }

            // Collect all non-master container FormIDs (tagged + filter-assigned + catch-all)
            std::set<RE::FormID> containers;

            // From tag registry
            const auto& tags = mgr->GetTagRegistry();
            for (const auto& [formID, tag] : tags) {
                if (masters.find(formID) == masters.end() && formID != sellFormID) {
                    containers.insert(formID);
                }
            }

            // From all network filters and catch-all
            for (const auto& net : networks) {
                for (const auto& filter : net.filters) {
                    if (filter.containerFormID != 0
                        && masters.find(filter.containerFormID) == masters.end()
                        && filter.containerFormID != sellFormID) {
                        containers.insert(filter.containerFormID);
                    }
                }
                if (net.catchAllFormID != 0
                    && masters.find(net.catchAllFormID) == masters.end()
                    && net.catchAllFormID != sellFormID) {
                    containers.insert(net.catchAllFormID);
                }
            }

            // Remove sell container from masters/containers sets (it gets orange)
            masters.erase(sellFormID);
            containers.erase(sellFormID);

            // Apply shaders
            constexpr float kDetectDuration = 15.0f;
            int applied = 0;

            // Orange shader for sell container
            if (sellFormID != 0 && shaderOrange) {
                auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(sellFormID);
                if (ref) {
                    ref->ApplyEffectShader(shaderOrange, kDetectDuration);
                    ++applied;
                }
            }

            for (auto formID : masters) {
                auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(formID);
                if (ref) {
                    ref->ApplyEffectShader(shaderWhite, kDetectDuration);
                    ++applied;
                }
            }

            for (auto formID : containers) {
                auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(formID);
                if (ref) {
                    ref->ApplyEffectShader(shaderBlue, kDetectDuration);
                    ++applied;
                }
            }

            std::string msg = TF("$SLID_NotifyDetected", std::to_string(applied));
            RE::DebugNotification(msg.c_str());
            Feedback::OnDetectContainers();

            logger::info("BeginDetect: {} masters (white), {} others (blue), sell={} (orange), {} total applied",
                         masters.size(), containers.size(), sellFormID != 0 ? 1 : 0, applied);
        }

        void BeginSellContainer(RE::StaticFunctionTag*) {
            auto* ref = GetCapturedTarget();
            if (!ref || ref->As<RE::Actor>()) {
                RE::DebugNotification(T("$SLID_ErrNoTarget").c_str());
                Feedback::OnError();
                return;
            }

            if (!ref->GetContainer()) {
                RE::DebugNotification(T("$SLID_ErrNotContainer").c_str());
                Feedback::OnError();
                return;
            }

            auto formID = ref->GetFormID();
            auto* mgr = NetworkManager::GetSingleton();

            // Cannot designate a master container as sell container
            auto masterNet = mgr->FindNetworkByMaster(formID);
            if (!masterNet.empty()) {
                RE::DebugNotification(T("$SLID_ErrMasterAsSell").c_str());
                Feedback::OnError();
                return;
            }

            // Toggle: if already the sell container, clear it
            if (mgr->GetSellContainerFormID() == formID) {
                mgr->ClearSellContainer();
                RE::DebugNotification(T("$SLID_NotifySellRemoved").c_str());
                Feedback::OnClearSellContainer(ref);
                return;
            }

            // Block if a different sell container already exists — must clear first
            if (mgr->HasSellContainer()) {
                RE::DebugNotification(T("$SLID_ErrSellAlreadySet").c_str());
                Feedback::OnError();
                return;
            }

            mgr->SetSellContainer(formID);

            // Auto-tag as "Sell Container" if not already tagged
            if (!mgr->IsTagged(formID)) {
                mgr->TagContainer(formID, T("$SLID_SellContainer"));
            }

            RE::DebugNotification(T("$SLID_NotifySellDesignated").c_str());
            Feedback::OnSetSellContainer(ref);

            // Show welcome tutorial (first time only)
            WelcomeMenu::TryShowWelcome();
        }

        void BeginSummonChest(RE::StaticFunctionTag*) {
            auto* mgr = NetworkManager::GetSingleton();
            auto names = mgr->GetNetworkNames();

            if (names.empty()) {
                RE::DebugNotification(T("$SLID_ErrNoNetworks").c_str());
                return;
            }

            if (names.size() == 1) {
                SKSE::GetTaskInterface()->AddTask([name = names[0]]() {
                    SummonChest::Summon(name);
                });
                return;
            }

            // Multiple networks — MessageBox picker
            SKSE::GetTaskInterface()->AddTask([names]() {
                UIHelper::ShowMessageBox(T("$SLID_DlgChooseNetwork"), names,
                    [names](int idx) {
                        if (idx >= 0 && idx < static_cast<int>(names.size())) {
                            auto name = names[idx];
                            SKSE::GetTaskInterface()->AddTask([name]() {
                                SummonChest::Summon(name);
                            });
                        }
                    });
            });
        }

        void DespawnSummonChest(RE::StaticFunctionTag*) {
            if (SummonChest::IsActive()) {
                SKSE::GetTaskInterface()->AddTask([]() {
                    SummonChest::Despawn();
                });
            }
        }

        void OnVendorDialogueAccept() {
            // Get the vendor actor from ActivationHook's tracked state
            auto vendorActorID = ActivationHook::GetLastVendorActorID();
            if (vendorActorID == 0) {
                logger::error("OnVendorDialogueAccept: no vendor actor tracked");
                return;
            }

            auto* actor = RE::TESForm::LookupByID<RE::Actor>(vendorActorID);
            if (!actor) {
                logger::error("OnVendorDialogueAccept: vendor actor {:08X} not found", vendorActorID);
                return;
            }

            auto* npc = actor->GetActorBase();
            if (!npc) {
                logger::error("OnVendorDialogueAccept: actor has no base NPC");
                return;
            }

            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) return;

            constexpr RE::FormID kGold001 = 0x0000000F;
            auto* goldForm = RE::TESForm::LookupByID<RE::TESBoundObject>(kGold001);
            if (!goldForm) {
                logger::error("OnVendorDialogueAccept: Gold001 not found");
                return;
            }

            auto npcBaseID = npc->GetFormID();
            auto* vendorReg = VendorRegistry::GetSingleton();
            std::string vendorName = actor->GetName() ? actor->GetName() : "Unknown Vendor";
            int32_t cost = Settings::iVendorCost;

            // Check if vendor was previously registered but cancelled (inactive)
            auto* existing = vendorReg->FindVendor(npcBaseID);
            if (existing && !existing->active) {
                // Reactivate existing vendor
                vendorReg->SetVendorActive(npcBaseID, true);
                player->RemoveItem(goldForm, cost, RE::ITEM_REMOVE_REASON::kStoreInContainer, nullptr, nullptr);
                logger::info("OnVendorDialogueAccept: reactivated {} ({:08X}), cost {} gold",
                             vendorName, npcBaseID, cost);
            } else if (!existing) {
                // Find the vendor faction
                RE::TESFaction* vendorFaction = nullptr;
                for (const auto& factionRank : npc->factions) {
                    if (factionRank.faction && factionRank.faction->IsVendor()) {
                        vendorFaction = factionRank.faction;
                        break;
                    }
                }

                // Build vendor record
                RegisteredVendor vendor;
                vendor.npcBaseFormID = npcBaseID;
                vendor.factionFormID = vendorFaction ? vendorFaction->GetFormID() : 0;
                vendor.vendorName = vendorName;

                if (vendorFaction && vendorFaction->GetFullName() && vendorFaction->GetFullName()[0] != '\0') {
                    vendor.storeName = vendorFaction->GetFullName();
                } else {
                    vendor.storeName = "General Store";
                }

                float now = 0.0f;
                if (auto* calendar = RE::Calendar::GetSingleton()) {
                    now = calendar->GetHoursPassed();
                }
                vendor.registrationTime = now;
                vendor.lastVisitTime = now + RandomJitter();

                if (!vendorReg->RegisterVendor(vendor)) {
                    logger::warn("OnVendorDialogueAccept: failed to register vendor");
                    return;
                }

                player->RemoveItem(goldForm, cost, RE::ITEM_REMOVE_REASON::kStoreInContainer, nullptr, nullptr);
                logger::info("OnVendorDialogueAccept: registered {} ({:08X}) from {}, cost {} gold",
                             vendor.vendorName, vendor.npcBaseFormID, vendor.storeName, cost);
            } else {
                // Already active — shouldn't happen (dialogue shouldn't show), but be safe
                logger::warn("OnVendorDialogueAccept: vendor {:08X} already active", npcBaseID);
                return;
            }

            // Update the global so re-talking in same conversation shows "already registered"
            auto* globalRegistered = RE::TESForm::LookupByEditorID<RE::TESGlobal>("SLID_VendorRegistered");
            if (globalRegistered) {
                globalRegistered->value = 1.0f;
            }

            std::string msg = TF("$SLID_NotifyVendorEstablished", vendorName);
            RE::DebugNotification(msg.c_str());

            // Close dialogue — the transaction is complete
            if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
                queue->AddMessage(RE::DialogueMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
            }
        }

        void OnVendorDialogueCancel() {
            auto vendorActorID = ActivationHook::GetLastVendorActorID();
            if (vendorActorID == 0) {
                logger::error("OnVendorDialogueCancel: no vendor actor tracked");
                return;
            }

            auto* actor = RE::TESForm::LookupByID<RE::Actor>(vendorActorID);
            if (!actor) {
                logger::error("OnVendorDialogueCancel: vendor actor {:08X} not found", vendorActorID);
                return;
            }

            auto* npc = actor->GetActorBase();
            if (!npc) {
                logger::error("OnVendorDialogueCancel: actor has no base NPC");
                return;
            }

            auto npcBaseID = npc->GetFormID();
            auto* vendorReg = VendorRegistry::GetSingleton();
            const auto* vendor = vendorReg->FindVendor(npcBaseID);
            if (!vendor) {
                logger::warn("OnVendorDialogueCancel: vendor {:08X} not registered", npcBaseID);
                return;
            }

            std::string vendorName = vendor->vendorName;

            // Deregister
            vendorReg->SetVendorActive(npcBaseID, false);

            // Refund half
            constexpr int32_t kRefund = 2500;
            auto* player = RE::PlayerCharacter::GetSingleton();
            constexpr RE::FormID kGold001 = 0x0000000F;
            auto* goldForm = RE::TESForm::LookupByID<RE::TESBoundObject>(kGold001);
            if (player && goldForm) {
                player->AddObjectToContainer(goldForm, nullptr, kRefund, nullptr);
            }

            // Update global so dialogue switches back to unregistered state
            auto* globalRegistered = RE::TESForm::LookupByEditorID<RE::TESGlobal>("SLID_VendorRegistered");
            if (globalRegistered) {
                globalRegistered->value = 0.0f;
            }

            std::string msg = TF("$SLID_NotifyVendorCancelled", vendorName);
            RE::DebugNotification(msg.c_str());

            logger::info("OnVendorDialogueCancel: deregistered {} ({:08X}), refunded {} gold",
                         vendorName, npcBaseID, kRefund);

            // Close dialogue — the transaction is complete
            if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
                queue->AddMessage(RE::DialogueMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
            }
        }

        RE::BSFixedString GetMasterNetwork(RE::StaticFunctionTag*) {
            auto* ref = GetCapturedTarget();
            if (!ref) return "";

            auto name = NetworkManager::GetSingleton()->FindNetworkByMaster(ref->GetFormID());
            return RE::BSFixedString(name.c_str());
        }

        void RemoveNetwork(RE::StaticFunctionTag*, RE::BSFixedString a_name) {
            std::string name = a_name.c_str();
            if (name.empty()) {
                logger::error("RemoveNetwork: name cannot be empty");
                return;
            }

            logger::debug("RemoveNetwork: '{}'", name);
            NetworkManager::GetSingleton()->RemoveNetwork(name);
        }

        void RemoveAllNetworks(RE::StaticFunctionTag*) {
            NetworkManager::GetSingleton()->ClearAll();
        }

        void RefreshPowers(RE::StaticFunctionTag*) {
            auto* dataHandler = RE::TESDataHandler::GetSingleton();
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!dataHandler || !player) return;

            // All SPEL FormIDs that have ever existed (includes deprecated ones)
            constexpr RE::FormID kAllSpells[] = {0x801, 0x803, 0x805, 0x807, 0x809, 0x80B, 0x816, 0x818};
            // Current active set (SetMaster, Tag, Deregister, Detect, SellContainer)
            constexpr RE::FormID kBaseSpells[] = {0x801, 0x803, 0x805, 0x809, 0x816};
            constexpr RE::FormID kSummonSpell = 0x818;

            // Remove all known SLID spells
            for (auto localID : kAllSpells) {
                auto* spell = dataHandler->LookupForm<RE::SpellItem>(localID, kPluginName);
                if (spell && player->HasSpell(spell)) {
                    player->RemoveSpell(spell);
                }
            }

            // If mod is disabled, don't re-add
            if (!Settings::bModEnabled) {
                logger::info("RefreshPowers: removed all powers (mod disabled)");
                return;
            }

            // Re-add base powers
            uint32_t added = 0;
            for (auto localID : kBaseSpells) {
                auto* spell = dataHandler->LookupForm<RE::SpellItem>(localID, kPluginName);
                if (spell) {
                    player->AddSpell(spell);
                    ++added;
                }
            }

            // Conditionally add Summon power
            if (Settings::bSummonEnabled) {
                auto* spell = dataHandler->LookupForm<RE::SpellItem>(kSummonSpell, kPluginName);
                if (spell) {
                    player->AddSpell(spell);
                    ++added;
                }
            }

            logger::info("RefreshPowers: removed all, re-added {} powers", added);
        }

        // =================================================================
        // MCM Native Functions - Settings
        // =================================================================

        bool GetModEnabled(RE::StaticFunctionTag*) {
            return Settings::bModEnabled;
        }

        void SetModEnabled(RE::StaticFunctionTag*, bool a_enabled) {
            Settings::bModEnabled = a_enabled;
            Settings::Save();
            // Refresh powers to add/remove them based on new state
            RefreshPowers(nullptr);
            logger::info("SetModEnabled: {}", a_enabled);
        }

        bool GetDebugLogging(RE::StaticFunctionTag*) {
            return Settings::bDebugLogging;
        }

        void SetDebugLogging(RE::StaticFunctionTag*, bool a_enabled) {
            Settings::bDebugLogging = a_enabled;
            if (a_enabled) {
                spdlog::default_logger()->set_level(spdlog::level::debug);
                spdlog::default_logger()->flush_on(spdlog::level::debug);
            } else {
                spdlog::default_logger()->set_level(spdlog::level::info);
                spdlog::default_logger()->flush_on(spdlog::level::info);
            }
            Settings::Save();
            logger::info("SetDebugLogging: {}", a_enabled);
        }

        bool GetSummonEnabled(RE::StaticFunctionTag*) {
            return Settings::bSummonEnabled;
        }

        void SetSummonEnabled(RE::StaticFunctionTag*, bool a_enabled) {
            Settings::bSummonEnabled = a_enabled;
            Settings::Save();
            logger::info("SetSummonEnabled: {}", a_enabled);
        }

        bool GetShownWelcomeTutorial(RE::StaticFunctionTag*) {
            return Settings::bShownWelcomeTutorial;
        }

        void SetShownWelcomeTutorial(RE::StaticFunctionTag*, bool a_shown) {
            Settings::bShownWelcomeTutorial = a_shown;
            Settings::Save();
            logger::info("SetShownWelcomeTutorial: {}", a_shown);
        }

        // =================================================================
        // MCM Native Functions - Container Picker
        // =================================================================

        bool GetIncludeUnlinkedContainers(RE::StaticFunctionTag*) {
            return Settings::bIncludeUnlinkedContainers;
        }

        void SetIncludeUnlinkedContainers(RE::StaticFunctionTag*, bool a_enabled) {
            Settings::bIncludeUnlinkedContainers = a_enabled;
            Settings::Save();
            logger::info("SetIncludeUnlinkedContainers: {}", a_enabled);
        }

        bool GetIncludeSCIEContainers(RE::StaticFunctionTag*) {
            return Settings::bSCIEIncludeContainers;
        }

        void SetIncludeSCIEContainers(RE::StaticFunctionTag*, bool a_enabled) {
            Settings::bSCIEIncludeContainers = a_enabled;
            Settings::Save();
            logger::info("SetIncludeSCIEContainers: {}", a_enabled);
        }

        // =================================================================
        // MCM Native Functions - Sales Settings
        // =================================================================

        float GetSellPricePercent(RE::StaticFunctionTag*) {
            return Settings::fSellPricePercent;
        }

        void SetSellPricePercent(RE::StaticFunctionTag*, float a_value) {
            Settings::fSellPricePercent = std::clamp(a_value, 0.0f, 1.0f);
            Settings::Save();
        }

        int32_t GetSellBatchSize(RE::StaticFunctionTag*) {
            return Settings::iSellBatchSize;
        }

        void SetSellBatchSize(RE::StaticFunctionTag*, int32_t a_value) {
            Settings::iSellBatchSize = std::max(1, a_value);
            Settings::Save();
        }

        float GetSellIntervalHours(RE::StaticFunctionTag*) {
            return Settings::fSellIntervalHours;
        }

        void SetSellIntervalHours(RE::StaticFunctionTag*, float a_value) {
            Settings::fSellIntervalHours = std::max(1.0f, a_value);
            Settings::Save();
        }

        float GetVendorPricePercent(RE::StaticFunctionTag*) {
            return Settings::fVendorPricePercent;
        }

        void SetVendorPricePercent(RE::StaticFunctionTag*, float a_value) {
            Settings::fVendorPricePercent = std::clamp(a_value, 0.0f, 1.0f);
            Settings::Save();
        }

        int32_t GetVendorBatchSize(RE::StaticFunctionTag*) {
            return Settings::iVendorBatchSize;
        }

        void SetVendorBatchSize(RE::StaticFunctionTag*, int32_t a_value) {
            Settings::iVendorBatchSize = std::max(1, a_value);
            Settings::Save();
        }

        float GetVendorIntervalHours(RE::StaticFunctionTag*) {
            return Settings::fVendorIntervalHours;
        }

        void SetVendorIntervalHours(RE::StaticFunctionTag*, float a_value) {
            Settings::fVendorIntervalHours = std::max(1.0f, a_value);
            Settings::Save();
        }

        int32_t GetVendorCost(RE::StaticFunctionTag*) {
            return Settings::iVendorCost;
        }

        void SetVendorCost(RE::StaticFunctionTag*, int32_t a_value) {
            Settings::iVendorCost = std::max(0, a_value);
            Settings::Save();
        }

        // =================================================================
        // MCM Native Functions - Network Operations
        // =================================================================

        int32_t GetNetworkCount(RE::StaticFunctionTag*) {
            return static_cast<int32_t>(NetworkManager::GetSingleton()->GetNetworks().size());
        }

        std::vector<RE::BSFixedString> GetNetworkNames(RE::StaticFunctionTag*) {
            auto names = NetworkManager::GetSingleton()->GetNetworkNames();
            std::vector<RE::BSFixedString> result;
            result.reserve(names.size());
            for (const auto& n : names) {
                result.push_back(RE::BSFixedString(n.c_str()));
            }
            return result;
        }

        RE::BSFixedString GetNetworkMasterName(RE::StaticFunctionTag*, RE::BSFixedString a_networkName) {
            auto* mgr = NetworkManager::GetSingleton();
            auto* network = mgr->FindNetwork(a_networkName.c_str());
            if (!network || network->masterFormID == 0) return "";

            auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(network->masterFormID);
            if (!ref) return "";

            const char* name = ref->GetName();
            if (name && name[0] != '\0') return RE::BSFixedString(name);

            if (auto* base = ref->GetBaseObject()) {
                if (base->GetName() && base->GetName()[0] != '\0') {
                    return RE::BSFixedString(base->GetName());
                }
            }
            return T("$SLID_Container");
        }

        int32_t RunSort(RE::StaticFunctionTag*, RE::BSFixedString a_networkName) {
            std::string name = a_networkName.c_str();
            if (name.empty()) return 0;

            auto result = Distributor::Distribute(name);
            logger::info("RunSort({}): {} items distributed", name, result.totalItems);
            return static_cast<int32_t>(result.totalItems);
        }

        int32_t RunSweep(RE::StaticFunctionTag*, RE::BSFixedString a_networkName) {
            std::string name = a_networkName.c_str();
            if (name.empty()) return 0;

            auto gathered = Distributor::GatherToMaster(name);
            logger::info("RunSweep({}): {} items gathered", name, gathered);
            return static_cast<int32_t>(gathered);
        }

        int32_t GetNetworkContainerCount(RE::StaticFunctionTag*, RE::BSFixedString a_networkName) {
            auto* mgr = NetworkManager::GetSingleton();
            auto* network = mgr->FindNetwork(a_networkName.c_str());
            if (!network) return 0;

            // Count unique containers: master + filter stages + catch-all (deduplicated)
            std::set<RE::FormID> containers;
            containers.insert(network->masterFormID);
            for (const auto& stage : network->filters) {
                if (stage.containerFormID != 0) {
                    containers.insert(stage.containerFormID);
                }
            }
            if (network->catchAllFormID != 0) {
                containers.insert(network->catchAllFormID);
            }
            return static_cast<int32_t>(containers.size());
        }

        std::vector<RE::BSFixedString> GetNetworkContainerNames(RE::StaticFunctionTag*, RE::BSFixedString a_networkName) {
            std::vector<RE::BSFixedString> result;
            auto* mgr = NetworkManager::GetSingleton();
            auto* network = mgr->FindNetwork(a_networkName.c_str());
            if (!network) return result;

            // Resolve display name for a container FormID
            auto getDisplayName = [&](RE::FormID fid) -> std::string {
                std::string name = T("$SLID_Container");
                auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(fid);
                if (ref) {
                    if (mgr->IsTagged(fid)) {
                        name = mgr->GetTagName(fid);
                    } else if (ref->GetName() && ref->GetName()[0] != '\0') {
                        name = ref->GetName();
                    } else if (auto* base = ref->GetBaseObject()) {
                        if (base->GetName() && base->GetName()[0] != '\0') {
                            name = base->GetName();
                        }
                    }
                }
                return name;
            };

            std::set<RE::FormID> seen;
            std::vector<std::pair<RE::FormID, std::string>> containers;

            // Master first
            containers.emplace_back(network->masterFormID, getDisplayName(network->masterFormID));
            seen.insert(network->masterFormID);

            // Filter stage containers (deduplicated, skip master and Pass)
            for (const auto& stage : network->filters) {
                if (stage.containerFormID == 0) continue;
                if (!seen.insert(stage.containerFormID).second) continue;
                containers.emplace_back(stage.containerFormID, getDisplayName(stage.containerFormID));
            }

            // Catch-all if unique
            if (network->catchAllFormID != 0 && seen.insert(network->catchAllFormID).second) {
                containers.emplace_back(network->catchAllFormID,
                                        getDisplayName(network->catchAllFormID) + " (Catch-All)");
            }

            for (const auto& c : containers) {
                result.push_back(RE::BSFixedString(c.second.c_str()));
            }
            return result;
        }

        void RemoveContainerFromNetwork(RE::StaticFunctionTag*, RE::BSFixedString a_networkName, int32_t a_index) {
            auto* mgr = NetworkManager::GetSingleton();
            auto* network = mgr->FindNetwork(a_networkName.c_str());
            if (!network) return;

            // Build deduplicated list in same order as GetNetworkContainerNames
            std::set<RE::FormID> seen;
            std::vector<RE::FormID> containers;
            // Master first (index 0)
            containers.push_back(network->masterFormID);
            seen.insert(network->masterFormID);
            for (const auto& stage : network->filters) {
                if (stage.containerFormID == 0) continue;
                if (!seen.insert(stage.containerFormID).second) continue;
                containers.push_back(stage.containerFormID);
            }
            if (network->catchAllFormID != 0 && seen.insert(network->catchAllFormID).second) {
                containers.push_back(network->catchAllFormID);
            }

            if (a_index < 0 || a_index >= static_cast<int32_t>(containers.size())) return;

            RE::FormID targetFID = containers[a_index];
            mgr->ClearContainerReferences(targetFID);
            logger::info("RemoveContainerFromNetwork: removed {:08X} from '{}'", targetFID, a_networkName.c_str());
        }

        // =================================================================
        // MCM Native Functions - Compatibility
        // =================================================================

        bool IsTCCInstalled(RE::StaticFunctionTag*) {
            auto* dataHandler = RE::TESDataHandler::GetSingleton();
            return dataHandler && dataHandler->LookupModByName("DBM_RelicNotifications.esp");
        }

        bool IsSCIEInstalled(RE::StaticFunctionTag*) {
            auto* dataHandler = RE::TESDataHandler::GetSingleton();
            return dataHandler && dataHandler->LookupModByName("CraftingInventoryExtender.esp");
        }

        bool GetSCIEIntegration(RE::StaticFunctionTag*) {
            return Settings::bSCIEIntegration;
        }

        void SetSCIEIntegration(RE::StaticFunctionTag*, bool a_enabled) {
            Settings::bSCIEIntegration = a_enabled;
            Settings::Save();
        }

        bool GetSCIEIncludeContainers(RE::StaticFunctionTag*) {
            return Settings::bSCIEIncludeContainers;
        }

        void SetSCIEIncludeContainers(RE::StaticFunctionTag*, bool a_enabled) {
            Settings::bSCIEIncludeContainers = a_enabled;
            Settings::Save();
        }

        // =================================================================
        // MCM Native Functions - Wholesale Arrangements
        // =================================================================

        int32_t GetRegisteredVendorCount(RE::StaticFunctionTag*) {
            return static_cast<int32_t>(VendorRegistry::GetSingleton()->GetActiveCount());
        }

        std::vector<RE::BSFixedString> GetRegisteredVendorNames(RE::StaticFunctionTag*) {
            std::vector<RE::BSFixedString> result;
            const auto& vendors = VendorRegistry::GetSingleton()->GetVendors();
            for (const auto& v : vendors) {
                if (v.active) {
                    result.push_back(RE::BSFixedString(v.vendorName.c_str()));
                }
            }
            return result;
        }

        // Describe what a vendor buys based on their faction's buy list
        static std::string DescribeVendorBuyList(RE::FormID a_factionFormID) {
            if (a_factionFormID == 0) return "All items";

            auto* faction = RE::TESForm::LookupByID<RE::TESFaction>(a_factionFormID);
            if (!faction) return "Unknown";

            auto* buyList = faction->vendorData.vendorSellBuyList;
            bool inverted = faction->vendorData.vendorValues.notBuySell;

            if (!buyList) {
                return inverted ? "All items" : "Nothing";
            }

            std::vector<std::string> keywords;
            buyList->ForEachForm([&](RE::TESForm& form) {
                auto* kw = form.As<RE::BGSKeyword>();
                if (kw) {
                    std::string name = kw->GetFormEditorID();
                    // Strip "VendorItem" prefix for readability
                    if (name.size() > 10 && name.substr(0, 10) == "VendorItem") {
                        name = name.substr(10);
                    }
                    keywords.push_back(name);
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });

            if (keywords.empty()) {
                return inverted ? "All items" : "Nothing";
            }

            std::string result;
            for (size_t i = 0; i < keywords.size(); i++) {
                if (i > 0) result += ", ";
                result += keywords[i];
            }

            if (inverted) {
                result = "Everything except " + result;
            }

            return result;
        }

        RE::BSFixedString GetVendorStoreName(RE::StaticFunctionTag*, int32_t a_index) {
            const auto& vendors = VendorRegistry::GetSingleton()->GetVendors();
            int32_t activeIdx = 0;
            for (const auto& v : vendors) {
                if (!v.active) continue;
                if (activeIdx == a_index) {
                    return RE::BSFixedString(v.storeName.c_str());
                }
                ++activeIdx;
            }
            return "";
        }

        RE::BSFixedString GetVendorCategories(RE::StaticFunctionTag*, int32_t a_index) {
            const auto& vendors = VendorRegistry::GetSingleton()->GetVendors();
            int32_t activeIdx = 0;
            for (const auto& v : vendors) {
                if (!v.active) continue;
                if (activeIdx == a_index) {
                    // Return what items they buy based on faction buy list
                    return RE::BSFixedString(DescribeVendorBuyList(v.factionFormID).c_str());
                }
                ++activeIdx;
            }
            return "";
        }

        float GetVendorBonusPercent(RE::StaticFunctionTag*, int32_t a_index) {
            const auto& vendors = VendorRegistry::GetSingleton()->GetVendors();
            int32_t activeIdx = 0;
            for (const auto& v : vendors) {
                if (!v.active) continue;
                if (activeIdx == a_index) {
                    // Invested vendors get bonus (difference between vendor rate and base rate)
                    if (v.invested) {
                        // Invested bonus: vendor pays 10% more than non-invested
                        return 10.0f;
                    }
                    return 0.0f;
                }
                ++activeIdx;
            }
            return 0.0f;
        }

        RE::BSFixedString GetVendorLastVisit(RE::StaticFunctionTag*, int32_t a_index) {
            const auto& vendors = VendorRegistry::GetSingleton()->GetVendors();
            int32_t activeIdx = 0;
            for (const auto& v : vendors) {
                if (!v.active) continue;
                if (activeIdx == a_index) {
                    if (v.lastVisitTime <= 0.0f) {
                        return "Never";
                    }
                    // Calculate hours since last visit
                    auto* calendar = RE::Calendar::GetSingleton();
                    float currentTime = calendar ? calendar->GetHoursPassed() : 0.0f;
                    float hoursSince = currentTime - v.lastVisitTime;
                    if (hoursSince < 1.0f) {
                        return "Less than 1 hour ago";
                    } else if (hoursSince < 24.0f) {
                        char buf[32];
                        std::snprintf(buf, sizeof(buf), "%.0f hours ago", hoursSince);
                        return buf;
                    } else {
                        float days = hoursSince / 24.0f;
                        char buf[32];
                        std::snprintf(buf, sizeof(buf), "%.1f days ago", days);
                        return buf;
                    }
                }
                ++activeIdx;
            }
            return "";
        }

        // =================================================================
        // MCM Native Functions - Presets
        // =================================================================

        int32_t GetPresetCount(RE::StaticFunctionTag*) {
            return static_cast<int32_t>(NetworkManager::GetSingleton()->GetPresetCount());
        }

        std::vector<RE::BSFixedString> GetPresetNames(RE::StaticFunctionTag*) {
            std::vector<RE::BSFixedString> result;
            const auto& presets = NetworkManager::GetSingleton()->GetPresets();
            for (const auto& p : presets) {
                result.push_back(RE::BSFixedString(p.name.c_str()));
            }
            return result;
        }

        RE::BSFixedString GetPresetStatus(RE::StaticFunctionTag*, RE::BSFixedString a_name) {
            auto* mgr = NetworkManager::GetSingleton();
            const auto* preset = mgr->FindPresetByName(a_name.c_str());
            if (!preset || preset->resolvedMasterFormID == 0) {
                return "Unavailable";
            }
            return "Available";
        }

        RE::BSFixedString GetPresetWarningsNative(RE::StaticFunctionTag*, RE::BSFixedString a_name) {
            return RE::BSFixedString(NetworkManager::GetSingleton()->GetPresetWarnings(a_name.c_str()).c_str());
        }

        bool ActivatePresetNative(RE::StaticFunctionTag*, RE::BSFixedString a_name) {
            return NetworkManager::GetSingleton()->ActivatePreset(a_name.c_str());
        }

        RE::BSFixedString GetPresetMasterConflict(RE::StaticFunctionTag*, RE::BSFixedString a_name) {
            auto* mgr = NetworkManager::GetSingleton();
            const auto* preset = mgr->FindPresetByName(a_name.c_str());
            if (!preset || preset->resolvedMasterFormID == 0) return "";

            auto conflict = mgr->FindNetworkByMaster(preset->resolvedMasterFormID);
            if (!conflict.empty()) {
                logger::info("GetPresetMasterConflict '{}': master {:08X} conflicts with network '{}'",
                             a_name.c_str(), preset->resolvedMasterFormID, conflict);
            }
            return RE::BSFixedString(conflict.c_str());
        }

        void ReloadPresets(RE::StaticFunctionTag*) {
            NetworkManager::GetSingleton()->ReloadPresets();
        }

        RE::BSFixedString GetPresetDescription(RE::StaticFunctionTag*, RE::BSFixedString a_name) {
            auto* preset = NetworkManager::GetSingleton()->FindPresetByName(a_name.c_str());
            return preset ? RE::BSFixedString(preset->description.c_str()) : "";
        }

        bool IsPresetUserGenerated(RE::StaticFunctionTag*, RE::BSFixedString a_name) {
            auto* preset = NetworkManager::GetSingleton()->FindPresetByName(a_name.c_str());
            return preset ? preset->userGenerated : false;
        }

        int32_t GetContainerListCount(RE::StaticFunctionTag*) {
            return static_cast<int32_t>(NetworkManager::GetSingleton()->GetContainerListCount());
        }

        std::vector<RE::BSFixedString> GetContainerListNames(RE::StaticFunctionTag*) {
            std::vector<RE::BSFixedString> result;
            const auto& lists = NetworkManager::GetSingleton()->GetContainerLists();
            for (const auto& cl : lists) {
                result.push_back(RE::BSFixedString(cl.name.c_str()));
            }
            return result;
        }

        RE::BSFixedString GetContainerListDescription(RE::StaticFunctionTag*, RE::BSFixedString a_name) {
            auto* cl = NetworkManager::GetSingleton()->FindContainerListByName(a_name.c_str());
            return cl ? RE::BSFixedString(cl->description.c_str()) : "";
        }

        bool IsContainerListEnabled(RE::StaticFunctionTag*, RE::BSFixedString a_name) {
            return NetworkManager::GetSingleton()->IsContainerListEnabled(a_name.c_str());
        }

        void SetContainerListEnabled(RE::StaticFunctionTag*, RE::BSFixedString a_name, bool a_enabled) {
            NetworkManager::GetSingleton()->SetContainerListEnabled(a_name.c_str(), a_enabled);
        }

        // =================================================================
        // MCM Native Functions - About
        // =================================================================

        RE::BSFixedString GetPluginVersion(RE::StaticFunctionTag*) {
            std::string v = std::to_string(Version::MAJOR) + "." +
                           std::to_string(Version::MINOR) + "." +
                           std::to_string(Version::PATCH);
            return RE::BSFixedString(v.c_str());
        }

        // =================================================================
        // MCM Native Functions - Debug
        // =================================================================

        void DumpContainers(RE::StaticFunctionTag*) {
            NetworkManager::GetSingleton()->DumpToLog();
            logger::info("DumpContainers: logged network state");
        }

        void DumpFilters(RE::StaticFunctionTag*) {
            FilterRegistry::GetSingleton()->DumpToLog();
            logger::info("DumpFilters: logged filter registry");
        }

        void DumpVendors(RE::StaticFunctionTag*) {
            VendorRegistry::GetSingleton()->DumpToLog();
            logger::info("DumpVendors: logged vendor registry");
        }

        // =================================================================
        // MCM Native Functions - Mod Author Export
        // =================================================================

        /// Format a FormID as "PluginName.esp|0xLocalID" for INI export
        std::string FormatFormIDForExport(RE::FormID a_formID) {
            if (a_formID == 0) return "";

            auto* dataHandler = RE::TESDataHandler::GetSingleton();
            if (!dataHandler) return fmt::format("0x{:08X}", a_formID);

            // Determine if ESL-flagged (light plugin)
            bool isLight = (a_formID & 0xFF000000) == 0xFE000000;

            std::string pluginName;
            RE::FormID localID;

            if (isLight) {
                // ESL: FE{index:3}{local:3} — 12-bit local IDs
                auto lightIndex = (a_formID >> 12) & 0xFFF;
                localID = a_formID & 0xFFF;

                // Find the plugin by light index
                for (auto* file : dataHandler->files) {
                    if (file && file->IsLight() && file->GetSmallFileCompileIndex() == lightIndex) {
                        pluginName = file->GetFilename();
                        break;
                    }
                }
            } else {
                // Regular: {index:2}{local:6} — 24-bit local IDs
                auto modIndex = (a_formID >> 24) & 0xFF;
                localID = a_formID & 0x00FFFFFF;

                auto* file = dataHandler->LookupLoadedModByIndex(static_cast<uint8_t>(modIndex));
                if (file) {
                    pluginName = file->GetFilename();
                }
            }

            if (pluginName.empty()) {
                return fmt::format("0x{:08X}", a_formID);
            }

            return fmt::format("{}|0x{:X}", pluginName, localID);
        }

        /// Extract plugin name from a runtime FormID, or empty string if not resolvable
        std::string GetPluginNameForFormID(RE::FormID a_formID) {
            if (a_formID == 0) return "";
            auto* dataHandler = RE::TESDataHandler::GetSingleton();
            if (!dataHandler) return "";

            bool isLight = (a_formID & 0xFF000000) == 0xFE000000;
            if (isLight) {
                auto lightIndex = (a_formID >> 12) & 0xFFF;
                for (auto* file : dataHandler->files) {
                    if (file && file->IsLight() && file->GetSmallFileCompileIndex() == lightIndex) {
                        return std::string(file->GetFilename());
                    }
                }
            } else {
                auto modIndex = (a_formID >> 24) & 0xFF;
                auto* file = dataHandler->LookupLoadedModByIndex(static_cast<uint8_t>(modIndex));
                if (file) {
                    return std::string(file->GetFilename());
                }
            }
            return "";
        }

        /// Get a display name for a container FormID (tag name or base object name)
        std::string GetContainerDisplayName(NetworkManager* a_mgr, RE::FormID a_formID) {
            if (a_formID == 0) return "";
            if (a_mgr->IsTagged(a_formID)) {
                return a_mgr->GetTagName(a_formID);
            }
            auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_formID);
            if (ref) {
                if (ref->GetName() && ref->GetName()[0] != '\0') return ref->GetName();
                if (auto* base = ref->GetBaseObject()) {
                    if (base->GetName() && base->GetName()[0] != '\0') return base->GetName();
                }
            }
            return "";
        }

        bool GeneratePresetINI(RE::StaticFunctionTag*, RE::BSFixedString a_networkName, RE::BSFixedString a_presetName) {
            std::string networkName = a_networkName.c_str();
            std::string presetName = a_presetName.c_str();
            if (networkName.empty()) {
                logger::error("GeneratePresetINI: empty network name");
                return false;
            }

            auto* mgr = NetworkManager::GetSingleton();
            auto* network = mgr->FindNetwork(networkName);
            if (!network) {
                logger::error("GeneratePresetINI: network '{}' not found", networkName);
                return false;
            }

            // Use custom preset name if provided, otherwise network name
            if (presetName.empty()) {
                presetName = networkName;
            }

            // Sanitize name for filename
            std::string sanitized = presetName;
            for (auto& c : sanitized) {
                if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') {
                    c = '_';
                }
            }

            auto dir = Settings::GetDataDir();
            auto outputPath = dir / fmt::format("SLID_GEN_{}.ini", sanitized);

            std::ofstream out(outputPath);
            if (!out.is_open()) {
                logger::error("GeneratePresetINI: failed to open {}", outputPath.string());
                return false;
            }

            // Collect all unique container FormIDs and count assignments
            int assignedCount = 0;
            std::set<RE::FormID> uniqueContainers;
            std::set<RE::FormID> allFormIDs;  // all FormIDs referenced in the network
            allFormIDs.insert(network->masterFormID);
            for (const auto& stage : network->filters) {
                if (stage.containerFormID != 0 && stage.containerFormID != network->masterFormID) {
                    uniqueContainers.insert(stage.containerFormID);
                    allFormIDs.insert(stage.containerFormID);
                    ++assignedCount;
                }
            }
            if (network->catchAllFormID != 0) {
                allFormIDs.insert(network->catchAllFormID);
            }

            // Collect all unique non-base-game plugins referenced by network containers
            static const std::set<std::string, std::less<>> kBaseGamePlugins = {
                "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm"
            };
            std::set<std::string> requiredPlugins;
            for (auto fid : allFormIDs) {
                auto plugin = GetPluginNameForFormID(fid);
                if (!plugin.empty() && !kBaseGamePlugins.count(plugin)) {
                    requiredPlugins.insert(plugin);
                }
            }

            // --- Header comment block ---
            out << "; =============================================================================\n";
            out << "; SLID Preset: " << presetName << "\n";
            out << "; Generated by SLID v" << Version::MAJOR << "." << Version::MINOR << "." << Version::PATCH << "\n";
            out << "; =============================================================================\n";
            out << ";\n";
            out << "; " << uniqueContainers.size() << " containers linked, " << assignedCount << " filter assignments.\n";
            out << ";\n";
            out << "; Any file matching *SLID_*.ini will be loaded by SLID.\n";
            out << "\n";

            // --- [Preset:Name] ---
            auto masterComment = GetContainerDisplayName(mgr, network->masterFormID);
            out << "[Preset:" << presetName << "]\n";
            out << "Master = " << FormatFormIDForExport(network->masterFormID);
            if (!masterComment.empty()) {
                out << "  ; " << masterComment;
            }
            out << "\n";
            for (const auto& plugin : requiredPlugins) {
                out << "RequirePlugin = " << plugin << "\n";
            }
            // Auto-generate description with timestamp
            {
                auto now = std::chrono::system_clock::now();
                auto time_t = std::chrono::system_clock::to_time_t(now);
                std::tm tm{};
                localtime_s(&tm, &time_t);
                char dateBuf[64];
                std::strftime(dateBuf, sizeof(dateBuf), "%d-%b-%Y at %H:%M", &tm);
                out << "Description = Generated from '" << networkName << "' on " << dateBuf << "\n";
            }
            out << "UserGenerated = true\n";
            out << "\n";

            // --- [Preset:Name:Filters] ---
            out << "[Preset:" << presetName << ":Filters]\n";
            for (const auto& stage : network->filters) {
                out << stage.filterID << " = ";
                if (stage.containerFormID == network->masterFormID) {
                    out << "Keep";
                } else if (stage.containerFormID == 0) {
                    out << "Pass";
                } else {
                    out << FormatFormIDForExport(stage.containerFormID);
                    auto comment = GetContainerDisplayName(mgr, stage.containerFormID);
                    if (!comment.empty()) {
                        out << "  ; " << comment;
                    }
                }
                out << "\n";
            }
            // CatchAll line
            if (network->catchAllFormID == 0 || network->catchAllFormID == network->masterFormID) {
                out << "CatchAll = Keep\n";
            } else {
                out << "CatchAll = " << FormatFormIDForExport(network->catchAllFormID);
                auto comment = GetContainerDisplayName(mgr, network->catchAllFormID);
                if (!comment.empty()) {
                    out << "  ; " << comment;
                }
                out << "\n";
            }
            out << "\n";

            // --- [Preset:Name:Tags] ---
            // Collect all unique container FormIDs that belong to this network
            std::set<RE::FormID> networkContainers;
            networkContainers.insert(network->masterFormID);
            for (const auto& stage : network->filters) {
                if (stage.containerFormID != 0) {
                    networkContainers.insert(stage.containerFormID);
                }
            }
            if (network->catchAllFormID != 0) {
                networkContainers.insert(network->catchAllFormID);
            }

            // Write tags for network containers
            bool hasTagSection = false;
            // Master first
            if (mgr->IsTagged(network->masterFormID)) {
                if (!hasTagSection) {
                    out << "[Preset:" << presetName << ":Tags]\n";
                    hasTagSection = true;
                }
                out << FormatFormIDForExport(network->masterFormID) << " = " << mgr->GetTagName(network->masterFormID) << "\n";
            }
            // Then other network containers
            for (auto fid : networkContainers) {
                if (fid == network->masterFormID) continue;  // already written
                if (mgr->IsTagged(fid)) {
                    if (!hasTagSection) {
                        out << "[Preset:" << presetName << ":Tags]\n";
                        hasTagSection = true;
                    }
                    out << FormatFormIDForExport(fid) << " = " << mgr->GetTagName(fid) << "\n";
                }
            }
            if (hasTagSection) {
                out << "\n";
            }

            // --- [Preset:Name:Whoosh] ---
            if (network->whooshConfigured && !network->whooshFilters.empty()) {
                // Map individual filter IDs back to family roots
                auto* filterReg = FilterRegistry::GetSingleton();
                std::set<std::string> rootIDs;  // sorted set for deterministic output
                for (const auto& filterID : network->whooshFilters) {
                    auto* filter = filterReg->GetFilter(filterID);
                    if (!filter) continue;
                    // Walk to root
                    const IFilter* current = filter;
                    while (current->GetParent()) {
                        current = current->GetParent();
                    }
                    rootIDs.insert(std::string(current->GetID()));
                }

                if (!rootIDs.empty()) {
                    out << "[Preset:" << presetName << ":Whoosh]\n";
                    for (const auto& rootID : rootIDs) {
                        out << rootID << " = true\n";
                    }
                    out << "\n";
                }
            }

            out.close();
            logger::info("GeneratePresetINI: wrote {} (preset '{}' from network '{}')",
                         outputPath.string(), presetName, networkName);
            return true;
        }

        void BeginGeneratePreset(RE::StaticFunctionTag*, RE::BSFixedString a_networkName) {
            std::string networkName = a_networkName.c_str();
            if (networkName.empty()) {
                logger::error("BeginGeneratePreset: empty network name");
                return;
            }

            // Close the journal menu (MCM)
            if (auto* msgQueue = RE::UIMessageQueue::GetSingleton()) {
                msgQueue->AddMessage("Journal Menu", RE::UI_MESSAGE_TYPE::kHide, nullptr);
            }

            // Wait several frames for the journal menu to fully close before opening TagInputMenu.
            // A single AddTask (1 frame) is too fast — the close animation hasn't finished.
            auto state = std::make_shared<std::pair<int, std::string>>(10, networkName);
            auto tick = std::make_shared<std::function<void()>>();
            *tick = [state, tick]() {
                if (--state->first > 0) {
                    SKSE::GetTaskInterface()->AddTask(std::function<void()>(*tick));
                    return;
                }
                auto& netName = state->second;
                TagInputMenu::Menu::ShowWithCallback(T("$SLID_DlgNamePreset"), netName,
                    [netName](const std::string& chosenName) {
                        if (chosenName.empty()) return;

                        RE::BSFixedString netBS(netName.c_str());
                        RE::BSFixedString nameBS(chosenName.c_str());
                        bool result = GeneratePresetINI(nullptr, netBS, nameBS);
                        if (result) {
                            NetworkManager::GetSingleton()->ReloadPresets();
                            RE::DebugNotification(T("$SLID_ExportGenerated").c_str());
                        } else {
                            RE::DebugNotification(T("$SLID_ExportFailed").c_str());
                        }
                    });
            };
            SKSE::GetTaskInterface()->AddTask(std::function<void()>(*tick));
        }
    }

    void ShowConfigMenu(RE::StaticFunctionTag*) {
        logger::info("ShowConfigMenu native called");
        auto* mgr = NetworkManager::GetSingleton();
        auto names = mgr->GetNetworkNames();
        if (names.empty()) {
            RE::DebugNotification(T("$SLID_ErrNoNetworks").c_str());
            return;
        }
        auto networkName = names[0];
        SKSE::GetTaskInterface()->AddTask([networkName]() {
            logger::info("ShowConfigMenu task executing for network '{}'", networkName);
            SLIDMenu::ConfigMenu::Show(networkName);
        });
    }

    void HideConfigMenu(RE::StaticFunctionTag*) {
        SKSE::GetTaskInterface()->AddTask([]() {
            SLIDMenu::ConfigMenu::Hide();
        });
    }

    bool RegisterFunctions(RE::BSScript::IVirtualMachine* a_vm) {
        const auto className = "SLID_Native"sv;

        // Core functions
        a_vm->RegisterFunction("SetMasterAuto"sv, className, SetMasterAuto);
        a_vm->RegisterFunction("BeginTagContainer"sv, className, BeginTagContainer);
        a_vm->RegisterFunction("BeginDeregister"sv, className, BeginDeregister);
        a_vm->RegisterFunction("BeginDetect"sv, className, BeginDetect);
        a_vm->RegisterFunction("BeginSellContainer"sv, className, BeginSellContainer);
        a_vm->RegisterFunction("BeginSummonChest"sv, className, BeginSummonChest);
        a_vm->RegisterFunction("DespawnSummonChest"sv, className, DespawnSummonChest);
        a_vm->RegisterFunction("GetMasterNetwork"sv, className, GetMasterNetwork);
        a_vm->RegisterFunction("RemoveNetwork"sv, className, RemoveNetwork);
        a_vm->RegisterFunction("RemoveAllNetworks"sv, className, RemoveAllNetworks);
        a_vm->RegisterFunction("RefreshPowers"sv, className, RefreshPowers);
        a_vm->RegisterFunction("ShowConfigMenu"sv, className, ShowConfigMenu);
        a_vm->RegisterFunction("HideConfigMenu"sv, className, HideConfigMenu);

        // MCM Settings - General
        a_vm->RegisterFunction("GetModEnabled"sv, className, GetModEnabled);
        a_vm->RegisterFunction("SetModEnabled"sv, className, SetModEnabled);
        a_vm->RegisterFunction("GetDebugLogging"sv, className, GetDebugLogging);
        a_vm->RegisterFunction("SetDebugLogging"sv, className, SetDebugLogging);
        a_vm->RegisterFunction("GetSummonEnabled"sv, className, GetSummonEnabled);
        a_vm->RegisterFunction("SetSummonEnabled"sv, className, SetSummonEnabled);
        a_vm->RegisterFunction("GetShownWelcomeTutorial"sv, className, GetShownWelcomeTutorial);
        a_vm->RegisterFunction("SetShownWelcomeTutorial"sv, className, SetShownWelcomeTutorial);

        // MCM Settings - Container Picker
        a_vm->RegisterFunction("GetIncludeUnlinkedContainers"sv, className, GetIncludeUnlinkedContainers);
        a_vm->RegisterFunction("SetIncludeUnlinkedContainers"sv, className, SetIncludeUnlinkedContainers);
        a_vm->RegisterFunction("GetIncludeSCIEContainers"sv, className, GetIncludeSCIEContainers);
        a_vm->RegisterFunction("SetIncludeSCIEContainers"sv, className, SetIncludeSCIEContainers);

        // MCM Settings - Sales
        a_vm->RegisterFunction("GetSellPricePercent"sv, className, GetSellPricePercent);
        a_vm->RegisterFunction("SetSellPricePercent"sv, className, SetSellPricePercent);
        a_vm->RegisterFunction("GetSellBatchSize"sv, className, GetSellBatchSize);
        a_vm->RegisterFunction("SetSellBatchSize"sv, className, SetSellBatchSize);
        a_vm->RegisterFunction("GetSellIntervalHours"sv, className, GetSellIntervalHours);
        a_vm->RegisterFunction("SetSellIntervalHours"sv, className, SetSellIntervalHours);
        a_vm->RegisterFunction("GetVendorPricePercent"sv, className, GetVendorPricePercent);
        a_vm->RegisterFunction("SetVendorPricePercent"sv, className, SetVendorPricePercent);
        a_vm->RegisterFunction("GetVendorBatchSize"sv, className, GetVendorBatchSize);
        a_vm->RegisterFunction("SetVendorBatchSize"sv, className, SetVendorBatchSize);
        a_vm->RegisterFunction("GetVendorIntervalHours"sv, className, GetVendorIntervalHours);
        a_vm->RegisterFunction("SetVendorIntervalHours"sv, className, SetVendorIntervalHours);
        a_vm->RegisterFunction("GetVendorCost"sv, className, GetVendorCost);
        a_vm->RegisterFunction("SetVendorCost"sv, className, SetVendorCost);

        // MCM Link Page
        a_vm->RegisterFunction("GetNetworkCount"sv, className, GetNetworkCount);
        a_vm->RegisterFunction("GetNetworkNames"sv, className, GetNetworkNames);
        a_vm->RegisterFunction("GetNetworkMasterName"sv, className, GetNetworkMasterName);
        a_vm->RegisterFunction("RunSort"sv, className, RunSort);
        a_vm->RegisterFunction("RunSweep"sv, className, RunSweep);
        a_vm->RegisterFunction("GetNetworkContainerCount"sv, className, GetNetworkContainerCount);
        a_vm->RegisterFunction("GetNetworkContainerNames"sv, className, GetNetworkContainerNames);
        a_vm->RegisterFunction("RemoveContainerFromNetwork"sv, className, RemoveContainerFromNetwork);

        // MCM Compatibility
        a_vm->RegisterFunction("IsTCCInstalled"sv, className, IsTCCInstalled);
        a_vm->RegisterFunction("IsSCIEInstalled"sv, className, IsSCIEInstalled);
        a_vm->RegisterFunction("GetSCIEIntegration"sv, className, GetSCIEIntegration);
        a_vm->RegisterFunction("SetSCIEIntegration"sv, className, SetSCIEIntegration);
        a_vm->RegisterFunction("GetSCIEIncludeContainers"sv, className, GetSCIEIncludeContainers);
        a_vm->RegisterFunction("SetSCIEIncludeContainers"sv, className, SetSCIEIncludeContainers);

        // MCM Wholesale Arrangements
        a_vm->RegisterFunction("GetRegisteredVendorCount"sv, className, GetRegisteredVendorCount);
        a_vm->RegisterFunction("GetRegisteredVendorNames"sv, className, GetRegisteredVendorNames);
        a_vm->RegisterFunction("GetVendorStoreName"sv, className, GetVendorStoreName);
        a_vm->RegisterFunction("GetVendorCategories"sv, className, GetVendorCategories);
        a_vm->RegisterFunction("GetVendorBonusPercent"sv, className, GetVendorBonusPercent);
        a_vm->RegisterFunction("GetVendorLastVisit"sv, className, GetVendorLastVisit);

        // MCM Presets
        a_vm->RegisterFunction("GetPresetCount"sv, className, GetPresetCount);
        a_vm->RegisterFunction("GetPresetNames"sv, className, GetPresetNames);
        a_vm->RegisterFunction("GetPresetStatus"sv, className, GetPresetStatus);
        a_vm->RegisterFunction("GetPresetWarnings"sv, className, GetPresetWarningsNative);
        a_vm->RegisterFunction("ActivatePreset"sv, className, ActivatePresetNative);
        a_vm->RegisterFunction("GetPresetMasterConflict"sv, className, GetPresetMasterConflict);
        a_vm->RegisterFunction("ReloadPresets"sv, className, ReloadPresets);
        a_vm->RegisterFunction("GetPresetDescription"sv, className, GetPresetDescription);
        a_vm->RegisterFunction("IsPresetUserGenerated"sv, className, IsPresetUserGenerated);
        a_vm->RegisterFunction("GetContainerListCount"sv, className, GetContainerListCount);
        a_vm->RegisterFunction("GetContainerListNames"sv, className, GetContainerListNames);
        a_vm->RegisterFunction("GetContainerListDescription"sv, className, GetContainerListDescription);
        a_vm->RegisterFunction("IsContainerListEnabled"sv, className, IsContainerListEnabled);
        a_vm->RegisterFunction("SetContainerListEnabled"sv, className, SetContainerListEnabled);
        a_vm->RegisterFunction("BeginGeneratePreset"sv, className, BeginGeneratePreset);

        // MCM About
        a_vm->RegisterFunction("GetPluginVersion"sv, className, GetPluginVersion);

        // MCM Mod Author / Debug
        a_vm->RegisterFunction("GeneratePresetINI"sv, className, GeneratePresetINI);
        a_vm->RegisterFunction("DumpContainers"sv, className, DumpContainers);
        a_vm->RegisterFunction("DumpFilters"sv, className, DumpFilters);
        a_vm->RegisterFunction("DumpVendors"sv, className, DumpVendors);

        // Keep old names registered as stubs so existing saves don't error
        a_vm->RegisterFunction("BeginLinkContainer"sv, className, BeginTagContainer);
        a_vm->RegisterFunction("BeginDismantleNetwork"sv, className, BeginDeregister);

        logger::info("Registered SLID_Native Papyrus functions");
        return true;
    }

    void RegisterEventSink() {
        auto* dataHandler = RE::TESDataHandler::GetSingleton();

        for (auto localID : kSpellIDs) {
            auto* spell = dataHandler->LookupForm<RE::SpellItem>(localID, kPluginName);
            if (spell) {
                g_slidSpellIDs.insert(spell->GetFormID());
            }
        }

        auto* holder = RE::ScriptEventSourceHolder::GetSingleton();
        if (holder) {
            holder->AddEventSink(SpellCastListener::GetSingleton());
            logger::info("SpellCastListener: registered for {} SLID spells", g_slidSpellIDs.size());

            // Resolve SLID_VendorAccept INFO runtime FormID.
            // INFO records aren't in the normal LookupForm/LookupByEditorID maps.
            // Derive the runtime FormID from a sibling form's ESL load-order prefix.
            // ESL format: FE{index:3 hex}{local:3 hex} — 12-bit local IDs (0x000–0xFFF).
            auto* siblingForm = RE::TESForm::LookupByEditorID("SLID_VendorDialogueFaction");
            if (siblingForm) {
                constexpr RE::FormID kAcceptLocalID = 0x821;  // SLID_VendorAccept INFO
                constexpr RE::FormID kCancelLocalID = 0x822;  // SLID_VendorCancel INFO
                auto siblingRuntime = siblingForm->GetFormID();  // e.g., FE136824
                auto prefix = siblingRuntime & 0xFFFFF000;       // FE136000 (ESL prefix)
                g_vendorAcceptInfoID = prefix | kAcceptLocalID;  // FE136821
                g_vendorCancelInfoID = prefix | kCancelLocalID;  // FE136822

                logger::info("TopicInfoListener: accept={:08X}, cancel={:08X} (prefix from {:08X})",
                             g_vendorAcceptInfoID, g_vendorCancelInfoID, siblingRuntime);
                holder->AddEventSink(TopicInfoListener::GetSingleton());
            } else {
                logger::warn("TopicInfoListener: could not resolve SLID ESL prefix");
            }
        }
    }
}
