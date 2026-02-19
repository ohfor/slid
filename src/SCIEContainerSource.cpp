#include "ContainerRegistry.h"
#include "SCIEIntegration.h"
#include "Settings.h"
#include "TranslationService.h"

namespace {
    constexpr uint32_t COLOR_SCIE = 0xBB99DD;  // Light purple
    constexpr uint32_t COLOR_DISABLED = 0x555555;
}

/**
 * SCIEContainerSource - Provides SCIE (Skyrim Crafting Inventory Extender) containers
 *
 * Group 3 entries (shifted from 2). These are containers registered with SCIE for crafting purposes.
 * Only active when SCIE integration is enabled in settings and SCIE ESP is installed.
 */
class SCIEContainerSource : public IContainerSource {
public:
    const char* GetSourceID() const override { return "scie"; }

    // After tagged containers (10), before cell scan (100)
    int GetPriority() const override { return 20; }

    bool OwnsContainer(RE::FormID a_formID) const override {
        if (a_formID == 0) return false;

        // Check settings first
        if (!Settings::bSCIEIncludeContainers || !SCIEIntegration::IsInstalled()) {
            return false;
        }

        // Check if this FormID is in SCIE's container list
        const auto& containers = SCIEIntegration::GetCachedContainers();
        for (auto fid : containers) {
            if (fid == a_formID) return true;
        }
        return false;
    }

    ContainerDisplay Resolve(RE::FormID a_formID) const override {
        auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_formID);
        bool available = (ref != nullptr);

        std::string name = "SCIE Container";
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
            .color = available ? COLOR_SCIE : COLOR_DISABLED,
            .available = available,
            .group = 3
        };
    }

    std::vector<PickerEntry> GetPickerEntries(RE::FormID a_masterFormID) const override {
        std::vector<PickerEntry> entries;

        // Check settings
        if (!Settings::bSCIEIncludeContainers || !SCIEIntegration::IsInstalled()) {
            return entries;
        }

        const auto& containers = SCIEIntegration::GetCachedContainers();
        for (auto formID : containers) {
            // Skip master container (handled by SpecialContainerSource as Keep)
            if (formID == a_masterFormID) continue;

            auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(formID);
            if (!ref) continue;

            std::string name = "SCIE Container";
            if (auto* base = ref->GetBaseObject()) {
                if (base->GetName() && base->GetName()[0] != '\0') {
                    name = base->GetName();
                }
            }

            std::string location;
            if (auto* cell = ref->GetParentCell()) {
                if (cell->GetFullName() && cell->GetFullName()[0] != '\0') {
                    location = cell->GetFullName();
                }
            }

            entries.push_back(PickerEntry{
                .name = name,
                .location = location,
                .formID = formID,
                .isTagged = false,
                .color = COLOR_SCIE,
                .group = 3,
                .enabled = true
            });
        }

        return entries;
    }
};

// Registration function called from main.cpp
void RegisterSCIEContainerSource() {
    ContainerRegistry::GetSingleton()->Register(
        std::make_unique<SCIEContainerSource>()
    );
}
