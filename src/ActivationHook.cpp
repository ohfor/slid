#include "ActivationHook.h"
#include "NetworkManager.h"
#include "VendorRegistry.h"
#include "UIHelper.h"
#include "SLIDMenu.h"
#include "SellOverviewMenu.h"
#include "SummonChest.h"
#include "Distributor.h"
#include "WhooshConfigMenu.h"
#include "FilterRegistry.h"
#include "Feedback.h"
#include "Settings.h"
#include "TranslationService.h"

#include <MinHook.h>

namespace ActivationHook {

    namespace {
        // Original function trampoline
        using ActivateRef_t = bool (*)(RE::TESObjectREFR*, RE::TESObjectREFR*, uint8_t,
                                       RE::TESBoundObject*, int32_t, bool);
        ActivateRef_t _originalActivateRef = nullptr;

        // Bypass flag — when set, skip interception for this FormID (one-shot)
        std::atomic<RE::FormID> s_bypassFormID{0};

        // ESP EditorIDs for vendor dialogue system (FormIDs assigned by xEdit)
        constexpr auto kVendorEnabledEditorID    = "SLID_VendorEnabled"sv;
        constexpr auto kVendorRegisteredEditorID = "SLID_VendorRegistered"sv;
        constexpr auto kVendorQuestEditorID      = "SLID_VendorQuest"sv;
        constexpr auto kVendorDialogueFactionID  = "SLID_VendorDialogueFaction"sv;

        // Track the last NPC we added to our dialogue faction so we can remove them next time
        RE::FormID s_lastDialogueFactionNPC = 0;

        /// Check if an NPC has any vendor faction and return the first one found.
        RE::TESFaction* GetVendorFaction(RE::Actor* a_actor) {
            if (!a_actor) return nullptr;

            auto* npc = a_actor->GetActorBase();
            if (!npc) return nullptr;

            // Check all factions the NPC belongs to
            for (const auto& factionRank : npc->factions) {
                auto* faction = factionRank.faction;
                if (!faction) continue;
                if (faction->IsVendor()) {
                    return faction;
                }
            }
            return nullptr;
        }

