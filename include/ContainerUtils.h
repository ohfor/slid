#pragma once

#include <RE/Skyrim.h>

namespace ContainerUtils {

    /// Returns true if the reference is non-persistent (will be evicted when its cell unloads).
    /// Uses kRefOriginalPersistent which reflects the plugin record, unlike kPersistent
    /// which the engine sets on ALL loaded refs regardless of record flags.
    inline bool IsNonPersistent(RE::TESObjectREFR* a_ref) {
        if (!a_ref) return false;
        return !a_ref->inGameFormFlags.any(RE::TESForm::InGameFormFlag::kRefOriginalPersistent);
    }

}
