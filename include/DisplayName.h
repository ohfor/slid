#pragma once

#include <RE/Skyrim.h>

namespace DisplayName {
    /// Apply the appropriate SLID display name to a container based on its current role.
    /// Priority: master > sell > tagged. No-op if ref can't be resolved or has no role.
    void Apply(RE::FormID a_formID);

    /// Remove SLID display name from a container, restoring the base form name.
    void Clear(RE::FormID a_formID);

    /// Apply display names to all known SLID containers (masters, sell, tagged).
    void ApplyAll();
}
