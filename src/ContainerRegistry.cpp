#include "ContainerRegistry.h"

#include <algorithm>
#include <set>

ContainerRegistry* ContainerRegistry::GetSingleton() {
    static ContainerRegistry instance;
    return &instance;
}

void ContainerRegistry::Register(std::unique_ptr<IContainerSource> a_source) {
    if (!a_source) return;

    logger::info("ContainerRegistry: Registering source '{}' with priority {}",
                 a_source->GetSourceID(), a_source->GetPriority());

    m_sources.push_back(std::move(a_source));
    m_sorted = false;
}

void ContainerRegistry::EnsureSorted() {
    if (m_sorted) return;

    std::sort(m_sources.begin(), m_sources.end(),
        [](const auto& a, const auto& b) {
            return a->GetPriority() < b->GetPriority();
        });

    m_sorted = true;
}

ContainerDisplay ContainerRegistry::Resolve(RE::FormID a_formID) const {
    // Iterate sources in priority order, return first match
    const_cast<ContainerRegistry*>(this)->EnsureSorted();

    for (const auto& source : m_sources) {
        if (source->OwnsContainer(a_formID)) {
            auto display = source->Resolve(a_formID);
            logger::debug("ContainerRegistry::Resolve: {:08X} -> '{}' via source '{}' (available={})",
                         a_formID, display.name, source->GetSourceID(), display.available);
            return display;
        }
    }

    // Fallback for unclaimed FormIDs
    logger::debug("ContainerRegistry::Resolve: {:08X} -> unclaimed by all {} sources",
                 a_formID, m_sources.size());
    return ContainerDisplay{
        .name = "Unknown",
        .location = "",
        .color = 0x555555,
        .available = false,
        .group = 255
    };
}

int ContainerRegistry::CountItems(RE::FormID a_formID) const {
    if (a_formID == 0) return 0;

    const_cast<ContainerRegistry*>(this)->EnsureSorted();

    for (const auto& source : m_sources) {
        if (source->OwnsContainer(a_formID)) {
            return source->CountItems(a_formID);
        }
    }

    // Unclaimed — fall back to default counting
    auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_formID);
    if (!ref) return 0;
    int count = 0;
    auto inv = ref->GetInventory();
    for (auto& [item, data] : inv) {
        if (!item || data.first <= 0 || IsPhantomItem(item)) continue;
        count += data.first;
    }
    return count;
}

std::vector<PickerEntry> ContainerRegistry::BuildPickerList(RE::FormID a_masterFormID) const {
    const_cast<ContainerRegistry*>(this)->EnsureSorted();

    std::vector<PickerEntry> result;
    std::set<RE::FormID> seen;

    // Gather entries from all sources (already in priority order)
    for (const auto& source : m_sources) {
        auto entries = source->GetPickerEntries(a_masterFormID);
        for (auto& entry : entries) {
            // Deduplicate by FormID
            // Pass (formID=0) is special and should not be deduplicated
            if (entry.formID != 0 && seen.count(entry.formID)) {
                continue;
            }
            seen.insert(entry.formID);
            result.push_back(std::move(entry));
        }
    }

    // Sort by group, then subGroup, then alphabetically within subGroup
    std::sort(result.begin(), result.end(),
        [](const PickerEntry& a, const PickerEntry& b) {
            if (a.group != b.group) return a.group < b.group;
            if (a.subGroup != b.subGroup) return a.subGroup < b.subGroup;
            return a.name < b.name;
        });

    // Inject non-selectable header entries before each new non-empty subGroup
    {
        std::vector<PickerEntry> withHeaders;
        withHeaders.reserve(result.size() + 8);
        std::string lastSubGroup;
        uint8_t lastGroup = 255;
        for (auto& entry : result) {
            if (!entry.subGroup.empty() &&
                (entry.subGroup != lastSubGroup || entry.group != lastGroup)) {
                PickerEntry header;
                header.name = entry.subGroup;
                header.subGroup = entry.subGroup;
                header.group = entry.group;
                header.formID = 0;
                header.enabled = false;
                withHeaders.push_back(std::move(header));
            }
            lastSubGroup = entry.subGroup;
            lastGroup = entry.group;
            withHeaders.push_back(std::move(entry));
        }
        result = std::move(withHeaders);
    }

    logger::debug("ContainerRegistry::BuildPickerList: {} entries from {} sources",
                  result.size(), m_sources.size());

    return result;
}
