#include "ContainerRegistry.h"
#include "NetworkManager.h"
#include "TranslationService.h"

namespace {
    constexpr uint32_t COLOR_CONTAINERLIST = 0xBBAADD;  // Soft purple for container list entries
    constexpr uint32_t COLOR_DISABLED = 0x555555;
}

/**
 * ContainerListSource - Provides containers from INI [ContainerList:*] sections
 *
 * Group 2 entries (same as tagged). Container lists are INI-declared sets of
 * named containers that appear in the picker dropdown for any Link.
 * Reads directly from NetworkManager::GetContainerLists() on each call (no cache).
 */
class ContainerListSource : public IContainerSource {
public:
    const char* GetSourceID() const override { return "containerlist"; }

    // After tagged (10), before SCIE (20)
    int GetPriority() const override { return 15; }

    bool OwnsContainer(RE::FormID a_formID) const override {
        if (a_formID == 0) return false;
        auto* mgr = NetworkManager::GetSingleton();
        const auto& lists = mgr->GetContainerLists();
        for (const auto& clist : lists) {
            if (!mgr->IsContainerListEnabled(clist.name)) continue;
            for (const auto& entry : clist.containers) {
                if (entry.resolvedFormID == a_formID) return true;
            }
        }
        return false;
    }

    ContainerDisplay Resolve(RE::FormID a_formID) const override {
        auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_formID);
        bool available = (ref != nullptr);

        // Find the entry for the display name override
        std::string name;
        auto* mgr = NetworkManager::GetSingleton();
        const auto& lists = mgr->GetContainerLists();
        for (const auto& clist : lists) {
            if (!mgr->IsContainerListEnabled(clist.name)) continue;
            for (const auto& entry : clist.containers) {
                if (entry.resolvedFormID == a_formID) {
                    if (!entry.displayName.empty()) {
                        name = entry.displayName;
                    }
                    break;
                }
            }
            if (!name.empty()) break;
        }

        // Fall back to base object name
        if (name.empty()) {
            if (ref && ref->GetBaseObject()) {
                auto* base = ref->GetBaseObject();
                if (base->GetName() && base->GetName()[0] != '\0') {
                    name = base->GetName();
                } else {
                    name = T("$SLID_Container");
                }
            } else {
                name = T("$SLID_Container");
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
            .color = available ? COLOR_CONTAINERLIST : COLOR_DISABLED,
            .available = available,
            .group = 2
        };
    }

    std::vector<PickerEntry> GetPickerEntries(RE::FormID a_masterFormID) const override {
        std::vector<PickerEntry> entries;
        auto* mgr = NetworkManager::GetSingleton();
        const auto& lists = mgr->GetContainerLists();

        for (const auto& clist : lists) {
            if (!mgr->IsContainerListEnabled(clist.name)) continue;
            for (const auto& entry : clist.containers) {
                if (entry.resolvedFormID == 0) continue;
                // Skip if this is the master container
                if (entry.resolvedFormID == a_masterFormID) continue;

                auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(entry.resolvedFormID);
                bool available = (ref != nullptr);

                std::string name;
                if (!entry.displayName.empty()) {
                    name = entry.displayName;
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

                entries.push_back(PickerEntry{
                    .name = name,
                    .location = location,
                    .formID = entry.resolvedFormID,
                    .isTagged = false,
                    .color = available ? 0 : COLOR_DISABLED,
                    .group = 2,
                    .enabled = available,
                    .subGroup = clist.name
                });
            }
        }

        return entries;
    }
};

// Registration function called from main.cpp
void RegisterContainerListSource() {
    ContainerRegistry::GetSingleton()->Register(
        std::make_unique<ContainerListSource>()
    );
}
