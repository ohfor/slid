#pragma once

#include "IContainerSource.h"

#include <memory>
#include <vector>

/**
 * ContainerRegistry - Central registry for container data sources
 *
 * == Registry Rules ==
 *
 * MUST:
 * 1. Iterate sources in priority order for Resolve()
 * 2. Stop at first source that claims ownership
 * 3. Return fallback ContainerDisplay for unclaimed FormIDs
 * 4. Deduplicate BuildPickerList() results by FormID
 * 5. Sort picker results by group, then alphabetically
 *
 * MUST NOT:
 * 1. Contain any source-specific code or type checks
 * 2. Cache Resolve() results
 * 3. Know the names/types of registered sources
 * 4. Modify sources after registration
 *
 * == UI Consumer Rules ==
 *
 * MUST:
 * 1. Only use Registry::Resolve() and Registry::BuildPickerList()
 * 2. Trust the ContainerDisplay values (color, available, name) without recomputing
 * 3. Handle available=false entries (show disabled, not omitted)
 *
 * MUST NOT:
 * 1. Import any source headers
 * 2. Check FormIDs against known source patterns
 * 3. Apply source-specific rendering logic
 * 4. Cache ContainerDisplay values across frames
 */
class ContainerRegistry {
public:
    static ContainerRegistry* GetSingleton();

    // Register a container source. Call during plugin initialization.
    // Sources are sorted by priority after registration.
    void Register(std::unique_ptr<IContainerSource> a_source);

    // Resolve a container FormID to display information.
    // Iterates sources in priority order, returns first match.
    // Returns fallback display for unclaimed FormIDs.
    ContainerDisplay Resolve(RE::FormID a_formID) const;

    // Build aggregated picker list from all sources.
    // Deduplicates by FormID, sorts by group then alphabetically.
    std::vector<PickerEntry> BuildPickerList(RE::FormID a_masterFormID) const;

    // Count playable items in a container, routed through the owning source.
    // UI code must use this instead of LookupByID + GetInventory directly.
    int CountItems(RE::FormID a_formID) const;

    // Expose sources for testing (integration tests only)
    const std::vector<std::unique_ptr<IContainerSource>>& GetSources() const { return m_sources; }

private:
    ContainerRegistry() = default;

    std::vector<std::unique_ptr<IContainerSource>> m_sources;
    bool m_sorted = false;

    void EnsureSorted();
};