        /// Set vendor dialogue faction + globals and fill quest alias before dialogue opens.
        /// Called on every NPC activation — lightweight early-out for non-vendors.
        ///
        /// Dialogue filtering uses GetFactionRank(SLID_VendorDialogueFaction) >= 0 on INFOs.
        /// We add the NPC to this faction synchronously here — the engine evaluates it
        /// immediately when building the dialogue topic list. This is the NFF-proven pattern.
        void PrepareVendorDialogue(RE::Actor* a_actor) {
            auto* dialogueFaction = RE::TESForm::LookupByEditorID<RE::TESFaction>(kVendorDialogueFactionID);
            auto* globalEnabled = RE::TESForm::LookupByEditorID<RE::TESGlobal>(kVendorEnabledEditorID);
            auto* globalRegistered = RE::TESForm::LookupByEditorID<RE::TESGlobal>(kVendorRegisteredEditorID);
            if (!dialogueFaction || !globalEnabled || !globalRegistered) {
                logger::debug("PrepareVendorDialogue: dialogue faction or globals not found in ESP");
                return;
            }

            // Remove the previous NPC from our dialogue faction (if any)
            if (s_lastDialogueFactionNPC != 0) {
                auto* prevActor = RE::TESForm::LookupByID<RE::Actor>(s_lastDialogueFactionNPC);
                if (prevActor) {
                    prevActor->AddToFaction(dialogueFaction, -1);
                    logger::debug("PrepareVendorDialogue: removed {:08X} from dialogue faction",
                                  s_lastDialogueFactionNPC);
                }
                s_lastDialogueFactionNPC = 0;
            }

            auto* vendorFaction = GetVendorFaction(a_actor);
            if (!vendorFaction || NetworkManager::GetSingleton()->GetSellContainerFormID() == 0) {
                // Not a vendor, or no sell container designated — ensure not in faction
                globalEnabled->value = 0.0f;
                globalRegistered->value = 0.0f;
                return;
            }

            // Gate: Investor perk required
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* investorPerk = RE::TESForm::LookupByID<RE::BGSPerk>(0x58F7B);
            if (!investorPerk || !player || !player->HasPerk(investorPerk)) {
                globalEnabled->value = 0.0f;
                globalRegistered->value = 0.0f;
                a_actor->AddToFaction(dialogueFaction, -1);
                return;
            }

            // Gate: NPC must be in vendor whitelist
            auto* npcBase = a_actor->GetActorBase();
            if (!npcBase || !VendorRegistry::GetSingleton()->IsAllowedVendor(npcBase->GetFormID())) {
                logger::debug("PrepareVendorDialogue: not whitelisted ({:08X})",
                              npcBase ? npcBase->GetFormID() : 0);
                globalEnabled->value = 0.0f;
                globalRegistered->value = 0.0f;
                a_actor->AddToFaction(dialogueFaction, -1);
                return;
            }

            globalEnabled->value = 1.0f;

            // Add this vendor NPC to our dialogue faction (synchronous, immediate)
            a_actor->AddToFaction(dialogueFaction, 0);
            s_lastDialogueFactionNPC = a_actor->GetFormID();

            // Check if already registered AND active
            auto npcBaseID = npcBase->GetFormID();
            auto* vendorReg = VendorRegistry::GetSingleton();
            auto* vendor = vendorReg->FindVendorMutable(npcBaseID);
            globalRegistered->value = (vendor && vendor->active) ? 1.0f : 0.0f;

            // Detect investment: if the NPC has >= 500 gold on their person, the
            // player used the vanilla Investor perk on them (adds 500 gold to NPC)
            if (vendor && vendor->active && !vendor->invested) {
                constexpr RE::FormID kGold001 = 0x0000000F;
                auto* goldForm = RE::TESForm::LookupByID<RE::TESBoundObject>(kGold001);
                if (goldForm) {
                    auto inv = a_actor->GetInventory();
                    auto it = inv.find(goldForm);
                    if (it != inv.end() && it->second.first >= 500) {
                        vendor->invested = true;
                        logger::info("PrepareVendorDialogue: {} detected as invested ({}g on person)",
                                     vendor->vendorName, it->second.first);
                    }
                }
            }

            // Quest should already be running from kPostLoadGame start.
            // Lazy start here as safety net only.
            auto* quest = RE::TESForm::LookupByEditorID<RE::TESQuest>(kVendorQuestEditorID);
            if (!quest) {
                logger::warn("PrepareVendorDialogue: SLID_VendorQuest not found");
            } else if (!quest->IsRunning()) {
                quest->Start();
                logger::warn("PrepareVendorDialogue: quest was not running, started (running={})",
                             quest->IsRunning());
            }

            // Diagnostic: log every value the dialogue engine evaluates
            int32_t playerGold = 0;
            if (player) {
                auto goldForm = RE::TESForm::LookupByID(0x0000000F);  // Gold001
                if (goldForm) {
                    auto inv = player->GetInventory();
                    auto it = inv.find(goldForm->As<RE::TESBoundObject>());
                    if (it != inv.end()) playerGold = it->second.first;
                }
            }
            int32_t factionRank = a_actor->GetFactionRank(dialogueFaction, false);

            logger::info("PrepareVendorDialogue: {} ({:08X})"
                         " | faction {:08X} rank={}"
                         " | enabled={:.0f} registered={:.0f}"
                         " | quest running={}"
                         " | playerGold={}",
                         a_actor->GetName(), a_actor->GetFormID(),
                         dialogueFaction->GetFormID(), factionRank,
                         globalEnabled->value, globalRegistered->value,
                         quest ? quest->IsRunning() : false,
                         playerGold);

            // Fill the quest's reference alias (index 0) with this vendor actor.
            // The Papyrus accept fragment reads this to identify the vendor.
            // ForceRefIntoAlias is not exposed in our CommonLibSSE-NG headers, so
            // write directly to the refAliasMap (the runtime storage GetReference reads).
            if (quest) {
                auto handle = a_actor->GetHandle();
                quest->refAliasMap.erase(0);
                quest->refAliasMap.insert({0, handle});
            }
        }

        // Execute Whoosh for a network — pops WhooshConfigMenu if not yet configured
        void ExecuteWhoosh(const std::string& a_networkName) {
            auto* mgr = NetworkManager::GetSingleton();
            auto* net = mgr->FindNetwork(a_networkName);
            if (!net) return;

            if (!net->whooshConfigured) {
                auto defaultFilters = FilterRegistry::DefaultWhooshFilters();
                WhooshConfig::Menu::Show(defaultFilters,
                    [networkName = a_networkName](bool confirmed, std::unordered_set<std::string> filters) {
                        if (!confirmed) return;
                        auto* nmgr = NetworkManager::GetSingleton();
                        nmgr->SetWhooshConfig(networkName, filters);

                        auto moved = Distributor::Whoosh(networkName);
                        if (moved > 0) {
                            Feedback::OnWhoosh();
                            std::string msg = TF("$SLID_NotifyWhooshed", std::to_string(moved));
                            RE::DebugNotification(msg.c_str());
                        } else {
                            RE::DebugNotification(T("$SLID_NothingToWhoosh").c_str());
                        }
                    });
                return;
            }

            auto moved = Distributor::Whoosh(a_networkName);
            if (moved > 0) {
                Feedback::OnWhoosh();
                std::string msg = TF("$SLID_NotifyWhooshed", std::to_string(moved));
                RE::DebugNotification(msg.c_str());
            } else {
                RE::DebugNotification(T("$SLID_NothingToWhoosh").c_str());
            }
        }

