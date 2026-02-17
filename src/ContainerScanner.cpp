#include "ContainerScanner.h"
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
        if (a_formID == 0) return {"Unknown", ""};

        auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_formID);
        if (!ref) return {"Unknown", ""};

        // Check tag registry first — tagged containers use their custom name
        auto tagName = NetworkManager::GetSingleton()->GetTagName(a_formID);

        // Get base object name as fallback
        std::string name = T("$SLID_Container");
        if (!tagName.empty()) {
            name = tagName;
        } else if (auto* base = ref->GetBaseObject()) {
            if (base->GetName() && base->GetName()[0] != '\0') {
                name = base->GetName();
            }
        }

        // Get parent cell name for location
        std::string location;
        auto* cell = ref->GetParentCell();
        if (cell && cell->GetFullName() && cell->GetFullName()[0] != '\0') {
            location = cell->GetFullName();
        }

        return {name, location};
    }

    std::vector<PickerEntry> BuildContainerList(RE::FormID a_masterFormID, bool a_includeMaster) {
        std::vector<PickerEntry> result;

        auto* mgr = NetworkManager::GetSingleton();
        std::set<RE::FormID> addedIDs;

        // --- Group 0: Special entries (Keep / Pass / Sell) ---

        // Keep — items stay in master container
        if (a_masterFormID != 0) {
            result.push_back({T("$SLID_Keep"), "", a_masterFormID, false, 0x88CC88, 0, true});
            addedIDs.insert(a_masterFormID);
        }

        // Pass — filter skipped, items fall through to next filter
        result.push_back({T("$SLID_Pass"), "", 0, false, 0xDDAA44, 0, true});

        // Sell Container — always shown; disabled when none designated
        auto sellFormID = mgr->GetSellContainerFormID();
        if (sellFormID != 0 && sellFormID != a_masterFormID) {
            auto [sellName, sellLoc] = ResolveContainerInfo(sellFormID);
            result.push_back({T("$SLID_SellContainer"), sellLoc, sellFormID, false, 0x88BBDD, 0, true});
            addedIDs.insert(sellFormID);
        } else {
            result.push_back({T("$SLID_SellContainer"), "", 0, false, 0x555555, 0, false});
        }

        // --- Group 1: Tagged containers ---
        const auto& tags = mgr->GetTagRegistry();
        std::vector<PickerEntry> taggedEntries;

        for (const auto& [formID, tag] : tags) {
            if (formID == a_masterFormID) continue;
            if (addedIDs.count(formID)) continue;
            auto [cName, cLoc] = ResolveContainerInfo(formID);
            std::string displayName = tag.customName.empty() ? cName : tag.customName;
            taggedEntries.push_back({displayName, cLoc, formID, true, 0, 1, true});
            addedIDs.insert(formID);
        }

        std::sort(taggedEntries.begin(), taggedEntries.end(),
            [](const PickerEntry& a, const PickerEntry& b) { return a.name < b.name; });
        for (auto& e : taggedEntries) result.push_back(std::move(e));

        // --- Group 2: SCIE containers (only if setting enabled and SCIE installed) ---
        if (Settings::bSCIEIncludeContainers && SCIEIntegration::IsInstalled()) {
            std::vector<PickerEntry> scieEntries;

            const auto& scieContainers = SCIEIntegration::GetCachedContainers();
            for (auto formID : scieContainers) {
                if (addedIDs.count(formID)) continue;
                if (formID == a_masterFormID) continue;

                auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(formID);
                if (!ref) continue;

                std::string name = "SCIE Container";
                if (auto* base = ref->GetBaseObject()) {
                    if (base->GetName() && base->GetName()[0] != '\0') {
                        name = base->GetName();
                    }
                }

                // Get location
                std::string location;
                if (auto* cell = ref->GetParentCell()) {
                    if (cell->GetFullName() && cell->GetFullName()[0] != '\0') {
                        location = cell->GetFullName();
                    }
                }

                // Group 2 for SCIE, with a distinct color (light purple)
                scieEntries.push_back({name, location, formID, false, 0xBB99DD, 2, true});
                addedIDs.insert(formID);
            }

            std::sort(scieEntries.begin(), scieEntries.end(),
                [](const PickerEntry& a, const PickerEntry& b) { return a.name < b.name; });
            for (auto& e : scieEntries) result.push_back(std::move(e));
        }

        // --- Group 3: Scanned containers (only if setting enabled) ---
        if (Settings::bIncludeUnlinkedContainers) {
            const auto& kGenericNames = Settings::sGenericContainerNames;
            std::vector<PickerEntry> scannedEntries;

            auto scanned = ScanCellContainers(a_includeMaster ? 0 : a_masterFormID);
            for (const auto& sc : scanned) {
                if (addedIDs.count(sc.formID)) continue;
                bool isGeneric = false;
                for (const auto& g : kGenericNames) {
                    if (sc.gameName == g) { isGeneric = true; break; }
                }
                if (isGeneric) continue;
                scannedEntries.push_back({sc.gameName, sc.cellName, sc.formID, false, 0, 3, true});
                addedIDs.insert(sc.formID);
            }

            std::sort(scannedEntries.begin(), scannedEntries.end(),
                [](const PickerEntry& a, const PickerEntry& b) { return a.name < b.name; });
            for (auto& e : scannedEntries) result.push_back(std::move(e));
        }

        return result;
    }
}
