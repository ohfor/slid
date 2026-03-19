#include "Version.h"
#include "Settings.h"
#include "ActivationHook.h"
#include "NetworkManager.h"
#include "VendorRegistry.h"
#include "ConsoleCommands.h"
#include "SLIDMenu.h"
#include "TagInputMenu.h"
#include "WhooshConfigMenu.h"
#include "RestockConfigMenu.h"
#include "SalesProcessor.h"
#include "SellOverviewMenu.h"
#include "SummonChest.h"
#include "FilterRegistry.h"
#include "TraitEvaluator.h"
#include "SCIEIntegration.h"
#include "TranslationService.h"
#include "APIMessaging.h"
#include "WelcomeMenu.h"
#include "FontTestMenu.h"
#include "ContextMenu.h"
#include "ContainerRegistry.h"
#include "ContainerRegistryTest.h"
#include "DisplayName.h"
#include "Lifecycle.h"
#include "Diagnostics.h"

// Container source registration functions (defined in source files)
void RegisterSpecialContainerSource();
void RegisterNFFContainerSource();
void RegisterKWFContainerSource();
void RegisterTaggedContainerSource();
void RegisterSCIEContainerSource();
void RegisterContainerListSource();
void RegisterCellScanContainerSource();

// Plugin version info for SKSE
extern "C" __declspec(dllexport) constinit auto SKSEPlugin_Version = []() {
    SKSE::PluginVersionData v;
    v.PluginVersion(REL::Version(Version::MAJOR, Version::MINOR, Version::PATCH, 0));
    v.PluginName(Version::NAME);
    v.AuthorName(Version::AUTHOR);
    v.UsesAddressLibrary();  // Resolve addresses via Address Library (version-independent)
    v.UsesNoStructs();       // Don't depend on game struct layouts
    return v;
}();

namespace {
    void InitializeLogging() {
        auto logDir = SKSE::log::log_directory();
        if (!logDir) {
            SKSE::stl::report_and_fail("SKSE log_directory not provided, logs disabled.");
        }

        auto path = *logDir / fmt::format(FMT_STRING("{}.log"), Version::NAME);

        try {
            auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), true);
            auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
            log->set_level(spdlog::level::info);
            log->flush_on(spdlog::level::info);

            spdlog::set_default_logger(std::move(log));
            spdlog::set_pattern("[%H:%M:%S.%e] [%l] %v"s);

            logger::info("{} v{}.{}.{} loaded", Version::NAME, Version::MAJOR, Version::MINOR, Version::PATCH);

