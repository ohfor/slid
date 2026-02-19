#include "ContainerScanner.h"
#include "ContainerRegistry.h"
#include "NetworkManager.h"
#include "Settings.h"
#include "SCIEIntegration.h"
#include "TranslationService.h"

namespace ContainerScanner {

    std::vector<ScannedContainer> ScanCellContainers(RE::FormID a_excludeFormID) {
        std::vector<ScannedContainer> result;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return result;

        auto* cell = player->GetParentCell();
        if (!cell) return result;

        std::string cellName;
        if (cell->GetFullName() && cell->GetFullName()[0] != '\0') {
            cellName = cell->GetFullName();
        }

        auto playerPos = player->GetPosition();

        cell->ForEachReferenceInRange(playerPos, 100000.0f, [&](RE::TESObjectREFR& a_ref) -> RE::BSContainer::ForEachResult {
            // Skip disabled/deleted
            if (a_ref.IsDisabled() || a_ref.IsDeleted()) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            // Must have container data
            if (!a_ref.GetContainer()) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            auto formID = a_ref.GetFormID();

            // Skip the excluded container (typically the master)
            if (formID == a_excludeFormID) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            // Safety heuristic: non-respawning containers are safe
            // Player-owned containers are safe
            // We include both for now; the picker shows all qualifying containers
            auto* base = a_ref.GetBaseObject();
            if (!base) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            auto* cont = base->As<RE::TESObjectCONT>();
            if (!cont) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            // Check respawn flag — respawning containers are risky
            bool respawns = cont->data.flags.any(RE::CONT_DATA::Flag::kRespawn);

            // Check ownership
            auto* owner = a_ref.GetOwner();
            bool playerOwned = false;
            if (owner) {
                if (owner->GetFormID() == 0x14) {
                    playerOwned = true;
                } else if (auto* faction = owner->As<RE::TESFaction>()) {
                    playerOwned = player->IsInFaction(faction);
                }
            }

            // Include if: non-respawning OR player-owned
            if (!respawns || playerOwned) {
                std::string name = T("$SLID_Container");
                if (base->GetName() && base->GetName()[0] != '\0') {
                    name = base->GetName();
                }
                result.push_back({formID, name, cellName});
            }

            return RE::BSContainer::ForEachResult::kContinue;
        });

        logger::debug("ScanCellContainers: found {} containers in cell '{}'", result.size(), cellName);
        return result;
    }

    std::pair<std::string, std::string> ResolveContainerInfo(RE::FormID a_formID) {
        // Delegate to ContainerRegistry
        auto display = ContainerRegistry::GetSingleton()->Resolve(a_formID);
        return {display.name, display.location};
    }

    std::vector<PickerEntry> BuildContainerList(RE::FormID a_masterFormID, [[maybe_unused]] bool a_includeMaster) {
        // Delegate to ContainerRegistry
        return ContainerRegistry::GetSingleton()->BuildPickerList(a_masterFormID);
    }
}
