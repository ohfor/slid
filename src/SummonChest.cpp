#include "SummonChest.h"
#include "NetworkManager.h"
#include "TranslationService.h"

#include <thread>

namespace SummonChest {

    namespace {
        RE::FormID   s_chestRefID    = 0;       // spawned ref FormID (0 = none)
        std::string  s_networkName;             // which network

        // Vanilla chest base form (TreasChestSmallEMPTYNoRespawn from Skyrim.esm)
        constexpr RE::FormID kChestBaseFormID = 0x0F8478;
        constexpr auto       kSkyrimPlugin    = "Skyrim.esm"sv;

        // Effect shaders and spell from our ESP
        constexpr RE::FormID kSummonEFSH   = 0x81B;    // Summon shader (applied to chest)
        constexpr RE::FormID kSummonSPEL   = 0x818;    // Conjure Link Chest spell
        constexpr auto       kPluginName   = "SLID.esp"sv;
        constexpr float      kShaderDuration = 120.0f;  // matches MGEF duration (2 minutes)

        // Spawn distance
        constexpr float kSpawnDistance = 200.0f;   // ~2 meters in front

        // Raycast: cast from player height down to well below
        constexpr float kRaycastAbovePlayer = 50.0f;   // start slightly above player head
        constexpr float kRaycastBelowPlayer = 500.0f;  // reach well below player feet

        // Dispel the summon MGEF (triggers OnEffectFinish → DespawnSummonChest)
        void DispelSummonEffect() {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* dh = RE::TESDataHandler::GetSingleton();
            if (!player || !dh) return;

            auto* spell = dh->LookupForm<RE::SpellItem>(kSummonSPEL, kPluginName);
            if (spell) {
                auto handle = player->GetHandle();
                player->AsMagicTarget()->DispelEffect(spell, handle);
            }
        }

        // Cell-detach listener: despawns summoned chest when its ref detaches from the loaded cell
        class CellDetachListener : public RE::BSTEventSink<RE::TESCellAttachDetachEvent> {
        public:
            static CellDetachListener* GetSingleton() {
                static CellDetachListener singleton;
                return &singleton;
            }

            RE::BSEventNotifyControl ProcessEvent(
                const RE::TESCellAttachDetachEvent* a_event,
                RE::BSTEventSource<RE::TESCellAttachDetachEvent>*) override {

                // Only care about detach events while a chest is active
                if (!a_event || a_event->attached || s_chestRefID == 0) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                // Check if the detaching ref is our summoned chest
                auto* ref = a_event->reference.get();
                if (!ref || ref->GetFormID() != s_chestRefID) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                logger::info("SummonChest: chest {:08X} detaching from cell — despawning", s_chestRefID);

                // Despawn immediately (ref is still accessible during the detach event)
                Despawn();

                // Dispel the MGEF on the game thread so OnEffectFinish doesn't try to double-despawn
                SKSE::GetTaskInterface()->AddTask([]() {
                    DispelSummonEffect();
                });

                return RE::BSEventNotifyControl::kContinue;
            }

        private:
            CellDetachListener() = default;
        };
    }

    void Summon(const std::string& a_networkName) {
        // If already active, despawn the old one first
        if (IsActive()) {
            Despawn();
        }

        auto* mgr = NetworkManager::GetSingleton();
        auto* net = mgr->FindNetwork(a_networkName);
        if (!net) {
            logger::error("SummonChest::Summon: network '{}' not found", a_networkName);
            RE::DebugNotification(T("$SLID_ErrNetworkNotFound").c_str());
            return;
        }

        // Get player for positioning
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            logger::error("SummonChest::Summon: player not available");
            return;
        }

