#include "ContainerRegistry.h"
#include "ContainerScanner.h"  // For ScanCellContainers()
#include "Settings.h"
#include "TranslationService.h"

namespace {
    constexpr uint32_t COLOR_SCANNED = 0xCCCCCC;  // Light gray
    constexpr uint32_t COLOR_DISABLED = 0x555555;
}

/**
 * CellScanContainerSource - Provides containers scanned from the current cell
 *
 * Group 4 entries (shifted from 3). These are non-respawning or player-owned containers found
 * in the player's current cell. Only active when bIncludeUnlinkedContainers is
 * enabled in settings.
 *
 * Note: This source has lowest priority since scanned containers should only
 * be selected when no other source claims them.
 */
class CellScanContainerSource : public IContainerSource {
public:
    const char* GetSourceID() const override { return "cellscan"; }

    // Lowest priority - scanned containers are fallback
    int GetPriority() const override { return 100; }

    bool OwnsContainer(RE::FormID a_formID) const override {
        if (!Settings::bIncludeUnlinkedContainers) return false;
        if (a_formID == 0) return false;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return false;

        auto* playerCell = player->GetParentCell();
        if (!playerCell) return false;

        auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_formID);
        if (!ref || !ref->GetContainer()) return false;
        if (ref->GetParentCell() != playerCell) return false;

        auto* base = ref->GetBaseObject();
        if (!base) return false;

        auto* cont = base->As<RE::TESObjectCONT>();
        if (!cont) return false;

        return true;
    }

    ContainerDisplay Resolve(RE::FormID a_formID) const override {
        auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_formID);
        bool available = (ref != nullptr && ref->GetContainer());

        std::string name = T("$SLID_Container");
        if (ref && ref->GetBaseObject()) {
            auto* base = ref->GetBaseObject();
            if (base->GetName() && base->GetName()[0] != '\0') {
                name = base->GetName();
            }
        }

        std::string location;
        if (ref) {
            auto* cell = ref->GetParentCell();
            if (cell && cell->GetFullName() && cell->GetFullName()[0] != '\0') {
                location = cell->GetFullName();
            }
        }

        return ContainerDisplay{
            .name = name,
            .location = location,
            .color = available ? COLOR_SCANNED : COLOR_DISABLED,
            .available = available,
            .group = 4
        };
    }

    std::vector<PickerEntry> GetPickerEntries(RE::FormID a_masterFormID) const override {
        std::vector<PickerEntry> entries;

        // Check settings
        if (!Settings::bIncludeUnlinkedContainers) {
            return entries;
        }

        // Use the existing ScanCellContainers function
        auto scanned = ContainerScanner::ScanCellContainers(a_masterFormID);
        const auto& genericNames = Settings::sGenericContainerNames;

        for (const auto& sc : scanned) {
            // Skip generic container names
            bool isGeneric = false;
            for (const auto& g : genericNames) {
                if (sc.gameName == g) {
                    isGeneric = true;
                    break;
                }
            }
            if (isGeneric) continue;

            entries.push_back(PickerEntry{
                .name = sc.gameName,
                .location = sc.cellName,
                .formID = sc.formID,
                .isTagged = false,
                .color = 0,  // Use default picker color
                .group = 4,
                .enabled = true
            });
        }

        return entries;
    }
};

// Registration function called from main.cpp
void RegisterCellScanContainerSource() {
    ContainerRegistry::GetSingleton()->Register(
        std::make_unique<CellScanContainerSource>()
    );
}
