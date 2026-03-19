#pragma once

#include <string>

namespace RE {
    class TESBoundObject;
}

namespace TraitEvaluator {
    /// Resolve keyword pointers and build dispatch table.
    /// Call once at kDataLoaded, after Settings::LoadUniqueItems().
    void Init();

    /// Evaluate a single atomic trait against an item.
    /// Returns false for unknown trait names (logs warning once).
    bool Evaluate(const std::string& traitName, RE::TESBoundObject* item);

    /// Pre-resolve all keyword: traits found in loaded filter definitions.
    /// Returns count of keywords that failed to resolve (0 = all OK).
    /// Logs warnings for each failed keyword.
    uint32_t ValidateKeywords();
}