            // Log runtime variant (SE/AE/VR)
            auto ver = REL::Module::get().version();
            const char* runtime = "Unknown";
            if (REL::Module::IsVR()) {
                runtime = "VR";
            } else if (ver >= SKSE::RUNTIME_SSE_1_6_317) {
                runtime = "AE";
            } else {
                runtime = "SE";
            }
            logger::info("Game: Skyrim {} v{}.{}.{}.{}", runtime, ver[0], ver[1], ver[2], ver[3]);
        } catch (const std::exception& ex) {
            SKSE::stl::report_and_fail(fmt::format("Log init failed: {}", ex.what()));
        }
    }

    void ResetVendorGlobals() {
        auto* globalEnabled = RE::TESForm::LookupByEditorID<RE::TESGlobal>("SLID_VendorEnabled");
        auto* globalRegistered = RE::TESForm::LookupByEditorID<RE::TESGlobal>("SLID_VendorRegistered");

        if (globalEnabled) globalEnabled->value = 0.0f;
        if (globalRegistered) globalRegistered->value = 0.0f;

        logger::debug("ResetVendorGlobals: vendor dialogue globals reset to 0");
    }

    void GrantPowers() {
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) return;

        // All historical spell local IDs (legacy + summon) — always remove these
        constexpr RE::FormID kLegacySpellIDs[] = {
            0x801, 0x803, 0x805, 0x807, 0x809, 0x80B, 0x816, 0x818
        };
        constexpr auto kPluginName = "SLID.esp"sv;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            logger::warn("GrantPowers: player not available");
            return;
        }

        // Remove all legacy powers unconditionally
        uint32_t removed = 0;
        for (auto localID : kLegacySpellIDs) {
            auto* spell = dataHandler->LookupForm<RE::SpellItem>(localID, kPluginName);
            if (spell && player->HasSpell(spell)) {
                player->RemoveSpell(spell);
                ++removed;
            }
        }
        if (removed > 0) {
            logger::debug("GrantPowers: removed {} legacy powers", removed);
        }

        // Look up the unified context power by EditorID
        auto* contextSpell = RE::TESForm::LookupByEditorID<RE::SpellItem>("SLID_ContextSPEL");

        if (!Settings::bModEnabled) {
            // Mod disabled — also remove context power if present
            if (contextSpell && player->HasSpell(contextSpell)) {
                player->RemoveSpell(contextSpell);
                logger::debug("GrantPowers: removed context power (mod disabled)");
            }
            return;
        }

        if (!contextSpell) {
            // ESP not yet updated with new SPEL — log and skip gracefully
            logger::warn("GrantPowers: SLID_ContextSPEL not found in ESP (ESP not yet updated?)");
            return;
        }

        if (!player->HasSpell(contextSpell)) {
            player->AddSpell(contextSpell);
            logger::debug("GrantPowers: granted unified context power");
        }
    }

    // Event sink: on first cell load after kGameLoading, transitions to kWorldReady
    class CellLoadedHandler : public RE::BSTEventSink<RE::TESCellFullyLoadedEvent> {
    public:
        static CellLoadedHandler* GetSingleton() {
            static CellLoadedHandler singleton;
            return &singleton;
        }

        RE::BSEventNotifyControl ProcessEvent(
            const RE::TESCellFullyLoadedEvent*,
            RE::BSTEventSource<RE::TESCellFullyLoadedEvent>*) override
        {
            if (Lifecycle::GetState() == Lifecycle::State::kGameLoading) {
                Lifecycle::TransitionTo(Lifecycle::State::kWorldReady);
            }
            return RE::BSEventNotifyControl::kContinue;
        }

    private:
        CellLoadedHandler() = default;
    };

    void MessageHandler(SKSE::MessagingInterface::Message* a_msg) {
        switch (a_msg->type) {
            case SKSE::MessagingInterface::kDataLoaded:
                Lifecycle::TransitionTo(Lifecycle::State::kDataLoaded);
                logger::info("Data loaded, initializing...");
                TranslationService::GetSingleton()->Load();
                Settings::LoadUniqueItems();
                TraitEvaluator::Init();
                FilterRegistry::GetSingleton()->Init();
                // Register container sources (order determines priority fallback)
                RegisterSpecialContainerSource();
                RegisterNFFContainerSource();
                RegisterKWFContainerSource();
                RegisterTaggedContainerSource();
                RegisterSCIEContainerSource();
                RegisterContainerListSource();
                RegisterCellScanContainerSource();
#ifdef _DEBUG
                // Run integration tests in debug builds
                ContainerRegistryTest::RunTests();
#endif
                VendorRegistry::GetSingleton()->LoadWhitelist();
                if (auto* papyrus = SKSE::GetPapyrusInterface()) {
                    papyrus->Register(ConsoleCommands::RegisterFunctions);
                }
                ConsoleCommands::RegisterEventSink();
                SLIDMenu::ConfigMenu::Register();
                SLIDMenu::InputHandler::Register();
                SLIDMenu::ContainerCloseListener::Register();
                TagInputMenu::Menu::Register();
                TagInputMenu::InputHandler::Register();
                WhooshConfig::Menu::Register();
                WhooshConfig::InputHandler::Register();
                RestockConfig::Menu::Register();
                RestockConfig::InputHandler::Register();
                SalesProcessor::RegisterEventSinks();
                SummonChest::RegisterEventSink();
                SellOverview::Menu::Register();
                SellOverview::InputHandler::Register();
                WelcomeMenu::Menu::Register();
                WelcomeMenu::InputHandler::Register();
                FontTestMenu::Menu::Register();
                FontTestMenu::InputHandler::Register();
                ContextMenu::Menu::Register();
                ContextMenu::InputHandler::Register();
                SCIEIntegration::RegisterListener();
                APIMessaging::Initialize();
                // Register cell-loaded sink for deferred init (new-game + load-game validation)
                RE::ScriptEventSourceHolder::GetSingleton()
                    ->AddEventSink<RE::TESCellFullyLoadedEvent>(CellLoadedHandler::GetSingleton());
                break;

            case SKSE::MessagingInterface::kPostLoadGame: {
                Lifecycle::TransitionTo(Lifecycle::State::kGameLoading);
                logger::info("Game loaded — deferring REFR validation to first cell load");
                WelcomeMenu::ResetSession();
                SummonChest::Clear();
                // Load network/tag/sell config from INI (mod author presets — only adds missing entries)
                // Safe here: reads INI data + cosave state, doesn't need LookupByID for REFRs
                NetworkManager::GetSingleton()->LoadConfigFromINI();
                // Reset vendor dialogue globals (safety — stale values from previous session)
                ResetVendorGlobals();

                // Stop and restart VendorQuest to reset dialogue engine state
                if (auto* quest = RE::TESForm::LookupByEditorID<RE::TESQuest>("SLID_VendorQuest")) {
                    logger::info("VendorQuest: pre-restart running={}", quest->IsRunning());
                    quest->Stop();
                    SKSE::GetTaskInterface()->AddTask([quest]() {
                        quest->Start();
                        logger::info("VendorQuest: deferred start (running={})", quest->IsRunning());
                    });
                }
                GrantPowers();
                // Defer REFR-dependent work to first cell load
                Lifecycle::DeferUntilWorldReady([]() {
                    auto result = NetworkManager::GetSingleton()->ValidateNetworks();
                    auto vendorsPruned = VendorRegistry::GetSingleton()->Validate();
                    if (result.prunedNetworks > 0 || result.prunedTags > 0 || result.prunedFilters > 0 || result.prunedSell || vendorsPruned > 0) {
                        std::string msg = "SLID: Pruned " +
                            std::to_string(result.prunedNetworks) + " networks, " +
                            std::to_string(result.prunedTags) + " tags, " +
                            std::to_string(result.prunedFilters) + " filters" +
                            (result.prunedSell ? ", sell container" : "") +
                            (vendorsPruned > 0 ? ", " + std::to_string(vendorsPruned) + " vendors" : "") +
                            " (missing mods?)";
                        RE::DebugNotification(msg.c_str());
                        logger::warn("{}", msg);
                    }
                    DisplayName::ApplyAll();
                    Diagnostics::ValidateState();
                }, "LoadGameInit");
                // Cell events may have already fired during the loading screen
                // before kPostLoadGame arrives. If the player is already in-world,
                // transition immediately rather than waiting for the next cell load.
                if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                    if (player->GetParentCell()) {
                        Lifecycle::TransitionTo(Lifecycle::State::kWorldReady);
                    }
                }
                break;
            }

            case SKSE::MessagingInterface::kNewGame:
                Lifecycle::TransitionTo(Lifecycle::State::kGameLoading);
                logger::info("New game started — deferring init to first cell load");
                WelcomeMenu::ResetSession();
                SummonChest::Clear();
                // Defer all player-dependent init to first cell load
                Lifecycle::DeferUntilWorldReady([]() {
                    NetworkManager::GetSingleton()->LoadConfigFromINI();
                    DisplayName::ApplyAll();
                    ResetVendorGlobals();
                    GrantPowers();
                    if (auto* quest = RE::TESForm::LookupByEditorID<RE::TESQuest>("SLID_VendorQuest")) {
                        if (!quest->IsRunning()) {
                            quest->Start();
                            logger::info("VendorQuest: started for new game (running={})", quest->IsRunning());
                        }
                    }
                    Diagnostics::ValidateState();
                }, "NewGameInit");
                // In case cells loaded before this message (unlikely for new game,
                // but defensive), transition immediately.
                if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                    if (player->GetParentCell()) {
                        Lifecycle::TransitionTo(Lifecycle::State::kWorldReady);
                    }
                }
                break;
        }
    }

    void APIMessageHandler(SKSE::MessagingInterface::Message* a_msg) {
        // Route inter-plugin API messages to handlers
        APIMessaging::HandleMessage(a_msg);
        // Also route to SCIE integration for SCIE responses
        SCIEIntegration::HandleMessage(a_msg);
    }
}

