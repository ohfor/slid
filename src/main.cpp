#include "Version.h"
#include "Settings.h"
#include "ActivationHook.h"
#include "NetworkManager.h"
#include "VendorRegistry.h"
#include "ConsoleCommands.h"
#include "SLIDMenu.h"
#include "TagInputMenu.h"
#include "WhooshConfigMenu.h"
#include "SalesProcessor.h"
#include "SellOverviewMenu.h"
#include "SummonChest.h"
#include "FilterRegistry.h"
#include "TraitEvaluator.h"
#include "SCIEIntegration.h"
#include "TranslationService.h"
#include "APIMessaging.h"
#include "WelcomeMenu.h"
#include "ContainerRegistry.h"
#include "ContainerRegistryTest.h"

// Container source registration functions (defined in source files)
void RegisterSpecialContainerSource();
void RegisterNFFContainerSource();
void RegisterKWFContainerSource();
void RegisterTaggedContainerSource();
void RegisterSCIEContainerSource();
void RegisterCellScanContainerSource();

#include <ShlObj.h>  // SHGetKnownFolderPath — needed because SKSE::log::log_directory() is broken (see reference override)

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
    /// Get the SKSE log directory by deriving the game folder name from the DLL's own path.
    /// SKSE::log::log_directory() uses a relocation that resolves to "Skyrim.INI" instead of
    /// the game folder name, so we derive it from the DLL location instead.
    std::optional<std::filesystem::path> GetLogDirectory() {
        HMODULE hModule = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCWSTR>(&GetLogDirectory), &hModule)) {
            return std::nullopt;
        }

        wchar_t dllPath[MAX_PATH];
        if (GetModuleFileNameW(hModule, dllPath, MAX_PATH) == 0) {
            return std::nullopt;
        }

        // DLL path: {GameRoot}\Data\SKSE\Plugins\SLID.dll
        // Navigate up 4 levels to get game root, then extract folder name
        std::filesystem::path dllLocation = dllPath;
        auto gameRoot = dllLocation.parent_path()  // Plugins
                                   .parent_path()  // SKSE
                                   .parent_path()  // Data
                                   .parent_path(); // GameRoot (e.g., "Skyrim Special Edition")

        auto gameFolderName = gameRoot.filename();

        // Get Documents folder
        wchar_t* docPath = nullptr;
        if (!SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docPath))) {
            return std::nullopt;
        }

        std::filesystem::path result = docPath;
        CoTaskMemFree(docPath);

        // Build: Documents\My Games\{GameFolderName}\SKSE
        result /= "My Games";
        result /= gameFolderName;
        result /= "SKSE";
        return result;
    }

    void InitializeLogging() {
        auto logDir = GetLogDirectory();
        if (!logDir) {
            SKSE::stl::report_and_fail("Could not determine log directory");
        }

        std::filesystem::create_directories(*logDir);

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

        // SetMaster (0x801), Tag (0x803), Deregister (0x805), Detect (0x809), SellContainer (0x816), Summon (0x818)
        constexpr RE::FormID kBaseSpellIDs[] = {0x801, 0x803, 0x805, 0x809, 0x816};
        constexpr RE::FormID kSummonSpellID = 0x818;
        constexpr auto kPluginName = "SLID.esp"sv;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            logger::warn("GrantPowers: player not available");
            return;
        }

        // If mod is disabled, remove all SLID powers
        if (!Settings::bModEnabled) {
            uint32_t removed = 0;
            for (auto localID : kBaseSpellIDs) {
                auto* spell = dataHandler->LookupForm<RE::SpellItem>(localID, kPluginName);
                if (spell && player->HasSpell(spell)) {
                    player->RemoveSpell(spell);
                    ++removed;
                }
            }
            auto* summonSpell = dataHandler->LookupForm<RE::SpellItem>(kSummonSpellID, kPluginName);
            if (summonSpell && player->HasSpell(summonSpell)) {
                player->RemoveSpell(summonSpell);
                ++removed;
            }
            if (removed > 0) {
                logger::debug("GrantPowers: removed {} powers (mod disabled)", removed);
            }
            return;
        }

        uint32_t added = 0;

        // Always add base spells
        for (auto localID : kBaseSpellIDs) {
            auto* spell = dataHandler->LookupForm<RE::SpellItem>(localID, kPluginName);
            if (!spell) {
                logger::error("GrantPowers: form {:03X} not found in {}", localID, kPluginName);
                continue;
            }
            if (!player->HasSpell(spell)) {
                player->AddSpell(spell);
                ++added;
            }
        }

        // Conditionally add/remove Summon power based on setting
        auto* summonSpell = dataHandler->LookupForm<RE::SpellItem>(kSummonSpellID, kPluginName);
        if (summonSpell) {
            if (Settings::bSummonEnabled) {
                if (!player->HasSpell(summonSpell)) {
                    player->AddSpell(summonSpell);
                    ++added;
                }
            } else {
                if (player->HasSpell(summonSpell)) {
                    player->RemoveSpell(summonSpell);
                    logger::debug("GrantPowers: removed Summon power (disabled in settings)");
                }
            }
        }

        if (added > 0) {
            logger::debug("GrantPowers: added {} powers to player", added);
        }
    }

    void MessageHandler(SKSE::MessagingInterface::Message* a_msg) {
        switch (a_msg->type) {
            case SKSE::MessagingInterface::kDataLoaded:
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
                SalesProcessor::RegisterEventSinks();
                SellOverview::Menu::Register();
                SellOverview::InputHandler::Register();
                WelcomeMenu::Menu::Register();
                WelcomeMenu::InputHandler::Register();
                SCIEIntegration::RegisterListener();
                APIMessaging::Initialize();
                break;

            case SKSE::MessagingInterface::kPostLoadGame: {
                logger::info("Game loaded");
                SummonChest::Clear();
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
                // Load network/tag/sell config from INI (mod author presets — only adds missing entries)
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
                break;
            }

            case SKSE::MessagingInterface::kNewGame:
                logger::info("New game started");
                SummonChest::Clear();
                // Load network/tag/sell config from INI (mod author presets)
                NetworkManager::GetSingleton()->LoadConfigFromINI();
                ResetVendorGlobals();
                GrantPowers();
                if (auto* quest = RE::TESForm::LookupByEditorID<RE::TESQuest>("SLID_VendorQuest")) {
                    if (!quest->IsRunning()) {
                        quest->Start();
                        logger::info("VendorQuest: started for new game (running={})", quest->IsRunning());
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
