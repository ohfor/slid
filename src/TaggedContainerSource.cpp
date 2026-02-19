#include "ContainerRegistry.h"
#include "NetworkManager.h"
#include "TranslationService.h"

namespace {
    constexpr uint32_t COLOR_TAGGED = 0x99CCFF;  // Light blue for tagged containers
    constexpr uint32_t COLOR_DISABLED = 0x555555;
}

/**
 * TaggedContainerSource - Provides user-tagged containers
 *
 * Group 2 entries (shifted from 1). Users explicitly tag containers with custom names
 * via the "Add Container to Link" power. These appear after follower storage but above
 * SCIE and scanned containers.
 */
class TaggedContainerSource : public IContainerSource {
public:
    const char* GetSourceID() const override { return "tagged"; }

    // After special (0), before SCIE (20)
    int GetPriority() const override { return 10; }

    bool OwnsContainer(RE::FormID a_formID) const override {
        if (a_formID == 0) return false;
        return NetworkManager::GetSingleton()->IsTagged(a_formID);
    }

    ContainerDisplay Resolve(RE::FormID a_formID) const override {
        auto* mgr = NetworkManager::GetSingleton();
        auto tagName = mgr->GetTagName(a_formID);

        // Get the actual container ref to check availability
        auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_formID);
        bool available = (ref != nullptr);

        std::string name;
        if (!tagName.empty()) {
            name = tagName;
        } else if (ref && ref->GetBaseObject()) {
            auto* base = ref->GetBaseObject();
            if (base->GetName() && base->GetName()[0] != '\0') {
                name = base->GetName();
            } else {
                name = T("$SLID_Container");
            }
        } else {
            name = T("$SLID_Container");
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
            .color = available ? COLOR_TAGGED : COLOR_DISABLED,
            .available = available,
            .group = 2
        };
    }

    std::vector<PickerEntry> GetPickerEntries(RE::FormID a_masterFormID) const override {
        std::vector<PickerEntry> entries;
        auto* mgr = NetworkManager::GetSingleton();
        const auto& tags = mgr->GetTagRegistry();

        for (const auto& [formID, tag] : tags) {
            // Skip if this is the master container (handled by SpecialContainerSource as Keep)
            if (formID == a_masterFormID) continue;

            auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(formID);
            bool available = (ref != nullptr);

            // Use custom name if set, otherwise resolve from base object
            std::string name;
            if (!tag.customName.empty()) {
                name = tag.customName;
            } else if (ref && ref->GetBaseObject()) {
                auto* base = ref->GetBaseObject();
                if (base->GetName() && base->GetName()[0] != '\0') {
                    name = base->GetName();
                } else {
                    name = T("$SLID_Container");
                }
            } else {
                name = T("$SLID_Container");
            }

            // Get location
            std::string location;
            if (ref) {
                auto* cell = ref->GetParentCell();
                if (cell && cell->GetFullName() && cell->GetFullName()[0] != '\0') {
                    location = cell->GetFullName();
                }
            }

            entries.push_back(PickerEntry{
                .name = name,
                .location = location,
                .formID = formID,
                .isTagged = true,
                .color = available ? 0 : COLOR_DISABLED,  // 0 = use picker's tagged color
                .group = 2,
                .enabled = available
            });
        }

        return entries;
    }
};

// Registration function called from main.cpp
void RegisterTaggedContainerSource() {
    ContainerRegistry::GetSingleton()->Register(
        std::make_unique<TaggedContainerSource>()
    );
}