// Legacy Query entry point for SE 1.5.97 (old SKSE64 looks for this)
extern "C" __declspec(dllexport) bool SKSEPlugin_Query(const SKSE::QueryInterface*, SKSE::PluginInfo* a_info) {
    a_info->infoVersion = SKSE::PluginInfo::kVersion;
    a_info->name = Version::NAME.data();
    a_info->version = Version::MAJOR;
    return true;
}

// Main load entry point
SKSEPluginLoad(const SKSE::LoadInterface* a_skse) {
    SKSE::Init(a_skse);

    InitializeLogging();

    Settings::Load();
    if (Settings::bDebugLogging) {
        spdlog::default_logger()->set_level(spdlog::level::debug);
        spdlog::default_logger()->flush_on(spdlog::level::debug);
        logger::debug("Debug logging enabled via INI");
    }

    logger::info("{} is loading...", Version::NAME);

    // Register for SKSE messages
    auto messaging = SKSE::GetMessagingInterface();
    if (!messaging->RegisterListener(MessageHandler)) {
        logger::error("Failed to register messaging listener");
        return false;
    }

    // Register for inter-plugin API messages (nullptr = listen to all plugins)
    if (!messaging->RegisterListener(nullptr, APIMessageHandler)) {
        logger::error("Failed to register API messaging listener");
        return false;
    }

    auto serialization = SKSE::GetSerializationInterface();
    serialization->SetUniqueID('SLID');
    serialization->SetSaveCallback(NetworkManager::OnGameSaved);
    serialization->SetLoadCallback(NetworkManager::OnGameLoaded);
    serialization->SetRevertCallback(NetworkManager::OnRevert);

    if (!ActivationHook::Install()) {
        logger::critical("{} DISABLED - hook installation failed", Version::NAME);
        return true;
    }

    logger::info("{} loaded successfully", Version::NAME);
    return true;
}
