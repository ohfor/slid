#pragma once

#include "RestockCategory.h"

#include <RE/Skyrim.h>

#include <string>
#include <unordered_set>
#include <vector>

struct FilterStage {
    std::string filterID;              // e.g. "weapons", "armor"
    RE::FormID containerFormID = 0;    // 0 = unlinked
};

struct Network {
    std::string name;
    RE::FormID masterFormID = 0;
    std::vector<FilterStage> filters;              // ordered pipeline (menu rows); last entry is catch-all (__catchall)

    // Whoosh configuration (per-network)
    std::unordered_set<std::string> whooshFilters;  // filter IDs enabled for drain
    bool whooshConfigured = false;                   // Has player configured Whoosh for this network?

    // Restock configuration (per-network)
    RestockCategory::RestockConfig restockConfig;

    // Runtime-only state (not persisted to cosave)
    bool masterUnavailable = false;  // Set by ValidateNetworks when master can't be resolved
};

/// Extract the catch-all container FormID from a network's filter vector.
/// The catch-all is always the last entry with filterID == "__catchall".
/// Returns 0 if no catch-all found (shouldn't happen in practice).
inline RE::FormID ExtractCatchAllFormID(const std::vector<FilterStage>& a_filters) {
    if (!a_filters.empty() && a_filters.back().filterID == "__catchall") {
        return a_filters.back().containerFormID;
    }
    return 0;
}