        // Look up chest base form
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) return;

        auto* chestBase = dataHandler->LookupForm<RE::TESObjectCONT>(kChestBaseFormID, kSkyrimPlugin);
        if (!chestBase) {
            logger::error("SummonChest::Summon: chest base form {:06X} not found in {}",
                         kChestBaseFormID, kSkyrimPlugin);
            RE::DebugNotification(T("$SLID_ErrSummonFailed").c_str());
            return;
        }

        // Spawn chest at player location
        auto spawnedPtr = player->PlaceObjectAtMe(chestBase, false);
        auto* spawnedRef = spawnedPtr.get();
        if (!spawnedRef) {
            logger::error("SummonChest::Summon: PlaceObjectAtMe failed");
            RE::DebugNotification(T("$SLID_ErrSummonFailed").c_str());
            return;
        }

        // Calculate horizontal spawn position ~2m in front of player
        auto playerPos = player->GetPosition();
        float angleZ = player->GetAngleZ();
        RE::NiPoint3 spawnPos;
        spawnPos.x = playerPos.x + kSpawnDistance * std::sin(angleZ);
        spawnPos.y = playerPos.y + kSpawnDistance * std::cos(angleZ);
        spawnPos.z = playerPos.z;  // initial Z, refined by raycast below

        // Raycast straight down at the spawn XY to find ground height.
        // Start slightly above the player (avoids hitting the ceiling from outside)
        // and cast well below the player's feet.
        float groundZ = spawnPos.z;
        auto* cell = player->GetParentCell();
        auto* bhkWorld = cell ? cell->GetbhkWorld() : nullptr;
        if (bhkWorld) {
            auto* hkWorld = bhkWorld->GetWorld1();
            if (hkWorld) {
                float scale = RE::bhkWorld::GetWorldScale();

                RE::hkpWorldRayCastInput  rayIn;
                RE::hkpWorldRayCastOutput rayOut;

                // Ray starts near player head height, ends well below feet
                float rayTopZ    = playerPos.z + kRaycastAbovePlayer;
                float rayBottomZ = playerPos.z - kRaycastBelowPlayer;
                float rayLength  = rayTopZ - rayBottomZ;

                rayIn.from = RE::hkVector4(spawnPos.x * scale, spawnPos.y * scale,
                                           rayTopZ * scale, 0.0f);
                rayIn.to   = RE::hkVector4(spawnPos.x * scale, spawnPos.y * scale,
                                           rayBottomZ * scale, 0.0f);

                hkWorld->CastRay(rayIn, rayOut);

                if (rayOut.HasHit()) {
                    // hitFraction is 0..1 along the ray (top to bottom)
                    float hitZ = rayTopZ - rayOut.hitFraction * rayLength;
                    groundZ = hitZ;
                    logger::info("SummonChest: raycast hit at Z={:.1f} (fraction={:.4f}, playerZ={:.1f})",
                                groundZ, rayOut.hitFraction, playerPos.z);
                } else {
                    logger::warn("SummonChest: raycast missed — using player Z ({:.1f})", playerPos.z);
                }
            }
        } else {
            logger::warn("SummonChest: no bhkWorld — using player Z");
        }

        spawnPos.z = groundZ;
        spawnedRef->SetPosition(spawnPos);

        // Force flat rotation: zero pitch/roll, face player's yaw direction
        spawnedRef->data.angle.x = 0.0f;  // pitch
        spawnedRef->data.angle.y = 0.0f;  // roll
        spawnedRef->data.angle.z = angleZ; // yaw — face same direction as player

        // Name the chest "<network> Link"
        std::string chestName = a_networkName + " Link";
        spawnedRef->SetDisplayName(chestName.c_str(), true);

        // Set ownership to player so the chest never shows "Steal from" in merchant cells
        auto* playerBase = RE::TESForm::LookupByID<RE::TESNPC>(0x7);  // PlayerRef base form
        if (playerBase) {
            spawnedRef->extraList.SetOwner(playerBase);
        }

        // Store state
        s_chestRefID = spawnedRef->GetFormID();
        s_networkName = a_networkName;

        logger::info("SummonChest::Summon: spawned chest {:08X} for network '{}' at ({:.0f}, {:.0f}, {:.0f})",
                     s_chestRefID, a_networkName, spawnPos.x, spawnPos.y, spawnPos.z);

        // Wait for 3D to load, then apply shader on game thread
        auto chestID = s_chestRefID;
        std::thread([chestID]() {
            // Poll until 3D is ready (up to ~3 seconds)
            for (int i = 0; i < 30; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (chestID != s_chestRefID) return;  // chest was replaced/despawned

                auto ready = std::make_shared<std::atomic<bool>>(false);
                SKSE::GetTaskInterface()->AddTask([chestID, ready]() {
                    auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(chestID);
                    ready->store(ref && ref->Get3D());
                });

                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                if (!ready->load()) continue;

                logger::info("SummonChest: 3D ready after {}ms, applying shader", (i + 1) * 150);

                SKSE::GetTaskInterface()->AddTask([chestID]() {
                    auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(chestID);
                    if (!ref || !ref->Get3D()) return;

                    auto* dh = RE::TESDataHandler::GetSingleton();
                    auto* shader = dh ? dh->LookupForm<RE::TESEffectShader>(kSummonEFSH, kPluginName) : nullptr;
                    if (shader) {
                        ref->ApplyEffectShader(shader, kShaderDuration);
                        logger::info("SummonChest: shader applied to {:08X}", chestID);
                    }
                });
                return;
            }
            logger::warn("SummonChest: 3D never loaded for {:08X} after 3s", chestID);
        }).detach();
    }

    void Despawn() {
        if (s_chestRefID == 0) return;

        auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(s_chestRefID);
        if (ref) {
            ref->Disable();
            ref->SetDelete(true);
            logger::info("SummonChest::Despawn: removed chest {:08X}", s_chestRefID);
        } else {
            logger::warn("SummonChest::Despawn: chest {:08X} not in memory (cell unloaded?)", s_chestRefID);
        }

        s_chestRefID = 0;
        s_networkName.clear();
    }

    bool IsActive() {
        return s_chestRefID != 0;
    }

    bool IsSummonedChest(RE::FormID a_id) {
        return a_id != 0 && a_id == s_chestRefID;
    }

    std::string GetNetworkName() {
        return s_networkName;
    }

    void Clear() {
        // Try to clean up the chest ref before forgetting about it
        if (s_chestRefID != 0) {
            auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(s_chestRefID);
            if (ref) {
                ref->Disable();
                ref->SetDelete(true);
                logger::info("SummonChest::Clear: cleaned up chest {:08X}", s_chestRefID);
            }
        }
        s_chestRefID = 0;
        s_networkName.clear();
        logger::debug("SummonChest::Clear: state reset");
    }

    void RegisterEventSink() {
        auto* holder = RE::ScriptEventSourceHolder::GetSingleton();
        if (!holder) {
            logger::error("SummonChest: ScriptEventSourceHolder not available");
            return;
        }
        holder->AddEventSink(CellDetachListener::GetSingleton());
        logger::info("SummonChest: registered cell-detach event sink");
    }
}
