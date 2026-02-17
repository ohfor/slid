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

                // Log the event for debugging
                logger::debug("TopicInfoEvent: speaker={:08X}, info={:08X}, type={}",
                              a_event->speakerRef ? a_event->speakerRef->GetFormID() : 0,
                              a_event->topicInfoFormID,
                              a_event->type);

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

            auto formID = ref->GetFormID();

            // Show naming popup — network is created when user confirms
            SKSE::GetTaskInterface()->AddTask([formID, baseName]() {
                TagInputMenu::Menu::ShowWithCallback("Name Link", baseName,
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
                UIHelper::ShowMessageBox("SLID: Choose Network", names,
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

            // Count unique containers from filter stages + catch-all
            std::set<RE::FormID> containers;
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

            // Collect unique containers with display names
            std::vector<std::pair<RE::FormID, std::string>> containers;
            for (const auto& stage : network->filters) {
                if (stage.containerFormID != 0) {
                    auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(stage.containerFormID);
                    std::string name = T("$SLID_Container");
                    if (ref) {
                        if (mgr->IsTagged(stage.containerFormID)) {
                            name = mgr->GetTagName(stage.containerFormID);
                        } else if (ref->GetName() && ref->GetName()[0] != '\0') {
                            name = ref->GetName();
                        } else if (auto* base = ref->GetBaseObject()) {
                            if (base->GetName() && base->GetName()[0] != '\0') {
                                name = base->GetName();
                            }
                        }
                    }
                    containers.emplace_back(stage.containerFormID, name);
                }
            }

            // Add catch-all if different
            if (network->catchAllFormID != 0) {
                bool found = false;
                for (const auto& c : containers) {
                    if (c.first == network->catchAllFormID) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(network->catchAllFormID);
                    std::string name = "Catch-All";
                    if (ref) {
                        if (mgr->IsTagged(network->catchAllFormID)) {
                            name = mgr->GetTagName(network->catchAllFormID) + " (Catch-All)";
                        } else if (ref->GetName() && ref->GetName()[0] != '\0') {
                            name = std::string(ref->GetName()) + " (Catch-All)";
                        }
                    }
                    containers.emplace_back(network->catchAllFormID, name);
                }
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

            // Build list of container FormIDs in same order as GetNetworkContainerNames
            std::vector<RE::FormID> containers;
            for (const auto& stage : network->filters) {
                if (stage.containerFormID != 0) {
                    containers.push_back(stage.containerFormID);
                }
            }
            if (network->catchAllFormID != 0) {
                bool found = false;
                for (auto fid : containers) {
                    if (fid == network->catchAllFormID) { found = true; break; }
                }
                if (!found) containers.push_back(network->catchAllFormID);
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

        bool GenerateModAuthorExport(RE::StaticFunctionTag*, bool a_networks, bool a_filters, bool a_vendors) {
            // Build output path
            auto path = Settings::GetINIPath();
            auto dir = path.parent_path();
            auto outputPath = dir / "SLID_ModAuthorExport.ini";

            std::ofstream out(outputPath);
            if (!out.is_open()) {
                logger::error("GenerateModAuthorExport: failed to open {}", outputPath.string());
                return false;
            }

            out << "; SLID Mod Author Export\n";
            out << "; Generated by SLID v" << Version::MAJOR << "." << Version::MINOR << "." << Version::PATCH << "\n";
            out << ";\n";
            out << "; This file is NOT loaded by SLID — it's a template for mod authors.\n";
            out << "; To use: rename to YourMod_SLID.ini and ship with your mod.\n";
            out << "; Any file matching *SLID_*.ini (except this one) will be loaded.\n";
            out << ";\n";
            out << "; Entries can be disabled by a patch INI setting = false\n\n";

            auto* mgr = NetworkManager::GetSingleton();

            if (a_networks) {
                const auto& networks = mgr->GetNetworks();
                if (!networks.empty()) {
                    out << "; =============================================================================\n";
                    out << "; NETWORKS\n";
                    out << "; =============================================================================\n";
                    out << "; Creates storage networks with the specified master container.\n";
                    out << "; Filter pipeline and catch-all are configured by the user.\n\n";

                    for (const auto& net : networks) {
                        out << "[Network:" << net.name << "]\n";
                        out << "Master = " << FormatFormIDForExport(net.masterFormID) << "\n\n";
                    }
                }

                // Export sell container
                auto sellFormID = mgr->GetSellContainerFormID();
                if (sellFormID != 0) {
                    out << "; =============================================================================\n";
                    out << "; SELL CONTAINER\n";
                    out << "; =============================================================================\n";
                    out << "; Designates a container for automated sales.\n\n";

                    out << "[SellContainer]\n";
                    out << FormatFormIDForExport(sellFormID) << " = true\n\n";
                }
            }

            if (a_filters) {
                // Export tagged containers (for naming/identification)
                const auto& tags = mgr->GetTagRegistry();
                if (!tags.empty()) {
                    out << "; =============================================================================\n";
                    out << "; TAGGED CONTAINERS\n";
                    out << "; =============================================================================\n";
                    out << "; Display names for containers in the picker UI.\n";
                    out << "; Format: Plugin.esp|0xFormID|Display Name = true\n\n";

                    out << "[TaggedContainers]\n";
                    for (const auto& [formID, tagData] : tags) {
                        out << FormatFormIDForExport(formID) << "|" << tagData.customName << " = true\n";
                    }
                    out << "\n";
                }
            }

            if (a_vendors) {
                // Export vendor whitelist
                const auto& vendors = VendorRegistry::GetSingleton()->GetVendors();
                bool hasActive = false;
                for (const auto& v : vendors) {
                    if (v.active) { hasActive = true; break; }
                }
                if (hasActive) {
                    out << "; =============================================================================\n";
                    out << "; VENDOR WHITELIST\n";
                    out << "; =============================================================================\n";
                    out << "; NPCs that can be offered wholesale trade arrangements.\n";
                    out << "; Format: Plugin.esp|0xFormID|VendorName = true\n\n";

                    out << "[Vendors]\n";
                    for (const auto& v : vendors) {
                        if (v.active) {
                            out << FormatFormIDForExport(v.npcBaseFormID) << "|" << v.vendorName << " = true\n";
                        }
                    }
                    out << "\n";
                }
            }

            out.close();
            logger::info("GenerateModAuthorExport: wrote {}", outputPath.string());
            return true;
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

        // MCM About
        a_vm->RegisterFunction("GetPluginVersion"sv, className, GetPluginVersion);

        // MCM Mod Author / Debug
        a_vm->RegisterFunction("GenerateModAuthorExport"sv, className, GenerateModAuthorExport);
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