        bool Hook_ActivateRef(RE::TESObjectREFR* a_this,
                              RE::TESObjectREFR* a_activator,
                              uint8_t a_arg2,
                              RE::TESBoundObject* a_object,
                              int32_t a_count,
                              bool a_defaultProcessingOnly) {
            // Only intercept player-initiated activation
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (a_activator != player) {
                return _originalActivateRef(a_this, a_activator, a_arg2, a_object, a_count, a_defaultProcessingOnly);
            }

            // Mod disabled — pass through without interception
            if (!Settings::bModEnabled) {
                return _originalActivateRef(a_this, a_activator, a_arg2, a_object, a_count, a_defaultProcessingOnly);
            }

            // Check and consume bypass flag
            auto thisID = a_this->GetFormID();
            auto expected = thisID;
            if (s_bypassFormID.compare_exchange_strong(expected, 0)) {
                logger::debug("ActivateRef hook: bypass consumed for {:08X}", thisID);
                return _originalActivateRef(a_this, a_activator, a_arg2, a_object, a_count, a_defaultProcessingOnly);
            }

            // --- Vendor NPC detection ---
            // If the target is an NPC with a vendor faction, set globals for dialogue conditions.
            // We do NOT suppress activation — the normal dialogue menu proceeds, and our
            // SLID_VendorBranch topic appears among the NPC's dialogue options.
            if (auto* actor = a_this->As<RE::Actor>()) {
                PrepareVendorDialogue(actor);
                return _originalActivateRef(a_this, a_activator, a_arg2, a_object, a_count, a_defaultProcessingOnly);
            }

            // Check roles for this container
            auto* mgr = NetworkManager::GetSingleton();
            auto networkName = mgr->FindNetworkByMaster(thisID);
            bool isMaster = !networkName.empty();
            bool isSell = (thisID == mgr->GetSellContainerFormID());
            bool isSummon = SummonChest::IsSummonedChest(thisID);

            if (!isMaster && !isSell && !isSummon) {
                return _originalActivateRef(a_this, a_activator, a_arg2, a_object, a_count, a_defaultProcessingOnly);
            }

            auto containerID = thisID;
            auto activatorID = a_activator->GetFormID();

            if (isSummon) {
                auto summonNetwork = SummonChest::GetNetworkName();
                logger::info("ActivateRef hook: intercepted summoned chest {:08X} for network '{}'",
                            thisID, summonNetwork);

                SKSE::GetTaskInterface()->AddTask([summonNetwork, activatorID]() {
                    UIHelper::ShowMessageBox(T("$SLID_MsgLinkPrefix") + summonNetwork, {T("$SLID_MsgOpen"), T("$SLID_MsgWhoosh"), T("$SLID_MsgAccessLink"), T("$SLID_MsgDismiss")},
                        [summonNetwork, activatorID](int idx) {
                            if (idx == 0) {
                                // Open master directly — no gather, no auto-sort
                                auto* mgr = NetworkManager::GetSingleton();
                                auto* net = mgr->FindNetwork(summonNetwork);
                                if (!net) return;
                                auto masterID = net->masterFormID;

                                SKSE::GetTaskInterface()->AddTask([masterID, activatorID]() {
                                    auto* master = RE::TESForm::LookupByID<RE::TESObjectREFR>(masterID);
                                    auto* activator = RE::TESForm::LookupByID<RE::TESObjectREFR>(activatorID);
                                    if (master && activator) {
                                        SetBypass(masterID);
                                        master->ActivateRef(activator, 0, nullptr, 0, false);
                                    }
                                });
                            } else if (idx == 1) {
                                SKSE::GetTaskInterface()->AddTask([summonNetwork]() {
                                    ExecuteWhoosh(summonNetwork);
                                });
                            } else if (idx == 2) {
                                SKSE::GetTaskInterface()->AddTask([summonNetwork]() {
                                    SLIDMenu::ConfigMenu::Show(summonNetwork);
                                });
                            } else {
                                // Dispel the summon effect — triggers OnEffectFinish → DespawnSummonChest
                                SKSE::GetTaskInterface()->AddTask([]() {
                                    auto* player = RE::PlayerCharacter::GetSingleton();
                                    auto* dh = RE::TESDataHandler::GetSingleton();
                                    if (!player || !dh) return;

                                    constexpr RE::FormID kSummonSPEL = 0x818;
                                    auto* spell = dh->LookupForm<RE::SpellItem>(kSummonSPEL, "SLID.esp"sv);
                                    if (spell) {
                                        auto handle = player->GetHandle();
                                        player->AsMagicTarget()->DispelEffect(spell, handle);
                                    }
                                });
                            }
                        });
                });

                return true;
            }

            if (isSell) {
                // Sell container only — 2-option MessageBox
                logger::info("ActivateRef hook: intercepted sell container {:08X}", thisID);

                SKSE::GetTaskInterface()->AddTask([containerID, activatorID]() {
                    UIHelper::ShowMessageBox(T("$SLID_MsgLinkPrefix") + T("$SLID_SellContainer"), {T("$SLID_MsgOpen"), T("$SLID_MsgOverview")},
                        [containerID, activatorID](int idx) {
                            if (idx == 0) {
                                SKSE::GetTaskInterface()->AddTask([containerID, activatorID]() {
                                    auto* container = RE::TESForm::LookupByID<RE::TESObjectREFR>(containerID);
                                    auto* activator = RE::TESForm::LookupByID<RE::TESObjectREFR>(activatorID);
                                    if (container && activator) {
                                        SetBypass(containerID);
                                        container->ActivateRef(activator, 0, nullptr, 0, false);
                                    }
                                });
                            } else if (idx == 1) {
                                SKSE::GetTaskInterface()->AddTask([]() {
                                    SellOverview::Menu::Show();
                                });
                            }
                        });
                });
            } else {
                // Master container only — 2-option MessageBox (existing behavior)
                logger::info("ActivateRef hook: intercepted master {:08X} for network '{}'",
                            thisID, networkName);

                SKSE::GetTaskInterface()->AddTask([networkName, containerID, activatorID]() {
                    UIHelper::ShowMessageBox(T("$SLID_MsgLinkPrefix") + networkName, {T("$SLID_MsgOpen"), T("$SLID_MsgWhoosh"), T("$SLID_MsgAccessLink")},
                        [networkName, containerID, activatorID](int idx) {
                            if (idx == 0) {
                                SKSE::GetTaskInterface()->AddTask([containerID, activatorID]() {
                                    auto* container = RE::TESForm::LookupByID<RE::TESObjectREFR>(containerID);
                                    auto* activator = RE::TESForm::LookupByID<RE::TESObjectREFR>(activatorID);
                                    if (container && activator) {
                                        SetBypass(containerID);
                                        container->ActivateRef(activator, 0, nullptr, 0, false);
                                    }
                                });
                            } else if (idx == 1) {
                                SKSE::GetTaskInterface()->AddTask([networkName]() {
                                    ExecuteWhoosh(networkName);
                                });
                            } else if (idx == 2) {
                                SKSE::GetTaskInterface()->AddTask([networkName]() {
                                    SLIDMenu::ConfigMenu::Show(networkName);
                                });
                            }
                        });
                });
            }

            return true;  // Suppress default activation
        }
    }

    bool Install() {
        if (MH_Initialize() != MH_OK) {
            logger::critical("MinHook initialization failed");
            return false;
        }

        // TESObjectREFR::ActivateRef — SE 19369, AE 19796
        auto addr = REL::RelocationID(19369, 19796).address();

        if (MH_CreateHook(reinterpret_cast<void*>(addr),
                          reinterpret_cast<void*>(&Hook_ActivateRef),
                          reinterpret_cast<void**>(&_originalActivateRef)) != MH_OK) {
            logger::critical("Failed to create ActivateRef hook");
            return false;
        }

        if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
            logger::critical("Failed to enable hooks");
            return false;
        }

        logger::info("ActivateRef hook installed at {:X}", addr);
        return true;
    }

    void SetBypass(RE::FormID a_formID) {
        s_bypassFormID.store(a_formID);
    }

    RE::FormID GetLastVendorActorID() {
        return s_lastDialogueFactionNPC;
    }
}
