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
    std::vector<FilterStage> filters;              // ordered pipeline (menu rows)
    RE::FormID catchAllFormID = 0;                 // 0 = master (default)

    // Whoosh configuration (per-network)
    std::unordered_set<std::string> whooshFilters;  // filter IDs enabled for drain
    bool whooshConfigured = false;                   // Has player configured Whoosh for this network?

    // Restock configuration (per-network)
    RestockCategory::RestockConfig restockConfig;

    // Runtime-only state (not persisted to cosave)
    bool masterUnavailable = false;  // Set by ValidateNetworks when master can't be resolved
};
