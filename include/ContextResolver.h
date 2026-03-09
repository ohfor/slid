#pragma once

#include <string>
#include <vector>

namespace ContextResolver {

    enum class Context {
        kMaster,     // Crosshair on a master container
        kSell,       // Crosshair on the sell container
        kKnown,      // Crosshair on a tagged or filter-assigned container
        kContainer,  // Crosshair on an unknown container
        kAir,        // Nothing targeted, player has networks
        kAirEmpty,   // Nothing targeted, no networks
        kVendor      // Crosshair on an actor (vendor or not)
    };

    enum class Action {
        kOpen,
        kWhoosh,
        kSort,
        kSweep,
        kConfigure,
        kDetect,
        kSummary,
        kRename,
        kRemove,
        kDestroyLink,
        kCreateLink,
        kAddToLink,
        kSetAsSell,
        kWhooshConfigure,
        kRestock,
        kRestockConfigure,
        kWhooshAndRestock
    };

    struct ActionEntry {
        Action      action;
        std::string nameKey;   // e.g. "$SLID_CtxSort"
        std::string descKey;   // e.g. "$SLID_CtxSortDesc"
    };

    struct ResolvedContext {
        Context                  context;
        std::string              networkName;       // locked or first cyclable
        std::string              containerName;     // tag name or base form name (kKnown only)
        std::vector<std::string> cyclableNetworks;  // empty = locked to networkName
        std::vector<ActionEntry> actions;
        RE::FormID               targetFormID = 0;
    };

    /// Resolve a crosshair target into a context with available actions.
    /// @param a_target  FormID from crosshair capture (0 = nothing targeted)
    ResolvedContext Resolve(RE::FormID a_target);
}
